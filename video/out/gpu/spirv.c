#include "common/msg.h"
#include "options/m_config.h"

#include "spirv.h"
#include "config.h"

extern const struct spirv_compiler_fns spirv_shaderc;
extern const struct spirv_compiler_fns spirv_nvidia_builtin;
extern const struct spirv_compiler_fns spirv_glslang_subprocess;

// in probe-order
enum {
    SPIRV_AUTO = 0,
    SPIRV_SHADERC, // generally preferred, but not packaged everywhere
    SPIRV_NVIDIA,  // can be useful for testing, only available on nvidia
    SPIRV_GLSLANG, // dumb hack for when all else fails
};

static const struct spirv_compiler_fns *compilers[] = {
#if HAVE_SHADERC
    [SPIRV_SHADERC] = &spirv_shaderc,
#endif
#if HAVE_VULKAN
    [SPIRV_NVIDIA]  = &spirv_nvidia_builtin,
#endif
    [SPIRV_GLSLANG] = &spirv_glslang_subprocess,
};

static const struct m_opt_choice_alternatives compiler_choices[] = {
    {"auto",        SPIRV_AUTO},
#if HAVE_SHADERC
    {"shaderc",     SPIRV_SHADERC},
#endif
#if HAVE_VULKAN
    {"nvidia",      SPIRV_NVIDIA},
#endif
    {"glslang-bin", SPIRV_GLSLANG},
    {0}
};

struct spirv_opts {
    int compiler;
};

#define OPT_BASE_STRUCT struct spirv_opts
const struct m_sub_options spirv_conf = {
    .opts = (const struct m_option[]) {
        OPT_CHOICE_C("spirv-compiler", compiler, 0, compiler_choices),
        {0}
    },
    .size = sizeof(struct spirv_opts),
};

bool spirv_compiler_init(struct ra_ctx *ctx)
{
    struct spirv_opts *opts = mp_get_config_group(ctx, ctx->global, &spirv_conf);
    int compiler = opts->compiler;
    talloc_free(opts);

    for (int i = SPIRV_AUTO+1; i < MP_ARRAY_SIZE(compilers); i++) {
        if (compiler != SPIRV_AUTO && i != compiler)
            continue;
        if (!compilers[i])
            continue;

        ctx->spirv = talloc_zero(NULL, struct spirv_compiler);
        ctx->spirv->log = ctx->log,
        ctx->spirv->fns = compilers[i];

        const char *name = m_opt_choice_str(compiler_choices, i);
        strncpy(ctx->spirv->name, name, sizeof(ctx->spirv->name));
        MP_VERBOSE(ctx, "Initializing SPIR-V compiler '%s'\n", name);
        if (ctx->spirv->fns->init(ctx))
            return true;
        talloc_free(ctx->spirv);
        ctx->spirv = NULL;
    }

    MP_ERR(ctx, "Failed initializing SPIR-V compiler!\n");
    return false;
}
