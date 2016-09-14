#include "malloc.h"
#include "utils.h"
#include "osdep/timer.h"

// Controls the multiplication factor for new slab allocations. The new slab
// will always be allocated such that the size of the slab is this factor times
// the previous slab. Higher values make it grow faster.
#define MPVK_HEAP_SLAB_GROWTH_RATE 4

// Controls the minimum slab size, to reduce the frequency at which very small
// slabs would need to get allocated when allocating the first few buffers.
// (Default: 1 MB)
#define MPVK_HEAP_MINIMUM_SLAB_SIZE (1 << 20)

// Controls the maximum slab size, to reduce the effect of unbounded slab
// growth exhausting memory. If the application needs a single allocation
// that's bigger than this value, it will be allocated directly from the
// device. (Default: 512 MB)
#define MPVK_HEAP_MAXIMUM_SLAB_SIZE (1 << 29)

// Controls the minimum free region size, to reduce thrashing the free space
// map with lots of small buffers during uninit. (Default: 1 KB)
#define MPVK_HEAP_MINIMUM_REGION_SIZE (1 << 10)

// Represents a region of available memory
struct vk_region {
    size_t start; // first offset in region
    size_t end;   // first offset *not* in region
};

static inline size_t region_len(struct vk_region r)
{
    return r.end - r.start;
}

// A single slab represents a contiguous region of allocated memory. Actual
// allocations are served as slices of this. Slabs are organized into linked
// lists, which represent individual heaps.
struct vk_slab {
    VkDeviceMemory mem;   // underlying device allocation
    size_t size;          // total size of `slab`
    size_t used;          // number of bytes actually in use (for GC accounting)
    bool dedicated;       // slab is allocated specifically for one object
    // free space map: a sorted list of memory regions that are available
    struct vk_region *regions;
    int num_regions;
    // optional, depends on the memory type:
    VkBuffer buffer;      // buffer spanning the entire slab
    void *data;           // mapped memory corresponding to `mem`
};

struct vk_heap {
    struct vk_memtype *type;     // the memory type this heap belongs to
    VkBufferUsageFlagBits usage; // or 0 for generic heaps
    struct vk_slab **slabs;      // array of slabs sorted by size
    int num_slabs;
};

// Represents a single memory type. All allocations of this memory type are
// grouped together into heaps; one per buffer usage type and one for generic
// allocations (e.g. images).
struct vk_memtype {
    int index;                      // the memory type index
    int heapIndex;                  // the memory heap index
    VkMemoryPropertyFlagBits flags; // the memory type bits
    struct vk_heap *heaps;          // array of heaps (grouped by buffer type)
    int num_heaps;
};

// The overall state of the allocator, which keeps track of a vk_heap for each
// memory type supported by the device.
struct vk_malloc {
    struct vk_memtype types[VK_MAX_MEMORY_TYPES];
    int num_types;
};

static void slab_free(struct mpvk_ctx *vk, struct vk_slab *slab)
{
    if (!slab)
        return;

    assert(slab->used == 0);

    int64_t start = mp_time_us();
    vkDestroyBuffer(vk->dev, slab->buffer, MPVK_ALLOCATOR);
    // also implicitly unmaps the memory if needed
    vkFreeMemory(vk->dev, slab->mem, MPVK_ALLOCATOR);
    int64_t stop = mp_time_us();

    MP_VERBOSE(vk, "Freeing slab of size %lu took %ld μs.\n",
               slab->size, stop - start);

    talloc_free(slab);
}

static struct vk_slab *slab_alloc(struct mpvk_ctx *vk, struct vk_heap *heap,
                                  size_t size)
{
    struct vk_slab *slab = talloc_ptrtype(NULL, slab);
    *slab = (struct vk_slab) {
        .size = size,
    };

    MP_TARRAY_APPEND(slab, slab->regions, slab->num_regions, (struct vk_region) {
        .start = 0,
        .end   = slab->size,
    });

    struct vk_memtype *type = heap->type;
    MP_VERBOSE(vk, "Allocating %lu memory of type 0x%x (id %d) in heap %d.\n",
               slab->size, type->flags, type->index, type->heapIndex);

    VkMemoryAllocateInfo minfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .memoryTypeIndex = heap->type->index,
        .allocationSize = slab->size,
    };

    if (heap->usage) {
        VkBufferCreateInfo binfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size  = slab->size,
            .usage = heap->usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        VK(vkCreateBuffer(vk->dev, &binfo, MPVK_ALLOCATOR, &slab->buffer));

        VkMemoryRequirements reqs;
        vkGetBufferMemoryRequirements(vk->dev, slab->buffer, &reqs);
        minfo.allocationSize = reqs.size; // this can be larger than slab->size

        // Sanity check the memory requirements to make sure we didn't screw up
        if (!(reqs.memoryTypeBits & (1 << type->index))) {
            MP_ERR(vk, "Chosen memory type %d does not support buffer usage "
                   "0x%x!\n", type->index, heap->usage);
            goto error;
        }
    }

    VK(vkAllocateMemory(vk->dev, &minfo, MPVK_ALLOCATOR, &slab->mem));

    if (type->flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        VK(vkMapMemory(vk->dev, slab->mem, 0, VK_WHOLE_SIZE, 0, &slab->data));

    if (heap->usage)
        VK(vkBindBufferMemory(vk->dev, slab->buffer, slab->mem, 0));

    return slab;

error:
    slab_free(vk, slab);
    return NULL;
}

