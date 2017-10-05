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

#include "bstr/bstr.h"

// Encode the unicode codepoint as UTF-8, and append to the end of the
// talloc'ed buffer. All guarantees bstr_xappend() give applies, such as
// implicit \0-termination for convenience.
void bstr_append_utf8(void *talloc_ctx, struct bstr *buf, uint32_t codepoint);

// Decode the UTF-8 code point at the start of the string, and return the
// character.
// After calling this function, *out_next will point to the next character.
// out_next can be NULL.
// On error, -1 is returned, and *out_next is not modified.
int bstr_decode_utf8(struct bstr str, struct bstr *out_next);

// Return the UTF-8 code point at the start of the string.
// After calling this function, *out_next will point to the next character.
// out_next can be NULL.
// On error, an empty string is returned, and *out_next is not modified.
struct bstr bstr_split_utf8(struct bstr str, struct bstr *out_next);

// Return the length of the UTF-8 sequence that starts with the given byte.
// Given a string char *s, the next UTF-8 code point is to be expected at
//      s + bstr_parse_utf8_code_length(s[0])
// On error, -1 is returned. On success, it returns a value in the range [1, 4].
int bstr_parse_utf8_code_length(unsigned char b);

// Return >= 0 if the string is valid UTF-8, otherwise negative error code.
// Embedded \0 bytes are considered valid.
// This returns -N if the UTF-8 string was likely just cut-off in the middle of
// an UTF-8 sequence: -1 means 1 byte was missing, -5 5 bytes missing.
// If the string was likely not cut off, -8 is returned.
// Use (return_value > -8) to check whether the string is valid UTF-8 or valid
// but cut-off UTF-8.
int bstr_validate_utf8(struct bstr s);

// Force the input string to valid UTF-8. If invalid UTF-8 encoding is
// encountered, the invalid bytes are interpreted as Latin-1.
// Embedded \0 bytes are considered valid.
// If replacement happens, a newly allocated string is returned (with a \0
// byte added past its end for convenience). The string is allocated via
// talloc, with talloc_ctx as parent.
struct bstr bstr_sanitize_utf8_latin1(void *talloc_ctx, struct bstr s);
