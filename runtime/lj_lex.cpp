/*
** Major portions taken verbatim or adapted from LuaJIT.
** See Copyright Notice below.
**
** LuaJIT -- a Just-In-Time Compiler for Lua. https://luajit.org/
**
** Copyright (C) 2005-2022 Mike Pall. All rights reserved.
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
**
**
** Major portions taken verbatim or adapted from the Lua interpreter.
** See Copyright Notice below.
**
** Lua - An Extensible Extension Language
** Lua.org, PUC-Rio, Brazil (https://www.lua.org)
** Copyright (C) 1994-2008 Lua.org, PUC-Rio.  All rights reserved.
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
******************************************************************************/

#define LJ_IMPORTED_SOURCE

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wcomma"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wswitch-enum"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
#pragma clang diagnostic ignored "-Wfloat-equal"

#include "lj_lex_details.h"
#include "lj_parse_details.h"
#include "lj_char_trait_details.h"
#include "lj_strscan.h"
#include "lj_strfmt.h"
#include "lj_strfmt_details.h"

#include "lj_parser_wrapper.h"
#include "lj_parse_details.h"

#include "vm.h"

#define TKSTR1(name) +1
#define TKSTR2(name, sym) +1
constexpr size_t x_num_tokens = 0 TKDEF(TKSTR1, TKSTR2);
#undef TKSTR1
#undef TKSTR2

/* Lua lexer token names. */
static const char *const tokennames[x_num_tokens + 1] = {
#define TKSTR1(name)		#name,
#define TKSTR2(name, sym)	#sym,
    TKDEF(TKSTR1, TKSTR2)
#undef TKSTR1
#undef TKSTR2
    NULL
};

/* -- Buffer handling ----------------------------------------------------- */

#define LEX_EOF (-1)
#define lex_iseol(ls) (ls->c == '\n' || ls->c == '\r')

NO_INLINE NO_RETURN void parser_throw(LexState* ls)
{
    longjmp(ls->longjmp_buf, 1);
}

/* Get more input from reader. */
static NO_INLINE LexChar lex_more(LexState* ls)
{
    size_t sz;
    const char* p = ls->rfunc(ls->L, ls->rdata, &sz);
    if (p == NULL || sz == 0)
        return LEX_EOF;
    ls->pe = p + sz;
    ls->p = p + 1;
    return (LexChar)(uint8_t)p[0];
}

/* Get next character. */
static ALWAYS_INLINE LexChar lex_next(LexState* ls)
{
    return (ls->c = ls->p < ls->pe ? (LexChar)(uint8_t)*ls->p++ : lex_more(ls));
}

/* Save character. */
static ALWAYS_INLINE void lex_save(LexState* ls, LexChar c)
{
    char* buf = ls->sb->Reserve(1);
    buf[0] = (char)c;
    ls->sb->Update(buf + 1);
}

/* Save previous character and get next character. */
static ALWAYS_INLINE LexChar lex_savenext(LexState* ls)
{
    lex_save(ls, ls->c);
    return lex_next(ls);
}

#define LJ_MAX_LINE 0x7fffff00

/* Skip line break. Handles "\n", "\r", "\r\n" or "\n\r". */
static void lex_newline(LexState* ls)
{
    LexChar old = ls->c;
    assert(lex_iseol(ls) && "bad usage");
    lex_next(ls); /* Skip "\n" or "\r". */
    if (lex_iseol(ls) && ls->c != old)
        lex_next(ls); /* Skip "\n\r" or "\r\n". */
    if (++ls->linenumber >= LJ_MAX_LINE)
    {
        ls->errorCode = LJ_ERR_XLINES;
        ls->errorToken = ls->tok;
        parser_throw(ls);
    }
}

/* -- Scanner for terminals ----------------------------------------------- */

