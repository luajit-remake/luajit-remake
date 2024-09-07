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

#define LJ_IMPORTED_SOURCE

#include "common_utils.h"
#include "simple_string_stream.h"
#include "lj_strfmt.h"
#include "lj_strfmt_details.h"
#include "lj_char_trait_details.h"
#include "lj_strfmt_num.h"
#include "tvalue.h"
#include "lualib_tonumber_util.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wcomma"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wswitch-enum"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
#pragma clang diagnostic ignored "-Wfloat-equal"

static void lj_strfmt_init(FormatState* fs, const char* p, MSize len)
{
    fs->p = (const uint8_t*)p;
    fs->e = (const uint8_t*)p + len;
    /* Must be NUL-terminated. May have NULs inside, too. */
    assert(*fs->e == 0 && "format not NUL-terminated");
}

/* -- Format parser ------------------------------------------------------- */

static const uint8_t strfmt_map[('x' - 'A') + 1] = {
    STRFMT_A, 0, 0, 0, STRFMT_E, STRFMT_F, STRFMT_G, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, STRFMT_X, 0, 0,
    0, 0, 0, 0, 0, 0,
    STRFMT_A, 0, STRFMT_C, STRFMT_D, STRFMT_E, STRFMT_F, STRFMT_G, 0, STRFMT_I, 0, 0, 0, 0,
    0, STRFMT_O, STRFMT_P, STRFMT_Q, 0, STRFMT_S, 0, STRFMT_U, 0, 0, STRFMT_X
};

static SFormat lj_strfmt_parse(FormatState* fs)
{
    const uint8_t *p = fs->p, *e = fs->e;
    fs->str = (const char*)p;
    for (; p < e; p++)
    {
        if (*p == '%')
        {
            /* Escape char? */
            if (p[1] == '%')
            {
                /* '%%'? */
                fs->p = ++p + 1;
                goto retlit;
            }
            else
            {
                SFormat sf = 0;
                uint32_t c;
                if (p != (const uint8_t*)fs->str)
                    break;
                for (p++; (uint32_t)*p - ' ' <= (uint32_t)('0' - ' '); p++)
                {
                    /* Parse flags. */
                    if (*p == '-')
                        sf |= STRFMT_F_LEFT;
                    else if (*p == '+')
                        sf |= STRFMT_F_PLUS;
                    else if (*p == '0')
                        sf |= STRFMT_F_ZERO;
                    else if (*p == ' ')
                        sf |= STRFMT_F_SPACE;
                    else if (*p == '#')
                        sf |= STRFMT_F_ALT;
                    else
                        break;
                }
                if ((uint32_t)*p - '0' < 10)
                {
                    /* Parse width. */
                    uint32_t width = (uint32_t)*p++ - '0';
                    if ((uint32_t)*p - '0' < 10)
                        width = (uint32_t)*p++ - '0' + width * 10;
                    sf |= (width << STRFMT_SH_WIDTH);
                }
                if (*p == '.')
                {
                    /* Parse precision. */
                    uint32_t prec = 0;
                    p++;
                    if ((uint32_t)*p - '0' < 10)
                    {
                        prec = (uint32_t)*p++ - '0';
                        if ((uint32_t)*p - '0' < 10)
                            prec = (uint32_t)*p++ - '0' + prec * 10;
                    }
                    sf |= ((prec + 1) << STRFMT_SH_PREC);
                }
                /* Parse conversion. */
                c = (uint32_t)*p - 'A';
                if (likely(c <= (uint32_t)('x' - 'A')))
                {
                    uint32_t sx = strfmt_map[c];
                    if (sx)
                    {
                        fs->p = p + 1;
                        return (sf | sx | ((c & 0x20) ? 0 : STRFMT_F_UPPER));
                    }
                }
                /* Return error location. */
                if (*p >= 32)
                    p++;
                fs->len = (MSize)(p - (const uint8_t*)fs->str);
                fs->p = fs->e;
                return STRFMT_ERR;
            }
        }
    }
    fs->p = p;
retlit:
    fs->len = (MSize)(p - (const uint8_t*)fs->str);
    return fs->len ? STRFMT_LIT : STRFMT_EOF;
}

/* -- Raw conversions ----------------------------------------------------- */

#define WINT_R(x, sh, sc)                                     \
    {                                                         \
        uint32_t d = (x * (((1 << sh) + sc - 1) / sc)) >> sh; \
        x -= d * sc;                                          \
        *p++ = (char)('0' + d);                               \
    }

