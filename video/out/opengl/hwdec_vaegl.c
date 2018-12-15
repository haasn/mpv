/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include <va/va_drmcommon.h>

#include <libavutil/common.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>

#include "config.h"

#include "video/out/gpu/hwdec.h"
#include "video/out/placebo/ra_pl.h"
#include "video/out/vulkan/common.h"
#include "video/mp_image_pool.h"
#include "video/vaapi.h"
#include "common.h"
#include "ra_gl.h"
#include "libmpv/render_gl.h"

#if HAVE_VAAPI_X11
#include <va/va_x11.h>

static VADisplay *create_x11_va_display(struct ra *ra)
{
    Display *x11 = ra_get_native_resource(ra, "x11");
    return x11 ? vaGetDisplay(x11) : NULL;
}
#endif

#if HAVE_VAAPI_WAYLAND
#include <va/va_wayland.h>

static VADisplay *create_wayland_va_display(struct ra *ra)
{
    struct wl_display *wl = ra_get_native_resource(ra, "wl");
    return wl ? vaGetDisplayWl(wl) : NULL;
}
#endif

#if HAVE_VAAPI_DRM
#include <va/va_drm.h>

static VADisplay *create_drm_va_display(struct ra *ra)
{
    mpv_opengl_drm_params *params = ra_get_native_resource(ra, "drm_params");
    if (!params || params->render_fd < 0)
        return NULL;

    return vaGetDisplayDRM(params->render_fd);
}
#endif

struct va_create_native {
    const char *name;
    VADisplay *(*create)(struct ra *ra);
};

static const struct va_create_native create_native_cbs[] = {
#if HAVE_VAAPI_X11
    {"x11",     create_x11_va_display},
#endif
#if HAVE_VAAPI_WAYLAND
    {"wayland", create_wayland_va_display},
#endif
#if HAVE_VAAPI_DRM
    {"drm",     create_drm_va_display},
#endif
};

static VADisplay *create_native_va_display(struct ra *ra, struct mp_log *log)
{
    for (int n = 0; n < MP_ARRAY_SIZE(create_native_cbs); n++) {
        const struct va_create_native *disp = &create_native_cbs[n];
        mp_verbose(log, "Trying to open a %s VA display...\n", disp->name);
        VADisplay *display = disp->create(ra);
        if (display)
            return display;
    }
    return NULL;
}

struct priv_owner {
    struct mp_vaapi_ctx *ctx;
    VADisplay *display;
    int *formats;
    bool probing_formats; // temporary during init
};

struct priv {
    struct mp_image layout;
    struct ra_tex *tex[4];
    VADRMPRIMESurfaceDescriptor desc;
};

static void determine_working_formats(struct ra_hwdec *hw);

static void uninit(struct ra_hwdec *hw)
{
    struct priv_owner *p = hw->priv;
    if (p->ctx)
        hwdec_devices_remove(hw->devs, &p->ctx->hwctx);
    va_destroy(p->ctx);
}

static int init(struct ra_hwdec *hw)
{
    struct priv_owner *p = hw->priv;

    if (ra_is_gl(hw->ra)) {
        MP_VERBOSE(hw, "using VAAPI EGL interop\n");
        return -1;
    }
    if (ra_pl_get(hw->ra)) {
        const struct pl_gpu *gpu = ra_pl_get(hw->ra);
        if (!(gpu->import_caps.shared_mem & PL_HANDLE_DMA_BUF)) {
            MP_VERBOSE(hw, "VAAPI Vulkan interop requires support for "
                           "dma_buf import in Vulkan.\n");
            return -1;
        }

        MP_VERBOSE(hw, "using VAAPI Vulkan interop\n");
    }

    p->display = create_native_va_display(hw->ra, hw->log);
    if (!p->display) {
        MP_VERBOSE(hw, "Could not create a VA display.\n");
        return -1;
    }

    p->ctx = va_initialize(p->display, hw->log, true);
    if (!p->ctx) {
        vaTerminate(p->display);
        return -1;
    }
    if (!p->ctx->av_device_ref) {
        MP_VERBOSE(hw, "libavutil vaapi code rejected the driver?\n");
        return -1;
    }

    if (hw->probing && va_guess_if_emulated(p->ctx)) {
        return -1;
    }

    determine_working_formats(hw);
    if (!p->formats || !p->formats[0]) {
        return -1;
    }

    p->ctx->hwctx.supported_formats = p->formats;
    p->ctx->hwctx.driver_name = hw->driver->name;
    hwdec_devices_add(hw->devs, &p->ctx->hwctx);
    return 0;
}