static void insert_region(struct vk_slab *slab, struct vk_region region)
{
    if (region.start == region.end)
        return;

    bool big_enough = region_len(region) >= MPVK_HEAP_MINIMUM_REGION_SIZE;

    // Find the index of the first region that comes after this
    for (int i = 0; i < slab->num_regions; i++) {
        struct vk_region *r = &slab->regions[i];

        // Check for a few special cases which can be coalesced
        if (r->end == region.start) {
            // The new region is at the tail of this region. In addition to
            // modifying this region, we also need to coalesce all the following
            // regions for as long as possible
            r->end = region.end;

            struct vk_region *next = &slab->regions[i+1];
            while (i+1 < slab->num_regions && r->end == next->start) {
                r->end = next->end;
                MP_TARRAY_REMOVE_AT(slab->regions, slab->num_regions, i+1);
            }
            return;
        }

        if (r->start == region.end) {
            // The new region is at the head of this region. We don't need to
            // do anything special here - because if this could be further
            // coalesced backwards, the previous loop iteration would already
            // have caught it.
            r->start = region.start;
            return;
        }

        if (r->start > region.start) {
            // The new region comes somewhere before this region, so insert
            // it into this index in the array.
            if (big_enough) {
                MP_TARRAY_INSERT_AT(slab, slab->regions, slab->num_regions,
                                    i, region);
            }
            return;
        }
    }

    // If we've reached the end of this loop, then all of the regions
    // come before the new region, and are disconnected - so append it
    if (big_enough)
        MP_TARRAY_APPEND(slab, slab->regions, slab->num_regions, region);
}

static void heap_uninit(struct mpvk_ctx *vk, struct vk_heap *heap)
{
    for (int i = 0; i < heap->num_slabs; i++)
        slab_free(vk, heap->slabs[i]);

    talloc_free(heap->slabs);
    *heap = (struct vk_heap){0};
}

static void heap_init(struct mpvk_ctx *vk, struct vk_memtype *type,
                      VkBufferUsageFlagBits usage, struct vk_heap *heap)
{
    *heap = (struct vk_heap) {
        .type = type,
        .usage = usage,
    };
}

void vk_malloc_init(struct mpvk_ctx *vk)
{
    assert(vk->physd);

    struct vk_malloc *ma = vk->alloc = talloc_zero(NULL, struct vk_malloc);

    VkPhysicalDeviceMemoryProperties prop;
    vkGetPhysicalDeviceMemoryProperties(vk->physd, &prop);

    ma->num_types = prop.memoryTypeCount;
    for (int i = 0; i < prop.memoryTypeCount; i++) {
        ma->types[i] = (struct vk_memtype) {
            .index = i,
            .heapIndex = prop.memoryTypes[i].heapIndex,
            .flags = prop.memoryTypes[i].propertyFlags,
        };
    }
}

void vk_malloc_uninit(struct mpvk_ctx *vk)
{
    struct vk_malloc *ma = vk->alloc;
    if (!ma)
        return;

    for (int i = 0; i < ma->num_types; i++) {
        for (int n = 0; n < ma->types[i].num_heaps; n++)
            heap_uninit(vk, &ma->types[i].heaps[n]);
        talloc_free(ma->types[i].heaps);
    }

    talloc_free(ma);
    vk->alloc = NULL;
}

void vk_free_memslice(struct mpvk_ctx *vk, struct vk_memslice slice)
{
    struct vk_slab *slab = slice.priv;
    assert(slab);
    assert(slab->used >= slice.size);
    slab->used -= slice.size;

    MP_DBG(vk, "Freeing slice %lu + %lu from slab with size %lu\n",
           slice.offset, slice.size, slab->size);

    if (slab->dedicated) {
        // If the slab was purpose-allocated for this memslice, we can just
        // free it here
        slab_free(vk, slab);
    } else {
        // Return the allocation to the free space map
        insert_region(slab, (struct vk_region) {
            .start = slice.offset,
            .end   = slice.offset + slice.size,
        });
    }
}

// reqs: optional
static struct vk_memtype *find_best_memtype(struct mpvk_ctx *vk,
                                            VkMemoryPropertyFlagBits flags,
                                            VkMemoryRequirements *reqs)
{
    struct vk_malloc *ma = vk->alloc;

    // The vulkan spec requires memory types to be sorted in the "optimal"
    // order, so the first matching type we find will be the best/fastest one.
    for (int i = 0; i < ma->num_types; i++) {
        // The memory type flags must include our properties
        if ((ma->types[i].flags & flags) != flags)
            continue;
        // The memory type must be supported by the requirements (bitfield)
        if (reqs && !(reqs->memoryTypeBits & (1 << i)))
            continue;

        return &ma->types[i];
    }

    MP_ERR(vk, "Found no memory type matching property flags 0x%x!\n", flags);
    return NULL;
}

