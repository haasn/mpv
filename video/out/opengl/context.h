#pragma once

#include "common/global.h"
#include "video/out/gpu/context.h"
#include "common.h"

extern const int mpgl_preferred_gl_versions[];

// Returns whether or not a candidate GL version should be accepted or not
// (based on the --opengl opts). Implementations may call this before
// ra_gl_ctx_init if they wish to probe for multiple possible GL versions.
bool ra_gl_ctx_test_version(struct ra_ctx *ctx, int version, bool es);

// These are a set of helpers for ra_ctx providers based on ra_gl.
// The init function also initializes ctx->ra and ctx->swchain, so the user
// doesn't have to do this manually. (Similarly, the uninit function will
// clean them up)

struct ra_gl_ctx_params {
    // Set to the platform-specific function to swap buffers, like
    // glXSwapBuffers, eglSwapBuffers etc. Required.
    void (*swap_buffers)(struct ra_ctx *ctx);

    // Set to false if the implementation follows normal GL semantics, which is
    // upside down. Set to true if it does *not*, i.e. if rendering is right
    // side up
    bool flipped;

    // Set to true if the context is using its own internal swapchain
    // mechanism. (This basically disables simulating --swapchain-depth using
    // GL sync objects)
    bool external_swchain;

    // For hwdec_vaegl.c:
    const char *native_display_type;
    void *native_display;
};

void ra_gl_ctx_uninit(struct ra_ctx *ctx);
bool ra_gl_ctx_init(struct ra_ctx *ctx, GL *gl, struct ra_gl_ctx_params params);

// Call this any time the window size or main framebuffer changes
void ra_gl_ctx_resize(struct ra_swchain *sw, int w, int h, int fbo);

// These functions are normally set in the ra_swchain->fns, but if an
// implementation has a need to override this fns struct with custom functions
// for whatever reason, these can be used to inherit the original behavior.
int ra_gl_ctx_color_depth(struct ra_swchain *sw);
struct mp_image *ra_gl_ctx_screenshot(struct ra_swchain *sw);
void ra_gl_ctx_update_length(struct ra_swchain *sw, int depth);
bool ra_gl_ctx_start_frame(struct ra_swchain *sw, struct ra_fbo *out_fbo);
bool ra_gl_ctx_submit_frame(struct ra_swchain *sw, const struct vo_frame *frame);
void ra_gl_ctx_swap_buffers(struct ra_swchain *sw);
