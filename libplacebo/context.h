#pragma once

#include <stdarg.h>
#include <stdbool.h>

#include "misc/bstr.h"
#include "public/context.h"

struct pl_context {
    // Logging-related state:
    enum pl_log_level loglevel;
    struct bstr logbuffer;
    void *logpriv;
    void (*logfun)(void *priv, enum pl_log_level level, const char *msg);
};

// Logging-related functions
static inline bool pl_msg_test(struct pl_context *ctx, enum pl_log_level lev)
{
    return ctx->logfun && ctx->loglevel >= lev;
}

void pl_msg(struct pl_context *ctx, enum pl_log_level lev, const char *fmt, ...)
    PRINTF_ATTRIBUTE(3, 4);
void pl_msg_va(struct pl_context *ctx, enum pl_log_level lev, const char *fmt,
               va_list va);

// Convenience macros
#define pl_fatal(log, ...)      pl_msg(ctx, PL_LOG_FATAL, __VA_ARGS__)
#define pl_err(log, ...)        pl_msg(ctx, PL_LOG_ERR, __VA_ARGS__)
#define pl_warn(log, ...)       pl_msg(ctx, PL_LOG_WARN, __VA_ARGS__)
#define pl_info(log, ...)       pl_msg(ctx, PL_LOG_INFO, __VA_ARGS__)
#define pl_debug(log, ...)      pl_msg(ctx, PL_LOG_DEBUG, __VA_ARGS__)
#define pl_trace(log, ...)      pl_msg(ctx, PL_LOG_TRACE, __VA_ARGS__)

#define PL_MSG(obj, lev, ...)   pl_msg((obj)->log, lev, __VA_ARGS__)

#define PL_FATAL(obj, ...)      PL_MSG(obj, PL_LOG_FATAL, __VA_ARGS__)
#define PL_ERR(obj, ...)        PL_MSG(obj, PL_LOG_ERR, __VA_ARGS__)
#define PL_WARN(obj, ...)       PL_MSG(obj, PL_LOG_WARN, __VA_ARGS__)
#define PL_INFO(obj, ...)       PL_MSG(obj, PL_LOG_INFO, __VA_ARGS__)
#define PL_DEBUG(obj, ...)      PL_MSG(obj, PL_LOG_DEBUG, __VA_ARGS__)
#define PL_TRACE(obj, ...)      PL_MSG(obj, PL_LOG_TRACE, __VA_ARGS__)