/* Parse a number literal. */
static void lex_number(LexState* ls, TValue* tv)
{
    LexChar c, xp = 'e';
    assert(lj_char_isdigit(ls->c) && "bad usage");
    if ((c = ls->c) == '0' && (lex_savenext(ls) | 0x20) == 'x')
        xp = 'p';
    while (lj_char_isident(ls->c) || ls->c == '.' ||
           ((ls->c == '-' || ls->c == '+') && (c | 0x20) == xp))
    {
        c = ls->c;
        lex_savenext(ls);
    }
    lex_save(ls, '\0');
    assert(ls->sb->Len() > 0);
    StrScanResult scanResult = TryConvertStringToDoubleWithLuaSemantics(ls->sb->Begin(), ls->sb->Len() - 1);
    if (scanResult.fmt == STRSCAN_ERROR)
    {
        ls->errorCode = LJ_ERR_XNUMBER;
        ls->errorToken = TK_number;
        parser_throw(ls);
    }
    assert(scanResult.fmt == STRSCAN_NUM);
    *tv = TValue::Create<tDouble>(scanResult.d);
}

/* Skip equal signs for "[=...=[" and "]=...=]" and return their count. */
static int lex_skipeq(LexState* ls)
{
    int count = 0;
    LexChar s = ls->c;
    assert((s == '[' || s == ']') && "bad usage");
    while (lex_savenext(ls) == '=' && count < 0x20000000)
        count++;
    return (ls->c == s) ? count : (-count) - 1;
}

/* Parse a long string or long comment (tv set to NULL). */
static void lex_longstring(LexState* ls, TValue* tv, int sep)
{
    lex_savenext(ls); /* Skip second '['. */
    if (lex_iseol(ls)) /* Skip initial newline. */
        lex_newline(ls);
    for (;;)
    {
        switch (ls->c)
        {
        case LEX_EOF:
        {
            ls->errorCode = tv ? LJ_ERR_XLSTR : LJ_ERR_XLCOM;
            ls->errorToken = TK_eof;
            parser_throw(ls);
        }
        case ']':
        {
            if (lex_skipeq(ls) == sep)
            {
                lex_savenext(ls); /* Skip second ']'. */
                goto endloop;
            }
            break;
        }
        case '\n':
        case '\r':
        {
            lex_save(ls, '\n');
            lex_newline(ls);
            if (!tv)
                ls->sb->Clear(); /* Don't waste space for comments. */
            break;
        }
        default:
        {
            lex_savenext(ls);
            break;
        }
        }   /* switch ls->c */
    }
endloop:
    if (tv)
    {
        const char* str = ls->sb->Begin() + (2 + (MSize)sep);
        size_t len = ls->sb->Len() - 2 * (2 + (MSize)sep);
        *tv = lj_parse_keepstr(ls, str, len);
    }
}

