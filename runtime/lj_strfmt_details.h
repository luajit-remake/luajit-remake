#pragma once

// This file contains code from LuaJIT, see copyright notice below.

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
#include "simple_string_stream.h"

typedef uint32_t SFormat;  /* Format indicator. */

using MSize = size_t;

/* Format parser state. */
typedef struct FormatState {
    const uint8_t *p;	/* Current format string pointer. */
    const uint8_t *e;	/* End of format string. */
    const char *str;	/* Returned literal string. */
    MSize len;		/* Size of literal string. */
} FormatState;

/* Format types (max. 16). */
typedef enum FormatType {
    STRFMT_EOF, STRFMT_ERR, STRFMT_LIT,
    STRFMT_INT, STRFMT_UINT, STRFMT_NUM, STRFMT_STR, STRFMT_CHAR, STRFMT_PTR
} FormatType;

/* Format subtypes (bits are reused). */
#define STRFMT_T_HEX	0x0010	/* STRFMT_UINT */
#define STRFMT_T_OCT	0x0020	/* STRFMT_UINT */
#define STRFMT_T_FP_A	0x0000	/* STRFMT_NUM */
#define STRFMT_T_FP_E	0x0010	/* STRFMT_NUM */
#define STRFMT_T_FP_F	0x0020	/* STRFMT_NUM */
#define STRFMT_T_FP_G	0x0030	/* STRFMT_NUM */
#define STRFMT_T_QUOTED	0x0010	/* STRFMT_STR */

/* Format flags. */
#define STRFMT_F_LEFT	0x0100
#define STRFMT_F_PLUS	0x0200
#define STRFMT_F_ZERO	0x0400
#define STRFMT_F_SPACE	0x0800
#define STRFMT_F_ALT	0x1000
#define STRFMT_F_UPPER	0x2000

/* Format indicator fields. */
#define STRFMT_SH_WIDTH	16
#define STRFMT_SH_PREC	24

#define STRFMT_TYPE(sf)		((FormatType)((sf) & 15))
#define STRFMT_WIDTH(sf)	(((sf) >> STRFMT_SH_WIDTH) & 255u)
#define STRFMT_PREC(sf)		((((sf) >> STRFMT_SH_PREC) & 255u) - 1u)
#define STRFMT_FP(sf)		(((sf) >> 4) & 3)

/* Formats for conversion characters. */
#define STRFMT_A	(STRFMT_NUM|STRFMT_T_FP_A)
#define STRFMT_C	(STRFMT_CHAR)
#define STRFMT_D	(STRFMT_INT)
#define STRFMT_E	(STRFMT_NUM|STRFMT_T_FP_E)
#define STRFMT_F	(STRFMT_NUM|STRFMT_T_FP_F)
#define STRFMT_G	(STRFMT_NUM|STRFMT_T_FP_G)
#define STRFMT_I	STRFMT_D
#define STRFMT_O	(STRFMT_UINT|STRFMT_T_OCT)
#define STRFMT_P	(STRFMT_PTR)
#define STRFMT_Q	(STRFMT_STR|STRFMT_T_QUOTED)
#define STRFMT_S	(STRFMT_STR)
#define STRFMT_U	(STRFMT_UINT)
#define STRFMT_X	(STRFMT_UINT|STRFMT_T_HEX)
#define STRFMT_G14	(STRFMT_G | ((14+1) << STRFMT_SH_PREC))

/* Maximum buffer sizes for conversions. */
#define STRFMT_MAXBUF_XINT	(1+22)  /* '0' prefix + uint64_t in octal. */
#define STRFMT_MAXBUF_INT	(1+10)  /* Sign + int32_t in decimal. */
#define STRFMT_MAXBUF_NUM	32  /* Must correspond with STRFMT_G14. */
#define STRFMT_MAXBUF_PTR	(2+2*sizeof(ptrdiff_t))  /* "0x" + hex ptr. */

SimpleTempStringStream *lj_strfmt_putfnum(SimpleTempStringStream *sb, uint32_t sf, double n);