/* Write integer to buffer. */
static char* lj_strfmt_wint(char* p, int32_t k)
{
    uint32_t u = (uint32_t)k;
    if (k < 0)
    {
        u = (uint32_t)-k;
        *p++ = '-';
    }
    if (u < 10000)
    {
        if (u < 10) goto dig1;
        if (u < 100) goto dig2;
        if (u < 1000) goto dig3;
    }
    else
    {
        uint32_t v = u / 10000;
        u -= v * 10000;
        if (v < 10000)
        {
            if (v < 10) goto dig5;
            if (v < 100) goto dig6;
            if (v < 1000) goto dig7;
        }
        else
        {
            uint32_t w = v / 10000;
            v -= w * 10000;
            if (w >= 10)
                WINT_R(w, 10, 10)
            *p++ = (char)('0' + w);
        }
        WINT_R(v, 23, 1000)
dig7:   WINT_R(v, 12, 100)
dig6:   WINT_R(v, 10, 10)
dig5:   *p++ = (char)('0' + v);
    }
    WINT_R(u, 23, 1000)
dig3: WINT_R(u, 12, 100)
dig2: WINT_R(u, 10, 10)
dig1: *p++ = (char)('0' + u);
    return p;
}
#undef WINT_R

#define lj_fls(x) ((uint32_t)(__builtin_clz(x) ^ 31))

/* Write pointer to buffer. */
static char* lj_strfmt_wptr(char* p, const void* v)
{
    ptrdiff_t x = (ptrdiff_t)v;
    MSize i, n = STRFMT_MAXBUF_PTR;
    if (x == 0)
    {
        *p++ = 'N';
        *p++ = 'U';
        *p++ = 'L';
        *p++ = 'L';
        return p;
    }

    /* Shorten output for 64 bit pointers. */
    n = 2 + 2 * 4 + ((x >> 32) ? 2 + 2 * (lj_fls((uint32_t)(x >> 32)) >> 3) : 0);

    p[0] = '0';
    p[1] = 'x';
    for (i = n - 1; i >= 2; i--, x >>= 4)
        p[i] = "0123456789abcdef"[(x & 15)];
    return p + n;
}

/* -- Unformatted conversions to buffer ----------------------------------- */

/* Add integer to buffer. */
static SimpleTempStringStream* lj_strfmt_putint(SimpleTempStringStream* sb, int32_t k)
{
    sb->Update(lj_strfmt_wint(sb->Reserve(STRFMT_MAXBUF_INT), k));
    return sb;
}

static SimpleTempStringStream* lj_strfmt_putptr(SimpleTempStringStream* sb, const void* v)
{
    sb->Update(lj_strfmt_wptr(sb->Reserve(STRFMT_MAXBUF_PTR), v));
    return sb;
}

static void lj_buf_putb(SimpleTempStringStream* sb, int c)
{
    char* w = sb->Reserve(1);
    *w++ = (char)c;
    sb->Update(w);
}

/* Add quoted string to buffer. */
static SimpleTempStringStream* strfmt_putquotedlen(SimpleTempStringStream* sb, const char* s, MSize len)
{
    lj_buf_putb(sb, '"');
    while (len--)
    {
        uint32_t c = (uint32_t)(uint8_t)*s++;
        char* w = sb->Reserve(4);
        if (c == '"' || c == '\\' || c == '\n')
        {
            *w++ = '\\';
        }
        else if (lj_char_iscntrl(c))
        { /* This can only be 0-31 or 127. */
            uint32_t d;
            *w++ = '\\';
            if (c >= 100 || lj_char_isdigit((uint8_t)*s))
            {
                *w++ = (char)('0' + (c >= 100));
                if (c >= 100)
                    c -= 100;
                goto tens;
            }
            else if (c >= 10)
            {
            tens:
                d = (c * 205) >> 11;
                c -= d * 10;
                *w++ = (char)('0' + d);
            }
            c += '0';
        }
        *w++ = (char)c;
        sb->Update(w);
    }
    lj_buf_putb(sb, '"');
    return sb;
}

/* -- Formatted conversions to buffer ------------------------------------- */

/* Add formatted char to buffer. */
static SimpleTempStringStream* lj_strfmt_putfchar(SimpleTempStringStream* sb, SFormat sf, int32_t c)
{
    MSize width = STRFMT_WIDTH(sf);
    char* w = sb->Reserve(width > 1 ? width : 1);
    if ((sf & STRFMT_F_LEFT))
        *w++ = (char)c;
    while (width-- > 1)
        *w++ = ' ';
    if (!(sf & STRFMT_F_LEFT))
        *w++ = (char)c;
    sb->Update(w);
    return sb;
}

static char* lj_buf_wmem(char* p, const void* q, MSize len)
{
    return (char*)memcpy(p, q, len) + len;
}