/* Parse a string. */
static void lex_string(LexState* ls, TValue* tv)
{
    LexChar delim = ls->c; /* Delimiter is '\'' or '"'. */
    lex_savenext(ls);
    while (ls->c != delim)
    {
        switch (ls->c)
        {
        case LEX_EOF:
        {
            ls->errorCode = LJ_ERR_XSTR;
            ls->errorToken = TK_eof;
            parser_throw(ls);
        }
        case '\n':
        case '\r':
        {
            ls->errorCode = LJ_ERR_XSTR;
            ls->errorToken = TK_string;
            parser_throw(ls);
        }
        case '\\':
        {
            LexChar c = lex_next(ls); /* Skip the '\\'. */
            switch (c)
            {
            case 'a': { c = '\a'; break; }
            case 'b': { c = '\b'; break; }
            case 'f': { c = '\f'; break; }
            case 'n': { c = '\n'; break; }
            case 'r': { c = '\r'; break; }
            case 't': { c = '\t'; break; }
            case 'v': { c = '\v'; break; }
            case 'x':
            {
                /* Hexadecimal escape '\xXX'. */
                c = (lex_next(ls) & 15u) << 4;
                if (!lj_char_isdigit(ls->c))
                {
                    if (!lj_char_isxdigit(ls->c))
                        goto err_xesc;
                    c += 9 << 4;
                }
                c += (lex_next(ls) & 15u);
                if (!lj_char_isdigit(ls->c))
                {
                    if (!lj_char_isxdigit(ls->c))
                        goto err_xesc;
                    c += 9;
                }
                break;
            }
            case 'u':
            {
                /* Unicode escape '\u{XX...}'. */
                if (lex_next(ls) != '{') { goto err_xesc; }
                lex_next(ls);
                c = 0;
                do
                {
                    c = (c << 4) | (ls->c & 15u);
                    if (!lj_char_isdigit(ls->c))
                    {
                        if (!lj_char_isxdigit(ls->c)) { goto err_xesc; }
                        c += 9;
                    }
                    if (c >= 0x110000)
                    {
                        /* Out of Unicode range. */
                        goto err_xesc;
                    }
                } while (lex_next(ls) != '}');
                if (c < 0x800)
                {
                    if (c < 0x80) { break; }
                    lex_save(ls, 0xc0 | (c >> 6));
                }
                else
                {
                    if (c >= 0x10000)
                    {
                        lex_save(ls, 0xf0 | (c >> 18));
                        lex_save(ls, 0x80 | ((c >> 12) & 0x3f));
                    }
                    else
                    {
                        if (c >= 0xd800 && c < 0xe000)
                            goto err_xesc; /* No surrogates. */
                        lex_save(ls, 0xe0 | (c >> 12));
                    }
                    lex_save(ls, 0x80 | ((c >> 6) & 0x3f));
                }
                c = 0x80 | (c & 0x3f);
                break;
            }
            case 'z':
            {
                /* Skip whitespace. */
                lex_next(ls);
                while (lj_char_isspace(ls->c))
                    if (lex_iseol(ls))
                        lex_newline(ls);
                    else
                        lex_next(ls);
                continue;
            }
            case '\n':
            case '\r':
            {
                lex_save(ls, '\n');
                lex_newline(ls);
                continue;
            }
            case '\\':
            case '\"':
            case '\'':
            {
                break;
            }
            case LEX_EOF:
            {
                continue;
            }
            default:
            {
                if (!lj_char_isdigit(c))
                {
                    goto err_xesc;
                }
                c -= '0'; /* Decimal escape '\ddd'. */
                if (lj_char_isdigit(lex_next(ls)))
                {
                    c = c * 10 + (ls->c - '0');
                    if (lj_char_isdigit(lex_next(ls)))
                    {
                        c = c * 10 + (ls->c - '0');
                        if (c > 255)
                        {
                        err_xesc:
                            ls->errorCode = LJ_ERR_XESC;
                            ls->errorToken = TK_string;
                            parser_throw(ls);
                        }
                        lex_next(ls);
                    }
                }
                lex_save(ls, c);
                continue;
            }
            } /* switch c */
            lex_save(ls, c);
            lex_next(ls);
            continue;
        }
        default:
        {
            lex_savenext(ls);
            break;
        }
        } /* switch ls->c */
    }
    lex_savenext(ls); /* Skip trailing delimiter. */
    *tv = lj_parse_keepstr(ls, ls->sb->Begin() + 1, ls->sb->Len() - 2);
}

/* -- Main lexical scanner ------------------------------------------------ */

