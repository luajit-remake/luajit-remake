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

#include "string_to_number.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wcomma"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wswitch-enum"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
#pragma clang diagnostic ignored "-Wfloat-equal"

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

/* Options for accepted/returned formats. */
#define STRSCAN_OPT_TOINT	0x01  /* Convert to int32_t, if possible. */
#define STRSCAN_OPT_TONUM	0x02  /* Always convert to double. */
#define STRSCAN_OPT_IMAG	0x04
#define STRSCAN_OPT_LL		0x08
#define STRSCAN_OPT_C		0x10

#define strscan_error_result() StrScanResult { .fmt = STRSCAN_ERROR }

/* -- Scanning numbers ---------------------------------------------------- */

/*
** Rationale for the builtin string to number conversion library:
**
** It removes a dependency on libc's strtod(), which is a true portability
** nightmare. Mainly due to the plethora of supported OS and toolchain
** combinations. Sadly, the various implementations
** a) are often buggy, incomplete (no hex floats) and/or imprecise,
** b) sometimes crash or hang on certain inputs,
** c) return non-standard NaNs that need to be filtered out, and
** d) fail if the locale-specific decimal separator is not a dot,
**    which can only be fixed with atrocious workarounds.
**
** Also, most of the strtod() implementations are hopelessly bloated,
** which is not just an I-cache hog, but a problem for static linkage
** on embedded systems, too.
**
** OTOH the builtin conversion function is very compact. Even though it
** does a lot more, like parsing long longs, octal or imaginary numbers
** and returning the result in different formats:
** a) It needs less than 3 KB (!) of machine code (on x64 with -Os),
** b) it doesn't perform any dynamic allocation and,
** c) it needs only around 600 bytes of stack space.
**
** The builtin function is faster than strtod() for typical inputs, e.g.
** "123", "1.5" or "1e6". Arguably, it's slower for very large exponents,
** which are not very common (this could be fixed, if needed).
**
** And most importantly, the builtin function is equally precise on all
** platforms. It correctly converts and rounds any input to a double.
** If this is not the case, please send a bug report -- but PLEASE verify
** that the implementation you're comparing to is not the culprit!
**
** The implementation quickly pre-scans the entire string first and
** handles simple integers on-the-fly. Otherwise, it dispatches to the
** base-specific parser. Hex and octal is straightforward.
**
** Decimal to binary conversion uses a fixed-length circular buffer in
** base 100. Some simple cases are handled directly. For other cases, the
** number in the buffer is up-scaled or down-scaled until the integer part
** is in the proper range. Then the integer part is rounded and converted
** to a double which is finally rescaled to the result. Denormals need
** special treatment to prevent incorrect 'double rounding'.
*/

/* Definitions for circular decimal digit buffer (base 100 = 2 digits/byte). */
#define STRSCAN_DIG	1024
#define STRSCAN_MAXDIG	800		/* 772 + extra are sufficient. */
#define STRSCAN_DDIG	(STRSCAN_DIG/2)
#define STRSCAN_DMASK	(STRSCAN_DDIG-1)

/* Helpers for circular buffer. */
#define DNEXT(a)	(((a)+1) & STRSCAN_DMASK)
#define DPREV(a)	(((a)-1) & STRSCAN_DMASK)
#define DLEN(lo, hi)	((int32_t)(((lo)-(hi)) & STRSCAN_DMASK))

#define casecmp(c, k)	(((c) | 0x20) == k)

#define U64x(hi, lo)	(((uint64_t)0x##hi << 32) + (uint64_t)0x##lo)

/* Final conversion to double. */
static double WARN_UNUSED strscan_double(uint64_t x, int32_t ex2, int32_t neg)
{
    double n;

    /* Avoid double rounding for denormals. */
    if (unlikely(ex2 <= -1075 && x != 0)) {
        int32_t b = (int32_t)(__builtin_clzll(x)^63);
        if ((int32_t)b + ex2 <= -1023 && (int32_t)b + ex2 >= -1075) {
            uint64_t rb = (uint64_t)1 << (-1075-ex2);
            if ((x & rb) && ((x & (rb+rb+rb-1)))) x += rb+rb;
            x = (x & ~(rb+rb-1));
        }
    }

    /* Convert to double using a signed int64_t conversion, then rescale. */
    assert((int64_t)x >= 0 && "bad double conversion");
    n = (double)(int64_t)x;
    if (neg) n = -n;
    if (ex2) n = ldexp(n, ex2);
    return n;
}

