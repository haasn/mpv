#pragma once

#include "video/out/gpu/ra.h"

#include "common.h"
#include "utils.h"

struct ra *ra_create_vk(struct mpvk_ctx *vk, struct mp_log *log,
                        struct spirv_compiler *spirv);

// Access to the VkDevice is needed for swapchain creation
VkDevice ra_vk_get_dev(struct ra *ra);

// Allocates a ra_tex that wraps a swapchain image. The contents of the image
// will be invalidated, and access to it will only be internally synchronized.
// So the calling could should not do anything else with the VkImage.
struct ra_tex *ra_vk_wrap_swchain_img(struct ra *ra, VkImage vkimg,
                                      VkSwapchainCreateInfoKHR info);

// This function flushes the command buffers, and enqueues the image for
// presentation. This command must only be used after drawing to the tex,
// but before the command buffers are flushed for other reasons (for
// synchronization). The frames_in_flight pointer will be used to track how
// many frames are currently in flight. (That is, it will be incremented when
// this function is called, and decremented when the command completes)
// `acquired` will be waited on before issueing the rendering command, and
// `index` must correspond to the index of `tex` in `swchain`.
bool ra_vk_present_frame(struct ra *ra, struct ra_tex *tex, VkSemaphore acquired,
                         VkSwapchainKHR swchain, int index, int *inflight);

// May be called on a struct ra of any type. Returns NULL if the ra is not
// a vulkan ra.
struct mpvk_ctx *ra_vk_get(struct ra *ra);