/* Get next lexical token. */
static LexToken lex_scan(LexState* ls, TValue* tv)
{
    ls->sb->Clear();
    for (;;)
    {
        if (lj_char_isident(ls->c))
        {
            if (lj_char_isdigit(ls->c))
            { /* Numeric literal. */
                lex_number(ls, tv);
                return TK_number;
            }
            /* Identifier or reserved word. */
            do
            {
                lex_savenext(ls);
            } while (lj_char_isident(ls->c));
            TValue tvStr = lj_parse_keepstr(ls, ls->sb->Begin(), ls->sb->Len());
            *tv = tvStr;
            assert(tvStr.Is<tString>());
            HeapString* s = TranslateToRawPointer(tvStr.As<tString>());
            if (HeapString::IsReservedWord(s) > 0) /* Reserved word? */
            {
                return TK_OFS + HeapString::GetReservedWordOrdinal(s) + 1;
            }
            return TK_name;
        }
        switch (ls->c)
        {
        case '\n':
        case '\r':
        {
            lex_newline(ls);
            continue;
        }
        case ' ':
        case '\t':
        case '\v':
        case '\f':
        {
            lex_next(ls);
            continue;
        }
        case '-':
        {
            lex_next(ls);
            if (ls->c != '-')
            {
                return '-';
            }
            lex_next(ls);
            if (ls->c == '[')
            {
                /* Long comment "--[=*[...]=*]". */
                int sep = lex_skipeq(ls);
                ls->sb->Clear(); /* `lex_skipeq' may dirty the buffer */
                if (sep >= 0)
                {
                    lex_longstring(ls, NULL, sep);
                    ls->sb->Clear();
                    continue;
                }
            }
            /* Short comment "--.*\n". */
            while (!lex_iseol(ls) && ls->c != LEX_EOF)
            {
                lex_next(ls);
            }
            continue;
        }
        case '[':
        {
            int sep = lex_skipeq(ls);
            if (sep >= 0)
            {
                lex_longstring(ls, tv, sep);
                return TK_string;
            }
            else if (sep == -1)
            {
                return '[';
            }
            else
            {
                ls->errorCode = LJ_ERR_XLDELIM;
                ls->errorToken = TK_string;
                parser_throw(ls);
            }
        }
        case '=':
        {
            lex_next(ls);
            if (ls->c != '=')
                return '=';
            else
            {
                lex_next(ls);
                return TK_eq;
            }
        }
        case '<':
        {
            lex_next(ls);
            if (ls->c != '=')
                return '<';
            else
            {
                lex_next(ls);
                return TK_le;
            }
        }
        case '>':
        {
            lex_next(ls);
            if (ls->c != '=')
                return '>';
            else
            {
                lex_next(ls);
                return TK_ge;
            }
        }
        case '~':
        {
            lex_next(ls);
            if (ls->c != '=')
                return '~';
            else
            {
                lex_next(ls);
                return TK_ne;
            }
        }
        case ':':
        {
            lex_next(ls);
            if (ls->c != ':')
                return ':';
            else
            {
                lex_next(ls);
                return TK_label;
            }
        }
        case '"':
        case '\'':
        {
            lex_string(ls, tv);
            return TK_string;
        }
        case '.':
        {
            if (lex_savenext(ls) == '.')
            {
                lex_next(ls);
                if (ls->c == '.')
                {
                    lex_next(ls);
                    return TK_dots; /* ... */
                }
                return TK_concat; /* .. */
            }
            else if (!lj_char_isdigit(ls->c))
            {
                return '.';
            }
            else
            {
                lex_number(ls, tv);
                return TK_number;
            }
        }
        case LEX_EOF:
        {
            return TK_eof;
        }
        default:
        {
            LexChar c = ls->c;
            lex_next(ls);
            return c; /* Single-char tokens (+ - / ...). */
        }
        }
    }
}

/* -- Lexer API ----------------------------------------------------------- */

