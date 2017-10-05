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

#include <string.h>
#include <strings.h>
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#include <libavutil/common.h>

#include "bstr_utf8.h"

void bstr_append_utf8(void *talloc_ctx, struct bstr *buf, uint32_t codepoint)
{
    char data[8];
    uint8_t tmp;
    char *output = data;
    PUT_UTF8(codepoint, tmp, *output++ = tmp;);
    bstr_xappend(talloc_ctx, buf, (bstr){data, output - data});
}

int bstr_parse_utf8_code_length(unsigned char b)
{
    if (b < 128)
        return 1;
    int bytes = 7 - av_log2(b ^ 255);
    return (bytes >= 2 && bytes <= 4) ? bytes : -1;
}

int bstr_decode_utf8(struct bstr s, struct bstr *out_next)
{
    if (s.len == 0)
        return -1;
    unsigned int codepoint = s.start[0];
    s.start++; s.len--;
    if (codepoint >= 128) {
        int bytes = bstr_parse_utf8_code_length(codepoint);
        if (bytes < 1 || s.len < bytes - 1)
            return -1;
        codepoint &= 127 >> bytes;
        for (int n = 1; n < bytes; n++) {
            int tmp = (unsigned char)s.start[0];
            if ((tmp & 0xC0) != 0x80)
                return -1;
            codepoint = (codepoint << 6) | (tmp & ~0xC0);
            s.start++; s.len--;
        }
        if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
            return -1;
        // Overlong sequences - check taken from libavcodec.
        // (The only reason we even bother with this is to make libavcodec's
        //  retarded subtitle utf-8 check happy.)
        unsigned int min = bytes == 2 ? 0x80 : 1 << (5 * bytes - 4);
        if (codepoint < min)
            return -1;
    }
    if (out_next)
        *out_next = s;
    return codepoint;
}

struct bstr bstr_split_utf8(struct bstr str, struct bstr *out_next)
{
    bstr rest;
    int code = bstr_decode_utf8(str, &rest);
    if (code < 0)
        return (bstr){0};
    if (out_next)
        *out_next = rest;
    return bstr_splice(str, 0, str.len - rest.len);
}

int bstr_validate_utf8(struct bstr s)
{
    while (s.len) {
        if (bstr_decode_utf8(s, &s) < 0) {
            // Try to guess whether the sequence was just cut-off.
            unsigned int codepoint = (unsigned char)s.start[0];
            int bytes = bstr_parse_utf8_code_length(codepoint);
            if (bytes > 1 && s.len < 6) {
                // Manually check validity of left bytes
                for (int n = 1; n < bytes; n++) {
                    if (n >= s.len) {
                        // Everything valid until now - just cut off.
                        return -(bytes - s.len);
                    }
                    int tmp = (unsigned char)s.start[n];
                    if ((tmp & 0xC0) != 0x80)
                        break;
                }
            }
            return -8;
        }
    }
    return 0;
}

struct bstr bstr_sanitize_utf8_latin1(void *talloc_ctx, struct bstr s)
{
    bstr new = {0};
    bstr left = s;
    unsigned char *first_ok = s.start;
    while (left.len) {
        int r = bstr_decode_utf8(left, &left);
        if (r < 0) {
            bstr_xappend(talloc_ctx, &new, (bstr){first_ok, left.start - first_ok});
            bstr_append_utf8(talloc_ctx, &new, (unsigned char)left.start[0]);
            left.start += 1;
            left.len -= 1;
            first_ok = left.start;
        }
    }
    if (!new.start)
        return s;
    if (first_ok != left.start)
        bstr_xappend(talloc_ctx, &new, (bstr){first_ok, left.start - first_ok});
    return new;
}