/* Parse hexadecimal number. */
static StrScanResult WARN_UNUSED strscan_hex(const uint8_t *p,
                                             StrScanFmt fmt, uint32_t opt,
                                             int32_t ex2, int32_t neg, uint32_t dig)
{
    uint64_t x = 0;
    uint32_t i;

    /* Scan hex digits. */
    for (i = dig > 16 ? 16 : dig ; i; i--, p++) {
        uint32_t d = (*p != '.' ? *p : *++p); if (d > '9') d += 9;
        x = (x << 4) + (d & 15);
    }

    /* Summarize rounding-effect of excess digits. */
    for (i = 16; i < dig; i++, p++)
        x |= ((*p != '.' ? *p : *++p) != '0'), ex2 += 4;

    /* Format-specific handling. */
    switch (fmt) {
    case STRSCAN_INT:
        if (!(opt & STRSCAN_OPT_TONUM) && x < 0x80000000u+neg) {
            /* Fast path for 32 bit integers. */
            return StrScanResult {
                .fmt = STRSCAN_INT,
                .i32 = neg ? -(int32_t)x : (int32_t)x
            };
        }
        if (!(opt & STRSCAN_OPT_C)) { fmt = STRSCAN_NUM; break; }
        [[fallthrough]];
    case STRSCAN_U32:
        if (dig > 8) return strscan_error_result();
        return StrScanResult { .fmt = STRSCAN_U32, .i32 = neg ? -(int32_t)x : (int32_t)x };
    case STRSCAN_I64:
    case STRSCAN_U64:
        if (dig > 16) return strscan_error_result();
        return StrScanResult { .fmt = fmt, .u64 = neg ? (uint64_t)-(int64_t)x : x };
    default:
        break;
    }

    /* Reduce range, then convert to double. */
    if ((x & U64x(c0000000,0000000))) { x = (x >> 2) | (x & 3); ex2 += 2; }
    return StrScanResult { .fmt = fmt, .d = strscan_double(x, ex2, neg) };
}

/* Parse octal number. */
static StrScanResult WARN_UNUSED strscan_oct(const uint8_t *p,
                                             StrScanFmt fmt, int32_t neg, uint32_t dig)
{
    uint64_t x = 0;

    /* Scan octal digits. */
    if (dig > 22 || (dig == 22 && *p > '1')) return strscan_error_result();
    while (dig-- > 0) {
        if (!(*p >= '0' && *p <= '7')) return strscan_error_result();
        x = (x << 3) + (*p++ & 7);
    }

    /* Format-specific handling. */
    switch (fmt) {
    case STRSCAN_INT:
        if (x >= 0x80000000u+neg) fmt = STRSCAN_U32;
        [[fallthrough]];
    case STRSCAN_U32:
        if ((x >> 32)) return strscan_error_result();
        return StrScanResult { .fmt = fmt, .i32 = neg ? -(int32_t)x : (int32_t)x };
    default:
    case STRSCAN_I64:
    case STRSCAN_U64:
        return StrScanResult { .fmt = fmt, .u64 = neg ? (uint64_t)-(int64_t)x : x };
    }
}

