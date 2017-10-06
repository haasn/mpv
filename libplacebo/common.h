#pragma once

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "mpv_talloc.h"
#include "config.h"

// Align up to the nearest multiple of an arbitrary alignment, which may also
// be 0 to signal no alignment requirements.
#define PL_ALIGN(x, align) ((align) ? ((x) + (align) - 1) / (align) * (align) : (x))

// This is faster but must only be called on positive powers of two.
#define PL_ALIGN2(x, align) (((x) + (align) - 1) & ~((align) - 1))

// Returns the size of a static array with known size.
#define PL_ARRAY_SIZE(s) (sizeof(s) / sizeof((s)[0]))

// Swaps two variables
#define PL_SWAP(a, b)           \
    do {                        \
        __typeof__ tmp = (a);   \
        (a) = (b);              \
        (b) = tmp;              \
    } while (0)