/* Setup lexer state. */
void lj_lex_setup(CoroutineRuntimeContext* L, LexState* ls)
{
    ls->L = L;
    ls->fs = NULL;
    ls->pe = ls->p = NULL;
    ls->vstack.clear();
    ls->bcstack.clear();
    ls->tok = 0;
    ls->lookahead = TK_eof; /* No look-ahead token. */
    ls->linenumber = 1;
    ls->lastline = 1;
    ls->endmark = 0;
    ls->errorCode = 0;
    ls->errorMsg = nullptr;
    lex_next(ls); /* Read-ahead first char. */
    if (ls->c == 0xef && ls->p + 2 <= ls->pe && (uint8_t)ls->p[0] == 0xbb && (uint8_t)ls->p[1] == 0xbf)
    {
        /* Skip UTF-8 BOM (if buffered). */
        ls->p += 2;
        lex_next(ls);
    }
    if (ls->c == '#')
    {
        /* Skip POSIX #! header line. */
        do
        {
            lex_next(ls);
            if (ls->c == LEX_EOF)
            {
                return;
            }
        } while (!lex_iseol(ls));
        lex_newline(ls);
    }
}

/* Return next lexical token. */
void lj_lex_next(LexState* ls)
{
    ls->lastline = ls->linenumber;
    if (likely(ls->lookahead == TK_eof))
    {
        /* No lookahead token? */
        ls->tok = lex_scan(ls, &ls->tokval); /* Get next token. */
    }
    else
    {
        /* Otherwise return lookahead token. */
        ls->tok = ls->lookahead;
        ls->lookahead = TK_eof;
        ls->tokval = ls->lookaheadval;
    }
}

/* Look ahead for the next token. */
LexToken lj_lex_lookahead(LexState* ls)
{
    assert(ls->lookahead == TK_eof && "double lookahead");
    ls->lookahead = lex_scan(ls, &ls->lookaheadval);
    return ls->lookahead;
}

/* Initialize strings for reserved words. */
void lj_lex_init(VM* vm)
{
    uint32_t i;
    for (i = 0; i < TK_RESERVED; i++)
    {
        HeapString* s = vm->CreateStringObjectFromRawCString(tokennames[i]);
        HeapString::SetReservedWord(s, i /*reservedWordOrd*/);
    }
}