/* Parse decimal number. */
static StrScanResult WARN_UNUSED strscan_dec(const uint8_t *p, StrScanFmt fmt, uint32_t opt, int32_t ex10, int32_t neg, uint32_t dig)
{
    uint8_t xi[STRSCAN_DDIG], *xip = xi;

    if (dig) {
        uint32_t i = dig;
        if (i > STRSCAN_MAXDIG) {
            ex10 += (int32_t)(i - STRSCAN_MAXDIG);
            i = STRSCAN_MAXDIG;
        }
        /* Scan unaligned leading digit. */
        if (((ex10^i) & 1))
            *xip++ = ((*p != '.' ? *p : *++p) & 15), i--, p++;
        /* Scan aligned double-digits. */
        for ( ; i > 1; i -= 2) {
            uint32_t d = 10 * ((*p != '.' ? *p : *++p) & 15); p++;
            *xip++ = d + ((*p != '.' ? *p : *++p) & 15); p++;
        }
        /* Scan and realign trailing digit. */
        if (i) *xip++ = 10 * ((*p != '.' ? *p : *++p) & 15), ex10--, dig++, p++;

        /* Summarize rounding-effect of excess digits. */
        if (dig > STRSCAN_MAXDIG) {
            do {
                if ((*p != '.' ? *p : *++p) != '0') { xip[-1] |= 1; break; }
                p++;
            } while (--dig > STRSCAN_MAXDIG);
            dig = STRSCAN_MAXDIG;
        } else {  /* Simplify exponent. */
            while (ex10 > 0 && dig <= 18) *xip++ = 0, ex10 -= 2, dig += 2;
        }
    } else {  /* Only got zeros. */
        ex10 = 0;
        xi[0] = 0;
    }

    /* Fast path for numbers in integer format (but handles e.g. 1e6, too). */
    if (dig <= 20 && ex10 == 0) {
        uint8_t *xis;
        uint64_t x = xi[0];
        double n;
        for (xis = xi+1; xis < xip; xis++) x = x * 100 + *xis;
        if (!(dig == 20 && (xi[0] > 18 || (int64_t)x >= 0))) {  /* No overflow? */
            /* Format-specific handling. */
            switch (fmt) {
            case STRSCAN_INT:
                if (!(opt & STRSCAN_OPT_TONUM) && x < 0x80000000u+neg) {
                    /* Fast path for 32 bit integers. */
                    return StrScanResult { .fmt = STRSCAN_INT, .i32 = neg ? -(int32_t)x : (int32_t)x };
                }
                if (!(opt & STRSCAN_OPT_C)) { fmt = STRSCAN_NUM; goto plainnumber; }
                [[fallthrough]];
            case STRSCAN_U32:
                if ((x >> 32) != 0) return strscan_error_result();
                return StrScanResult { .fmt = STRSCAN_U32, .i32 = neg ? -(int32_t)x : (int32_t)x };
            case STRSCAN_I64:
            case STRSCAN_U64:
                return StrScanResult { .fmt = fmt, .u64 = neg ? (uint64_t)-(int64_t)x : x };
            default:
plainnumber:    /* Fast path for plain numbers < 2^63. */
                if ((int64_t)x < 0) break;
                n = (double)(int64_t)x;
                if (neg) n = -n;
                 return StrScanResult { .fmt = fmt, .d = n };
            }
        }
    }

    /* Slow non-integer path. */
    if (fmt == STRSCAN_INT) {
        if ((opt & STRSCAN_OPT_C)) return strscan_error_result();
        fmt = STRSCAN_NUM;
    } else if (fmt > STRSCAN_INT) {
        return strscan_error_result();
    }

    {
        uint32_t hi = 0, lo = (uint32_t)(xip-xi);
        int32_t ex2 = 0, idig = (int32_t)lo + (ex10 >> 1);

        assert(lo > 0 && (ex10 & 1) == 0);

        /* Handle simple overflow/underflow. */
        if (idig > 310/2) {
            if (neg) {
                return StrScanResult { .fmt = fmt, .d = -std::numeric_limits<double>::infinity() };
            } else {
                return StrScanResult { .fmt = fmt, .d = std::numeric_limits<double>::infinity() };
            }
        }
        else if (idig < -326/2) { return StrScanResult { .fmt = fmt, .d = (neg ? -0.0 : 0.0) }; }

        /* Scale up until we have at least 17 or 18 integer part digits. */
        while (idig < 9 && idig < DLEN(lo, hi)) {
            uint32_t i, cy = 0;
            ex2 -= 6;
            for (i = DPREV(lo); ; i = DPREV(i)) {
                uint32_t d = (xi[i] << 6) + cy;
                cy = (((d >> 2) * 5243) >> 17); d = d - cy * 100;  /* Div/mod 100. */
                xi[i] = (uint8_t)d;
                if (i == hi) break;
                if (d == 0 && i == DPREV(lo)) lo = i;
            }
            if (cy) {
                hi = DPREV(hi);
                if (xi[DPREV(lo)] == 0) lo = DPREV(lo);
                else if (hi == lo) { lo = DPREV(lo); xi[DPREV(lo)] |= xi[lo]; }
                xi[hi] = (uint8_t)cy; idig++;
            }
        }

        /* Scale down until no more than 17 or 18 integer part digits remain. */
        while (idig > 9) {
            uint32_t i = hi, cy = 0;
            ex2 += 6;
            do {
                cy += xi[i];
                xi[i] = (cy >> 6);
                cy = 100 * (cy & 0x3f);
                if (xi[i] == 0 && i == hi) hi = DNEXT(hi), idig--;
                i = DNEXT(i);
            } while (i != lo);
            while (cy) {
                if (hi == lo) { xi[DPREV(lo)] |= 1; break; }
                xi[lo] = (cy >> 6); lo = DNEXT(lo);
                cy = 100 * (cy & 0x3f);
            }
        }

        /* Collect integer part digits and convert to rescaled double. */
        {
            uint64_t x = xi[hi];
            uint32_t i;
            for (i = DNEXT(hi); --idig > 0 && i != lo; i = DNEXT(i))
                x = x * 100 + xi[i];
            if (i == lo) {
                while (--idig >= 0) x = x * 100;
            } else {  /* Gather round bit from remaining digits. */
                x <<= 1; ex2--;
                do {
                    if (xi[i]) { x |= 1; break; }
                    i = DNEXT(i);
                } while (i != lo);
            }
            return StrScanResult { .fmt = fmt, .d = strscan_double(x, ex2, neg) };
        }
    }
}

