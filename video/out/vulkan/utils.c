#include "video/out/placebo/utils.h"
#include "utils.h"

bool mpvk_init(struct mpvk_ctx *vk, struct ra_ctx *ctx, const char *surface_ext)
{
    vk->ctx = mppl_ctx_create(ctx, ctx->log);
    if (!vk->ctx)
        goto error;

    const char *exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        surface_ext,
    };

    vk->vkinst = pl_vk_inst_create(vk->ctx, &(struct pl_vk_inst_params) {
        .debug = ctx->opts.debug,
        .extensions = exts,
        .num_extensions = MP_ARRAY_SIZE(exts),
    });

    if (!vk->vkinst)
        goto error;

    return true;

error:
    mpvk_uninit(vk);
    return false;
}

void mpvk_uninit(struct mpvk_ctx *vk)
{
    if (vk->surface) {
        assert(vk->vkinst);
        vkDestroySurfaceKHR(vk->vkinst->instance, vk->surface, NULL);
        vk->surface = NULL;
    }

    pl_vk_inst_destroy(&vk->vkinst);
    pl_context_destroy(&vk->ctx);
}