static void mapper_unmap(struct ra_hwdec_mapper *mapper)
{
    for (int n = 0; n < 4; n++) {
        ra_tex_free(mapper->ra, &mapper->tex[n]);
    }
}

static void mapper_uninit(struct ra_hwdec_mapper *mapper)
{
}

static bool check_fmt(struct ra_hwdec_mapper *mapper, int fmt)
{
    struct priv_owner *p_owner = mapper->owner->priv;
    for (int n = 0; p_owner->formats && p_owner->formats[n]; n++) {
        if (p_owner->formats[n] == fmt)
            return true;
    }
    return false;
}

static int mapper_init(struct ra_hwdec_mapper *mapper)
{
    struct priv_owner *p_owner = mapper->owner->priv;
    struct priv *p = mapper->priv;

    mapper->dst_params = mapper->src_params;
    mapper->dst_params.imgfmt = mapper->src_params.hw_subfmt;
    mapper->dst_params.hw_subfmt = 0;

    struct ra_imgfmt_desc desc = {0};

    if (!ra_get_imgfmt_desc(mapper->ra, mapper->dst_params.imgfmt, &desc))
        return -1;

    mp_image_set_params(&p->layout, &mapper->dst_params);

    if (!p_owner->probing_formats && !check_fmt(mapper, mapper->dst_params.imgfmt))
    {
        MP_FATAL(mapper, "unsupported VA image format %s\n",
                 mp_imgfmt_to_name(mapper->dst_params.imgfmt));
        return -1;
    }

    return 0;
}

static int mapper_map(struct ra_hwdec_mapper *mapper)
{
    struct priv_owner *p_owner = mapper->owner->priv;
    struct priv *p = mapper->priv;
    VAStatus status;
    VADisplay *display = p_owner->display;

    status = vaExportSurfaceHandle(display, va_surface_id(mapper->src),
                                   VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                   VA_EXPORT_SURFACE_READ_ONLY |
                                   VA_EXPORT_SURFACE_SEPARATE_LAYERS,
                                   &p->desc);
    if (!CHECK_VA_STATUS(mapper, "vaAcquireSurfaceHandle()")) {
        return -1;
    }

    struct ra_imgfmt_desc desc;
    if (!ra_get_imgfmt_desc(mapper->ra, mapper->dst_params.imgfmt, &desc)) {
        goto error;
    }

    const struct pl_gpu *gpu = ra_pl_get(mapper->ra);
    for (int n = 0; n < p->desc.num_layers; n++) {
        if (p->desc.layers[n].num_planes > 1) {
            // Should never happen because we request separate layers
            MP_ERR(mapper, "Multi-plane VA surfaces are not supported\n");
            goto error;
        }

        const struct ra_format *format = desc.planes[n];
        int id = p->desc.layers[n].object_index[0];
        int fd = p->desc.objects[id].fd;
        uint32_t size = p->desc.objects[id].size;
        uint32_t offset = p->desc.layers[n].offset[0];

        struct pl_tex_params tex_params = {
            .w = mp_image_plane_w(&p->layout, n),
            .h = mp_image_plane_h(&p->layout, n),
            .d = 0,
            .format = format->priv,
            .sampleable = true,
            .sample_mode = format->linear_filter ? PL_TEX_SAMPLE_LINEAR
                                                 : PL_TEX_SAMPLE_NEAREST,
            .import_handle = PL_HANDLE_DMA_BUF,
            .shared_mem = (struct pl_shared_mem) {
                .handle = {
                    .fd = fd,
                },
                .size = size,
                .offset = offset,
            },
        };

        const struct pl_tex *pltex = pl_tex_create(gpu, &tex_params);
        if (!pltex) {
            goto error;
        }

        struct ra_tex *ratex = talloc_ptrtype(NULL, ratex);
        int ret = mppl_wrap_tex(mapper->ra, pltex, ratex);
        if (!ret) {
            pl_tex_destroy(gpu, &pltex);
            talloc_free(ratex);
            goto error;
        }
        mapper->tex[n] = ratex;

        MP_TRACE(mapper, "Object %d with fd %d imported as %p\n",
                id, fd, ratex);
    }

    if (p->desc.fourcc == VA_FOURCC_YV12)
        MPSWAP(struct ra_tex*, mapper->tex[1], mapper->tex[2]);

    for (int n = 0; n < p->desc.num_objects; n++) {
        close(p->desc.objects[n].fd);
    }

    return 0;

error:
    for (int n = 0; n < p->desc.num_objects; n++) {
        close(p->desc.objects[n].fd);
    }
    mapper_unmap(mapper);
    return -1;
}