/* Parse binary number. */
static StrScanResult WARN_UNUSED strscan_bin(const uint8_t *p, StrScanFmt fmt, uint32_t opt, int32_t ex2, int32_t neg, uint32_t dig)
{
    uint64_t x = 0;
    uint32_t i;

    if (ex2 || dig > 64) return strscan_error_result();

    /* Scan binary digits. */
    for (i = dig; i; i--, p++) {
        if ((*p & ~1) != '0') return strscan_error_result();
        x = (x << 1) | (*p & 1);
    }

    /* Format-specific handling. */
    switch (fmt) {
    case STRSCAN_INT:
        if (!(opt & STRSCAN_OPT_TONUM) && x < 0x80000000u+neg) {
            return StrScanResult { .fmt = STRSCAN_INT, .i32 =  neg ? -(int32_t)x : (int32_t)x }; /* Fast path for 32 bit integers. */
        }
        if (!(opt & STRSCAN_OPT_C)) { fmt = STRSCAN_NUM; break; }
        [[fallthrough]];
    case STRSCAN_U32:
        if (dig > 32) return strscan_error_result();
            return StrScanResult { .fmt = STRSCAN_U32, .i32 = neg ? -(int32_t)x : (int32_t)x };
    case STRSCAN_I64:
    case STRSCAN_U64:
        return StrScanResult { .fmt = fmt, .u64 = neg ? (uint64_t)-(int64_t)x : x };
    default:
        break;
    }

    /* Reduce range, then convert to double. */
    if ((x & U64x(c0000000,0000000))) { x = (x >> 2) | (x & 3); ex2 += 2; }
    return StrScanResult { .fmt = fmt, .d = strscan_double(x, ex2, neg) };
}