ParseResult WARN_UNUSED ParseLuaScript(CoroutineRuntimeContext* coroCtx, lua_Reader rd, void* ud)
{
    SimpleTempStringStream ss;
    LexState ls;
    ls.rfunc = rd;
    ls.rdata = ud;
    ls.chunkarg = "?";
    ls.mode = nullptr;
    ls.sb = &ss;

    if (!setjmp(ls.longjmp_buf))
    {
        VM* vm = VM::GetActiveVMForCurrentThread();
        lj_lex_setup(coroCtx, &ls);
        UnlinkedCodeBlock* chunkFn = lj_parse(&ls);
        std::unique_ptr<ScriptModule> module = std::make_unique<ScriptModule>();
        module->m_unlinkedCodeBlocks = std::move(ls.ucbList);
        module->m_defaultGlobalObject = coroCtx->m_globalObject;
        assert(module->m_unlinkedCodeBlocks.size() > 0);
        assert(module->m_unlinkedCodeBlocks.back() == chunkFn);
        for (UnlinkedCodeBlock* ucb : module->m_unlinkedCodeBlocks)
        {
            AssertIff(ucb != chunkFn, ucb->m_parent != nullptr);
            AssertIff(ucb != chunkFn, ucb->m_uvFixUpCompleted);
            assert(ucb->m_defaultCodeBlock == nullptr);
            ucb->m_defaultCodeBlock = CodeBlock::Create(vm, ucb, coroCtx->m_globalObject);
        }
        chunkFn->m_uvFixUpCompleted = true;
        assert(chunkFn->m_numFixedArguments == 0);
        assert(chunkFn->m_numUpvalues == 0);
        UserHeapPointer<FunctionObject> entryPointFunc = FunctionObject::Create(vm, chunkFn->GetCodeBlock(coroCtx->m_globalObject));
        module->m_defaultEntryPoint = entryPointFunc;
        return {
            .m_scriptModule = std::move(module),
            .errMsg = TValue::Create<tNil>()
        };
    }
    else
    {
        // Try to produce some sort of error message...
        //
        constexpr size_t errorMsgBufLen = 1000;
        char errorMsgBuf[errorMsgBufLen + 1];
        size_t offset = 0;
        auto check = [&](int res)
        {
            if (res < 0)
            {
                TestAssert(false);
                return;
            }
            offset += static_cast<size_t>(res);
            offset = std::min(offset, errorMsgBufLen - 1);
        };
        check(snprintf(errorMsgBuf + offset, errorMsgBufLen - offset, "Parser failed "));

        if (ls.linenumber > 0)
        {
            check(snprintf(errorMsgBuf + offset, errorMsgBufLen - offset, "at line %u ", static_cast<unsigned int>(ls.linenumber)));
        }

        check(snprintf(errorMsgBuf + offset, errorMsgBufLen - offset, "with error %d", ls.errorCode));

        if (ls.errorCode > 0 && ls.errorCode < END_OF_ENUM_LJ_PARSER_ERROR)
        {
            check(snprintf(errorMsgBuf + offset, errorMsgBufLen - offset, " (%s, %s)", x_lj_parser_error_name[ls.errorCode], x_lj_parser_error_msg[ls.errorCode]));
        }

        if (ls.errorToken > 0)
        {
            check(snprintf(errorMsgBuf + offset, errorMsgBufLen - offset, ", error token = "));
            if (ls.errorToken > TK_OFS)
            {
                uint32_t tokenId = ls.errorToken-TK_OFS-1;
                if (tokenId < x_num_tokens)
                {
                    check(snprintf(errorMsgBuf + offset, errorMsgBufLen - offset, "%s", tokennames[tokenId]));
                }
                else
                {
                    check(snprintf(errorMsgBuf + offset, errorMsgBufLen - offset, "(invalid token id %u)", static_cast<unsigned int>(tokenId)));
                }
            }
            else
            {
                uint32_t tokenChar = ls.errorToken;
                if (tokenChar < 256 && !lj_char_iscntrl(tokenChar))
                {
                    check(snprintf(errorMsgBuf + offset, errorMsgBufLen - offset, "'%c'", static_cast<char>(tokenChar)));
                }
                else
                {
                    check(snprintf(errorMsgBuf + offset, errorMsgBufLen - offset, "(char %u)", static_cast<unsigned int>(tokenChar)));
                }
            }
        }

        if (ls.errorMsg != nullptr)
        {
            check(snprintf(errorMsgBuf + offset, errorMsgBufLen - offset, ", error msg = %s", ls.errorMsg));
        }

        check(snprintf(errorMsgBuf + offset, errorMsgBufLen - offset, "\n"));

        HeapPtr<HeapString> msg = VM::GetActiveVMForCurrentThread()->CreateStringObjectFromRawString(errorMsgBuf, static_cast<uint32_t>(offset)).As();
        return {
            .m_scriptModule = nullptr,
            .errMsg = TValue::Create<tString>(msg)
        };
    }
}

struct LuaSimpleStringReaderState
{
    const char* m_data;
    size_t m_length;
    bool m_provided;
};

static const char* Parser_LuaSimpleStringReader(CoroutineRuntimeContext* /*ctx*/, void* stateVoid, size_t* size /*out*/)
{
    LuaSimpleStringReaderState* state = reinterpret_cast<LuaSimpleStringReaderState*>(stateVoid);
    if (state->m_provided)
    {
        *size = 0;
        return nullptr;
    }
    state->m_provided = true;
    *size = state->m_length;
    return state->m_data;
}

ParseResult WARN_UNUSED ParseLuaScript(CoroutineRuntimeContext* ctx, const std::string& str)
{
    LuaSimpleStringReaderState state;
    state.m_data = str.data();
    state.m_length = str.length();
    state.m_provided = false;
    return ParseLuaScript(ctx, Parser_LuaSimpleStringReader, &state);
}