static SimpleTempStringStream* lj_buf_putmem(SimpleTempStringStream* sb, const void* q, MSize len)
{
    char* w = sb->Reserve(len);
    w = lj_buf_wmem(w, q, len);
    sb->Update(w);
    return sb;
}

/* Add formatted string to buffer. */
static SimpleTempStringStream* strfmt_putfstrlen(SimpleTempStringStream* sb, SFormat sf, const char* s, MSize len)
{
    MSize width = STRFMT_WIDTH(sf);
    char* w;
    if (len > STRFMT_PREC(sf))
        len = STRFMT_PREC(sf);
    w = sb->Reserve(width > len ? width : len);
    if ((sf & STRFMT_F_LEFT))
        w = lj_buf_wmem(w, s, len);
    while (width-- > len)
        *w++ = ' ';
    if (!(sf & STRFMT_F_LEFT))
        w = lj_buf_wmem(w, s, len);
    sb->Update(w);
    return sb;
}

/* Add formatted signed/unsigned integer to buffer. */
static SimpleTempStringStream* lj_strfmt_putfxint(SimpleTempStringStream* sb, SFormat sf, uint64_t k)
{
    char buf[STRFMT_MAXBUF_XINT], *q = buf + sizeof(buf), *w;

    MSize prefix = 0, len, prec, pprec, width, need;

    /* Figure out signed prefixes. */
    if (STRFMT_TYPE(sf) == STRFMT_INT)
    {
        if ((int64_t)k < 0)
        {
            k = (uint64_t) - (int64_t)k;
            prefix = 256 + '-';
        }
        else if ((sf & STRFMT_F_PLUS))
        {
            prefix = 256 + '+';
        }
        else if ((sf & STRFMT_F_SPACE))
        {
            prefix = 256 + ' ';
        }
    }

    /* Convert number and store to fixed-size buffer in reverse order. */
    prec = STRFMT_PREC(sf);
    if ((int32_t)prec >= 0)
        sf &= ~STRFMT_F_ZERO;
    if (k == 0)
    {
        /* Special-case zero argument. */
        if (prec != 0 ||
            (sf & (STRFMT_T_OCT | STRFMT_F_ALT)) == (STRFMT_T_OCT | STRFMT_F_ALT))
            *--q = '0';
    }
    else if (!(sf & (STRFMT_T_HEX | STRFMT_T_OCT)))
    {
        /* Decimal. */
        uint32_t k2;
        while ((k >> 32))
        {
            *--q = (char)('0' + k % 10);
            k /= 10;
        }
        k2 = (uint32_t)k;
        do
        {
            *--q = (char)('0' + k2 % 10);
            k2 /= 10;
        } while (k2);
    }
    else if ((sf & STRFMT_T_HEX))
    {
        /* Hex. */
        const char* hexdig = (sf & STRFMT_F_UPPER) ? "0123456789ABCDEF" : "0123456789abcdef";
        do
        {
            *--q = hexdig[(k & 15)];
            k >>= 4;
        } while (k);
        if ((sf & STRFMT_F_ALT))
            prefix = 512 + ((sf & STRFMT_F_UPPER) ? 'X' : 'x');
    }
    else
    {
        /* Octal. */
        do
        {
            *--q = (char)('0' + (uint32_t)(k & 7));
            k >>= 3;
        } while (k);
        if ((sf & STRFMT_F_ALT))
            *--q = '0';
    }

    /* Calculate sizes. */
    len = (MSize)(buf + sizeof(buf) - q);
    if ((int32_t)len >= (int32_t)prec)
        prec = len;
    width = STRFMT_WIDTH(sf);
    pprec = prec + (prefix >> 8);
    need = width > pprec ? width : pprec;
    w = sb->Reserve(need);

    /* Format number with leading/trailing whitespace and zeros. */
    if ((sf & (STRFMT_F_LEFT | STRFMT_F_ZERO)) == 0)
        while (width-- > pprec)
            *w++ = ' ';
    if (prefix)
    {
        if ((char)prefix >= 'X')
            *w++ = '0';
        *w++ = (char)prefix;
    }
    if ((sf & (STRFMT_F_LEFT | STRFMT_F_ZERO)) == STRFMT_F_ZERO)
        while (width-- > pprec)
            *w++ = '0';
    while (prec-- > len)
        *w++ = '0';
    while (q < buf + sizeof(buf))
        *w++ = *q++; /* Add number itself. */
    if ((sf & STRFMT_F_LEFT))
        while (width-- > pprec)
            *w++ = ' ';

    sb->Update(w);
    return sb;
}