static struct vk_heap *find_heap(struct mpvk_ctx *vk, struct vk_memtype *type,
                                 VkBufferUsageFlagBits usage)
{
    if (!type)
        return NULL;

    for (int i = 0; i < type->num_heaps; i++) {
        if (type->heaps[i].usage == usage)
            return &type->heaps[i];
    }

    // Not found => add it
    MP_TARRAY_GROW(NULL, type->heaps, type->num_heaps + 1);
    struct vk_heap *heap = &type->heaps[type->num_heaps++];
    heap_init(vk, type, usage, heap);
    return heap;
}

static inline bool region_fits(struct vk_region r, size_t size, size_t align)
{
    return MP_ALIGN_UP(r.start, align) + size <= r.end;
}

// Finds the best-fitting region in a heap. If the heap is too small or too
// fragmented, a new slab will be allocated under the hood.
static bool heap_get_region(struct mpvk_ctx *vk, struct vk_heap *heap,
                            size_t size, size_t align,
                            struct vk_slab **out_slab, int *out_index)
{
    if (!heap)
        return false;

    struct vk_slab *slab = NULL;

    // If the allocation is very big, serve it directly instead of bothering
    // with the heap
    if (size > MPVK_HEAP_MAXIMUM_SLAB_SIZE) {
        slab = slab_alloc(vk, heap, size);
        *out_slab = slab;
        *out_index = 0;
        return !!slab;
    }

    for (int i = 0; i < heap->num_slabs; i++) {
        slab = heap->slabs[i];
        if (slab->size < size)
            continue;

        // Attempt a best fit search
        int best = -1;
        for (int n = 0; n < slab->num_regions; n++) {
            struct vk_region r = slab->regions[n];
            if (!region_fits(r, size, align))
                continue;
            if (best >= 0 && region_len(r) > region_len(slab->regions[best]))
                continue;
            best = n;
        }

        if (best >= 0) {
            *out_slab = slab;
            *out_index = best;
            return true;
        }
    }

    // Otherwise, allocate a new vk_slab and append it to the list.
    size_t cur_size = MPMAX(size, slab ? slab->size : 0);
    size_t slab_size = MPVK_HEAP_SLAB_GROWTH_RATE * cur_size;
    slab_size = MPMAX(MPVK_HEAP_MINIMUM_SLAB_SIZE, slab_size);
    slab_size = MPMIN(MPVK_HEAP_MAXIMUM_SLAB_SIZE, slab_size);
    assert(slab_size >= size);
    slab = slab_alloc(vk, heap, slab_size);
    if (!slab)
        return false;
    MP_TARRAY_APPEND(NULL, heap->slabs, heap->num_slabs, slab);

    // Return the only region there is in a newly allocated slab
    assert(slab->num_regions == 1);
    *out_slab = slab;
    *out_index = 0;
    return true;
}

static bool slice_heap(struct mpvk_ctx *vk, struct vk_heap *heap, size_t size,
                       size_t alignment, struct vk_memslice *out)
{
    struct vk_slab *slab;
    int index;
    alignment = MP_ALIGN_UP(alignment, vk->limits.bufferImageGranularity);
    if (!heap_get_region(vk, heap, size, alignment, &slab, &index))
        return false;

    struct vk_region reg = slab->regions[index];
    MP_TARRAY_REMOVE_AT(slab->regions, slab->num_regions, index);
    *out = (struct vk_memslice) {
        .vkmem = slab->mem,
        .offset = MP_ALIGN_UP(reg.start, alignment),
        .size = size,
        .priv = slab,
    };

    MP_DBG(vk, "Sub-allocating slice %lu + %lu from slab with size %lu\n",
           out->offset, out->size, slab->size);

    size_t out_end = out->offset + out->size;
    insert_region(slab, (struct vk_region) { reg.start, out->offset });
    insert_region(slab, (struct vk_region) { out_end, reg.end });

    slab->used += size;
    return true;
}

bool vk_malloc_generic(struct mpvk_ctx *vk, VkMemoryRequirements reqs,
                       VkMemoryPropertyFlagBits flags, struct vk_memslice *out)
{
    struct vk_memtype *type = find_best_memtype(vk, flags, &reqs);
    struct vk_heap *heap = find_heap(vk, type, 0);

    return slice_heap(vk, heap, reqs.size, reqs.alignment, out);
}

bool vk_malloc_buffer(struct mpvk_ctx *vk, VkBufferUsageFlagBits bufFlags,
                      VkMemoryPropertyFlagBits memFlags, VkDeviceSize size,
                      VkDeviceSize alignment, struct vk_bufslice *out)
{
    struct vk_memtype *type = find_best_memtype(vk, memFlags, NULL);
    struct vk_heap *heap = find_heap(vk, type, bufFlags);

    if (!slice_heap(vk, heap, size, alignment, &out->mem))
        return false;

    struct vk_slab *slab = out->mem.priv;
    out->buf = slab->buffer;
    if (slab->data)
        out->data = (void *)((uintptr_t)slab->data + (ptrdiff_t)out->mem.offset);

    return true;
}