ParseResult WARN_UNUSED ParseLuaScript(CoroutineRuntimeContext* ctx, const char* data, size_t length)
{
    LuaSimpleStringReaderState state;
    state.m_data = data;
    state.m_length = length;
    state.m_provided = false;
    return ParseLuaScript(ctx, Parser_LuaSimpleStringReader, &state);
}

struct LuaStringArrayReaderState
{
    TableObject* m_tab;
    uint32_t m_cur;
    uint32_t m_length;
};

static const char* Parser_LuaStringArrayReader(CoroutineRuntimeContext* /*ctx*/, void* stateVoid, size_t* size /*out*/)
{
    LuaStringArrayReaderState* state = reinterpret_cast<LuaStringArrayReaderState*>(stateVoid);
    if (state->m_cur > state->m_length)
    {
        *size = 0;
        return nullptr;
    }
    GetByIntegerIndexICInfo info;
    TableObject::PrepareGetByIntegerIndex(state->m_tab, info /*out*/);
    TValue tv = TableObject::GetByIntegerIndex(state->m_tab, state->m_cur, info);
    state->m_cur++;
    assert(tv.Is<tString>());
    HeapString* s = TranslateToRawPointer(tv.As<tString>());
    *size = s->m_length;
    return reinterpret_cast<const char*>(s->m_string);
}

ParseResult WARN_UNUSED ParseLuaScript(CoroutineRuntimeContext* ctx, TableObject* tab, uint32_t length)
{
#ifndef NDEBUG
    for (uint32_t i = 1; i <= length; i++)
    {
        GetByIntegerIndexICInfo info;
        TableObject::PrepareGetByIntegerIndex(tab, info /*out*/);
        TValue res = TableObject::GetByIntegerIndex(tab, i, info);
        assert(res.Is<tString>());
    }
#endif
    LuaStringArrayReaderState state;
    state.m_tab = tab;
    state.m_cur = 1;
    state.m_length = length;
    return ParseLuaScript(ctx, Parser_LuaStringArrayReader, &state);
}

struct LuaSimpleFileReaderState
{
    static constexpr size_t x_bufSize = 8192;
    FILE* fp;
    char buf[x_bufSize + 1];
};

static const char* Parser_LuaSimpleFileReader(CoroutineRuntimeContext* /*ctx*/, void* stateVoid, size_t* size /*out*/)
{
    LuaSimpleFileReaderState* state = reinterpret_cast<LuaSimpleFileReaderState*>(stateVoid);
    if (feof(state->fp))
    {
        *size = 0;
        return nullptr;
    }
    size_t sizeRead = fread(state->buf, 1, state->x_bufSize, state->fp);
    if (sizeRead == 0)
    {
        *size = 0;
        return nullptr;
    }
    *size = sizeRead;
    return state->buf;
}

ParseResult WARN_UNUSED ParseLuaScriptFromFile(CoroutineRuntimeContext* ctx, const char* fileName)
{
    FILE* fp = fopen(fileName, "rb");
    if (fp == nullptr)
    {
        int err = errno;
        constexpr size_t errorMsgBufLen = 1000;
        char errorMsgBuf[errorMsgBufLen + 1];
        snprintf(errorMsgBuf, errorMsgBufLen, "Failed to open file '%s', error %d (%s)", fileName, err, strerror(err));
        HeapString* msg = VM::GetActiveVMForCurrentThread()->CreateStringObjectFromRawCString(errorMsgBuf);
        return {
            .m_scriptModule = nullptr,
            .errMsg = TValue::Create<tString>(TranslateToHeapPtr(msg))
        };
    }

    LuaSimpleFileReaderState state;
    state.fp = fp;
    ParseResult res = ParseLuaScript(ctx, Parser_LuaSimpleFileReader, &state);
    fclose(fp);

    return res;
}

#pragma clang diagnostic pop
