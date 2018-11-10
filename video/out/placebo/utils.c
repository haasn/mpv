#include "common/common.h"
#include "utils.h"

static const int pl_log_to_msg_lev[PL_LOG_ALL+1] = {
    [PL_LOG_FATAL] = MSGL_FATAL,
    [PL_LOG_ERR]   = MSGL_ERR,
    [PL_LOG_WARN]  = MSGL_WARN,
    [PL_LOG_INFO]  = MSGL_V,
    [PL_LOG_DEBUG] = MSGL_DEBUG,
    [PL_LOG_TRACE] = MSGL_TRACE,
};

static const enum pl_log_level msg_lev_to_pl_log[MSGL_MAX+1] = {
    [MSGL_FATAL]   = PL_LOG_FATAL,
    [MSGL_ERR]     = PL_LOG_ERR,
    [MSGL_WARN]    = PL_LOG_WARN,
    [MSGL_INFO]    = PL_LOG_WARN,
    [MSGL_STATUS]  = PL_LOG_WARN,
    [MSGL_V]       = PL_LOG_INFO,
    [MSGL_DEBUG]   = PL_LOG_DEBUG,
    [MSGL_TRACE]   = PL_LOG_TRACE,
    [MSGL_MAX]     = PL_LOG_ALL,
};

static void log_cb(void *priv, enum pl_log_level level, const char *msg)
{
    struct mp_log *log = priv;
    mp_msg(log, pl_log_to_msg_lev[level], "%s\n", msg);
}

struct pl_context *mppl_ctx_create(void *tactx, struct mp_log *log)
{
    log = mp_log_new(tactx, log, "libplacebo");
    assert(log);

    struct pl_context *ctx;
    ctx = pl_context_create(PL_API_VER, &(struct pl_context_params) {
        .log_cb     = log_cb,
        .log_level  = msg_lev_to_pl_log[mp_msg_level(log)],
        .log_priv   = log,
    });

    if (!ctx) {
        talloc_free(log);
        return NULL;
    }

    return ctx;
}
