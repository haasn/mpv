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

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "options/m_option.h"
#include "libplacebo/public/colorspace.h"

/* NOTE: the csp and levels UNKNOWN values are converted to specific ones
 * above vf/vo level. At least vf_scale relies on all valid settings being
 * nonzero at vf/vo level.
 */

extern const struct m_opt_choice_alternatives mp_csp_names[];
extern const struct m_opt_choice_alternatives mp_csp_levels_names[];
extern const struct m_opt_choice_alternatives mp_csp_prim_names[];
extern const struct m_opt_choice_alternatives mp_csp_trc_names[];
extern const struct m_opt_choice_alternatives mp_csp_light_names[];
extern const struct m_opt_choice_alternatives mp_chroma_names[];

// The numeric values (except -1) match the Matroska StereoMode element value.
enum mp_stereo3d_mode {
    MP_STEREO3D_INVALID = -1,
    /* only modes explicitly referenced in the code are listed */
    MP_STEREO3D_MONO = 0,
    MP_STEREO3D_SBS2L = 1,
    MP_STEREO3D_AB2R = 2,
    MP_STEREO3D_AB2L = 3,
    MP_STEREO3D_SBS2R = 11,
    /* no explicit enum entries for most valid values */
    MP_STEREO3D_COUNT = 15, // 14 is last valid mode
};

extern const struct m_opt_choice_alternatives mp_stereo3d_names[];

#define MP_STEREO3D_NAME(x) m_opt_choice_str(mp_stereo3d_names, x)

#define MP_STEREO3D_NAME_DEF(x, def) \
    (MP_STEREO3D_NAME(x) ? MP_STEREO3D_NAME(x) : (def))

extern const struct m_sub_options mp_csp_equalizer_conf;

enum mp_csp_equalizer_param {
    MP_CSP_EQ_BRIGHTNESS,
    MP_CSP_EQ_CONTRAST,
    MP_CSP_EQ_HUE,
    MP_CSP_EQ_SATURATION,
    MP_CSP_EQ_GAMMA,
    MP_CSP_EQ_OUTPUT_LEVELS,
    MP_CSP_EQ_COUNT,
};

// Default initialization with 0 is enough, except for the capabilities field
struct mp_csp_equalizer_opts {
    // Value for each property is in the range [-100, 100].
    // 0 is default, meaning neutral or no change.
    int values[MP_CSP_EQ_COUNT];
};

struct mpv_global;
struct mp_csp_equalizer_state *mp_csp_equalizer_create(void *ta_parent,
                                                    struct mpv_global *global);
bool mp_csp_equalizer_state_changed(struct mp_csp_equalizer_state *state);
void mp_csp_equalizer_state_get(struct mp_csp_equalizer_state *state,
                                struct pl_color_adjustment *out_params,
                                enum pl_color_levels *out_levels);

enum pl_color_space avcol_spc_to_mp_csp(int avcolorspace);
enum pl_color_levels avcol_range_to_mp_csp_levels(int avrange);
enum pl_color_primaries avcol_pri_to_mp_csp_prim(int avpri);
enum pl_color_transfer avcol_trc_to_mp_csp_trc(int avtrc);
enum pl_chroma_location avchroma_location_to_mp(int avloc);

int mp_csp_to_avcol_spc(enum pl_color_space csp);
int mp_csp_levels_to_avcol_range(enum pl_color_levels levels);
int mp_csp_prim_to_avcol_pri(enum pl_color_primaries prim);
int mp_csp_trc_to_avcol_trc(enum pl_color_transfer trc);
int mp_chroma_location_to_av(enum pl_chroma_location loc);

void mp_map_fixp_color(int ibits, int in[3], int obits, int out[3],
                       struct pl_color_transform t);

struct mp_image_params;
struct pl_color mp_csp_from_image_params(const struct mp_image_params *imgparams);