/* Add number formatted as signed integer to buffer. */
static SimpleTempStringStream* lj_strfmt_putfnum_int(SimpleTempStringStream* sb, SFormat sf, double n)
{
    int64_t k = (int64_t)n;
    if ((k == (int32_t)(k)) && sf == STRFMT_INT)
        return lj_strfmt_putint(sb, (int32_t)k); /* Shortcut for plain %d. */
    else
        return lj_strfmt_putfxint(sb, sf, (uint64_t)k);
}

/* Add number formatted as unsigned integer to buffer. */
static SimpleTempStringStream* lj_strfmt_putfnum_uint(SimpleTempStringStream* sb, SFormat sf, double n)
{
    int64_t k;
    if (n >= 9223372036854775808.0)
        k = (int64_t)(n - 18446744073709551616.0);
    else
        k = (int64_t)n;
    return lj_strfmt_putfxint(sb, sf, (uint64_t)k);
}

/* Format stack arguments to buffer. */
StrFmtError WARN_UNUSED StringFormatterWithLuaSemantics(SimpleTempStringStream* sb, const char* fmt, size_t fmtLen, TValue* argBegin, size_t narg)
{
    size_t arg = 0;
    FormatState fs;
    SFormat sf;
    lj_strfmt_init(&fs, fmt, fmtLen);
    while ((sf = lj_strfmt_parse(&fs)) != STRFMT_EOF)
    {
        if (sf == STRFMT_LIT)
        {
            lj_buf_putmem(sb, fs.str, fs.len);
        }
        else if (sf == STRFMT_ERR)
        {
            return StrFmtError_BadFmt;
        }
        else
        {
            TValue o = argBegin[arg++];
            if (arg > narg)
            {
                return StrFmtError_TooFewArgs;
            }
            switch (STRFMT_TYPE(sf))
            {
            case STRFMT_INT:
            {
                if (o.Is<tInt32>())
                {
                    int32_t k = o.As<tInt32>();
                    if (sf == STRFMT_INT)
                        lj_strfmt_putint(sb, k); /* Shortcut for plain %d. */
                    else
                        lj_strfmt_putfxint(sb, sf, k);
                    break;
                }

                auto [success, val] = LuaLib_ToNumber(o);
                if (unlikely(!success))
                {
                    return StrFmtError_NotNumber;
                }
                lj_strfmt_putfnum_int(sb, sf, val);
                break;
            }
            case STRFMT_UINT:
            {
                if (o.Is<tInt32>())
                {
                    lj_strfmt_putfxint(sb, sf, o.As<tInt32>());
                    break;
                }

                auto [success, val] = LuaLib_ToNumber(o);
                if (unlikely(!success))
                {
                    return StrFmtError_NotNumber;
                }
                lj_strfmt_putfnum_uint(sb, sf, val);
                break;
            }
            case STRFMT_NUM:
            {
                auto [success, val] = LuaLib_ToNumber(o);
                if (unlikely(!success))
                {
                    return StrFmtError_NotNumber;
                }
                lj_strfmt_putfnum(sb, sf, val);

                break;
            }
            case STRFMT_STR:
            {
                char buf[std::max(x_default_tostring_buffersize_double, x_default_tostring_buffersize_int)];
                MSize len;
                const char* s;
                if (likely(o.Is<tString>()))
                {
                    len = o.As<tString>()->m_length;
                    s = reinterpret_cast<char*>(o.As<tString>()->m_string);
                }
                else if (o.Is<tDouble>())
                {
                    len = static_cast<MSize>(StringifyDoubleUsingDefaultLuaFormattingOptions(buf, o.As<tDouble>()) - buf);
                    s = buf;
                }
                else if (o.Is<tInt32>())
                {
                    len = static_cast<MSize>(StringifyInt32UsingDefaultLuaFormattingOptions(buf, o.As<tInt32>()) - buf);
                    s = buf;
                }
                else
                {
                    return StrFmtError_NotString;
                }
                if ((sf & STRFMT_T_QUOTED))
                    strfmt_putquotedlen(sb, s, len); /* No formatting. */
                else
                    strfmt_putfstrlen(sb, sf, s, len);
                break;
            }
            case STRFMT_CHAR:
            {
                auto [success, val] = LuaLib_ToNumber(o);
                if (unlikely(!success))
                {
                    return StrFmtError_NotNumber;
                }
                lj_strfmt_putfchar(sb, sf, static_cast<int32_t>(val));
                break;
            }
            case STRFMT_PTR: /* No formatting. */
            {
                void* val = nullptr;
                if (o.Is<tHeapEntity>())
                {
                    val = o.As<tHeapEntity>();
                }
                lj_strfmt_putptr(sb, val);
                break;
            }
            default:
                assert(false && "bad string format type");
                __builtin_unreachable();
            }
        }
    }
    return StrFmtNoError;
}

#pragma clang diagnostic pop
