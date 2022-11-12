// This file contains code adapted from LuaJIT.
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

#include "runtime_utils.h"

// LuaJIT employed a home-brewed implementation of 'pow' for integer exponent case.
// It is just the doubling algorithm for fast math power, but implemented in hand-tuned assembly.
//
// LuaJIT's original implementation involves direct asm call, which is not possible to use in C inline assembly
// ('call' in inline asm is generally unsafe due to red zone). So I manually split out the negative exponent case
// and the positive exponent case as two separate functions as a workaround.
//
// It turns out that LuaJIT's implementation is only faster than standard library (libm)'s implementation
// for small exponents (glibc version: 2.35).
//
// Specifically, LuaJIT's implementation is significantly faster than libm if the exponent is within [-32, 32),
// and in parallel with (sometimes faster than) libm if the exponent is within [-128, 128),
// but significantly slower than libm if the exponent does not fall within the [-128, 128) range.
//
// So we will only use LuaJIT's implementation if the exponent is an integer that falls within [-128, 128).
// This extra check for fastpath results in about 3% slowdown if the fastpath ends up not being used.
// I believe this is fair, since small integer exponent should consist of the majority of the uses of 'pow'.
//

// The assembly code is adapted, with changes, from LuaJIT vm_x64.dasc
//
static double WARN_UNUSED ALWAYS_INLINE math_fastpow_negative_or_zero_int_exponent(double b, int32_t e)
{
    uint64_t gpr1;
    double fpr1;
    double out;
    asm (
        "movabsq $0x3ff0000000000000, %[r1];"
        "movq %[r1], %[x3];"
        "test %[r0], %[r0];"
        "je 6f;"
        "neg %[r0];"
        "1:;"
        "test $0x1, %[r0];"
        "jnz 2f;"
        "mulsd %[x0], %[x0];"
        "shr $0x1, %[r0];"
        "jmp 1b;"
        "2:;"
        "shr $0x1, %[r0];"
        "jz 5f;"
        "movapd %[x0], %[x2];"
        "3:;"
        "mulsd %[x0], %[x0];"
        "shr $0x1, %[r0];"
        "jz 4f;"
        "jnc 3b;"
        "mulsd %[x0], %[x2];"
        "jmp 3b;"
        "4:;"
        "mulsd %[x2], %[x0];"
        "5:;"
        "divsd %[x0], %[x3];"
        "6:;"
        :
        [x0] "+x"(b) /*inout*/,
        [r0] "+r"(e) /*inout*/,
        [x2] "=&x"(fpr1) /*scratch*/,
        [x3] "=&x"(out) /*out*/,
        [r1] "=&r"(gpr1) /*scratch*/
        :   /*no read-only input*/
        :   "cc" /*clobber*/);
    return out;
}

// The assembly code is adapted, with changes, from LuaJIT vm_x64.dasc
//
static double WARN_UNUSED ALWAYS_INLINE math_fast_pow_positive_int_exponent(double b, int32_t e)
{
    uint64_t gpr1;
    double fpr1;
    asm (
        "cmp $0x2, %[r0];"
        "jle 4f;"
        "1:;"
        "test $0x1, %[r0];"
        "jnz 2f;"
        "mulsd %[x0], %[x0];"
        "shr $0x1, %[r0];"
        "jmp 1b;"
        "2:;"
        "shr $0x1, %[r0];"
        "jz 6f;"
        "movapd %[x0], %[x2];"
        "3:;"
        "mulsd %[x0], %[x0];"
        "shr $0x1, %[r0];"
        "jz 5f;"
        "jnc 3b;"
        "mulsd %[x0], %[x2];"
        "jmp 3b;"
        "4:;"
        "movapd %[x0], %[x2];"
        "jne 6f;"
        "5:;"
        "mulsd %[x2], %[x0];"
        "6:;"
        :
        [x0] "+x"(b) /*inout*/,
        [r0] "+r"(e) /*inout*/,
        [x2] "=&x"(fpr1) /*scratch*/,
        [r1] "=&r"(gpr1) /*scratch*/
        :  /*no read-only input*/
        :  "cc" /*clobber*/);
    return b;
}

// Computes b^ex
//
// For some reason this triggers clang spurious warning that cannot be fixed.. So we have to supress the warning.
//
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconditional-uninitialized"
double WARN_UNUSED math_fast_pow(double b, double ex)
{
    double fpr1;
    int32_t e;
    asm goto (
        "cvtsd2si %[x1], %[r0];"
        "movsbl %b[r0], %[r0];"             // only use fastpath if exponent is within [-128, 128)
        "cvtsi2sd %[r0], %[x2];"
        "ucomisd %[x1], %[x2];"
        "jne %l[slowpath];"
        "jp %l[slowpath];"
        :
            [r0] "=r"(e) /*out*/,
            [x2] "=&x"(fpr1) /*scratch*/
        :
            [x1] "x"(ex) /*in*/
        :
            "cc" /*clobber*/
        :
            slowpath /*goto*/);
    if (e > 0)
    {
        return math_fast_pow_positive_int_exponent(b, e);
    }
    else
    {
        return math_fastpow_negative_or_zero_int_exponent(b, e);
    }
slowpath:
    return pow(b, ex);
}
#pragma clang diagnostic pop
