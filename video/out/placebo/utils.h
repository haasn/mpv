#pragma once

#include "common/common.h"
#include "common/msg.h"

#include <libplacebo/common.h>

struct pl_context *mppl_ctx_create(void *tactx, struct mp_log *log);

static inline struct pl_rect2d mp_rect2d_to_pl(struct mp_rect rc)
{
    return (struct pl_rect2d) {
        .x0 = rc.x0,
        .y0 = rc.y0,
        .x1 = rc.x1,
        .y1 = rc.y1,
    };
}
