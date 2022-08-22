// This file contains code from LuaJIT, slightly modified to fit our purpose.
// See copyright notice below.

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

#include "common.h"

/* Returned format. */
// DEVNOTE: Don't touch this enum! LuaJIT has fragile code that relies on the ordering of this enum...
//
enum StrScanFmt
{
    STRSCAN_ERROR,
    STRSCAN_NUM,
    STRSCAN_IMAG,
    STRSCAN_INT,
    STRSCAN_U32,
    STRSCAN_I64,
    STRSCAN_U64,
} ;

// x64 cc can return two registers, so returning this struct is fine
//
struct StrScanResult
{
    StrScanFmt fmt;
    union {
        int32_t i32;
        uint64_t u64;
        double d;
    };
};
static_assert(sizeof(StrScanResult) == 16);

// The returned 'fmt' must be STRSCAN_ERROR or STRSCAN_NUM
//
StrScanResult WARN_UNUSED TryConvertStringToDoubleWithLuaSemantics(const void* str, size_t len);

// The returned 'fmt' must be STRSCAN_ERROR or STRSCAN_NUM or STRSCAN_INT
//
StrScanResult WARN_UNUSED TryConvertStringToDoubleOrInt32WithLuaSemantics(const void* str, size_t len);
