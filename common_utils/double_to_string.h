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

#pragma once

#include "common.h"
#include "simple_string_stream.h"

namespace ToyLang
{

/* Maximum buffer sizes for conversions. */
constexpr size_t x_default_tostring_buffersize_xint = (1+22) + 1;  /* '0' prefix + uint64_t in octal. */
constexpr size_t x_default_tostring_buffersize_int = (1+10) + 1;  /* Sign + int32_t in decimal. */
constexpr size_t x_default_tostring_buffersize_double = 32 + 1;  /* Must correspond with STRFMT_G14. */
constexpr size_t x_default_tostring_buffersize_ptr = (2+2*sizeof(ptrdiff_t)) + 1;  /* "0x" + hex ptr. */

// 'buf' must be at least of length x_default_tostring_buffersize_double
// Returns the address of the '\0'
//
char* StringifyDoubleUsingDefaultLuaFormattingOptions(char* buf /*out*/, double d);

// 'buf' must be at least of length x_default_tostring_buffersize_int
// Returns the address of the '\0'
//
char* StringifyInt32UsingDefaultLuaFormattingOptions(char* buf /*out*/, int32_t k);

}   // namespace ToyLang
