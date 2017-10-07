/*
 * Common code related to colorspaces and conversion
 *
 * Copyleft (C) 2009 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
 *
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

#include "config.h"

#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <libavutil/common.h>
#include <libavcodec/avcodec.h>

#include "mp_image.h"
#include "csputils.h"
#include "options/m_config.h"
#include "options/m_option.h"

const struct m_opt_choice_alternatives mp_csp_names[] = {
    {"auto",        PL_COLOR_SYSTEM_UNKNOWN},
    {"bt.601",      PL_COLOR_SYSTEM_BT_601},
    {"bt.709",      PL_COLOR_SYSTEM_BT_709},
    {"smpte-240m",  PL_COLOR_SYSTEM_SMPTE_240M},
    {"bt.2020-ncl", PL_COLOR_SYSTEM_BT_2020_NC},
    {"bt.2020-cl",  PL_COLOR_SYSTEM_BT_2020_C},
    {"rgb",         PL_COLOR_SYSTEM_RGB},
    {"xyz",         PL_COLOR_SYSTEM_XYZ},
    {"ycgco",       PL_COLOR_SYSTEM_YCGCO},
    {0}
};

const struct m_opt_choice_alternatives mp_csp_levels_names[] = {
    {"auto",        PL_COLOR_LEVELS_UNKNOWN},
    {"limited",     PL_COLOR_LEVELS_TV},
    {"full",        PL_COLOR_LEVELS_PC},
    {0}
};

const struct m_opt_choice_alternatives mp_csp_prim_names[] = {
    {"auto",        PL_COLOR_PRIM_UNKNOWN},
    {"bt.601-525",  PL_COLOR_PRIM_BT_601_525},
    {"bt.601-625",  PL_COLOR_PRIM_BT_601_625},
    {"bt.709",      PL_COLOR_PRIM_BT_709},
    {"bt.2020",     PL_COLOR_PRIM_BT_2020},
    {"bt.470m",     PL_COLOR_PRIM_BT_470M},
    {"apple",       PL_COLOR_PRIM_APPLE},
    {"adobe",       PL_COLOR_PRIM_ADOBE},
    {"prophoto",    PL_COLOR_PRIM_PRO_PHOTO},
    {"cie1931",     PL_COLOR_PRIM_CIE_1931},
    {"dci-p3",      PL_COLOR_PRIM_DCI_P3},
    {"v-gamut",     PL_COLOR_PRIM_V_GAMUT},
    {"s-gamut",     PL_COLOR_PRIM_S_GAMUT},
    {0}
};

const struct m_opt_choice_alternatives mp_csp_trc_names[] = {
    {"auto",        PL_COLOR_TRC_UNKNOWN},
    {"bt.1886",     PL_COLOR_TRC_BT_1886},
    {"srgb",        PL_COLOR_TRC_SRGB},
    {"linear",      PL_COLOR_TRC_LINEAR},
    {"gamma1.8",    PL_COLOR_TRC_GAMMA18},
    {"gamma2.2",    PL_COLOR_TRC_GAMMA22},
    {"gamma2.8",    PL_COLOR_TRC_GAMMA28},
    {"prophoto",    PL_COLOR_TRC_PRO_PHOTO},
    {"pq",          PL_COLOR_TRC_PQ},
    {"hlg",         PL_COLOR_TRC_HLG},
    {"v-log",       PL_COLOR_TRC_V_LOG},
    {"s-log1",      PL_COLOR_TRC_S_LOG1},
    {"s-log2",      PL_COLOR_TRC_S_LOG2},
    {0}
};

const struct m_opt_choice_alternatives mp_csp_light_names[] = {
    {"auto",        PL_COLOR_LIGHT_UNKNOWN},
    {"display",     PL_COLOR_LIGHT_DISPLAY},
    {"hlg",         PL_COLOR_LIGHT_SCENE_HLG},
    {"709-1886",    PL_COLOR_LIGHT_SCENE_709_1886},
    {"gamma1.2",    PL_COLOR_LIGHT_SCENE_1_2},
    {0}
};

const struct m_opt_choice_alternatives mp_chroma_names[] = {
    {"unknown",     PL_CHROMA_UNKNOWN},
    {"mpeg2/4/h264",PL_CHROMA_LEFT},
    {"mpeg1/jpeg",  PL_CHROMA_CENTER},
    {0}
};

// The short name _must_ match with what vf_stereo3d accepts (if supported).
// The long name in comments is closer to the Matroska spec (StereoMode element).
// The numeric index matches the Matroska StereoMode value. If you add entries
// that don't match Matroska, make sure demux_mkv.c rejects them properly.
const struct m_opt_choice_alternatives mp_stereo3d_names[] = {
    {"no",     -1}, // disable/invalid
    {"mono",    0},
    {"sbs2l",   1}, // "side_by_side_left"
    {"ab2r",    2}, // "top_bottom_right"
    {"ab2l",    3}, // "top_bottom_left"
    {"checkr",  4}, // "checkboard_right" (unsupported by vf_stereo3d)
    {"checkl",  5}, // "checkboard_left"  (unsupported by vf_stereo3d)
    {"irr",     6}, // "row_interleaved_right"
    {"irl",     7}, // "row_interleaved_left"
    {"icr",     8}, // "column_interleaved_right" (unsupported by vf_stereo3d)
    {"icl",     9}, // "column_interleaved_left" (unsupported by vf_stereo3d)
    {"arcc",   10}, // "anaglyph_cyan_red" (Matroska: unclear which mode)
    {"sbs2r",  11}, // "side_by_side_right"
    {"agmc",   12}, // "anaglyph_green_magenta" (Matroska: unclear which mode)
    {"al",     13}, // "alternating frames left first"
    {"ar",     14}, // "alternating frames right first"
    {0}
};

enum pl_color_system avcol_spc_to_mp_csp(int avcolorspace)
{
    switch (avcolorspace) {
    case AVCOL_SPC_BT709:       return PL_COLOR_SYSTEM_BT_709;
    case AVCOL_SPC_BT470BG:     return PL_COLOR_SYSTEM_BT_601;
    case AVCOL_SPC_BT2020_NCL:  return PL_COLOR_SYSTEM_BT_2020_NC;
    case AVCOL_SPC_BT2020_CL:   return PL_COLOR_SYSTEM_BT_2020_C;
    case AVCOL_SPC_SMPTE170M:   return PL_COLOR_SYSTEM_BT_601;
    case AVCOL_SPC_SMPTE240M:   return PL_COLOR_SYSTEM_SMPTE_240M;
    case AVCOL_SPC_RGB:         return PL_COLOR_SYSTEM_RGB;
    case AVCOL_SPC_YCOCG:       return PL_COLOR_SYSTEM_YCGCO;
    default:                    return PL_COLOR_SYSTEM_UNKNOWN;
    }
}

enum pl_color_levels avcol_range_to_mp_csp_levels(int avrange)
{
    switch (avrange) {
    case AVCOL_RANGE_MPEG:      return PL_COLOR_LEVELS_TV;
    case AVCOL_RANGE_JPEG:      return PL_COLOR_LEVELS_PC;
    default:                    return PL_COLOR_LEVELS_UNKNOWN;
    }
}

enum pl_color_primaries avcol_pri_to_mp_csp_prim(int avpri)
{
    switch (avpri) {
    case AVCOL_PRI_SMPTE240M:   // Same as below
    case AVCOL_PRI_SMPTE170M:   return PL_COLOR_PRIM_BT_601_525;
    case AVCOL_PRI_BT470BG:     return PL_COLOR_PRIM_BT_601_625;
    case AVCOL_PRI_BT709:       return PL_COLOR_PRIM_BT_709;
    case AVCOL_PRI_BT2020:      return PL_COLOR_PRIM_BT_2020;
    case AVCOL_PRI_BT470M:      return PL_COLOR_PRIM_BT_470M;
    default:                    return PL_COLOR_PRIM_UNKNOWN;
    }
}

enum pl_color_transfer avcol_trc_to_mp_csp_trc(int avtrc)
{
    switch (avtrc) {
    case AVCOL_TRC_BT709:
    case AVCOL_TRC_SMPTE170M:
    case AVCOL_TRC_SMPTE240M:
    case AVCOL_TRC_BT1361_ECG:
    case AVCOL_TRC_BT2020_10:
    case AVCOL_TRC_BT2020_12:    return PL_COLOR_TRC_BT_1886;
    case AVCOL_TRC_IEC61966_2_1: return PL_COLOR_TRC_SRGB;
    case AVCOL_TRC_LINEAR:       return PL_COLOR_TRC_LINEAR;
    case AVCOL_TRC_GAMMA22:      return PL_COLOR_TRC_GAMMA22;
    case AVCOL_TRC_GAMMA28:      return PL_COLOR_TRC_GAMMA28;
    case AVCOL_TRC_SMPTEST2084:  return PL_COLOR_TRC_PQ;
    case AVCOL_TRC_ARIB_STD_B67: return PL_COLOR_TRC_HLG;
    default:                     return PL_COLOR_TRC_UNKNOWN;
    }
}

enum pl_chroma_location avchroma_location_to_mp(int avloc)
{
    switch (avloc) {
    case AVCHROMA_LOC_LEFT:             return PL_CHROMA_LEFT;
    case AVCHROMA_LOC_CENTER:           return PL_CHROMA_CENTER;
    default:                            return PL_CHROMA_UNKNOWN;
    }
}

int mp_csp_to_avcol_spc(enum pl_color_system csp)
{
    switch (csp) {
    case PL_COLOR_SYSTEM_BT_709:     return AVCOL_SPC_BT709;
    case PL_COLOR_SYSTEM_BT_601:     return AVCOL_SPC_BT470BG;
    case PL_COLOR_SYSTEM_BT_2020_NC: return AVCOL_SPC_BT2020_NCL;
    case PL_COLOR_SYSTEM_BT_2020_C:  return AVCOL_SPC_BT2020_CL;
    case PL_COLOR_SYSTEM_SMPTE_240M: return AVCOL_SPC_SMPTE240M;
    case PL_COLOR_SYSTEM_RGB:        return AVCOL_SPC_RGB;
    case PL_COLOR_SYSTEM_YCGCO:      return AVCOL_SPC_YCOCG;
    default:                         return AVCOL_SPC_UNSPECIFIED;
    }
}

int mp_csp_levels_to_avcol_range(enum pl_color_levels levels)
{
    switch (levels) {
    case PL_COLOR_LEVELS_TV:      return AVCOL_RANGE_MPEG;
    case PL_COLOR_LEVELS_PC:      return AVCOL_RANGE_JPEG;
    default:                      return AVCOL_RANGE_UNSPECIFIED;
    }
}

int mp_csp_prim_to_avcol_pri(enum pl_color_primaries prim)
{
    switch (prim) {
    case PL_COLOR_PRIM_BT_601_525: return AVCOL_PRI_SMPTE170M;
    case PL_COLOR_PRIM_BT_601_625: return AVCOL_PRI_BT470BG;
    case PL_COLOR_PRIM_BT_709:     return AVCOL_PRI_BT709;
    case PL_COLOR_PRIM_BT_2020:    return AVCOL_PRI_BT2020;
    case PL_COLOR_PRIM_BT_470M:    return AVCOL_PRI_BT470M;
    default:                       return AVCOL_PRI_UNSPECIFIED;
    }
}

int mp_csp_trc_to_avcol_trc(enum pl_color_transfer trc)
{
    switch (trc) {
    // We just call it BT.1886 since we're decoding, but it's still BT.709
    case PL_COLOR_TRC_BT_1886:      return AVCOL_TRC_BT709;
    case PL_COLOR_TRC_SRGB:         return AVCOL_TRC_IEC61966_2_1;
    case PL_COLOR_TRC_LINEAR:       return AVCOL_TRC_LINEAR;
    case PL_COLOR_TRC_GAMMA22:      return AVCOL_TRC_GAMMA22;
    case PL_COLOR_TRC_GAMMA28:      return AVCOL_TRC_GAMMA28;
    case PL_COLOR_TRC_PQ:           return AVCOL_TRC_SMPTEST2084;
    case PL_COLOR_TRC_HLG:          return AVCOL_TRC_ARIB_STD_B67;
    default:                        return AVCOL_TRC_UNSPECIFIED;
    }
}

int mp_chroma_location_to_av(enum pl_chroma_location loc)
{
    switch (loc) {
    case PL_CHROMA_LEFT:            return AVCHROMA_LOC_LEFT;
    case PL_CHROMA_CENTER:          return AVCHROMA_LOC_CENTER;
    default:                        return AVCHROMA_LOC_UNSPECIFIED;
    }
}

#define OPT_BASE_STRUCT struct mp_csp_equalizer_opts

const struct m_sub_options mp_csp_equalizer_conf = {
    .opts = (const m_option_t[]) {
        OPT_INTRANGE("brightness", values[MP_CSP_EQ_BRIGHTNESS], 0, -100, 100),
        OPT_INTRANGE("saturation", values[MP_CSP_EQ_SATURATION], 0, -100, 100),
        OPT_INTRANGE("contrast", values[MP_CSP_EQ_CONTRAST], 0, -100, 100),
        OPT_INTRANGE("hue", values[MP_CSP_EQ_HUE], 0, -100, 100),
        OPT_INTRANGE("gamma", values[MP_CSP_EQ_GAMMA], 0, -100, 100),
        OPT_CHOICE_C("video-output-levels", values[MP_CSP_EQ_OUTPUT_LEVELS], 0,
                     mp_csp_levels_names),
        {0}
    },
    .size = sizeof(struct mp_csp_equalizer_opts),
};

struct mp_csp_equalizer_state *mp_csp_equalizer_create(void *ta_parent,
                                                    struct mpv_global *global)
{
    struct m_config_cache *c = m_config_cache_alloc(ta_parent, global,
                                                    &mp_csp_equalizer_conf);
    // The terrible, terrible truth.
    return (struct mp_csp_equalizer_state *)c;
}

bool mp_csp_equalizer_state_changed(struct mp_csp_equalizer_state *state)
{
    struct m_config_cache *c = (struct m_config_cache *)state;
    return m_config_cache_update(c);
}

void mp_csp_equalizer_state_get(struct mp_csp_equalizer_state *state,
                                struct pl_color_adjustment *out_params,
                                enum pl_color_levels *out_levels)
{
    struct m_config_cache *c = (struct m_config_cache *)state;
    m_config_cache_update(c);
    struct mp_csp_equalizer_opts *eq = c->opts;
    *out_params = (struct pl_color_adjustment) {
        .brightness = eq->values[MP_CSP_EQ_BRIGHTNESS] / 100.0,
        .contrast = (eq->values[MP_CSP_EQ_CONTRAST] + 100) / 100.0,
        .hue = eq->values[MP_CSP_EQ_HUE] / 100.0 * M_PI,
        .saturation = (eq->values[MP_CSP_EQ_SATURATION] + 100) / 100.0,
        .gamma = exp(log(8.0) * eq->values[MP_CSP_EQ_GAMMA] / 100.0),
    };
    *out_levels = eq->values[MP_CSP_EQ_OUTPUT_LEVELS];
}

// Multiply the color in c with the given matrix.
// i/o is {R, G, B} or {Y, U, V} (depending on input/output and matrix), using
// a fixed point representation with the given number of bits (so for bits==8,
// [0,255] maps to [0,1]). The output is clipped to the range as needed.
void mp_map_fixp_color(int ibits, int in[3], int obits, int out[3],
                       struct pl_color_transform t)
{
    for (int i = 0; i < 3; i++) {
        double val = t.c[i];
        for (int x = 0; x < 3; x++)
            val += t.mat.m[i][x] * in[x] / ((1 << ibits) - 1);
        int ival = lrint(val * ((1 << obits) - 1));
        out[i] = av_clip(ival, 0, (1 << obits) - 1);
    }
}

struct pl_color_repr mp_csp_from_image_params(const struct mp_image_params *imgparams)
{
    struct mp_image_params p = *imgparams;
    mp_image_params_guess_csp(&p); // ensure consistency
    return p.color_repr;
}