/* Scan string containing a number. Returns format. Returns value in o. */
static StrScanResult WARN_UNUSED lj_strscan_scan(const uint8_t *p, size_t len, uint32_t opt)
{
    int32_t neg = 0;
    const uint8_t *pe = p + len;

    /* Remove leading space, parse sign and non-numbers. */
    if (unlikely(!lj_char_isdigit(*p))) {
        while (lj_char_isspace(*p)) p++;
        if (*p == '+' || *p == '-') neg = (*p++ == '-');
        if (unlikely(*p >= 'A')) {  /* Parse "inf", "infinity" or "nan". */
            double tmp;
            tmp = std::numeric_limits<double>::quiet_NaN();
            if (casecmp(p[0],'i') && casecmp(p[1],'n') && casecmp(p[2],'f')) {
                if (neg) tmp = -std::numeric_limits<double>::infinity(); else tmp = std::numeric_limits<double>::infinity();
                p += 3;
                if (casecmp(p[0],'i') && casecmp(p[1],'n') && casecmp(p[2],'i') &&
                    casecmp(p[3],'t') && casecmp(p[4],'y')) p += 5;
            } else if (casecmp(p[0],'n') && casecmp(p[1],'a') && casecmp(p[2],'n')) {
                p += 3;
            }
            while (lj_char_isspace(*p)) p++;
            if (*p || p < pe) return strscan_error_result();
            return StrScanResult { .fmt = STRSCAN_NUM, .d = tmp };
        }
    }

    /* Parse regular number. */
    {
        StrScanFmt fmt = STRSCAN_INT;
        int cmask = LJ_CHAR_DIGIT;
        int base = (opt & STRSCAN_OPT_C) && *p == '0' ? 0 : 10;
        const uint8_t *sp, *dp = NULL;
        uint32_t dig = 0, hasdig = 0, x = 0;
        int32_t ex = 0;

        /* Determine base and skip leading zeros. */
        if (unlikely(*p <= '0')) {
            if (*p == '0') {
                if (casecmp(p[1], 'x'))
                    base = 16, cmask = LJ_CHAR_XDIGIT, p += 2;
                else if (casecmp(p[1], 'b'))
                    base = 2, cmask = LJ_CHAR_DIGIT, p += 2;
            }
            for ( ; ; p++) {
                if (*p == '0') {
                    hasdig = 1;
                } else if (*p == '.') {
                    if (dp) return strscan_error_result();
                    dp = p;
                } else {
                    break;
                }
            }
        }

        /* Preliminary digit and decimal point scan. */
        for (sp = p; ; p++) {
            if (likely(lj_char_isa(*p, cmask))) {
                x = x * 10 + (*p & 15);  /* For fast path below. */
                dig++;
            } else if (*p == '.') {
                if (dp) return strscan_error_result();
                dp = p;
            } else {
                break;
            }
        }
        if (!(hasdig | dig)) return strscan_error_result();

        /* Handle decimal point. */
        if (dp) {
            if (base == 2) return strscan_error_result();
            fmt = STRSCAN_NUM;
            if (dig) {
                ex = (int32_t)(dp-(p-1)); dp = p-1;
                while (ex < 0 && *dp-- == '0') ex++, dig--;  /* Skip trailing zeros. */
                if (base == 16) ex *= 4;
            }
        }

        /* Parse exponent. */
        if (base >= 10 && casecmp(*p, (uint32_t)(base == 16 ? 'p' : 'e'))) {
            uint32_t xx;
            int negx = 0;
            fmt = STRSCAN_NUM; p++;
            if (*p == '+' || *p == '-') negx = (*p++ == '-');
            if (!lj_char_isdigit(*p)) return strscan_error_result();
            xx = (*p++ & 15);
            while (lj_char_isdigit(*p)) {
                if (xx < 65536) xx = xx * 10 + (*p & 15);
                p++;
            }
            ex += negx ? -(int32_t)xx : (int32_t)xx;
        }

        /* Parse suffix. */
        if (*p) {
            /* I (IMAG), U (U32), LL (I64), ULL/LLU (U64), L (long), UL/LU (ulong). */
            /* NYI: f (float). Not needed until cp_number() handles non-integers. */
            if (casecmp(*p, 'i')) {
                if (!(opt & STRSCAN_OPT_IMAG)) return strscan_error_result();
                p++; fmt = STRSCAN_IMAG;
            } else if (fmt == STRSCAN_INT) {
                if (casecmp(*p, 'u')) p++, fmt = STRSCAN_U32;
                if (casecmp(*p, 'l')) {
                    p++;
                    if (casecmp(*p, 'l')) p++, fmt = static_cast<StrScanFmt>(fmt + STRSCAN_I64 - STRSCAN_INT);
                    else if (!(opt & STRSCAN_OPT_C)) return strscan_error_result();
                    else if (sizeof(long) == 8) fmt = static_cast<StrScanFmt>(fmt + STRSCAN_I64 - STRSCAN_INT);
                }
                if (casecmp(*p, 'u') && (fmt == STRSCAN_INT || fmt == STRSCAN_I64))
                    p++, fmt = static_cast<StrScanFmt>(fmt + STRSCAN_U32 - STRSCAN_INT);
                if ((fmt == STRSCAN_U32 && !(opt & STRSCAN_OPT_C)) ||
                    (fmt >= STRSCAN_I64 && !(opt & STRSCAN_OPT_LL)))
                    return strscan_error_result();
            }
            while (lj_char_isspace(*p)) p++;
            if (*p) return strscan_error_result();
        }
        if (p < pe) return strscan_error_result();

        /* Fast path for decimal 32 bit integers. */
        if (fmt == STRSCAN_INT && base == 10 &&
            (dig < 10 || (dig == 10 && *sp <= '2' && x < 0x80000000u+neg))) {
            if ((opt & STRSCAN_OPT_TONUM)) {
                return StrScanResult { .fmt = STRSCAN_NUM, .d = neg ? -(double)x : (double)x };
            } else {
                return StrScanResult { .fmt = STRSCAN_INT, .i32 = neg ? -(int32_t)x : (int32_t)x };
            }
        }

        /* Dispatch to base-specific parser. */
        if (base == 0 && !(fmt == STRSCAN_NUM || fmt == STRSCAN_IMAG))
            return strscan_oct(sp, fmt, neg, dig);

        StrScanResult res;
        if (base == 16)
            res = strscan_hex(sp, fmt, opt, ex, neg, dig);
        else if (base == 2)
            res = strscan_bin(sp, fmt, opt, ex, neg, dig);
        else
            res = strscan_dec(sp, fmt, opt, ex, neg, dig);

        /* Try to convert number to integer, if requested. */
        if (res.fmt == STRSCAN_NUM && (opt & STRSCAN_OPT_TOINT)) {
            double n = res.d;
            int32_t i = ((int32_t)(n));
            if (n == (double)i) { return StrScanResult { .fmt = STRSCAN_INT, .i32 = i }; }
        }
        return res;
    }
}

