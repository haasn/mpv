#include "common/msg.h"
#include "osdep/io.h"
#include "osdep/subprocess.h"

#include "context.h"
#include "spirv.h"

// Part of the glslang API
static const char *shader_names[] = {
    [GLSL_SHADER_VERTEX]   = "vert",
    [GLSL_SHADER_FRAGMENT] = "frag",
    [GLSL_SHADER_COMPUTE]  = "comp",
};

static bool braindeath(struct spirv_compiler *spirv, void *tactx,
                       enum glsl_shader type, const char *glsl,
                       struct bstr *out_spirv)
{
    // FIXME: use real tmpfiles, or stdin
    char *tmp_glsl  = "/tmp/glslang-hack.glsl";
    char *tmp_spirv = "/tmp/glslang-hack.spirv";

    FILE *fglsl = fopen(tmp_glsl, "wb");
    fputs(glsl, fglsl);
    fclose(fglsl);

    char *glslang_args[] = {
        "glslangValidator", "-V",
        "-o", tmp_spirv,
        "-S", (char *)shader_names[type],
        tmp_glsl,
        NULL
    };

    int status = mp_subprocess(glslang_args, NULL, spirv, NULL, NULL,
                               &(char*){0});
    // TODO: delete fglsl
    // TODO: redirect stdout/stderr

    if (status != 0)
        return false;

    // XXX: use stream_read_file
    FILE *fspirv = fopen(tmp_spirv, "rb");
    if (!fspirv) {
        MP_ERR(spirv, "glslang returned success but no SPIR-V found!\n");
        return false;
    }

    fseek(fspirv, 0, SEEK_END);
    out_spirv->len = ftell(fspirv);
    assert(out_spirv->len % 4 == 0);
    fseek(fspirv, 0, SEEK_SET);
    out_spirv->start = talloc_size(tactx, out_spirv->len);
    fread(out_spirv->start, out_spirv->len, 1, fspirv);
    fclose(fspirv);

    // TODO: delete fspirv
    return true;
}

static bool glslang_init(struct ra_ctx *ctx)
{
    ctx->spirv->glsl_version = 450; // detecting would be annoying...
    return true;
}

const struct spirv_compiler_fns spirv_glslang_subprocess = {
    .compile_glsl = braindeath,
    .init = glslang_init,
};