static bool try_format(struct ra_hwdec *hw, struct mp_image *surface)
{
    bool ok = false;
    struct ra_hwdec_mapper *mapper = ra_hwdec_mapper_create(hw, &surface->params);
    if (mapper)
        ok = ra_hwdec_mapper_map(mapper, surface) >= 0;
    ra_hwdec_mapper_free(&mapper);
    return ok;
}

static void determine_working_formats(struct ra_hwdec *hw)
{
    struct priv_owner *p = hw->priv;
    int num_formats = 0;
    int *formats = NULL;

    p->probing_formats = true;

    AVHWFramesConstraints *fc =
            av_hwdevice_get_hwframe_constraints(p->ctx->av_device_ref, NULL);
    if (!fc) {
        MP_WARN(hw, "failed to retrieve libavutil frame constraints\n");
        goto done;
    }
    for (int n = 0; fc->valid_sw_formats[n] != AV_PIX_FMT_NONE; n++) {
        AVBufferRef *fref = NULL;
        struct mp_image *s = NULL;
        AVFrame *frame = NULL;
        fref = av_hwframe_ctx_alloc(p->ctx->av_device_ref);
        if (!fref)
            goto err;
        AVHWFramesContext *fctx = (void *)fref->data;
        fctx->format = AV_PIX_FMT_VAAPI;
        fctx->sw_format = fc->valid_sw_formats[n];
        fctx->width = 1024;
        fctx->height = 1024;
        if (av_hwframe_ctx_init(fref) < 0)
            goto err;
        frame = av_frame_alloc();
        if (!frame)
            goto err;
        if (av_hwframe_get_buffer(fref, frame, 0) < 0)
            goto err;
        s = mp_image_from_av_frame(frame);
        if (!s || !mp_image_params_valid(&s->params))
            goto err;
        if (try_format(hw, s))
            MP_TARRAY_APPEND(p, formats, num_formats, s->params.hw_subfmt);
        else
            MP_WARN(hw, "Unsupported format: %s\n",
                    mp_imgfmt_to_name(s->params.hw_subfmt));
    err:
        talloc_free(s);
        av_frame_free(&frame);
        av_buffer_unref(&fref);
    }
    av_hwframe_constraints_free(&fc);

done:
    MP_TARRAY_APPEND(p, formats, num_formats, 0); // terminate it
    p->formats = formats;
    p->probing_formats = false;

    MP_VERBOSE(hw, "Supported formats:\n");
    for (int n = 0; formats[n]; n++)
        MP_VERBOSE(hw, " %s\n", mp_imgfmt_to_name(formats[n]));
}

const struct ra_hwdec_driver ra_hwdec_vaegl = {
    .name = "vaapi-egl",
    .priv_size = sizeof(struct priv_owner),
    .imgfmts = {IMGFMT_VAAPI, 0},
    .init = init,
    .uninit = uninit,
    .mapper = &(const struct ra_hwdec_mapper_driver){
        .priv_size = sizeof(struct priv),
        .init = mapper_init,
        .uninit = mapper_uninit,
        .map = mapper_map,
        .unmap = mapper_unmap,
    },
};