StrScanResult WARN_UNUSED TryConvertStringToDoubleWithLuaSemantics(const void* str, size_t len)
{
    StrScanResult res = lj_strscan_scan((const uint8_t *)str, len,
                                        STRSCAN_OPT_TONUM);
    assert((res.fmt == STRSCAN_ERROR || res.fmt == STRSCAN_NUM) && "bad scan format");
    return res;
}

StrScanResult WARN_UNUSED TryConvertStringToDoubleOrInt32WithLuaSemantics(const void* str, size_t len)
{
    StrScanResult res = lj_strscan_scan((const uint8_t *)str, len,
                                        STRSCAN_OPT_TOINT);
    assert((res.fmt == STRSCAN_ERROR || res.fmt == STRSCAN_NUM || res.fmt == STRSCAN_INT)
           && "bad scan format");
    return res;
}

StrScanResult WARN_UNUSED TryConvertStringWithBaseToDoubleWithLuaSemantics(int32_t base, const void* str)
{
    const char *p = reinterpret_cast<const char*>(str);
    char *ep;
    unsigned int neg = 0;
    unsigned long ul;
    while (lj_char_isspace((unsigned char)(*p))) p++;
    if (*p == '-') { p++; neg = 1; } else if (*p == '+') { p++; }
    if (lj_char_isalnum((unsigned char)(*p)))
    {
        ul = strtoul(p, &ep, base);
        if (p != ep)
        {
            while (lj_char_isspace((unsigned char)(*ep))) ep++;
            if (*ep == '\0')
            {
                double res = static_cast<double>(ul);
                if (neg) { res = -res; }
                return StrScanResult { .fmt = STRSCAN_NUM, .d = res };
            }
        }
    }
    return StrScanResult { .fmt = STRSCAN_ERROR };
}

#undef DNEXT
#undef DPREV
#undef DLEN

#pragma clang diagnostic pop
