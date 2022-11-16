#pragma once

// This file contains code from LuaJIT. See copyright notice below.

/*
** LuaJIT -- a Just-In-Time Compiler for Lua. https://luajit.org/
**
** Copyright (C) 2005-2021 Mike Pall. All rights reserved.
**
** Permission is hereby granted, free of charge, to any person obtaining
** a copy of this software and associated documentation files (the
** "Software"), to deal in the Software without restriction, including
** without limitation the rights to use, copy, modify, merge, publish,
** distribute, sublicense, and/or sell copies of the Software, and to
** permit persons to whom the Software is furnished to do so, subject to
** the following conditions:
**
** The above copyright notice and this permission notice shall be
** included in all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
** EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
** MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
** CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
** TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
** SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**
** [ MIT license: https://www.opensource.org/licenses/mit-license.php ]
*/

#ifndef LJ_IMPORTED_SOURCE
#error "This file should only be included by imported old LuaJIT code!"
#endif

#include "common.h"

#define LJ_CHAR_CNTRL	0x01
#define LJ_CHAR_SPACE	0x02
#define LJ_CHAR_PUNCT	0x04
#define LJ_CHAR_DIGIT	0x08
#define LJ_CHAR_XDIGIT	0x10
#define LJ_CHAR_UPPER	0x20
#define LJ_CHAR_LOWER	0x40
#define LJ_CHAR_IDENT	0x80
#define LJ_CHAR_ALPHA	(LJ_CHAR_LOWER|LJ_CHAR_UPPER)
#define LJ_CHAR_ALNUM	(LJ_CHAR_ALPHA|LJ_CHAR_DIGIT)
#define LJ_CHAR_GRAPH	(LJ_CHAR_ALNUM|LJ_CHAR_PUNCT)
/* Only pass -1 or 0..255 to these macros. Never pass a signed char! */
#define lj_char_isa(c, t)	((lj_char_bits+1)[(c)] & t)
#define lj_char_iscntrl(c)	lj_char_isa((c), LJ_CHAR_CNTRL)
#define lj_char_isspace(c)	lj_char_isa((c), LJ_CHAR_SPACE)
#define lj_char_ispunct(c)	lj_char_isa((c), LJ_CHAR_PUNCT)
#define lj_char_isdigit(c)	lj_char_isa((c), LJ_CHAR_DIGIT)
#define lj_char_isxdigit(c)	lj_char_isa((c), LJ_CHAR_XDIGIT)
#define lj_char_isupper(c)	lj_char_isa((c), LJ_CHAR_UPPER)
#define lj_char_islower(c)	lj_char_isa((c), LJ_CHAR_LOWER)
#define lj_char_isident(c)	lj_char_isa((c), LJ_CHAR_IDENT)
#define lj_char_isalpha(c)	lj_char_isa((c), LJ_CHAR_ALPHA)
#define lj_char_isalnum(c)	lj_char_isa((c), LJ_CHAR_ALNUM)
#define lj_char_isgraph(c)	lj_char_isa((c), LJ_CHAR_GRAPH)
#define lj_char_toupper(c)	((c) - (lj_char_islower(c) >> 1))
#define lj_char_tolower(c)	((c) + lj_char_isupper(c))

constexpr uint8_t lj_char_bits[257] = {
    0,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  3,  3,  3,  3,  3,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    2,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
    152,152,152,152,152,152,152,152,152,152,  4,  4,  4,  4,  4,  4,
    4,176,176,176,176,176,176,160,160,160,160,160,160,160,160,160,
    160,160,160,160,160,160,160,160,160,160,160,  4,  4,  4,  4,132,
    4,208,208,208,208,208,208,192,192,192,192,192,192,192,192,192,
    192,192,192,192,192,192,192,192,192,192,192,  4,  4,  4,  4,  1,
    128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
    128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
    128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
    128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
    128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
    128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
    128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
    128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128
};
