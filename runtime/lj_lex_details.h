#pragma once

// This file contains code from LuaJIT, see copyright notice below.

/*
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
*/

#ifndef LJ_IMPORTED_SOURCE
#error "This file should only be included by imported old LuaJIT code!"
#endif

#include "common.h"

#include <stdarg.h>
#include <setjmp.h>
#include "tvalue.h"
#include "simple_string_stream.h"

using BCPos = uint32_t;
using BCLine = uint32_t;
using MSize = uint64_t;

/* Lua lexer tokens. */
#define TKDEF(_, __) \
  _(and) _(break) _(do) _(else) _(elseif) _(end) _(false) \
  _(for) _(function) _(goto) _(if) _(in) _(local) _(nil) _(not) _(or) \
  _(repeat) _(return) _(then) _(true) _(until) _(while) \
  __(concat, ..) __(dots, ...) __(eq, ==) __(ge, >=) __(le, <=) __(ne, ~=) \
  __(label, ::) __(number, <number>) __(name, <name>) __(string, <string>) \
  __(eof, <eof>)

enum {
  TK_OFS = 256,
#define TKENUM1(name)		TK_##name,
#define TKENUM2(name, sym)	TK_##name,
TKDEF(TKENUM1, TKENUM2)
#undef TKENUM1
#undef TKENUM2
  TK_RESERVED = TK_while - TK_OFS
};

typedef int LexChar;	/* Lexical character. Unsigned ext. from char. */
typedef int LexToken;	/* Lexical token. */

struct BCIns
{
#ifndef NDEBUG
    bool isJumpDest : 1;
    bool hasA : 1;
    bool hasB : 1;
    bool hasC : 1;
    bool hasD : 1;
    bool hasCst : 1;
#else
    bool isJumpDest;
#endif
    uint8_t insOp;
    uint16_t insA;
    uint16_t insB;
    uint16_t insC;
    TValue ctv;
};
static_assert(sizeof(BCIns) == 16);

/* Combined bytecode ins/line. Only used during bytecode generation. */
typedef struct BCInsLine {
    BCIns inst;
    BCLine line;		/* Line number for this bytecode. */
    uint32_t finalPos;
} BCInsLine;

/* Info for local variables. Only used during bytecode generation. */
typedef struct VarInfo {
  HeapPtr<HeapString> name;		/* Local variable name or goto/label name. */
  BCPos startpc;	/* First point where the local variable is active. */
  BCPos endpc;		/* First point where the local variable is dead. */
  uint8_t slot;		/* Variable slot. */
  uint8_t info;		/* Variable/goto/label info. */
} VarInfo;

#define LJ_PARSER_ERROR_LIST                                                \
    (LJ_ERR_XLINES,     "chunk has too many lines")                         \
  , (LJ_ERR_XNUMBER,	"malformed number")                                 \
  , (LJ_ERR_XLSTR,	"unfinished long string")                           \
  , (LJ_ERR_XLCOM,	"unfinished long comment")                          \
  , (LJ_ERR_XSTR,	"unfinished string")                                \
  , (LJ_ERR_XESC,	"invalid escape sequence")                          \
  , (LJ_ERR_XLDELIM,	"invalid long string delimiter")                    \
  , (LJ_ERR_XTOKEN,     "invalid token")                                    \
  , (LJ_ERR_XLIMM,      "limitation exceeded")                              \
  , (LJ_ERR_XLIMF,      "limitation exceeded")                              \
  , (LJ_ERR_XJUMP,	"control structure too long")                       \
  , (LJ_ERR_XSLOTS,	"function or expression too complex")               \
  , (LJ_ERR_XMATCH,     "mismatched tokens pairs")                          \
  , (LJ_ERR_XGSCOPE,	"goto jumps into the scope of local")               \
  , (LJ_ERR_XLUNDEF,	"undefined label")                                  \
  , (LJ_ERR_XFIXUP,     "function too long for return fixup")               \
  , (LJ_ERR_XAMBIG,     "ambiguous syntax (function call x new statement)") \
  , (LJ_ERR_XFUNARG,    "function arguments expected")                      \
  , (LJ_ERR_XSYMBOL, 	"unexpected symbol")                                \
  , (LJ_ERR_XPARAM,     "parameter name or '...' expected")                 \
  , (LJ_ERR_XDOTS,      "cannot use '...' outside a vararg function")       \
  , (LJ_ERR_XLEVELS,	"chunk has too many syntax levels")                 \
  , (LJ_ERR_XSYNTAX,	"syntax error")                                     \
  , (LJ_ERR_XLDUP,	"duplicate label ")                                 \
  , (LJ_ERR_XFOR,	"'=' or 'in' expected")                             \

enum LJ_PARSER_ERROR_KIND_ENUM
{
    LJ_PARSER_NO_ERROR
#define macro(e) , PP_TUPLE_GET_1(e)
PP_FOR_EACH(macro, LJ_PARSER_ERROR_LIST)
#undef macro
    , END_OF_ENUM_LJ_PARSER_ERROR
};

constexpr const char* x_lj_parser_error_name[END_OF_ENUM_LJ_PARSER_ERROR] = {
    ""
#define macro(e) , PP_STRINGIFY(PP_TUPLE_GET_1(e))
    PP_FOR_EACH(macro, LJ_PARSER_ERROR_LIST)
#undef macro
};

constexpr const char* x_lj_parser_error_msg[END_OF_ENUM_LJ_PARSER_ERROR] = {
    ""
#define macro(e) , PP_TUPLE_GET_2(e)
    PP_FOR_EACH(macro, LJ_PARSER_ERROR_LIST)
#undef macro
};

using lua_Reader = const char*(*)(CoroutineRuntimeContext*, void*, size_t*);

/* Lua lexer state. */
typedef struct LexState {
  struct FuncState *fs;	/* Current FuncState. Defined in lj_parse.c. */
  CoroutineRuntimeContext *L;	/* Lua state. */
  TValue tokval;	/* Current token value. */
  TValue lookaheadval;	/* Lookahead token value. */
  const char *p;	/* Current position in input buffer. */
  const char *pe;	/* End of input buffer. */
  LexChar c;		/* Current character. */
  LexToken tok;		/* Current token. */
  LexToken lookahead;	/* Lookahead token. */
  SimpleTempStringStream* sb;		/* String buffer for tokens. */
  lua_Reader rfunc;	/* Reader callback. */
  void *rdata;		/* Reader callback data. */
  BCLine linenumber;	/* Input line counter. */
  BCLine lastline;	/* Line of last token. */
  HeapString* chunkname;	/* Current chunk name (interned string). */
  const char *chunkarg;	/* Chunk name argument. */
  const char *mode;	/* Allow loading bytecode (b) and/or source text (t). */
  std::vector<VarInfo> vstack;	/* Stack for names and extents of local variables. */
  std::vector<BCInsLine> bcstack;	/* Stack for bytecode instructions/line numbers. */
  uint32_t level;	/* Syntactical nesting level. */
  int endmark;		/* Trust bytecode end marker, even if not at EOF. */
  int errorCode;
  LexToken errorToken;
  const char* errorMsg;
  jmp_buf longjmp_buf;
  std::vector<UnlinkedCodeBlock*> ucbList;
} LexState;

NO_INLINE NO_RETURN void parser_throw(LexState* ls);
void lj_lex_next(LexState *ls);
LexToken lj_lex_lookahead(LexState *ls);
const char *lj_lex_token2str(LexState *ls, LexToken tok);
