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
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wunused-function"

#include "common.h"

#include <setjmp.h>

#include "lj_lex_details.h"
#include "lj_parse_details.h"
#include "lj_char_trait_details.h"
#include "lj_strscan.h"
#include "lj_strfmt.h"
#include "lj_strfmt_details.h"

#include "vm.h"
#include "bytecode_builder.h"

#include "deegen/deegen_options.h"

/* Bytecode instruction definition. Order matters, see below.
**
** (name, filler, Amode, Bmode, Cmode or Dmode, metamethod)
**
** The opcode name suffixes specify the type for RB/RC or RD:
** V = variable slot
** S = string const
** N = number const
** P = primitive type (~itype)
** B = unsigned byte literal
** M = multiple args/results
*/
#define BCDEF(_) \
/* Comparison ops. ORDER OPR. */ \
    _(ISLT,	var,	___,	var,	lt) \
    _(ISGE,	var,	___,	var,	lt) \
    _(ISLE,	var,	___,	var,	le) \
    _(ISGT,	var,	___,	var,	le) \
  \
    /* comparison ops with one side being constant number */ \
    _(ISLTNV,	var,	___,	num,	lt) \
    _(ISGENV,	var,	___,	num,	lt) \
    _(ISLENV,	var,	___,	num,	le) \
    _(ISGTNV,	var,	___,	num,	le) \
  \
    _(ISLTVN,	var,	___,	num,	lt) \
    _(ISGEVN,	var,	___,	num,	lt) \
    _(ISLEVN,	var,	___,	num,	le) \
    _(ISGTVN,	var,	___,	num,	le) \
  \
    _(ISEQV,	var,	___,	var,	eq) \
    _(ISNEV,	var,	___,	var,	eq) \
    _(ISEQS,	var,	___,	str,	eq) \
    _(ISNES,	var,	___,	str,	eq) \
    _(ISEQN,	var,	___,	num,	eq) \
    _(ISNEN,	var,	___,	num,	eq) \
    _(ISEQP,	var,	___,	pri,	eq) \
    _(ISNEP,	var,	___,	pri,	eq) \
  \
    /* Unary test and copy ops. */ \
    _(ISTC,	dst,	___,	var,	___) \
    _(ISFC,	dst,	___,	var,	___) \
    _(IST,	___,	___,	var,	___) \
    _(ISF,	___,	___,	var,	___) \
    _(ISTYPE,	var,	___,	lit,	___) \
    _(ISNUM,	var,	___,	lit,	___) \
  \
    /* Unary ops. */ \
    _(MOV,	dst,	___,	var,	___) \
    _(NOT,	dst,	___,	var,	___) \
    _(UNM,	dst,	___,	var,	unm) \
    _(LEN,	dst,	___,	var,	len) \
  \
    /* Binary ops. ORDER OPR. VV last, POW must be next. */ \
    _(ADDVN,	dst,	var,	num,	add) \
    _(SUBVN,	dst,	var,	num,	sub) \
    _(MULVN,	dst,	var,	num,	mul) \
    _(DIVVN,	dst,	var,	num,	div) \
    _(MODVN,	dst,	var,	num,	mod) \
  \
    _(ADDNV,	dst,	var,	num,	add) \
    _(SUBNV,	dst,	var,	num,	sub) \
    _(MULNV,	dst,	var,	num,	mul) \
    _(DIVNV,	dst,	var,	num,	div) \
    _(MODNV,	dst,	var,	num,	mod) \
  \
    _(ADDVV,	dst,	var,	var,	add) \
    _(SUBVV,	dst,	var,	var,	sub) \
    _(MULVV,	dst,	var,	var,	mul) \
    _(DIVVV,	dst,	var,	var,	div) \
    _(MODVV,	dst,	var,	var,	mod) \
  \
    _(POW,	dst,	var,	var,	pow) \
    _(CAT,	dst,	rbase,	rbase,	concat) \
  \
    /* Constant ops. */ \
    _(KSTR,	dst,	___,	str,	___) \
    _(KCDATA,	dst,	___,	cdata,	___) \
    _(KSHORT,	dst,	___,	lits,	___) \
    _(KNUM,	dst,	___,	num,	___) \
    _(KPRI,	dst,	___,	pri,	___) \
    _(KNIL,	base,	___,	base,	___) \
  \
    /* Upvalue and function ops. */ \
    _(UGET,	dst,	___,	uv,	___) \
    _(USETV,	uv,	___,	var,	___) \
    _(USETS,	uv,	___,	str,	___) \
    _(USETN,	uv,	___,	num,	___) \
    _(USETP,	uv,	___,	pri,	___) \
    _(UCLO,	rbase,	___,	jump,	___) \
    _(UCLO_LH,	rbase,	___,	jump,	___) \
    _(FNEW,	dst,	___,	func,	gc) \
  \
    /* Table ops. */ \
    _(TNEW,	dst,	___,	lit,	gc) \
    _(TDUP,	dst,	___,	tab,	gc) \
    _(GGET,	dst,	___,	str,	index) \
    _(GSET,	var,	___,	str,	newindex) \
    _(TGETV,	dst,	var,	var,	index) \
    _(TGETS,	dst,	var,	str,	index) \
    _(TGETB,	dst,	var,	lit,	index) \
    _(TGETR,	dst,	var,	var,	index) \
    _(TSETV,	var,	var,	var,	newindex) \
    _(TSETS,	var,	var,	str,	newindex) \
    _(TSETB,	var,	var,	lit,	newindex) \
    _(TSETM,	base,	___,	num,	newindex) \
    _(TSETR,	var,	var,	var,	newindex) \
  \
    /* Calls and vararg handling. T = tail call. */ \
    _(CALLM,	base,	lit,	lit,	call) \
    _(CALL,	base,	lit,	lit,	call) \
    _(CALLMT,	base,	___,	lit,	call) \
    _(CALLT,	base,	___,	lit,	call) \
    _(ITERC,	base,	lit,	lit,	call) \
    _(ITERN,	base,	lit,	lit,	call) \
    _(VARG,	base,	lit,	lit,	___) \
    _(ISNEXT,	base,	___,	jump,	___) \
  \
    /* Returns. */ \
    _(RETM,	base,	___,	lit,	___) \
    _(RET,	rbase,	___,	lit,	___) \
    _(RET0,	rbase,	___,	lit,	___) \
    _(RET1,	rbase,	___,	lit,	___) \
  \
    /* Loops and branches. I/J = interp/JIT, I/C/L = init/call/loop. */ \
    _(FORI,	base,	___,	jump,	___) \
    _(JFORI,	base,	___,	jump,	___) \
  \
    _(FORL,	base,	___,	jump,	___) \
    _(IFORL,	base,	___,	jump,	___) \
    _(JFORL,	base,	___,	lit,	___) \
  \
    _(ITERL,	base,	___,	jump,	___) \
    _(IITERL,	base,	___,	jump,	___) \
    _(JITERL,	base,	___,	lit,	___) \
  \
    _(LOOP,	rbase,	___,	jump,	___) \
    _(ILOOP,	rbase,	___,	jump,	___) \
    _(JLOOP,	rbase,	___,	lit,	___) \
  \
    /* REP_LH: the repeat loop header */     \
    _(REP_LH,	rbase,	___,	jump,	___) \
    _(JMP,	rbase,	___,	jump,	___) \
    _(JMP_LH,	rbase,	___,	jump,	___) \
  \
    /* Function headers. I/J = interp/JIT, F/V/C = fixarg/vararg/C func. */ \
    _(FUNCF,	rbase,	___,	___,	___) \
    _(IFUNCF,	rbase,	___,	___,	___) \
    _(JFUNCF,	rbase,	___,	lit,	___) \
    _(FUNCV,	rbase,	___,	___,	___) \
    _(IFUNCV,	rbase,	___,	___,	___) \
    _(JFUNCV,	rbase,	___,	lit,	___) \
    _(FUNCC,	rbase,	___,	___,	___) \
    _(FUNCCW,	rbase,	___,	___,	___)

/* Bytecode opcode numbers. */
typedef enum {
#define BCENUM(name, ma, mb, mc, mt)	BC_##name,
    BCDEF(BCENUM)
#undef BCENUM
    BC__MAX
} BCOp;

static_assert((int)BC_ISEQV+1 == (int)BC_ISNEV);
static_assert(((int)BC_ISEQV^1) == (int)BC_ISNEV);
static_assert(((int)BC_ISEQS^1) == (int)BC_ISNES);
static_assert(((int)BC_ISEQN^1) == (int)BC_ISNEN);
static_assert(((int)BC_ISEQP^1) == (int)BC_ISNEP);
static_assert(((int)BC_ISLT^1) == (int)BC_ISGE);
static_assert(((int)BC_ISLE^1) == (int)BC_ISGT);
static_assert(((int)BC_ISLT^3) == (int)BC_ISGT);
static_assert(((int)BC_ISLTVN^1) == (int)BC_ISGEVN);
static_assert(((int)BC_ISLEVN^1) == (int)BC_ISGTVN);
static_assert(((int)BC_ISLTVN^3) == (int)BC_ISGTVN);
static_assert(((int)BC_ISLTNV^1) == (int)BC_ISGENV);
static_assert(((int)BC_ISLENV^1) == (int)BC_ISGTNV);
static_assert(((int)BC_ISLTNV^3) == (int)BC_ISGTNV);
static_assert((int)BC_IST-(int)BC_ISTC == (int)BC_ISF-(int)BC_ISFC);
static_assert((int)BC_CALLT-(int)BC_CALL == (int)BC_CALLMT-(int)BC_CALLM);
static_assert((int)BC_CALLMT + 1 == (int)BC_CALLT);
static_assert((int)BC_RETM + 1 == (int)BC_RET);
static_assert((int)BC_FORL + 1 == (int)BC_IFORL);
static_assert((int)BC_FORL + 2 == (int)BC_JFORL);
static_assert((int)BC_ITERL + 1 == (int)BC_IITERL);
static_assert((int)BC_ITERL + 2 == (int)BC_JITERL);
static_assert((int)BC_LOOP + 1 == (int)BC_ILOOP);
static_assert((int)BC_LOOP + 2 == (int)BC_JLOOP);
static_assert((int)BC_FUNCF + 1 == (int)BC_IFUNCF);
static_assert((int)BC_FUNCF + 2 == (int)BC_JFUNCF);
static_assert((int)BC_FUNCV + 1 == (int)BC_IFUNCV);
static_assert((int)BC_FUNCV + 2 == (int)BC_JFUNCV);

/* -- Parser structures and definitions ----------------------------------- */

/* Expression kinds. */
typedef enum {
    /* Constant expressions must be first and in this order: */
    VKNIL,
    VKFALSE,
    VKTRUE,
    VKSTR,	/* sval = string value */
    VKNUM,	/* nval = number value */
    VKLAST = VKNUM,
    VKCDATA,	/* nval = cdata value, not treated as a constant expression */
    /* Non-constant expressions follow: */
    VLOCAL,	/* info = local register, aux = vstack index */
    VUPVAL,	/* info = upvalue index, aux = vstack index */
    VGLOBAL,	/* sval = string value */
    VINDEXED,	/* info = table register, aux = index reg/byte/string const */
    VJMP,		/* info = instruction PC */
    VRELOCABLE,	/* info = instruction PC */
    VNONRELOC,	/* info = result register */
    VCALL,	/* info = instruction PC, aux = base */
    VVOID
} ExpKind;

/* Expression descriptor. */
typedef struct ExpDesc {
    union {
        struct {
            uint32_t info;	/* Primary info. */
            uint32_t aux;	/* Secondary info. */
        } s;
        TValue nval;	/* Number value. */
        HeapPtr<HeapString> sval;	/* String value. */
    } u;
    ExpKind k;
    BCPos t;		/* True condition jump list. */
    BCPos f;		/* False condition jump list. */
    TValue aux2;
} ExpDesc;

#define check_exp(c, e)		(assert((c)), (e))

/* Macros for expressions. */
#define expr_hasjump(e)		((e)->t != (e)->f)

#define expr_isk(e)		((e)->k <= VKLAST)
#define expr_isk_nojump(e)	(expr_isk(e) && !expr_hasjump(e))
#define expr_isnumk(e)		((e)->k == VKNUM)
#define expr_isnumk_nojump(e)	(expr_isnumk(e) && !expr_hasjump(e))
#define expr_isstrk(e)		((e)->k == VKSTR)

#define expr_numtv(e)		check_exp(expr_isnumk((e)), &(e)->u.nval)

#define NO_JMP		(~(BCPos)0)

constexpr int LJ_FR2 = x_numSlotsForStackFrameHeader - 1;

/* Initialize expression. */
static ALWAYS_INLINE void expr_init(ExpDesc *e, ExpKind k, uint32_t info)
{
    e->k = k;
    e->u.s.info = info;
    e->f = e->t = NO_JMP;
}

/* Check number constant for +-0. */
static int expr_numiszero(ExpDesc *e)
{
    TValue *o = expr_numtv(e);
    assert(o->Is<tInt32>() || o->Is<tDouble>());
    return o->Is<tInt32>() ? (o->As<tInt32>() == 0) : (o->As<tDouble>() == 0.0);
}

static double expr_numberV(ExpDesc *e)
{
    TValue *o = expr_numtv(e);
    assert(o->Is<tInt32>() || o->Is<tDouble>());
    return o->Is<tInt32>() ? (o->As<tInt32>()) : (o->As<tDouble>());
}

/* Per-function linked list of scope blocks. */
typedef struct FuncScope {
    struct FuncScope *prev;	/* Link to outer scope. */
    MSize vstart;			/* Start of block-local variables. */
    uint8_t nactvar;		/* Number of active vars outside the scope. */
    uint8_t flags;		/* Scope flags. */
} FuncScope;

#define FSCOPE_LOOP		0x01	/* Scope is a (breakable) loop. */
#define FSCOPE_BREAK		0x02	/* Break used in scope. */
#define FSCOPE_GOLA		0x04	/* Goto or label used in scope. */
#define FSCOPE_UPVAL		0x08	/* Upvalue in scope. */
#define FSCOPE_NOCLOSE		0x10	/* Do not close upvalues. */

#define NAME_BREAK		(reinterpret_cast<HeapPtr<HeapString>>(1))

#define PROTO_CHILD		0x01	/* Has child prototypes. */
#define PROTO_VARARG		0x02	/* Vararg function. */
#define PROTO_FFI		0x04	/* Uses BC_KCDATA for FFI datatypes. */
#define PROTO_NOJIT		0x08	/* JIT disabled for this function. */
#define PROTO_ILOOP		0x10	/* Patched bytecode with ILOOP etc. */
/* Only used during parsing. */
#define PROTO_HAS_RETURN	0x20	/* Already emitted a return. */
#define PROTO_FIXUP_RETURN	0x40	/* Need to fixup emitted returns. */
/* Top bits used for counting created closures. */
#define PROTO_CLCOUNT		0x20	/* Base of saturating 3 bit counter. */
#define PROTO_CLC_BITS		3
#define PROTO_CLC_POLY		(3*PROTO_CLCOUNT)  /* Polymorphic threshold. */
#define PROTO_UV_LOCAL		0x8000	/* Upvalue for local slot. */
#define PROTO_UV_IMMUTABLE	0x4000	/* Immutable upvalue. */

/* Index into variable stack. */
typedef uint16_t VarIndex;
#define LJ_MAX_VSTACK		(65536 - LJ_MAX_UPVAL)

/* Variable/goto/label info. */
#define VSTACK_VAR_RW		0x01	/* R/W variable. */
#define VSTACK_GOTO		0x02	/* Pending goto. */
#define VSTACK_LABEL		0x04	/* Label. */

using BCReg = uint32_t;

#define LJ_52 0
#define LJ_MAX_LOCVAR 200
#define LJ_MAX_UPVAL 60
#define LJ_MAX_SLOTS 250
#define LJ_MAX_BCINS	(1<<26)
#define LJ_MAX_XLEVEL 200

using BytecodeBuilder = DeegenBytecodeBuilder::BytecodeBuilder;
using BCKind = DeegenBytecodeBuilder::BCKind;

/* Per-function state. */
typedef struct FuncState {
    LexState *ls;			/* Lexer state. */
    CoroutineRuntimeContext *L;			/* Lua state. */
    FuncScope *bl;		/* Current scope. */
    struct FuncState *prev;	/* Enclosing function. */
    BCPos pc;			/* Next bytecode position. */
    BCPos lasttarget;		/* Bytecode position of last jump target. */
    BCPos jpc;			/* Pending jump list to next bytecode. */
    BCReg freereg;		/* First free register. */
    BCReg nactvar;		/* Number of active local variables. */
    BCReg nkn, nkgc;		/* Number of lua_Number/GCobj constants */
    BCLine linedefined;		/* First line of the function definition. */
    BCInsLine *bcbase;		/* Base of bytecode stack. */
    BCPos bclim;			/* Limit of bytecode stack. */
    MSize vbase;			/* Base of variable stack for this function. */
    uint8_t flags;		/* Prototype flags. */
    uint8_t numparams;		/* Number of parameters. */
    uint8_t framesize;		/* Fixed frame size. */
    uint8_t nuv;			/* Number of upvalues */
    VarIndex varmap[LJ_MAX_LOCVAR];  /* Map from register to variable idx. */
    VarIndex uvmap[LJ_MAX_UPVAL];	/* Map from upvalue to variable idx. */
    VarIndex uvtmp[LJ_MAX_UPVAL];	/* Temporary upvalue map. */
} FuncState;

/* Binary and unary operators. ORDER OPR */
typedef enum BinOpr {
    OPR_ADD, OPR_SUB, OPR_MUL, OPR_DIV, OPR_MOD, OPR_POW,  /* ORDER ARITH */
    OPR_CONCAT,
    OPR_NE, OPR_EQ,
    OPR_LT, OPR_GE, OPR_LE, OPR_GT,
    OPR_AND, OPR_OR,
    OPR_NOBINOPR
} BinOpr;

static_assert((int)BC_ISGE-(int)BC_ISLT == (int)OPR_GE-(int)OPR_LT);
static_assert((int)BC_ISLE-(int)BC_ISLT == (int)OPR_LE-(int)OPR_LT);
static_assert((int)BC_ISGT-(int)BC_ISLT == (int)OPR_GT-(int)OPR_LT);
static_assert((int)BC_SUBVV-(int)BC_ADDVV == (int)OPR_SUB-(int)OPR_ADD);
static_assert((int)BC_MULVV-(int)BC_ADDVV == (int)OPR_MUL-(int)OPR_ADD);
static_assert((int)BC_DIVVV-(int)BC_ADDVV == (int)OPR_DIV-(int)OPR_ADD);
static_assert((int)BC_MODVV-(int)BC_ADDVV == (int)OPR_MOD-(int)OPR_ADD);

/* -- Error handling ------------------------------------------------------ */

NO_INLINE NO_RETURN static void err_syntax(LexState *ls, int em)
{
    ls->errorCode = em;
    ls->errorToken = ls->tok;
    parser_throw(ls);
}

NO_INLINE NO_RETURN static void err_token(LexState *ls, LexToken /*tok*/)
{
    err_syntax(ls, LJ_ERR_XTOKEN);
}

NO_INLINE NO_RETURN static void err_limit(FuncState *fs, uint32_t /*limit*/, const char *what)
{
    fs->ls->errorMsg = what;
    fs->ls->errorToken = 0;
    if (fs->linedefined == 0)
        fs->ls->errorCode = LJ_ERR_XLIMM;
    else
        fs->ls->errorCode = LJ_ERR_XLIMF;
    parser_throw(fs->ls);
}

#define checklimit(fs, v, l, m)		do { if ((v) >= (l)) err_limit(fs, l, m); } while (false)
#define checklimitgt(fs, v, l, m)	do { if ((v) > (l)) err_limit(fs, l, m); } while (false)
#define checkcond(ls, c, em)		do { if (!(c)) err_syntax(ls, em); } while (false)

/* -- Management of constants --------------------------------------------- */

static TValue WARN_UNUSED const_pri(ExpDesc *e)
{
    assert(e->k <= VKTRUE);
    switch (e->k)
    {
    case VKNIL: return TValue::Create<tNil>();
    case VKFALSE: return TValue::Create<tBool>(false);
    case VKTRUE: return TValue::Create<tBool>(true);
    default: { assert(false); __builtin_unreachable(); }
    }
}

/* Add a number constant. */
static TValue WARN_UNUSED const_num(ExpDesc *e)
{
    TValue ori = e->u.nval;
    assert(ori.Is<tDouble>() || ori.Is<tInt32>());
    double doubleVal;
    doubleVal = (ori.Is<tDouble>()) ? ori.As<tDouble>() : ori.As<tInt32>();
    TValue num = TValue::Create<tDouble>(doubleVal);
    return num;
}

/* Add a string constant. */
static TValue WARN_UNUSED const_str(ExpDesc *e)
{
    // TODO: we need to keep the object alive
    //
    assert((expr_isstrk(e) || e->k == VGLOBAL) && "bad usage");
    HeapPtr<HeapString> str = e->u.sval;
    return TValue::Create<tString>(str);
}

/* Anchor string constant to avoid GC. */
TValue lj_parse_keepstr(LexState* /*ls*/, const char *str, size_t len)
{
    // TODO: we need to keep the object alive
    //
    VM* vm = VM::GetActiveVMForCurrentThread();
    HeapPtr<HeapString> s = vm->CreateStringObjectFromRawString(str, len).As();
    return TValue::Create<tString>(s);
}

#define BCMAX_A		0x7fff
#define BCMAX_B		0x7fff
#define BCMAX_C		0x7fff
#define BCMAX_D		0xffff
#define NO_REG		BCMAX_A
#define BCBIAS_J        0x8000

static BCOp ALWAYS_INLINE bc_op(BCIns& ins)
{
    return static_cast<BCOp>(ins.insOp);
}

static void ALWAYS_INLINE setbc_op(BCIns& ins, int op)
{
    ins.insOp = static_cast<BCOp>(op);
}

static BCReg ALWAYS_INLINE bc_a(BCIns& ins)
{
    assert(ins.hasA);
    return ins.insA;
}

static void ALWAYS_INLINE setbc_a(BCIns& ins, BCReg val)
{
    assert(val <= BCMAX_A);
    DEBUG_ONLY(ins.hasA = true;)
    ins.insA = static_cast<uint16_t>(val);
}

static BCReg ALWAYS_INLINE bc_b(BCIns& ins)
{
    assert(ins.hasB);
    return ins.insB;
}

static void ALWAYS_INLINE setbc_b(BCIns& ins, BCReg val)
{
    assert(val <= BCMAX_B);
    DEBUG_ONLY(ins.hasB = true;)
    ins.insB = static_cast<uint16_t>(val);
}

static BCReg ALWAYS_INLINE bc_c(BCIns& ins)
{
    assert(ins.hasC);
    return ins.insC;
}

static void ALWAYS_INLINE setbc_c(BCIns& ins, BCReg val)
{
    assert(val <= BCMAX_C);
    DEBUG_ONLY(ins.hasC = true;)
    ins.insC = static_cast<uint16_t>(val);
}

// TODO: filter all use that actually fetches constants
//
static BCReg ALWAYS_INLINE bc_d(BCIns& ins)
{
    assert(ins.hasD);
    return ins.insC;
}

static void ALWAYS_INLINE setbc_d(BCIns& ins, BCReg val)
{
    assert(val <= BCMAX_D);
    DEBUG_ONLY(ins.hasD = true;)
    ins.insC = static_cast<uint16_t>(val);
}

static ssize_t ALWAYS_INLINE bc_j(BCIns& ins)
{
    return static_cast<ssize_t>(bc_d(ins)) - BCBIAS_J;
}

static void ALWAYS_INLINE setbc_j(BCIns& ins, BCPos pos)
{
    int32_t val = static_cast<int32_t>(pos) + BCBIAS_J;
    setbc_d(ins, static_cast<BCReg>(val));
}

static TValue ALWAYS_INLINE bc_cst(BCIns& ins)
{
    assert(ins.hasCst);
    return ins.ctv;
}

static void ALWAYS_INLINE setbc_cst(BCIns& ins, TValue tv)
{
    DEBUG_ONLY(ins.hasCst = true;)
    ins.ctv = tv;
}

static void ALWAYS_INLINE setbc_clear(BCIns& ins)
{
    ins.isJumpDest = false;
#ifndef NDEBUG
    ins.hasA = false;
    ins.hasB = false;
    ins.hasC = false;
    ins.hasD = false;
    ins.hasCst = false;
#endif
    ins.insOp = 255;
}

static void ALWAYS_INLINE setbcins_abc(BCIns& ins, int op, BCReg a, BCReg b, BCReg c)
{
    setbc_clear(ins);
    setbc_op(ins, op);
    setbc_a(ins, a);
    setbc_b(ins, b);
    setbc_c(ins, c);
}

static void ALWAYS_INLINE setbcins_abc(BCIns& ins, int op, BCReg a, TValue b, BCReg c)
{
    setbc_clear(ins);
    setbc_op(ins, op);
    setbc_a(ins, a);
    setbc_cst(ins, b);
    setbc_c(ins, c);
}

static void ALWAYS_INLINE setbcins_abc(BCIns& ins, int op, BCReg a, BCReg b, TValue c)
{
    setbc_clear(ins);
    setbc_op(ins, op);
    setbc_a(ins, a);
    setbc_b(ins, b);
    setbc_cst(ins, c);
}

static void ALWAYS_INLINE setbcins_ad(BCIns& ins, int op, BCReg a, BCReg d)
{
    setbc_clear(ins);
    setbc_op(ins, op);
    setbc_a(ins, a);
    setbc_d(ins, d);
}

static void ALWAYS_INLINE setbcins_ad(BCIns& ins, int op, BCReg a, TValue d)
{
    setbc_clear(ins);
    setbc_op(ins, op);
    setbc_a(ins, a);
    setbc_cst(ins, d);
}

static void ALWAYS_INLINE setbcins_aj(BCIns& ins, int op, BCReg a, BCPos j)
{
    setbc_clear(ins);
    setbc_op(ins, op);
    setbc_a(ins, a);
    setbc_j(ins, j);
}

/* -- Jump list handling -------------------------------------------------- */

/* Get next element in jump list. */

static BCPos jmp_next(FuncState *fs, BCPos pc)
{
    ptrdiff_t delta = bc_j(fs->bcbase[pc].inst);
    if ((BCPos)delta == NO_JMP)
        return NO_JMP;
    else
        return (BCPos)(((ptrdiff_t)pc+1)+delta);
}

/* Check if any of the instructions on the jump list produce no value. */
static bool jmp_novalue(FuncState *fs, BCPos list)
{
    for (; list != NO_JMP; list = jmp_next(fs, list)) {
        BCIns& p = fs->bcbase[list >= 1 ? list-1 : list].inst;
        if (!(bc_op(p) == BC_ISTC || bc_op(p) == BC_ISFC || bc_a(p) == NO_REG))
            return true;
    }
    return false;
}

/* Patch register of test instructions. */
static bool jmp_patchtestreg(FuncState *fs, BCPos pc, BCReg reg)
{
    BCInsLine *ilp = &fs->bcbase[pc >= 1 ? pc-1 : pc];
    BCOp op = bc_op(ilp->inst);
    if (op == BC_ISTC || op == BC_ISFC) {
        if (reg != NO_REG && reg != bc_d(ilp->inst)) {
            setbc_a(ilp->inst, reg);
        } else {  /* Nothing to store or already in the right register. */
            setbc_op(ilp->inst, op+(BC_IST-BC_ISTC));
            setbc_a(ilp->inst, 0);
        }
    } else if (bc_a(ilp->inst) == NO_REG) {
        if (reg == NO_REG) {
            setbcins_aj(ilp->inst, BC_JMP, bc_a(fs->bcbase[pc].inst), 0);
        } else {
            setbc_a(ilp->inst, reg);
            if (reg >= bc_a(ilp[1].inst))
                setbc_a(ilp[1].inst, reg+1);
        }
    } else {
        return false;  /* Cannot patch other instructions. */
    }
    return true;
}

/* Drop values for all instructions on jump list. */
static void jmp_dropval(FuncState *fs, BCPos list)
{
    for (; list != NO_JMP; list = jmp_next(fs, list))
        jmp_patchtestreg(fs, list, NO_REG);
}

/* Patch jump instruction to target. */
static void jmp_patchins(FuncState *fs, BCPos pc, BCPos dest)
{
    BCIns& jmp = fs->bcbase[pc].inst;
    BCPos offset = dest-(pc+1)+BCBIAS_J;
    assert(dest != NO_JMP && "uninitialized jump target");
    if (offset > BCMAX_D)
        err_syntax(fs->ls, LJ_ERR_XJUMP);
    setbc_d(jmp, offset);
}

/* Append to jump list. */
static void jmp_append(FuncState *fs, BCPos *l1, BCPos l2)
{
    if (l2 == NO_JMP) {
        return;
    } else if (*l1 == NO_JMP) {
        *l1 = l2;
    } else {
        BCPos list = *l1;
        BCPos next;
        while ((next = jmp_next(fs, list)) != NO_JMP)  /* Find last element. */
            list = next;
        jmp_patchins(fs, list, l2);
    }
    return;
}

/* Patch jump list and preserve produced values. */
static void jmp_patchval(FuncState *fs, BCPos list, BCPos vtarget,
                         BCReg reg, BCPos dtarget)
{
    while (list != NO_JMP) {
        BCPos next = jmp_next(fs, list);
        if (jmp_patchtestreg(fs, list, reg))
            jmp_patchins(fs, list, vtarget);  /* Jump to target with value. */
        else
            jmp_patchins(fs, list, dtarget);  /* Jump to default target. */
        list = next;
    }
}

/* Jump to following instruction. Append to list of pending jumps. */
static void jmp_tohere(FuncState *fs, BCPos list)
{
    fs->lasttarget = fs->pc;
    jmp_append(fs, &fs->jpc, list);
}

/* Patch jump list to target. */
static void jmp_patch(FuncState *fs, BCPos list, BCPos target)
{
    if (target == fs->pc) {
        jmp_tohere(fs, list);
    } else {
        assert(target < fs->pc && "bad jump target");
        jmp_patchval(fs, list, target, NO_REG, target);
    }
}

/* -- Bytecode register allocator ----------------------------------------- */

/* Bump frame size. */
static void bcreg_bump(FuncState *fs, BCReg n)
{
    BCReg sz = fs->freereg + n;
    if (sz > fs->framesize) {
        if (sz >= LJ_MAX_SLOTS)
            err_syntax(fs->ls, LJ_ERR_XSLOTS);
        fs->framesize = (uint8_t)sz;
    }
}

/* Reserve registers. */
static void bcreg_reserve(FuncState *fs, BCReg n)
{
    bcreg_bump(fs, n);
    fs->freereg += n;
}

/* Free register. */
static void bcreg_free(FuncState *fs, BCReg reg)
{
    if (reg >= fs->nactvar) {
        fs->freereg--;
        assert(reg == fs->freereg && "bad regfree");
    }
}

/* Free register for expression. */
static void expr_free(FuncState *fs, ExpDesc *e)
{
    if (e->k == VNONRELOC)
        bcreg_free(fs, e->u.s.info);
}

/* -- Bytecode emitter ---------------------------------------------------- */

/* Append an uninitialized bytecode instruction. */
static BCPos bcemit_impl(FuncState *fs, BCIns ins)
{
    BCPos pc = fs->pc;
    LexState *ls = fs->ls;
    jmp_patchval(fs, fs->jpc, pc, NO_REG, pc);
    fs->jpc = NO_JMP;
    if (unlikely(pc >= fs->bclim)) {
        assert(fs->bcbase >= ls->bcstack.data());
        ptrdiff_t base = fs->bcbase - ls->bcstack.data();
        size_t newSize = std::max<uint64_t>(ls->bcstack.size() * 3 / 2, 16);
        checklimit(fs, newSize, LJ_MAX_BCINS, "too many bytecode instructions");
        ls->bcstack.resize(newSize);
        assert(newSize >= static_cast<size_t>(base));
        fs->bclim = (BCPos)(newSize - base);
        fs->bcbase = ls->bcstack.data() + base;
    }
    assert(fs->bcbase >= ls->bcstack.data());
    assert(fs->bclim + static_cast<size_t>(fs->bcbase - ls->bcstack.data()) == ls->bcstack.size());
    assert(pc < fs->bclim);
    fs->bcbase[pc].inst = ins;
    fs->bcbase[pc].line = ls->lastline;
    fs->pc = pc+1;
    return pc;
}

static BCIns ALWAYS_INLINE WARN_UNUSED BCINS_ABC(int op, BCReg a, BCReg b, BCReg c)
{
    BCIns res;
    setbcins_abc(res, op, a, b, c);
    return res;
}

static BCIns ALWAYS_INLINE WARN_UNUSED BCINS_ABC(int op, BCReg a, TValue b, BCReg c)
{
    BCIns res;
    setbcins_abc(res, op, a, b, c);
    return res;
}

static BCIns ALWAYS_INLINE WARN_UNUSED BCINS_ABC(int op, BCReg a, BCReg b, TValue c)
{
    BCIns res;
    setbcins_abc(res, op, a, b, c);
    return res;
}

static BCIns ALWAYS_INLINE WARN_UNUSED BCINS_AD(int op, BCReg a, BCReg d)
{
    BCIns res;
    setbcins_ad(res, op, a, d);
    return res;
}

static BCIns ALWAYS_INLINE WARN_UNUSED BCINS_AD(int op, BCReg a, TValue d)
{
    BCIns res;
    setbcins_ad(res, op, a, d);
    return res;
}

static BCIns ALWAYS_INLINE WARN_UNUSED BCINS_AJ(int op, BCReg a, BCPos j)
{
    BCIns res;
    setbcins_aj(res, op, a, j);
    return res;
}

static BCPos ALWAYS_INLINE WARN_UNUSED bcemit_INS(FuncState *fs, BCIns ins)
{
    return bcemit_impl(fs, ins);
}

static BCPos ALWAYS_INLINE WARN_UNUSED bcemit_ABC(FuncState *fs, int op, BCReg a, BCReg b, BCReg c)
{
    return bcemit_INS(fs, BCINS_ABC(op, a, b, c));
}

static BCPos ALWAYS_INLINE WARN_UNUSED bcemit_ABC(FuncState *fs, int op, BCReg a, TValue b, BCReg c)
{
    return bcemit_INS(fs, BCINS_ABC(op, a, b, c));
}

static BCPos ALWAYS_INLINE WARN_UNUSED bcemit_ABC(FuncState *fs, int op, BCReg a, BCReg b, TValue c)
{
    return bcemit_INS(fs, BCINS_ABC(op, a, b, c));
}

static BCPos ALWAYS_INLINE WARN_UNUSED bcemit_AD(FuncState *fs, int op, BCReg a, BCReg d)
{
    return bcemit_INS(fs, BCINS_AD(op, a, d));
}

static BCPos ALWAYS_INLINE WARN_UNUSED bcemit_AD(FuncState *fs, int op, BCReg a, TValue d)
{
    return bcemit_INS(fs, BCINS_AD(op, a, d));
}

static BCPos ALWAYS_INLINE WARN_UNUSED bcemit_AJ(FuncState *fs, int op, BCReg a, BCPos j)
{
    return bcemit_INS(fs, BCINS_AJ(op, a, j));
}

#define bcptr(fs, e)			(&(fs)->bcbase[(e)->u.s.info].inst)

/* -- Bytecode emitter for expressions ------------------------------------ */

/* Discharge non-constant expression to any register. */
static void expr_discharge(FuncState *fs, ExpDesc *e)
{
    BCIns ins;
    if (e->k == VUPVAL) {
        ins = BCINS_AD(BC_UGET, 0, e->u.s.info);
    } else if (e->k == VGLOBAL) {
        ins = BCINS_AD(BC_GGET, 0, const_str(e));
    } else if (e->k == VINDEXED) {
        BCReg rc = e->u.s.aux;
        if ((int32_t)rc < 0) {
            ins = BCINS_ABC(BC_TGETS, 0, e->u.s.info, e->aux2);
        } else if (rc > BCMAX_C) {
            ins = BCINS_ABC(BC_TGETB, 0, e->u.s.info, rc-(BCMAX_C+1));
        } else {
            bcreg_free(fs, rc);
            ins = BCINS_ABC(BC_TGETV, 0, e->u.s.info, rc);
        }
        bcreg_free(fs, e->u.s.info);
    } else if (e->k == VCALL) {
        e->u.s.info = e->u.s.aux;
        e->k = VNONRELOC;
        return;
    } else if (e->k == VLOCAL) {
        e->k = VNONRELOC;
        return;
    } else {
        return;
    }
    e->u.s.info = bcemit_INS(fs, ins);
    e->k = VRELOCABLE;
}

/* Emit bytecode to set a range of registers to nil. */
static void bcemit_nil(FuncState *fs, BCReg from, BCReg n)
{
    if (fs->pc > fs->lasttarget)
    {
        /* No jumps to current position? */
        BCIns *ip = &fs->bcbase[fs->pc-1].inst;
        BCReg pto, pfrom = bc_a(*ip);
        switch (bc_op(*ip)) {  /* Try to merge with the previous instruction. */
        case BC_KPRI:
            if (bc_cst(*ip).m_value != TValue::Create<tNil>().m_value) break;
            if (from == pfrom) {
                if (n == 1) return;
            } else if (from == pfrom+1) {
                from = pfrom;
                n++;
            } else {
                break;
            }
            *ip = BCINS_AD(BC_KNIL, from, from+n-1);  /* Replace KPRI. */
            return;
        case BC_KNIL:
            pto = bc_d(*ip);
            if (pfrom <= from && from <= pto+1) {  /* Can we connect both ranges? */
                if (from+n-1 > pto)
                    setbc_d(*ip, from+n-1);  /* Patch previous instruction range. */
                return;
            }
            break;
        default:
            break;
        }
    }
    /* Emit new instruction or replace old instruction. */
    std::ignore = bcemit_INS(fs, n == 1 ? BCINS_AD(BC_KPRI, from, TValue::Create<tNil>()) :
                             BCINS_AD(BC_KNIL, from, from+n-1));
    return;
}

static bool ALWAYS_INLINE checki16(int32_t input)
{
    return input == static_cast<int32_t>(static_cast<int16_t>(input));
}

/* Discharge an expression to a specific register. Ignore branches. */
static void expr_toreg_nobranch(FuncState *fs, ExpDesc *e, BCReg reg)
{
    BCIns ins;
    expr_discharge(fs, e);
    if (e->k == VKSTR) {
        ins = BCINS_AD(BC_KSTR, reg, const_str(e));
    } else if (e->k == VKNUM) {

        TValue *tv = expr_numtv(e);
        assert(tv->Is<tInt32>() || tv->Is<tDouble>());
        if (tv->Is<tInt32>())
        {
            if (checki16(tv->As<tInt32>()))
                ins = BCINS_AD(BC_KSHORT, reg, static_cast<BCReg>(static_cast<uint16_t>(static_cast<int16_t>(tv->AsInt32()))));
            else
                ins = BCINS_AD(BC_KNUM, reg, TValue::Create<tDouble>(tv->As<tInt32>()));
        }
        else
        {
            double n = tv->As<tDouble>();
            int32_t k = static_cast<int32_t>(n);
            if (checki16(k) && n == (double)k)
                ins = BCINS_AD(BC_KSHORT, reg, static_cast<BCReg>(static_cast<uint16_t>(static_cast<int16_t>(k))));
            else
                ins = BCINS_AD(BC_KNUM, reg, TValue::Create<tDouble>(n));
        }
    } else if (e->k == VRELOCABLE) {
        setbc_a(*bcptr(fs, e), reg);
        goto noins;
    } else if (e->k == VNONRELOC) {
        if (reg == e->u.s.info)
            goto noins;
        ins = BCINS_AD(BC_MOV, reg, e->u.s.info);
    } else if (e->k == VKNIL) {
        bcemit_nil(fs, reg, 1);
        goto noins;
    } else if (e->k <= VKTRUE) {
        ins = BCINS_AD(BC_KPRI, reg, const_pri(e));
    } else {
        assert((e->k == VVOID || e->k == VJMP) && "bad expr type");
        return;
    }
    std::ignore = bcemit_INS(fs, ins);
noins:
    e->u.s.info = reg;
    e->k = VNONRELOC;
}

/* Forward declaration. */
static BCPos WARN_UNUSED bcemit_jmp(FuncState *fs);

/* Discharge an expression to a specific register. */
static void expr_toreg(FuncState *fs, ExpDesc *e, BCReg reg)
{
    expr_toreg_nobranch(fs, e, reg);
    if (e->k == VJMP)
        jmp_append(fs, &e->t, e->u.s.info);  /* Add it to the true jump list. */
    if (expr_hasjump(e)) {  /* Discharge expression with branches. */
        BCPos jend, jfalse = NO_JMP, jtrue = NO_JMP;
        if (jmp_novalue(fs, e->t) || jmp_novalue(fs, e->f)) {
            BCPos jval = (e->k == VJMP) ? NO_JMP : bcemit_jmp(fs);
            jfalse = bcemit_AD(fs, BC_KPRI, reg, TValue::Create<tBool>(false));
            std::ignore = bcemit_AJ(fs, BC_JMP, fs->freereg, 1);
            jtrue = bcemit_AD(fs, BC_KPRI, reg, TValue::Create<tBool>(true));
            jmp_tohere(fs, jval);
        }
        jend = fs->pc;
        fs->lasttarget = jend;
        jmp_patchval(fs, e->f, jend, reg, jfalse);
        jmp_patchval(fs, e->t, jend, reg, jtrue);
    }
    e->f = e->t = NO_JMP;
    e->u.s.info = reg;
    e->k = VNONRELOC;
}

/* Discharge an expression to the next free register. */
static void expr_tonextreg(FuncState *fs, ExpDesc *e)
{
    expr_discharge(fs, e);
    expr_free(fs, e);
    bcreg_reserve(fs, 1);
    expr_toreg(fs, e, fs->freereg - 1);
}

/* Discharge an expression to any register. */
static BCReg expr_toanyreg(FuncState *fs, ExpDesc *e)
{
    expr_discharge(fs, e);
    if (e->k == VNONRELOC) {
        if (!expr_hasjump(e)) return e->u.s.info;  /* Already in a register. */
        if (e->u.s.info >= fs->nactvar) {
            expr_toreg(fs, e, e->u.s.info);  /* Discharge to temp. register. */
            return e->u.s.info;
        }
    }
    expr_tonextreg(fs, e);  /* Discharge to next register. */
    return e->u.s.info;
}

/* Partially discharge expression to a value. */
static void expr_toval(FuncState *fs, ExpDesc *e)
{
    if (expr_hasjump(e))
        expr_toanyreg(fs, e);
    else
        expr_discharge(fs, e);
}

/* Emit store for LHS expression. */
static void bcemit_store(FuncState *fs, ExpDesc *var, ExpDesc *e)
{
    BCIns ins;
    if (var->k == VLOCAL) {
        assert(var->u.s.aux < fs->ls->vstack.size());
        fs->ls->vstack[var->u.s.aux].info |= VSTACK_VAR_RW;
        expr_free(fs, e);
        expr_toreg(fs, e, var->u.s.info);
        return;
    } else if (var->k == VUPVAL) {
        assert(var->u.s.aux < fs->ls->vstack.size());
        fs->ls->vstack[var->u.s.aux].info |= VSTACK_VAR_RW;
        expr_toval(fs, e);
        if (e->k <= VKTRUE)
            ins = BCINS_AD(BC_USETP, var->u.s.info, const_pri(e));
        else if (e->k == VKSTR)
            ins = BCINS_AD(BC_USETS, var->u.s.info, const_str(e));
        else if (e->k == VKNUM)
            ins = BCINS_AD(BC_USETN, var->u.s.info, const_num(e));
        else
            ins = BCINS_AD(BC_USETV, var->u.s.info, expr_toanyreg(fs, e));
    } else if (var->k == VGLOBAL) {
        BCReg ra = expr_toanyreg(fs, e);
        ins = BCINS_AD(BC_GSET, ra, const_str(var));
    } else {
        BCReg ra, rc;
        assert(var->k == VINDEXED && "bad expr type");
        ra = expr_toanyreg(fs, e);
        rc = var->u.s.aux;
        if ((int32_t)rc < 0) {
            ins = BCINS_ABC(BC_TSETS, ra, var->u.s.info, var->aux2);
        } else if (rc > BCMAX_C) {
            ins = BCINS_ABC(BC_TSETB, ra, var->u.s.info, rc-(BCMAX_C+1));
        } else {
            /* Free late alloced key reg to avoid assert on free of value reg. */
            /* This can only happen when called from expr_table(). */
            if (e->k == VNONRELOC && ra >= fs->nactvar && rc >= ra)
                bcreg_free(fs, rc);
            ins = BCINS_ABC(BC_TSETV, ra, var->u.s.info, rc);
        }
    }
    std::ignore = bcemit_INS(fs, ins);
    expr_free(fs, e);
}

/* Emit method lookup expression. */
static void bcemit_method(FuncState *fs, ExpDesc *e, ExpDesc *key)
{
    BCReg obj = expr_toanyreg(fs, e);
    expr_free(fs, e);
    BCReg func = fs->freereg;
    std::ignore = bcemit_AD(fs, BC_MOV, func+1+LJ_FR2, obj);  /* Copy object to 1st argument. */
    assert(expr_isstrk(key) && "bad usage");
    TValue idx = const_str(key);
    bcreg_reserve(fs, 2+LJ_FR2);
    std::ignore = bcemit_ABC(fs, BC_TGETS, func, obj, idx);
    e->u.s.info = func;
    e->k = VNONRELOC;
}

/* -- Bytecode emitter for branches --------------------------------------- */

/* Emit unconditional branch. */
static BCPos ALWAYS_INLINE bcemit_jmp_impl(FuncState *fs, bool isLoopHint)
{
    BCPos jpc = fs->jpc;
    BCPos j = fs->pc - 1;
    BCIns *ip = &fs->bcbase[j].inst;
    fs->jpc = NO_JMP;
    if ((int32_t)j >= (int32_t)fs->lasttarget && (bc_op(*ip) == BC_UCLO || bc_op(*ip) == BC_UCLO_LH)) {
        if (isLoopHint) { setbc_op(*ip, BC_UCLO_LH); }
        setbc_j(*ip, NO_JMP);
        fs->lasttarget = j+1;
    } else {
        j = bcemit_AJ(fs, isLoopHint ? BC_JMP_LH : BC_JMP, fs->freereg, NO_JMP);
    }
    jmp_append(fs, &j, jpc);
    return j;
}

static BCPos bcemit_jmp(FuncState *fs)
{
    return bcemit_jmp_impl(fs, false /*isLoopHint*/);
}

static BCPos bcemit_jmp_loophint(FuncState *fs)
{
    return bcemit_jmp_impl(fs, true /*isLoopHint*/);
}

/* Invert branch condition of bytecode instruction. */
static void invertcond(FuncState *fs, ExpDesc *e)
{
    BCIns *ip = &fs->bcbase[e->u.s.info - 1].inst;
    setbc_op(*ip, bc_op(*ip)^1);
}

/* Emit conditional branch. */
static BCPos bcemit_branch(FuncState *fs, ExpDesc *e, int cond)
{
    BCPos pc;
    if (e->k == VRELOCABLE) {
        BCIns *ip = bcptr(fs, e);
        if (bc_op(*ip) == BC_NOT) {
            *ip = BCINS_AD(cond ? BC_ISF : BC_IST, 0, bc_d(*ip));
            return bcemit_jmp(fs);
        }
    }
    if (e->k != VNONRELOC) {
        bcreg_reserve(fs, 1);
        expr_toreg_nobranch(fs, e, fs->freereg-1);
    }
    std::ignore = bcemit_AD(fs, cond ? BC_ISTC : BC_ISFC, NO_REG, e->u.s.info);
    pc = bcemit_jmp(fs);
    expr_free(fs, e);
    return pc;
}

/* Emit branch on true condition. */
static void bcemit_branch_t(FuncState *fs, ExpDesc *e)
{
    BCPos pc;
    expr_discharge(fs, e);
    if (e->k == VKSTR || e->k == VKNUM || e->k == VKTRUE)
        pc = NO_JMP;  /* Never jump. */
    else if (e->k == VJMP)
        invertcond(fs, e), pc = e->u.s.info;
    else if (e->k == VKFALSE || e->k == VKNIL)
        expr_toreg_nobranch(fs, e, NO_REG), pc = bcemit_jmp(fs);
    else
        pc = bcemit_branch(fs, e, 0);
    jmp_append(fs, &e->f, pc);
    jmp_tohere(fs, e->t);
    e->t = NO_JMP;
}

/* Emit branch on false condition. */
static void bcemit_branch_f(FuncState *fs, ExpDesc *e)
{
    BCPos pc;
    expr_discharge(fs, e);
    if (e->k == VKNIL || e->k == VKFALSE)
        pc = NO_JMP;  /* Never jump. */
    else if (e->k == VJMP)
        pc = e->u.s.info;
    else if (e->k == VKSTR || e->k == VKNUM || e->k == VKTRUE)
        expr_toreg_nobranch(fs, e, NO_REG), pc = bcemit_jmp(fs);
    else
        pc = bcemit_branch(fs, e, 1);
    jmp_append(fs, &e->t, pc);
    jmp_tohere(fs, e->f);
    e->f = NO_JMP;
}

/* -- Bytecode emitter for operators -------------------------------------- */

static double lj_vm_foldarith(double x, double y, int op)
{
    switch (op) {
    case OPR_ADD - OPR_ADD: return x+y;
    case OPR_SUB - OPR_ADD: return x-y;
    case OPR_MUL - OPR_ADD: return x*y;
    case OPR_DIV - OPR_ADD: return x/y;
    // TODO: Lua 5.3 compat
    case OPR_MOD - OPR_ADD: return x-floor(x/y)*y;
    case OPR_POW - OPR_ADD: return pow(x, y);
    default: { assert(false); __builtin_unreachable(); }
    }
}

static bool tvismzero(TValue o)
{
    return o.m_value == 0x8000000000000000ULL;
}

/* Try constant-folding of arithmetic operators. */
static bool WARN_UNUSED foldarith(BinOpr opr, ExpDesc *e1, ExpDesc *e2)
{
    double n;
    if (!expr_isnumk_nojump(e1) || !expr_isnumk_nojump(e2)) return false;

    n = lj_vm_foldarith(expr_numberV(e1), expr_numberV(e2), (int)opr-OPR_ADD);
    TValue o = TValue::Create<tDouble>(n);

    if (IsNaN(n) || tvismzero(o)) return false;  /* Avoid NaN and -0 as consts. */
#if 0
    if (LJ_DUALNUM) {
        int32_t k = lj_num2int(n);
        if ((lua_Number)k == n) {
            setintV(&e1->u.nval, k);
            return 1;
        }
    }
#endif
    e1->u.nval = o;
    return true;
}

/* Emit arithmetic operator. */
static void bcemit_arith(FuncState *fs, BinOpr opr, ExpDesc *e1, ExpDesc *e2)
{
    BCIns ins;
    if (foldarith(opr, e1, e2))
        return;
    if (opr == OPR_POW) {
        BCReg rc = expr_toanyreg(fs, e2);
        BCReg rb = expr_toanyreg(fs, e1);
        ins = BCINS_ABC(BC_POW, 0, rb, rc);
    } else {
        int op = opr-OPR_ADD+BC_ADDVV;
        /* Must discharge 2nd operand first since VINDEXED might free regs. */
        expr_toval(fs, e2);
        bool isRhsConstant;
        BCReg rhs = 0;
        TValue rhsConstant;
        if (expr_isnumk(e2))
        {
            isRhsConstant = true;
            rhsConstant = const_num(e2);
            op -= BC_ADDVV-BC_ADDVN;
        }
        else
        {
            isRhsConstant = false;
            rhs = expr_toanyreg(fs, e2);
        }
        /* 1st operand discharged by bcemit_binop_left, but need KNUM/KSHORT. */
        assert((expr_isnumk(e1) || e1->k == VNONRELOC) && "bad expr type");
        expr_toval(fs, e1);
        /* Avoid two consts to satisfy bytecode constraints. */
        if (isRhsConstant)
        {
            BCReg lhs = expr_toanyreg(fs, e1);
            ins = BCINS_ABC(op, 0, lhs, rhsConstant);
        }
        else if (expr_isnumk(e1))
        {
            TValue lhsConstant = const_num(e1);
            op -= BC_ADDVV-BC_ADDNV;
            // Note that the NV bytecode still takes the BCReg as B, so lhs and rhs are reversed
            //
            ins = BCINS_ABC(op, 0, rhs, lhsConstant);
        }
        else
        {
            BCReg lhs = expr_toanyreg(fs, e1);
            ins = BCINS_ABC(op, 0, lhs, rhs);
        }
    }
    /* Using expr_free might cause asserts if the order is wrong. */
    if (e1->k == VNONRELOC && e1->u.s.info >= fs->nactvar) fs->freereg--;
    if (e2->k == VNONRELOC && e2->u.s.info >= fs->nactvar) fs->freereg--;
    e1->u.s.info = bcemit_INS(fs, ins);
    e1->k = VRELOCABLE;
}

/* Emit comparison operator. */
static void bcemit_comp(FuncState *fs, BinOpr opr, ExpDesc *e1, ExpDesc *e2)
{
    ExpDesc *eret = e1;
    BCIns ins;
    if (opr == OPR_EQ || opr == OPR_NE) {
        expr_toval(fs, e1);
        BCOp op = opr == OPR_EQ ? BC_ISEQV : BC_ISNEV;
        BCReg ra;
        if (expr_isk(e1)) { e1 = e2; e2 = eret; }  /* Need constant in 2nd arg. */
        ra = expr_toanyreg(fs, e1);  /* First arg must be in a reg. */
        expr_toval(fs, e2);
        switch (e2->k) {
        case VKNIL: case VKFALSE: case VKTRUE:
            ins = BCINS_AD(op+(BC_ISEQP-BC_ISEQV), ra, const_pri(e2));
            break;
        case VKSTR:
            ins = BCINS_AD(op+(BC_ISEQS-BC_ISEQV), ra, const_str(e2));
            break;
        case VKNUM:
            ins = BCINS_AD(op+(BC_ISEQN-BC_ISEQV), ra, const_num(e2));
            break;
        default:
            ins = BCINS_AD(op, ra, expr_toanyreg(fs, e2));
            break;
        }
    } else {
        uint32_t op = opr-OPR_LT+BC_ISLT;
        if ((op-BC_ISLT) & 1) {  /* GT -> LT, GE -> LE */
            // We need to swap operands and opcode
            // e1 => rhs
            // e2 => lhs
            //
            op = ((op-BC_ISLT)^3)+BC_ISLT;
            ExpDesc* lhs = e2;
            ExpDesc* rhs = e1;

            // I have no idea if it is required to discharge e1 (i.e., rhs) before e2,
            // but I just don't want to introduce any bug in the parser logic.
            // So always discharge rhs (i.e., e1) first (as the original code is doing),
            // and make our logic adapt to the constraint..
            //
            expr_toval(fs, rhs);

            if (expr_isnumk(rhs))
            {
                // RHS is constant, use 'var op num' opcode
                //
                op += BC_ISLTVN - BC_ISLT;
                expr_toval(fs, lhs);
                BCReg lhsReg = expr_toanyreg(fs, lhs);
                TValue rhsConstant = const_num(rhs);
                ins = BCINS_AD(op, lhsReg, rhsConstant);
            }
            else
            {
                expr_toval(fs, lhs);
                if (expr_isnumk(lhs))
                {
                    // LHS is constant, use 'num op var' opcode
                    //
                    op += BC_ISLTNV - BC_ISLT;
                    TValue lhsConstant = const_num(lhs);
                    BCReg rhsReg = expr_toanyreg(fs, rhs);
                    ins = BCINS_AD(op, rhsReg, lhsConstant);
                }
                else
                {
                    // neither LHS nor RHS is constant
                    //
                    BCReg lhsReg = expr_toanyreg(fs, lhs);
                    BCReg rhsReg = expr_toanyreg(fs, rhs);
                    ins = BCINS_AD(op, lhsReg, rhsReg);
                }
            }
        } else {
            ExpDesc* lhs = e1;
            ExpDesc* rhs = e2;

            expr_toval(fs, lhs);

            if (expr_isnumk(lhs))
            {
                // LHS is constant, use 'num op var' opcode
                //
                op += BC_ISLTNV - BC_ISLT;
                TValue lhsConstant = const_num(lhs);
                BCReg rhsReg = expr_toanyreg(fs, rhs);
                ins = BCINS_AD(op, rhsReg, lhsConstant);
            }
            else
            {
                expr_toval(fs, rhs);
                if (expr_isnumk(rhs))
                {
                    // RHS is constant, use 'var op num' opcode
                    //
                    op += BC_ISLTVN - BC_ISLT;
                    BCReg lhsReg = expr_toanyreg(fs, lhs);
                    TValue rhsConstant = const_num(rhs);
                    ins = BCINS_AD(op, lhsReg, rhsConstant);
                }
                else
                {
                   // neither LHS nor RHS is constant
                   //
                   BCReg rhsReg = expr_toanyreg(fs, rhs);
                   BCReg lhsReg  = expr_toanyreg(fs, lhs);
                   ins = BCINS_AD(op, lhsReg, rhsReg);
                }
            }
        }
    }
    /* Using expr_free might cause asserts if the order is wrong. */
    if (e1->k == VNONRELOC && e1->u.s.info >= fs->nactvar) fs->freereg--;
    if (e2->k == VNONRELOC && e2->u.s.info >= fs->nactvar) fs->freereg--;
    std::ignore = bcemit_INS(fs, ins);
    eret->u.s.info = bcemit_jmp(fs);
    eret->k = VJMP;
}

/* Fixup left side of binary operator. */
static void bcemit_binop_left(FuncState *fs, BinOpr op, ExpDesc *e)
{
    if (op == OPR_AND) {
        bcemit_branch_t(fs, e);
    } else if (op == OPR_OR) {
        bcemit_branch_f(fs, e);
    } else if (op == OPR_CONCAT) {
        expr_tonextreg(fs, e);
    } else if (op == OPR_EQ || op == OPR_NE) {
        if (!expr_isk_nojump(e)) expr_toanyreg(fs, e);
    } else {
        if (!expr_isnumk_nojump(e)) expr_toanyreg(fs, e);
    }
}

/* Emit binary operator. */
static void bcemit_binop(FuncState *fs, BinOpr op, ExpDesc *e1, ExpDesc *e2)
{
    if (op <= OPR_POW) {
        bcemit_arith(fs, op, e1, e2);
    } else if (op == OPR_AND) {
        assert(e1->t == NO_JMP && "jump list not closed");
        expr_discharge(fs, e2);
        jmp_append(fs, &e2->f, e1->f);
        *e1 = *e2;
    } else if (op == OPR_OR) {
        assert(e1->f == NO_JMP && "jump list not closed");
        expr_discharge(fs, e2);
        jmp_append(fs, &e2->t, e1->t);
        *e1 = *e2;
    } else if (op == OPR_CONCAT) {
        expr_toval(fs, e2);
        if (e2->k == VRELOCABLE && bc_op(*bcptr(fs, e2)) == BC_CAT) {
            assert(e1->u.s.info == bc_b(*bcptr(fs, e2))-1 && "bad CAT stack layout");
            expr_free(fs, e1);
            setbc_b(*bcptr(fs, e2), e1->u.s.info);
            e1->u.s.info = e2->u.s.info;
        } else {
            expr_tonextreg(fs, e2);
            expr_free(fs, e2);
            expr_free(fs, e1);
            e1->u.s.info = bcemit_ABC(fs, BC_CAT, 0, e1->u.s.info, e2->u.s.info);
        }
        e1->k = VRELOCABLE;
    } else {
        assert((op == OPR_NE || op == OPR_EQ || op == OPR_LT || op == OPR_GE || op == OPR_LE || op == OPR_GT) && "bad binop");
        bcemit_comp(fs, op, e1, e2);
    }
}

/* Emit unary operator. */
static void bcemit_unop(FuncState *fs, BCOp op, ExpDesc *e)
{
    if (op == BC_NOT) {
        /* Swap true and false lists. */
        { BCPos temp = e->f; e->f = e->t; e->t = temp; }
        jmp_dropval(fs, e->f);
        jmp_dropval(fs, e->t);
        expr_discharge(fs, e);
        if (e->k == VKNIL || e->k == VKFALSE) {
            e->k = VKTRUE;
            return;
        } else if (expr_isk(e)) {
            e->k = VKFALSE;
            return;
        } else if (e->k == VJMP) {
            invertcond(fs, e);
            return;
        } else if (e->k == VRELOCABLE) {
            bcreg_reserve(fs, 1);
            setbc_a(*bcptr(fs, e), fs->freereg-1);
            e->u.s.info = fs->freereg-1;
            e->k = VNONRELOC;
        } else {
            assert(e->k == VNONRELOC && "bad expr type");
        }
    } else {
        assert((op == BC_UNM || op == BC_LEN) && "bad unop");
        if (op == BC_UNM && !expr_hasjump(e)) {  /* Constant-fold negations. */
                if (expr_isnumk(e) && !expr_numiszero(e)) {  /* Avoid folding to -0. */
                    TValue *o = expr_numtv(e);
                    assert(o->Is<tInt32>() || o->Is<tDouble>());
                    if (o->Is<tInt32>()) {
                        int32_t k = o->As<tInt32>();
                        if (k == std::numeric_limits<int32_t>::min())
                            *o = TValue::Create<tDouble>(-static_cast<double>(k));
                        else
                            *o = TValue::Create<tInt32>(-k);
                        return;
                    } else {
                        *o = TValue::Create<tDouble>(-(o->As<tDouble>()));
                        return;
                    }
                }
        }
        expr_toanyreg(fs, e);
    }
    expr_free(fs, e);
    e->u.s.info = bcemit_AD(fs, op, 0, e->u.s.info);
    e->k = VRELOCABLE;
}

/* -- Lexer support ------------------------------------------------------- */

/* Check and consume optional token. */
static int lex_opt(LexState *ls, LexToken tok)
{
    if (ls->tok == tok) {
        lj_lex_next(ls);
        return 1;
    }
    return 0;
}

/* Check and consume token. */
static void lex_check(LexState *ls, LexToken tok)
{
    if (ls->tok != tok)
        err_token(ls, tok);
    lj_lex_next(ls);
}

/* Check for matching token. */
static void lex_match(LexState *ls, LexToken what, LexToken /*who*/, BCLine line)
{
    if (!lex_opt(ls, what)) {
        if (line == ls->linenumber) {
            err_token(ls, what);
        } else {
            // const char *swhat = lj_lex_token2str(ls, what);
            // const char *swho = lj_lex_token2str(ls, who);
            ls->errorCode = LJ_ERR_XMATCH;
            ls->errorToken = ls->tok;
            parser_throw(ls);
        }
    }
}

/* Check for string token. */
static HeapPtr<HeapString> lex_str(LexState *ls)
{
    if (ls->tok != TK_name && (LJ_52 || ls->tok != TK_goto))
        err_token(ls, TK_name);
    assert(ls->tokval.Is<tString>());
    HeapPtr<HeapString> s = ls->tokval.As<tString>();
    lj_lex_next(ls);
    return s;
}

/* -- Variable handling --------------------------------------------------- */

VarInfo& var_get(LexState* ls, FuncState* fs, size_t i)
{
    assert(i < LJ_MAX_LOCVAR);
    VarIndex idx = fs->varmap[i];
    assert(idx < ls->vstack.size());
    return ls->vstack[idx];
}

/* Define a new local variable. */
static void var_new(LexState *ls, BCReg n, HeapPtr<HeapString> name)
{
    FuncState *fs = ls->fs;
    MSize vtop = ls->vstack.size();
    checklimit(fs, fs->nactvar+n, LJ_MAX_LOCVAR, "too many local variables");
    checklimit(fs, vtop, LJ_MAX_VSTACK, "variable stack exceeded limit");
    ls->vstack.push_back(VarInfo { .name = name });
    fs->varmap[fs->nactvar+n] = (uint16_t)vtop;
}

static void var_new_lit(LexState *ls, BCReg n, const char* lit)
{
    var_new(ls, n, lj_parse_keepstr(ls, lit, strlen(lit)).As<tString>());
}

static void var_new_fixed(LexState *ls, BCReg n, size_t vn)
{
    var_new(ls, n, reinterpret_cast<HeapPtr<HeapString>>(vn));
}

/* Add local variables. */
static void var_add(LexState *ls, BCReg nvars)
{
    FuncState *fs = ls->fs;
    BCReg nactvar = fs->nactvar;
    while (nvars--) {
        VarInfo *v = &var_get(ls, fs, nactvar);
        v->startpc = fs->pc;
        v->slot = nactvar++;
        v->info = 0;
    }
    fs->nactvar = nactvar;
}

/* Remove local variables. */
static void var_remove(LexState *ls, BCReg tolevel)
{
    FuncState *fs = ls->fs;
    while (fs->nactvar > tolevel)
        var_get(ls, fs, --fs->nactvar).endpc = fs->pc;
}

/* Lookup local variable name. */
static BCReg var_lookup_local(FuncState *fs, HeapPtr<HeapString> n)
{
    int i;
    for (i = fs->nactvar-1; i >= 0; i--) {
        if (n == var_get(fs->ls, fs, i).name)
            return (BCReg)i;
    }
    return (BCReg)-1;  /* Not found. */
}

/* Lookup or add upvalue index. */
static MSize var_lookup_uv(FuncState *fs, MSize vidx, ExpDesc *e)
{
    MSize i, n = fs->nuv;
    for (i = 0; i < n; i++)
        if (fs->uvmap[i] == vidx)
            return i;  /* Already exists. */
    /* Otherwise create a new one. */
    checklimit(fs, fs->nuv, LJ_MAX_UPVAL, "too many upvalues");
    assert((e->k == VLOCAL || e->k == VUPVAL) && "bad expr type");
    fs->uvmap[n] = (uint16_t)vidx;
    fs->uvtmp[n] = (uint16_t)(e->k == VLOCAL ? vidx : LJ_MAX_VSTACK+e->u.s.info);
    fs->nuv = n+1;
    return n;
}

/* Forward declaration. */
static void fscope_uvmark(FuncState *fs, BCReg level);

/* Recursively lookup variables in enclosing functions. */
static MSize var_lookup_(FuncState *fs, HeapPtr<HeapString> name, ExpDesc *e, int first)
{
    if (fs) {
        BCReg reg = var_lookup_local(fs, name);
        if ((int32_t)reg >= 0) {  /* Local in this function? */
            expr_init(e, VLOCAL, reg);
            if (!first)
                fscope_uvmark(fs, reg);  /* Scope now has an upvalue. */
            return (MSize)(e->u.s.aux = (uint32_t)fs->varmap[reg]);
        } else {
            MSize vidx = var_lookup_(fs->prev, name, e, 0);  /* Var in outer func? */
            if ((int32_t)vidx >= 0) {  /* Yes, make it an upvalue here. */
                e->u.s.info = (uint8_t)var_lookup_uv(fs, vidx, e);
                e->k = VUPVAL;
                return vidx;
            }
        }
    } else {  /* Not found in any function, must be a global. */
        expr_init(e, VGLOBAL, 0);
        e->u.sval = name;
    }
    return (MSize)-1;  /* Global. */
}

/* Lookup variable name. */
#define var_lookup(ls, e) \
var_lookup_((ls)->fs, lex_str(ls), (e), 1)

/* -- Goto an label handling ---------------------------------------------- */

/* Add a new goto or label. */
static MSize gola_new(LexState *ls, HeapPtr<HeapString> name, uint8_t info, BCPos pc)
{
    FuncState *fs = ls->fs;
    MSize vtop = ls->vstack.size();
    checklimit(fs, vtop, LJ_MAX_VSTACK, "variable stack exceeded limit");
    /* NOBARRIER: name is anchored in fs->kt and ls->vstack is not a GCobj. */
    ls->vstack.push_back(VarInfo {
        .name = name,
        .startpc = pc,
        .slot = (uint8_t)fs->nactvar,
        .info = info
    });
    return vtop;
}

#define gola_isgoto(v)		((v)->info & VSTACK_GOTO)
#define gola_islabel(v)		((v)->info & VSTACK_LABEL)
#define gola_isgotolabel(v)	((v)->info & (VSTACK_GOTO|VSTACK_LABEL))

/* Patch goto to jump to label. */
static void gola_patch(LexState *ls, VarInfo *vg, VarInfo *vl)
{
    FuncState *fs = ls->fs;
    BCPos pc = vg->startpc;
    vg->name = nullptr;  /* Invalidate pending goto. */
    setbc_a(fs->bcbase[pc].inst, vl->slot);
    jmp_patch(fs, pc, vl->startpc);
}

/* Patch goto to close upvalues. */
static void gola_close(LexState *ls, VarInfo *vg)
{
    FuncState *fs = ls->fs;
    BCPos pc = vg->startpc;
    BCIns *ip = &fs->bcbase[pc].inst;
    assert(gola_isgoto(vg) && "expected goto");
    assert((bc_op(*ip) == BC_JMP || bc_op(*ip) == BC_JMP_LH || bc_op(*ip) == BC_UCLO || bc_op(*ip) == BC_UCLO_LH) && "bad bytecode op");
    setbc_a(*ip, vg->slot);
    if (bc_op(*ip) == BC_JMP || bc_op(*ip) == BC_JMP_LH) {
        BCPos next = jmp_next(fs, pc);
        if (next != NO_JMP) jmp_patch(fs, next, pc);  /* Jump to UCLO. */
        setbc_op(*ip, bc_op(*ip) == BC_JMP ? BC_UCLO : BC_UCLO_LH);  /* Turn into UCLO. */
        setbc_j(*ip, NO_JMP);
    }
}

/* Resolve pending forward gotos for label. */
static void gola_resolve(LexState *ls, FuncScope *bl, MSize idx)
{
    VarInfo *vg = ls->vstack.data() + bl->vstart;
    VarInfo *vl = ls->vstack.data() + idx;
    for (; vg < vl; vg++)
        if (vg->name == vl->name && gola_isgoto(vg)) {
            if (vg->slot < vl->slot) {
                // HeapPtr<HeapString> name = var_get(ls, ls->fs, vg->slot).name;
                ls->linenumber = ls->fs->bcbase[vg->startpc].line;
                assert(vg->name != NAME_BREAK && "unexpected break");
                ls->errorCode = LJ_ERR_XGSCOPE;
                parser_throw(ls);
            }
            gola_patch(ls, vg, vl);
        }
}

/* Fixup remaining gotos and labels for scope. */
static void gola_fixup(LexState *ls, FuncScope *bl)
{
    VarInfo *v = ls->vstack.data() + bl->vstart;
    VarInfo *ve = ls->vstack.data() + ls->vstack.size();
    for (; v < ve; v++) {
        HeapPtr<HeapString> name = v->name;
        if (name != nullptr) {  /* Only consider remaining valid gotos/labels. */
            if (gola_islabel(v)) {
                VarInfo *vg;
                v->name = nullptr;  /* Invalidate label that goes out of scope. */
                for (vg = v+1; vg < ve; vg++)  /* Resolve pending backward gotos. */
                    if (vg->name == name && gola_isgoto(vg)) {
                        if ((bl->flags&FSCOPE_UPVAL) && vg->slot > v->slot)
                            gola_close(ls, vg);
                        gola_patch(ls, vg, v);
                    }
            } else if (gola_isgoto(v)) {
                if (bl->prev) {  /* Propagate goto or break to outer scope. */
                    bl->prev->flags |= name == NAME_BREAK ? FSCOPE_BREAK : FSCOPE_GOLA;
                    v->slot = bl->nactvar;
                    if ((bl->flags & FSCOPE_UPVAL))
                        gola_close(ls, v);
                } else {  /* No outer scope: undefined goto label or no loop. */
                    ls->linenumber = ls->fs->bcbase[v->startpc].line;
                    if (name == NAME_BREAK)
                        ls->errorCode = LJ_ERR_XGSCOPE;
                    else
                        ls->errorCode = LJ_ERR_XLUNDEF;
                    parser_throw(ls);
                }
            }
        }
    }
}

/* Find existing label. */
static VarInfo *gola_findlabel(LexState *ls, HeapPtr<HeapString> name)
{
    VarInfo *v = ls->vstack.data() + ls->fs->bl->vstart;
    VarInfo *ve = ls->vstack.data() + ls->vstack.size();
    for (; v < ve; v++)
        if (v->name == name && gola_islabel(v))
            return v;
    return NULL;
}

/* -- Scope handling ------------------------------------------------------ */

/* Begin a scope. */
static void fscope_begin(FuncState *fs, FuncScope *bl, int flags)
{
    bl->nactvar = (uint8_t)fs->nactvar;
    bl->flags = flags;
    bl->vstart = fs->ls->vstack.size();
    bl->prev = fs->bl;
    fs->bl = bl;
    assert(fs->freereg == fs->nactvar && "bad regalloc");
}

/* End a scope. */
static void fscope_end(FuncState *fs)
{
    FuncScope *bl = fs->bl;
    LexState *ls = fs->ls;
    fs->bl = bl->prev;
    var_remove(ls, bl->nactvar);
    fs->freereg = fs->nactvar;
    assert(bl->nactvar == fs->nactvar && "bad regalloc");
    if ((bl->flags & (FSCOPE_UPVAL|FSCOPE_NOCLOSE)) == FSCOPE_UPVAL)
        std::ignore = bcemit_AJ(fs, BC_UCLO, bl->nactvar, 0);
    if ((bl->flags & FSCOPE_BREAK)) {
        if ((bl->flags & FSCOPE_LOOP)) {
            MSize idx = gola_new(ls, NAME_BREAK, VSTACK_LABEL, fs->pc);
            ls->vstack.resize(idx);  /* Drop break label immediately. */
            gola_resolve(ls, bl, idx);
        } else {  /* Need the fixup step to propagate the breaks. */
            gola_fixup(ls, bl);
            return;
        }
    }
    if ((bl->flags & FSCOPE_GOLA)) {
        gola_fixup(ls, bl);
    }
}

/* Mark scope as having an upvalue. */
static void fscope_uvmark(FuncState *fs, BCReg level)
{
    FuncScope *bl;
    for (bl = fs->bl; bl && bl->nactvar > level; bl = bl->prev)
        ;
    if (bl)
        bl->flags |= FSCOPE_UPVAL;
}

/* -- Function state management ------------------------------------------- */

/* Fixup upvalues for child prototype, step #2. */
static void fs_fixup_uv2(FuncState *fs, UnlinkedCodeBlock* ucb)
{
    assert(!ucb->m_uvFixUpCompleted);
    ucb->m_uvFixUpCompleted = true;
    VarInfo *vstack = fs->ls->vstack.data();
    UpvalueMetadata* uv = ucb->m_upvalueInfo;
    size_t n = ucb->m_numUpvalues;
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t vidx = uv[i].m_slot;
        if (vidx >= LJ_MAX_VSTACK)
        {
            // The m_isImmutable field will be populated in the final uv fixup step
            //
            assert(!uv[i].m_immutabilityFieldFinalized);
            uv[i].m_isParentLocal = false;
            uv[i].m_slot = vidx - LJ_MAX_VSTACK;
        }
        else
        {
            assert(vidx < fs->ls->vstack.size());
            assert(!uv[i].m_immutabilityFieldFinalized);
            DEBUG_ONLY(uv[i].m_immutabilityFieldFinalized = true;)
            if ((vstack[vidx].info & VSTACK_VAR_RW))
            {
                uv[i].m_isParentLocal = true;
                uv[i].m_isImmutable = false;
                uv[i].m_slot = vstack[vidx].slot;
            }
            else
            {
                uv[i].m_isParentLocal = true;
                uv[i].m_isImmutable = true;
                uv[i].m_slot = vstack[vidx].slot;
            }
        }
    }
}

/* Fixup bytecode for prototype. */
static void fs_fixup_bc(FuncState *fs, UnlinkedCodeBlock* ucb, BytecodeBuilder& bw, MSize n)
{
    using namespace DeegenBytecodeBuilder;

    std::vector<size_t> bytecodeLocation;
    std::vector<std::pair<size_t, size_t>> jumpPatches;

    assert(ucb->m_parserUVGetFixupList == nullptr);
    ucb->m_parserUVGetFixupList = new std::vector<uint32_t>();

    size_t bcOrd;
    BCInsLine *base = fs->bcbase;

    // This function decodes and skips the trailing JMP bytecode and returns the bytecode ordinal the JMP bytecode targets
    //
    auto decodeAndSkipNextJumpBytecode = [&]() WARN_UNUSED -> size_t
    {
        int32_t selfBytecodeOrdinal = static_cast<int32_t>(bcOrd);

        bcOrd++;
        assert(bcOrd < n);
        BCIns nextIns = base[bcOrd].inst;
        assert(bc_op(nextIns) == BC_JMP);

        // The 'JMP' bytecode immediately following the comparsion should never be a valid jump target
        //
        bytecodeLocation.push_back(static_cast<size_t>(-1));

        int32_t jumpTargetOffset = bc_j(nextIns) + 1;
        int32_t jumpBytecodeOrdinal = selfBytecodeOrdinal + 1 + jumpTargetOffset;
        assert(jumpBytecodeOrdinal >= 0);
        return static_cast<size_t>(jumpBytecodeOrdinal);
    };

    // Similar to 'decodeAndSkipNextJumpBytecode', but skips the ITERL bytecode
    //
    auto decodeAndSkipNextITERLBytecode = [&]() WARN_UNUSED -> size_t
    {
        int32_t selfBytecodeOrdinal = static_cast<int32_t>(bcOrd);

        assert(bc_op(base[bcOrd].inst) == BC_ITERC || bc_op(base[bcOrd].inst) == BC_ITERN);
        [[maybe_unused]] int32_t curBase = bc_a(base[bcOrd].inst);

        bcOrd++;
        assert(bcOrd < n);
        BCIns nextIns = base[bcOrd].inst;
        assert(bc_op(nextIns) == BC_ITERL);

        // This 'ITERL' bytecode should never be a valid jump target
        //
        bytecodeLocation.push_back(static_cast<size_t>(-1));

        // The 'ITERL' bytecode should have the same base as the ITERC/ITERN bytecode
        //
        [[maybe_unused]] int32_t baseA = bc_a(nextIns);
        assert(baseA == curBase);

        int32_t jumpTargetOffset = bc_j(nextIns) + 1;
        int32_t jumpBytecodeOrdinal = selfBytecodeOrdinal + 1 + jumpTargetOffset;
        assert(jumpBytecodeOrdinal >= 0);
        return static_cast<size_t>(jumpBytecodeOrdinal);
    };

    auto getBytecodeOrdinalOfJump = [&](int32_t offset) -> size_t
    {
        int32_t selfBytecodeOrdinal = static_cast<int32_t>(bcOrd);
        int32_t jumpBytecodeOrdinal = selfBytecodeOrdinal + offset + 1;
        assert(jumpBytecodeOrdinal >= 0);
        return static_cast<size_t>(jumpBytecodeOrdinal);
    };

    bytecodeLocation.push_back(static_cast<size_t>(-1));

    for (bcOrd = 1; bcOrd < n; bcOrd++)
    {
        bytecodeLocation.push_back(bw.GetCurLength());

        BCIns ins = base[bcOrd].inst;
        int opcode = bc_op(ins);
        switch (opcode)
        {
        case BC_ADDVN:
        {
            bw.CreateAdd({
                .lhs = Local { bc_b(ins) },
                .rhs = bc_cst(ins),
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_SUBVN:
        {
            bw.CreateSub({
                .lhs = Local { bc_b(ins) },
                .rhs = bc_cst(ins),
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_MULVN:
        {
            bw.CreateMul({
                .lhs = Local { bc_b(ins) },
                .rhs = bc_cst(ins),
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_DIVVN:
        {
            bw.CreateDiv({
                .lhs = Local { bc_b(ins) },
                .rhs = bc_cst(ins),
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_MODVN:
        {
            bw.CreateMod({
                .lhs = Local { bc_b(ins) },
                .rhs = bc_cst(ins),
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_ADDNV:
        {
            bw.CreateAdd({
                .lhs = bc_cst(ins),
                .rhs = Local { bc_b(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_SUBNV:
        {
            bw.CreateSub({
                .lhs = bc_cst(ins),
                .rhs = Local { bc_b(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_MULNV:
        {
            bw.CreateMul({
                .lhs = bc_cst(ins),
                .rhs = Local { bc_b(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_DIVNV:
        {
            bw.CreateDiv({
                .lhs = bc_cst(ins),
                .rhs = Local { bc_b(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_MODNV:
        {
            bw.CreateMod({
                .lhs = bc_cst(ins),
                .rhs = Local { bc_b(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_ADDVV:
        {
            bw.CreateAdd({
                .lhs = Local { bc_b(ins) },
                .rhs = Local { bc_c(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_SUBVV:
        {
            bw.CreateSub({
                .lhs = Local { bc_b(ins) },
                .rhs = Local { bc_c(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_MULVV:
        {
            bw.CreateMul({
                .lhs = Local { bc_b(ins) },
                .rhs = Local { bc_c(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_DIVVV:
        {
            bw.CreateDiv({
                .lhs = Local { bc_b(ins) },
                .rhs = Local { bc_c(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_MODVV:
        {
            bw.CreateMod({
                .lhs = Local { bc_b(ins) },
                .rhs = Local { bc_c(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_POW:
        {
            bw.CreatePow({
                .lhs = Local { bc_b(ins) },
                .rhs = Local { bc_c(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_CAT:
        {
            assert(bc_c(ins) >= bc_b(ins));
            uint16_t num = SafeIntegerCast<uint16_t>(bc_c(ins) - bc_b(ins) + 1);
            bw.CreateConcat({
                .base = Local { bc_b(ins) },
                .num = num,
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_KSHORT:
        {
            bw.CreateSetConstInt16({
                .value = static_cast<int16_t>(static_cast<int32_t>(bc_d(ins))),
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_ISLT:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfLT({
                .lhs = Local { bc_a(ins) },
                .rhs = Local { bc_d(ins) }
            });
            break;
        }
        case BC_ISGE:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfNLT({
                .lhs = Local { bc_a(ins) },
                .rhs = Local { bc_d(ins) }
            });
            break;
        }
        case BC_ISLE:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfLE({
                .lhs = Local { bc_a(ins) },
                .rhs = Local { bc_d(ins) }
            });
            break;
        }
        case BC_ISGT:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfNLE({
                .lhs = Local { bc_a(ins) },
                .rhs = Local { bc_d(ins) }
            });
            break;
        }
        case BC_ISLTNV:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfLT({
                .lhs = bc_cst(ins),
                .rhs = Local { bc_a(ins) }
            });
            break;
        }
        case BC_ISGENV:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfNLT({
                .lhs = bc_cst(ins),
                .rhs = Local { bc_a(ins) }
            });
            break;
        }
        case BC_ISLENV:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfLE({
                .lhs = bc_cst(ins),
                .rhs = Local { bc_a(ins) }
            });
            break;
        }
        case BC_ISGTNV:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfNLE({
                .lhs = bc_cst(ins),
                .rhs = Local { bc_a(ins) }
            });
            break;
        }
        case BC_ISLTVN:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfLT({
                .lhs = Local { bc_a(ins) },
                .rhs = bc_cst(ins)
            });
            break;
        }
        case BC_ISGEVN:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfNLT({
                .lhs = Local { bc_a(ins) },
                .rhs = bc_cst(ins)
            });
            break;
        }
        case BC_ISLEVN:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfLE({
                .lhs = Local { bc_a(ins) },
                .rhs = bc_cst(ins)
            });
            break;
        }
        case BC_ISGTVN:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfNLE({
                .lhs = Local { bc_a(ins) },
                .rhs = bc_cst(ins)
            });
            break;
        }
        case BC_ISEQV:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfEq({
                .lhs = Local { bc_a(ins) },
                .rhs = Local { bc_d(ins) }
            });
            break;
        }
        case BC_ISNEV:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfNotEq({
                .lhs = Local { bc_a(ins) },
                .rhs = Local { bc_d(ins) }
            });
            break;
        }
        case BC_ISEQS:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfEq({
                .lhs = Local { bc_a(ins) },
                .rhs = bc_cst(ins)
            });
            break;
        }
        case BC_ISNES:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfNotEq({
                .lhs = Local { bc_a(ins) },
                .rhs = bc_cst(ins)
            });
            break;
        }
        case BC_ISEQN:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfEq({
                .lhs = Local { bc_a(ins) },
                .rhs = bc_cst(ins)
            });
            break;
        }
        case BC_ISNEN:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfNotEq({
                .lhs = Local { bc_a(ins) },
                .rhs = bc_cst(ins)
            });
            break;
        }
        case BC_ISEQP:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfEq({
                .lhs = Local { bc_a(ins) },
                .rhs = bc_cst(ins)
            });
            break;
        }
        case BC_ISNEP:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfNotEq({
                .lhs = Local { bc_a(ins) },
                .rhs = bc_cst(ins)
            });
            break;
        }
        case BC_ISTC:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateSelectAndBranchIfTruthy({
                .testValue = Local { bc_d(ins) },
                .defaultValue = Local { bc_a(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_ISFC:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateSelectAndBranchIfFalsy({
                .testValue = Local { bc_d(ins) },
                .defaultValue = Local { bc_a(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_IST:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfTruthy({
                .testValue = Local { bc_d(ins) }
            });
            break;
        }
        case BC_ISF:
        {
            size_t brTarget = decodeAndSkipNextJumpBytecode();
            jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
            bw.CreateBranchIfFalsy({
                .testValue = Local { bc_d(ins) }
            });
            break;
        }
        case BC_GGET:
        {
            bw.CreateGlobalGet({
                .index = bc_cst(ins),
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_GSET:
        {
            bw.CreateGlobalPut({
                .index = bc_cst(ins),
                .value = Local { bc_a(ins) }
            });
            break;
        }
        case BC_RETM:
        {
            // For RETM, D holds # of fixed return values
            //
            uint16_t numReturnValues = SafeIntegerCast<uint16_t>(bc_d(ins));
            bw.CreateRetM({
                .retStart = Local { bc_a(ins) },
                .numRet = numReturnValues
            });
            break;
        }
        case BC_RET:
        {
            // For RET, D holds 1 + # ret values
            //
            assert(bc_d(ins) >= 1);
            uint16_t numReturnValues = SafeIntegerCast<uint16_t>(bc_d(ins) - 1);
            bw.CreateRet({
                .retStart = Local { bc_a(ins) },
                .numRet = numReturnValues
            });
            break;
        }
        case BC_RET0:
        {
            bw.CreateRet0();
            break;
        }
        case BC_RET1:
        {
            bw.CreateRet({
                .retStart = Local { bc_a(ins) },
                .numRet = 1
            });
            break;
        }
        case BC_CALLM:
        {
            // B stores # fixed results + 1, and if opdata[1] == 0, it stores all results
            // Coincidentally we use -1 to represent 'store all results', so we can simply subtract 1
            //
            int32_t numResults = static_cast<int32_t>(bc_b(ins)) - 1;
            // For CALLM, C holds # of fixed params
            //
            uint32_t numFixedParams = SafeIntegerCast<uint32_t>(bc_c(ins));
            bw.CreateCallM({
                .base = Local { bc_a(ins) },
                .numArgs = numFixedParams,
                .numRets = numResults
            });
            break;
        }
        case BC_CALL:
        {
            // B stores # fixed results + 1, and if opdata[1] == 0, it stores all results
            // Coincidentally we use -1 to represent 'store all results', so we can simply subtract 1
            //
            int32_t numResults = static_cast<int32_t>(bc_b(ins)) - 1;
            // For CALL, C holds 1 + # of fixed params
            //
            assert(bc_c(ins) >= 1);
            uint32_t numFixedParams = SafeIntegerCast<uint32_t>(bc_c(ins) - 1);
            bw.CreateCall({
                .base = Local { bc_a(ins) },
                .numArgs = numFixedParams,
                .numRets = numResults
            });
            break;
        }
        case BC_CALLMT:
        {
            // For CALLMT, D holds # of fixed params
            //
            uint32_t numFixedParams = SafeIntegerCast<uint32_t>(bc_d(ins));
            bw.CreateCallMT({
                .base = Local { bc_a(ins) },
                .numArgs = numFixedParams
            });
            break;
        }
        case BC_CALLT:
        {
            // For CALLT, D holds 1 + # of fixed params
            //
            assert(bc_d(ins) >= 1);
            uint32_t numFixedParams = SafeIntegerCast<uint32_t>(bc_d(ins) - 1);
            bw.CreateCallT({
                .base = Local { bc_a(ins) },
                .numArgs = numFixedParams
            });
            break;
        }
        case BC_MOV:
        {
            bw.CreateMov({
                .input = Local { bc_d(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_NOT:
        {
            bw.CreateLogicalNot({
                .value = Local { bc_d(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_UNM:
        {
            bw.CreateUnaryMinus({
                .input = Local { bc_d(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_LEN:
        {
            bw.CreateLengthOf({
                .input = Local { bc_d(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_KSTR:
        {
            bw.CreateMov({
                .input = bc_cst(ins),
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_KNUM:
        {
            bw.CreateMov({
                .input = bc_cst(ins),
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_KPRI:
        {
            bw.CreateMov({
                .input = bc_cst(ins),
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_FNEW:
        {
            bw.CreateNewClosure({
                .unlinkedCb = bc_cst(ins),
                .output = Local { bc_a(ins) }
            });
            UnlinkedCodeBlock* childUcb = reinterpret_cast<UnlinkedCodeBlock*>(bc_cst(ins).m_value);
            fs_fixup_uv2(fs, childUcb);
            childUcb->m_parent = ucb;
            break;
        }
        case BC_TNEW:
        {
            // We modified LuaJIT's TNEW to take b and c instead.
            // b is the array part hint, c is the inline capacity hint
            //
            VM* vm = VM::GetActiveVMForCurrentThread();
            uint32_t arrayPartHint = bc_b(ins);
            uint32_t inlineCapacityHint = bc_c(ins);
            uint8_t stepping = Structure::GetInitialStructureSteppingForInlineCapacity(inlineCapacityHint);
            // Create the structure now, so we can call GetInitialStructureForSteppingKnowingAlreadyBuilt at runtime
            //
            std::ignore = Structure::GetInitialStructureForStepping(vm, stepping);

            bw.CreateTableNew({
                .inlineStorageSizeStepping = stepping,
                .arrayPartSizeHint = static_cast<uint16_t>(arrayPartHint),
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_TDUP:
        {
            TValue tv = bc_cst(ins);
            assert(tv.Is<tTable>());
            HeapPtr<TableObject> tab = tv.As<tTable>();
            bool usedSpecializedTableDup = false;
            if (TCGet(tab->m_hiddenClass).As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure)
            {
                HeapPtr<Structure> structure = TCGet(tab->m_hiddenClass).As<Structure>();
                if (structure->m_butterflyNamedStorageCapacity == 0 && !TCGet(tab->m_arrayType).HasSparseMap())
                {
                    uint8_t inlineCapacity = structure->m_inlineNamedStorageCapacity;
                    uint8_t stepping = Structure::GetInitialStructureSteppingForInlineCapacity(inlineCapacity);
                    assert(internal::x_inlineStorageSizeForSteppingArray[stepping] == inlineCapacity);
                    if (tab->m_butterfly == nullptr)
                    {
                        if (stepping <= TableObject::TableDupMaxInlineCapacitySteppingForNoButterflyCase())
                        {
                            usedSpecializedTableDup = true;
                            bw.CreateTableDup({
                                .src = tv,
                                .inlineCapacityStepping = stepping,
                                .hasButterfly = 0,
                                .output = Local { bc_a(ins) }
                            });
                        }
                    }
                    else
                    {
                        if (stepping <= TableObject::TableDupMaxInlineCapacitySteppingForHasButterflyCase())
                        {
                            usedSpecializedTableDup = true;
                            bw.CreateTableDup({
                                .src = tv,
                                .inlineCapacityStepping = stepping,
                                .hasButterfly = 1,
                                .output = Local { bc_a(ins) }
                            });
                        }
                    }
                }
            }
            if (!usedSpecializedTableDup)
            {
                bw.CreateTableDupGeneral({
                    .src = tv,
                    .output = Local { bc_a(ins) }
                });
            }
            break;
        }
        case BC_TGETV:
        {
            bw.CreateTableGetByVal({
                .base = Local { bc_b(ins) },
                .index = Local { bc_c(ins) },
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_TGETS:
        {
            bw.CreateTableGetById({
                .base = Local { bc_b(ins) },
                .index = bc_cst(ins),
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_TSETV:
        {
            bw.CreateTablePutByVal({
                .base = Local { bc_b(ins) },
                .index = Local { bc_c(ins) },
                .value = Local { bc_a(ins) }
            });
            break;
        }
        case BC_TSETS:
        {
            bw.CreateTablePutById({
                .base = Local { bc_b(ins) },
                .index = bc_cst(ins),
                .value = Local { bc_a(ins) }
            });
            break;
        }
        case BC_TGETB:
        {
            bw.CreateTableGetByImm({
                .base = Local { bc_b(ins) },
                .index = SafeIntegerCast<int16_t>(bc_c(ins)),
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_TSETB:
        {
            bw.CreateTablePutByImm({
                .base = Local { bc_b(ins) },
                .index = SafeIntegerCast<int16_t>(bc_c(ins)),
                .value = Local { bc_a(ins) }
            });
            break;
        }
        case BC_TSETM:
        {
            // This opcode reads from slot A-1...
            //
            assert(bc_a(ins) >= 1);
            uint32_t localSlot = static_cast<uint32_t>(bc_a(ins) - 1);
            TValue tvIndex = bc_cst(ins);
            assert(tvIndex.Is<tInt32>());
            int32_t idx = tvIndex.As<tInt32>();
            bw.CreateTableVariadicPutBySeq({
                .base = Local { localSlot },
                .index = Cst<tInt32>(idx)
            });
            break;
        }
        case BC_UGET:
        {
            ucb->m_parserUVGetFixupList->push_back(static_cast<uint32_t>(bw.GetCurLength()));
            bw.CreateUpvalueGetMutable({
                .ord = SafeIntegerCast<uint16_t>(bc_d(ins)),
                .output = Local { bc_a(ins) }
            });
            break;
        }
        case BC_USETV:
        {
            bw.CreateUpvaluePut({
                .ord = SafeIntegerCast<uint16_t>(bc_a(ins)),
                .value = Local { bc_d(ins) }
            });
            break;
        }
        case BC_USETS:
        {
            bw.CreateUpvaluePut({
                .ord = SafeIntegerCast<uint16_t>(bc_a(ins)),
                .value = bc_cst(ins)
            });
            break;
        }
        case BC_USETN:
        {
            bw.CreateUpvaluePut({
                .ord = SafeIntegerCast<uint16_t>(bc_a(ins)),
                .value = bc_cst(ins)
            });
            break;
        }
        case BC_USETP:
        {
            bw.CreateUpvaluePut({
                .ord = SafeIntegerCast<uint16_t>(bc_a(ins)),
                .value = bc_cst(ins)
            });
            break;
        }
        case BC_UCLO:
        {
            size_t jumpTarget = getBytecodeOrdinalOfJump(bc_j(ins));
            jumpPatches.push_back(std::make_pair(jumpTarget, bw.GetCurLength()));
            bw.CreateUpvalueClose({
                .base = Local { bc_a(ins) }
            });
            break;
        }
        case BC_UCLO_LH:
        {
            size_t jumpTarget = getBytecodeOrdinalOfJump(bc_j(ins));
            jumpPatches.push_back(std::make_pair(jumpTarget, bw.GetCurLength()));
            bw.CreateUpvalueCloseLoopHint({
                .base = Local { bc_a(ins) }
            });
            break;
        }
        case BC_FORI:
        {
            // Loop init
            // semantics:
            // [A] = tonumber([A]), [A+1] = tonumber([A+1]), [A+2] = tonumber([A+2])
            // if (!([A+2] > 0 && [A] <= [A+1]) || ([A+2] <= 0 && [A] >= [A+1])): jump
            // [A+3] = [A]
            //
            size_t jumpTarget = getBytecodeOrdinalOfJump(bc_j(ins));
            jumpPatches.push_back(std::make_pair(jumpTarget, bw.GetCurLength()));
            bw.CreateForLoopInit({
                .base = Local { bc_a(ins) }
            });
            break;
        }
        case BC_FORL:
        {
            // Loop step
            // semantics:
            // [A] += [A+2]
            // if ([A+2] > 0 && [A] <= [A+1]) || ([A+2] <= 0 && [A] >= [A+1]): jump
            //
            size_t jumpTarget = getBytecodeOrdinalOfJump(bc_j(ins));
            jumpPatches.push_back(std::make_pair(jumpTarget, bw.GetCurLength()));
            bw.CreateForLoopStep({
                .base = Local { bc_a(ins) }
            });
            break;
        }
        case BC_LOOP:
        {
            // LOOP is a no-op used for LuaJIT's internal profiling
            // For now make it a no-op for us as well
            //
            break;
        }
        case BC_REP_LH:
        {
            bw.CreateLoopHeaderHint();
            break;
        }
        case BC_JMP:
        {
            size_t jumpTarget = getBytecodeOrdinalOfJump(bc_j(ins));
            jumpPatches.push_back(std::make_pair(jumpTarget, bw.GetCurLength()));
            // bc_a is unused for now (indicates stack frame size after jump)
            bw.CreateBranch();
            break;
        }
        case BC_JMP_LH:
        {
            size_t jumpTarget = getBytecodeOrdinalOfJump(bc_j(ins));
            jumpPatches.push_back(std::make_pair(jumpTarget, bw.GetCurLength()));
            // bc_a is unused for now (indicates stack frame size after jump)
            bw.CreateBranchLoopHint();
            break;
        }
        case BC_VARG:
        {
            // should have 3 opdata, despite field C is ignored by us
            //
            int32_t fieldB = bc_b(ins);
            if (fieldB == 0)
            {
                // Put vararg as variadic returns
                //
                bw.CreateStoreVarArgsAsVariadicResults();
            }
            else
            {
                assert(fieldB >= 1);
                bw.CreateGetVarArgsPrefix({
                    .base = Local { bc_a(ins) },
                    .numToPut = SafeIntegerCast<uint16_t>(fieldB - 1)
                });
            }
            break;
        }
        case BC_KNIL:
        {
            assert(bc_d(ins) >= bc_a(ins));
            uint32_t numSlotsToFill = static_cast<uint32_t>(bc_d(ins) - bc_a(ins) + 1);
            bw.CreateRangeFillNils({
                .base = Local { bc_a(ins) },
                .numToPut = SafeIntegerCast<uint16_t>(numSlotsToFill)
            });
            break;
        }
        case BC_ITERN:
        {
            // semantics:
            // [A], ... [A+B-2] = [A-3]([A-2], [A-1])
            //
            assert(bc_c(ins) == 3);
            assert(bc_b(ins) >= 2);
            assert(bc_a(ins) >= 3);
            uint8_t numRets = SafeIntegerCast<uint8_t>(bc_b(ins) - 1);
            assert(1 <= numRets && numRets <= 2);
            size_t jumpTarget = decodeAndSkipNextITERLBytecode();
            jumpPatches.push_back(std::make_pair(jumpTarget, bw.GetCurLength()));
            bw.CreateKVLoopIter({
                .base = Local { bc_a(ins) - 3 },
                .numRets = numRets
            });
            break;
        }
        case BC_ITERC:
        {
            // semantics:
            // [A], ... [A+B-2] = [A-3]([A-2], [A-1])
            //
            assert(bc_c(ins) == 3);
            assert(bc_b(ins) >= 2);
            assert(bc_a(ins) >= 3);
            uint16_t numRets = SafeIntegerCast<uint16_t>(bc_b(ins) - 1);
            size_t jumpTarget = decodeAndSkipNextITERLBytecode();
            jumpPatches.push_back(std::make_pair(jumpTarget, bw.GetCurLength()));
            bw.CreateForLoopIter({
                .base = Local { bc_a(ins) - 3 },
                .numRets = numRets
            });
            break;
        }
        case BC_ITERL:
        {
            assert(false && "should never hit here since ITERL should always be after a ITERN/ITERC and skipped when we process the ITERN/ITERC");
            __builtin_unreachable();
        }
        case BC_ISNEXT:
        {
            assert(bc_a(ins) >= 3);
            size_t jumpTarget = getBytecodeOrdinalOfJump(bc_j(ins));
            jumpPatches.push_back(std::make_pair(jumpTarget, bw.GetCurLength()));
            bw.CreateValidateIsNextAndBranch({
                .base = Local { bc_a(ins) - 3 }
            });
            break;
        }
        default:
        {
            // These opcodes should never be generated by LuaJIT parser
            //
            assert(false && "Unexpected opcode");
            __builtin_unreachable();
        }
        }   /* switch opcode */
    }

    assert(bytecodeLocation.size() == n);

    for (auto& jumpPatch : jumpPatches)
    {
        size_t ljBytecodeOrd = jumpPatch.first;
        size_t bcPos = jumpPatch.second;
        assert(ljBytecodeOrd < bytecodeLocation.size());
        size_t bytecodeOffset = bytecodeLocation[ljBytecodeOrd];
        assert(bytecodeOffset < bw.GetCurLength());
        if (unlikely(!bw.SetBranchTarget(bcPos, bytecodeOffset)))
        {
            // TODO: gracefully handle
            fprintf(stderr, "[LOCKDOWN] Branch bytecode exceeded maximum branch offset limit. Maybe make your function smaller?\n");
            abort();
        }
    }

    assert(bw.CheckWellFormedness());
}

#if 0
#ifndef LUAJIT_DISABLE_DEBUGINFO
/* Prepare lineinfo for prototype. */
static size_t fs_prep_line(FuncState *fs, BCLine numline)
{
    return (fs->pc-1) << (numline < 256 ? 0 : numline < 65536 ? 1 : 2);
}

/* Fixup lineinfo for prototype. */
static void fs_fixup_line(FuncState *fs, GCproto *pt,
                          void *lineinfo, BCLine numline)
{
    BCInsLine *base = fs->bcbase + 1;
    BCLine first = fs->linedefined;
    MSize i = 0, n = fs->pc-1;
    pt->firstline = fs->linedefined;
    pt->numline = numline;
    setmref(pt->lineinfo, lineinfo);
    if (LJ_LIKELY(numline < 256)) {
        uint8_t *li = (uint8_t *)lineinfo;
        do {
            BCLine delta = base[i].line - first;
            lj_assertFS(delta >= 0 && delta < 256, "bad line delta");
            li[i] = (uint8_t)delta;
        } while (++i < n);
    } else if (LJ_LIKELY(numline < 65536)) {
        uint16_t *li = (uint16_t *)lineinfo;
        do {
            BCLine delta = base[i].line - first;
            lj_assertFS(delta >= 0 && delta < 65536, "bad line delta");
            li[i] = (uint16_t)delta;
        } while (++i < n);
    } else {
        uint32_t *li = (uint32_t *)lineinfo;
        do {
            BCLine delta = base[i].line - first;
            lj_assertFS(delta >= 0, "bad line delta");
            li[i] = (uint32_t)delta;
        } while (++i < n);
    }
}

/* Prepare variable info for prototype. */
static size_t fs_prep_var(LexState *ls, FuncState *fs, size_t *ofsvar)
{
    VarInfo *vs =ls->vstack, *ve;
    MSize i, n;
    BCPos lastpc;
    lj_buf_reset(&ls->sb);  /* Copy to temp. string buffer. */
    /* Store upvalue names. */
    for (i = 0, n = fs->nuv; i < n; i++) {
        GCstr *s = strref(vs[fs->uvmap[i]].name);
        MSize len = s->len+1;
        char *p = lj_buf_more(&ls->sb, len);
        p = lj_buf_wmem(p, strdata(s), len);
        ls->sb.w = p;
    }
    *ofsvar = sbuflen(&ls->sb);
    lastpc = 0;
    /* Store local variable names and compressed ranges. */
    for (ve = vs + ls->vtop, vs += fs->vbase; vs < ve; vs++) {
        if (!gola_isgotolabel(vs)) {
            GCstr *s = strref(vs->name);
            BCPos startpc;
            char *p;
            if ((uintptr_t)s < VARNAME__MAX) {
                p = lj_buf_more(&ls->sb, 1 + 2*5);
                *p++ = (char)(uintptr_t)s;
            } else {
                MSize len = s->len+1;
                p = lj_buf_more(&ls->sb, len + 2*5);
                p = lj_buf_wmem(p, strdata(s), len);
            }
            startpc = vs->startpc;
            p = lj_strfmt_wuleb128(p, startpc-lastpc);
            p = lj_strfmt_wuleb128(p, vs->endpc-startpc);
            ls->sb.w = p;
            lastpc = startpc;
        }
    }
    lj_buf_putb(&ls->sb, '\0');  /* Terminator for varinfo. */
    return sbuflen(&ls->sb);
}

/* Fixup variable info for prototype. */
static void fs_fixup_var(LexState *ls, GCproto *pt, uint8_t *p, size_t ofsvar)
{
    setmref(pt->uvinfo, p);
    setmref(pt->varinfo, (char *)p + ofsvar);
    memcpy(p, ls->sb.b, sbuflen(&ls->sb));  /* Copy from temp. buffer. */
}
#else

/* Initialize with empty debug info, if disabled. */
#define fs_prep_line(fs, numline)		(UNUSED(numline), 0)
#define fs_fixup_line(fs, pt, li, numline) \
pt->firstline = pt->numline = 0, setmref((pt)->lineinfo, NULL)
#define fs_prep_var(ls, fs, ofsvar)		(UNUSED(ofsvar), 0)
#define fs_fixup_var(ls, pt, p, ofsvar) \
    setmref((pt)->uvinfo, NULL), setmref((pt)->varinfo, NULL)

#endif

#endif

/* Check if bytecode op returns. */
static bool bcopisret(BCOp op)
{
    switch (op) {
    case BC_CALLMT: case BC_CALLT:
    case BC_RETM: case BC_RET: case BC_RET0: case BC_RET1:
        return true;
    default:
        return false;
    }
}

/* Fixup return instruction for prototype. */
static void fs_fixup_ret(FuncState *fs)
{
    BCPos lastpc = fs->pc;
    if (lastpc <= fs->lasttarget || !bcopisret(bc_op(fs->bcbase[lastpc-1].inst))) {
        if ((fs->bl->flags & FSCOPE_UPVAL))
            std::ignore = bcemit_AJ(fs, BC_UCLO, 0, 0);
        std::ignore = bcemit_AD(fs, BC_RET0, 0, 1);  /* Need final return. */
    }
    fs->bl->flags |= FSCOPE_NOCLOSE;  /* Handled above. */
    fscope_end(fs);
    assert(fs->bl == NULL && "bad scope nesting");
    /* May need to fixup returns encoded before first function was created. */
    if (fs->flags & PROTO_FIXUP_RETURN) {
        BCPos pc;
        for (pc = 1; pc < lastpc; pc++) {
            BCIns ins = fs->bcbase[pc].inst;
            BCPos offset;
            switch (bc_op(ins)) {
            case BC_CALLMT: case BC_CALLT:
            case BC_RETM: case BC_RET: case BC_RET0: case BC_RET1:
                offset = bcemit_INS(fs, ins);  /* Copy original instruction. */
                fs->bcbase[offset].line = fs->bcbase[pc].line;
                offset = offset-(pc+1)+BCBIAS_J;
                if (offset > BCMAX_D)
                    err_syntax(fs->ls, LJ_ERR_XFIXUP);
                /* Replace with UCLO plus branch. */
                fs->bcbase[pc].inst = BCINS_AD(BC_UCLO, 0, offset);
                break;
            case BC_FNEW:
                return;  /* We're done. */
            default:
                break;
            }
        }
    }
}

/* Finish a FuncState and return the new prototype. */
static UnlinkedCodeBlock* fs_finish(LexState *ls, BCLine /*line*/)
{
    VM* vm = VM::GetActiveVMForCurrentThread();
    UnlinkedCodeBlock* ucb = UnlinkedCodeBlock::Create(vm, ls->L->m_globalObject.As());

    FuncState *fs = ls->fs;

    /* Apply final fixups. */
    fs_fixup_ret(fs);

    ucb->m_numFixedArguments = fs->numparams;
    ucb->m_hasVariadicArguments = (fs->flags & PROTO_VARARG) > 0;
    ucb->m_stackFrameNumSlots = fs->framesize;
    ucb->m_bytecodeBuilder = new BytecodeBuilder();
    fs_fixup_bc(fs, ucb, *ucb->m_bytecodeBuilder, fs->pc);

    // BCLine numline = line - fs->linedefined;

    uint32_t numUpvalues = fs->nuv;
    ucb->m_numUpvalues = numUpvalues;
    ucb->m_upvalueInfo = new UpvalueMetadata[ucb->m_numUpvalues];
    for (uint32_t i = 0; i < numUpvalues; i++)
    {
        DEBUG_ONLY(ucb->m_upvalueInfo[i].m_immutabilityFieldFinalized = false;)
        ucb->m_upvalueInfo[i].m_slot = fs->uvtmp[i];
    }

    // fs_fixup_line(fs, pt, (void *)((char *)pt + ofsli), numline);
    // fs_fixup_var(ls, pt, (uint8_t *)((char *)pt + ofsdbg), ofsvar);

    ls->vstack.resize(fs->vbase);  /* Reset variable stack. */
    ls->fs = fs->prev;
    assert((ls->fs != NULL || ls->tok == TK_eof) && "bad parser state");
    return ucb;
}

/* Initialize a new FuncState. */
static void fs_init(LexState *ls, FuncState *fs)
{
    fs->prev = ls->fs; ls->fs = fs;  /* Append to list. */
    fs->ls = ls;
    fs->vbase = ls->vstack.size();
    fs->L = ls->L;
    fs->pc = 0;
    fs->lasttarget = 0;
    fs->jpc = NO_JMP;
    fs->freereg = 0;
    fs->nkgc = 0;
    fs->nkn = 0;
    fs->nactvar = 0;
    fs->nuv = 0;
    fs->bl = NULL;
    fs->flags = 0;
    fs->framesize = 1;  /* Minimum frame size. */
}

/* -- Expressions --------------------------------------------------------- */

/* Forward declaration. */
static void expr(LexState *ls, ExpDesc *v);

/* Return string expression. */
static void expr_str(LexState *ls, ExpDesc *e)
{
    expr_init(e, VKSTR, 0);
    e->u.sval = lex_str(ls);
}

static bool checku8(int32_t k)
{
    return 0 <= k && k <= 255;
}

/* Return index expression. */
static void expr_index(FuncState *fs, ExpDesc *t, ExpDesc *e)
{
    /* Already called: expr_toval(fs, e). */
    t->k = VINDEXED;
    if (expr_isnumk(e)) {
        double n = expr_numberV(e);
        int32_t k = static_cast<int32_t>(n);
        if (checku8(k) && n == (double)k) {
            t->u.s.aux = BCMAX_C+1+(uint32_t)k;  /* 256..511: const byte key */
            return;
        }
    } else if (expr_isstrk(e)) {
        TValue idx = const_str(e); /* const string key */
        t->u.s.aux = -1;
        t->aux2 = idx;
        return;
    }
    t->u.s.aux = expr_toanyreg(fs, e);  /* 0..255: register */
}

/* Parse index expression with named field. */
static void expr_field(LexState *ls, ExpDesc *v)
{
    FuncState *fs = ls->fs;
    ExpDesc key;
    expr_toanyreg(fs, v);
    lj_lex_next(ls);  /* Skip dot or colon. */
    expr_str(ls, &key);
    expr_index(fs, v, &key);
}

/* Parse index expression with brackets. */
static void expr_bracket(LexState *ls, ExpDesc *v)
{
    lj_lex_next(ls);  /* Skip '['. */
    expr(ls, v);
    expr_toval(ls->fs, v);
    lex_check(ls, ']');
}

/* Get value of constant expression. */
static void expr_kvalue(FuncState * /*fs*/, TValue *v, ExpDesc *e)
{
    if (e->k <= VKTRUE) {
        *v = const_pri(e);
    } else if (e->k == VKSTR) {
        *v = TValue::Create<tString>(e->u.sval);
    } else {
        TValue val = *expr_numtv(e);
        assert(val.Is<tInt32>() || val.Is<tDouble>());
        *v = val;
    }
}

/* Parse table constructor expression. */
static void expr_table(LexState *ls, ExpDesc *e)
{
    // debug knob to dump info about the template table / table size hint
    //
    constexpr bool x_debug_dump_table_info = false;

    FuncState *fs = ls->fs;
    BCLine line = ls->linenumber;
    int vcall = 0, needarr = 0;
    uint32_t narr = 1;  /* First array index. */
    uint32_t nhash = 0;  /* Number of hash entries. */
    BCReg freg = fs->freereg;
    BCPos pc = bcemit_ABC(fs, BC_TNEW, freg, 0, 0);
    expr_init(e, VNONRELOC, freg);
    bcreg_reserve(fs, 1);
    freg++;
    std::vector<std::pair<TValue, TValue>> tplTableKVs;
    lex_check(ls, '{');
    while (ls->tok != '}') {
        ExpDesc key, val;
        vcall = 0;
        if (ls->tok == '[') {
            expr_bracket(ls, &key);  /* Already calls expr_toval. */
            if (!expr_isk(&key)) expr_index(fs, e, &key);
            if (expr_isnumk(&key)) needarr = 1; else nhash++;
            lex_check(ls, '=');
        } else if ((ls->tok == TK_name || (!LJ_52 && ls->tok == TK_goto)) && lj_lex_lookahead(ls) == '=') {
            expr_str(ls, &key);
            lex_check(ls, '=');
            nhash++;
        } else {
            expr_init(&key, VKNUM, 0);
            key.u.nval = TValue::Create<tInt32>((int)narr);
            narr++;
            needarr = vcall = 1;
        }
        expr(ls, &val);
        if (expr_isk(&key) && key.k != VKNIL &&
            (key.k == VKSTR || expr_isk_nojump(&val))) {
            TValue k, v;
            vcall = 0;
            expr_kvalue(fs, &k, &key);
            if (expr_isk_nojump(&val)) {  /* Add const key/value to template table. */
                expr_kvalue(fs, &v, &val);
                tplTableKVs.push_back(std::make_pair(k, v));
            } else {  /* Otherwise create dummy string key (avoids lj_tab_newkey). */
                v = TValue::CreateImpossibleValue();
                tplTableKVs.push_back(std::make_pair(k, v));
                goto nonconst;
            }
        } else {
nonconst:
            if (val.k != VCALL) { expr_toanyreg(fs, &val); vcall = 0; }
            if (expr_isk(&key)) expr_index(fs, e, &key);
            bcemit_store(fs, e, &val);
        }
        fs->freereg = freg;
        if (!lex_opt(ls, ',') && !lex_opt(ls, ';')) break;
    }

    // Prepare template table if needed
    //
    bool usedTDUP = false;
    if (tplTableKVs.size() > 0)
    {
        usedTDUP = true;
        uint32_t numPropertyPartKeys = 0;
        uint32_t initButterflyArrayPartCapacity = 0;
        std::vector<std::pair<int32_t, uint64_t /*tv*/>> tplTableArrayPartKVs;
        for (auto& it: tplTableKVs)
        {
            TValue key = it.first;
            if (!key.Is<tInt32>() && !key.Is<tDouble>())
            {
                assert(!key.Is<tNil>());
                numPropertyPartKeys++;
            }
            else
            {
                bool isInt32 = false;
                int32_t ki32 = 0;
                if (key.Is<tInt32>())
                {
                    isInt32 = true;
                    ki32 = key.As<tInt32>();
                }
                else
                {
                    assert(key.Is<tDouble>());
                    double num = key.As<tDouble>();
                    int32_t i32 = static_cast<int32_t>(num);
                    if (static_cast<double>(i32) == num)
                    {
                        isInt32 = true;
                        ki32 = i32;
                    }
                }
                if (isInt32)
                {
                    if (ki32 >= ArrayGrowthPolicy::x_arrayBaseOrd && ki32 <= ArrayGrowthPolicy::x_alwaysVectorCutoff)
                    {
                        initButterflyArrayPartCapacity = std::max(initButterflyArrayPartCapacity, static_cast<uint32_t>(ki32));
                    }
                    // Since it is an array key, if it is a dummy key there is no need to pre-insert.
                    //
                    if (it.second.m_value != TValue::CreateImpossibleValue().m_value)
                    {
                        tplTableArrayPartKVs.push_back(std::make_pair(ki32, it.second.m_value));
                    }
                    // Remove the key-value pair from the property part list
                    //
                    it.first = TValue::Create<tNil>();
                }
            }
        }

        // Create the table, and insert all key-value pairs
        //
        VM* vm = VM::GetActiveVMForCurrentThread();
        // TODO: we need to anchor this table
        //
        HeapPtr<TableObject> tab = TableObject::CreateEmptyTableObject(vm, numPropertyPartKeys /*inlineCapacity*/, initButterflyArrayPartCapacity);

        if (x_debug_dump_table_info)
        {
            fprintf(stderr, "TDUP: inline capacity hint = %u, array part hint = %u\n",
                    static_cast<unsigned int>(numPropertyPartKeys), static_cast<unsigned int>(initButterflyArrayPartCapacity));
        }
        for (auto& it: tplTableKVs)
        {
            TValue key = it.first;
            TValue value = it.second;
            assert(!key.Is<tInt32>());
            if (key.Is<tNil>()) { continue; }
            if (value.m_value == TValue::CreateImpossibleValue().m_value)
            {
                value = TValue::Create<tNil>();
            }
            if (x_debug_dump_table_info)
            {
                fprintf(stderr, "TDUP table KV: key = ");
                PrintTValue(stderr, key);
                fprintf(stderr, ", value = ");
                PrintTValue(stderr, value);
                fprintf(stderr, "\n");
            }
            if (key.Is<tDouble>())
            {
                double indexDouble = key.As<tDouble>();
                assert(!IsNaN(indexDouble));
                TableObject::RawPutByValDoubleIndex(tab, indexDouble, value);
            }
            else if (key.Is<tHeapEntity>())
            {
                PutByIdICInfo icInfo;
                TableObject::PreparePutById(tab, UserHeapPointer<void> { key.As<tHeapEntity>() }, icInfo /*out*/);
                TableObject::PutById(tab, key.As<tHeapEntity>(), value, icInfo);
            }
            else
            {
                assert(key.Is<tBool>());
                UserHeapPointer<HeapString> specialKey = VM_GetSpecialKeyForBoolean(key.As<tBool>());
                PutByIdICInfo icInfo;
                TableObject::PreparePutById(tab, specialKey, icInfo /*out*/);
                TableObject::PutById(tab, specialKey.As<void>(), value, icInfo);
            }
        }

        // Put all the integer key-values in ascending order, to get a continuous array if possible
        //
        if (tplTableArrayPartKVs.size() > 0)
        {
            std::sort(tplTableArrayPartKVs.begin(), tplTableArrayPartKVs.end());
            for (auto& it : tplTableArrayPartKVs)
            {
                int32_t key = it.first;
                TValue value; value.m_value = it.second;
                assert(value.m_value != TValue::CreateImpossibleValue().m_value);
                if (x_debug_dump_table_info)
                {
                    fprintf(stderr, "TDUP table KV (array part): key = %d, value = ", static_cast<int>(key));
                    PrintTValue(stderr, value);
                    fprintf(stderr, "\n");
                }
                TableObject::RawPutByValIntegerIndex(tab, key, value);
            }
        }

        fs->bcbase[pc].inst = BCINS_AD(BC_TDUP, freg-1, TValue::Create<tTable>(tab));
    }

    lex_match(ls, '}', '{', line);
    if (vcall) {
        BCInsLine *ilp = &fs->bcbase[fs->pc-1];
        assert(bc_a(ilp->inst) == freg &&
                        bc_op(ilp->inst) == (narr > 256 ? BC_TSETV : BC_TSETB) &&
                    "bad CALL code generation");
        if (narr > 256) { fs->pc--; ilp--; }
        ilp->inst = BCINS_AD(BC_TSETM, freg, TValue::Create<tInt32>(narr-1));
        setbc_b(ilp[-1].inst, 0);
    }
    if (pc == fs->pc-1) {  /* Make expr relocable if possible. */
        e->u.s.info = pc;
        fs->freereg--;
        e->k = VRELOCABLE;
    } else {
        e->k = VNONRELOC;  /* May have been changed by expr_index. */
    }
    if (!usedTDUP) {
        // Populate TNEW inline capacity and array size hint
        //
        BCIns *ip = &fs->bcbase[pc].inst;
        if (!needarr) narr = 0;
        else if (narr < 3) narr = 3;
        else if (narr > 0x7ff) narr = 0x7ff;
        setbc_b(*ip, narr);
        setbc_c(*ip, nhash);
        if (x_debug_dump_table_info)
        {
            fprintf(stderr, "TNEW: inline capacity hint = %u, array part hint = %u\n",
                    static_cast<unsigned int>(nhash), static_cast<unsigned int>(narr));
        }
    }
}

/* Parse function parameters. */
static BCReg parse_params(LexState *ls, int needself)
{
    FuncState *fs = ls->fs;
    BCReg nparams = 0;
    lex_check(ls, '(');
    if (needself)
        var_new_lit(ls, nparams++, "self");
    if (ls->tok != ')') {
        do {
            if (ls->tok == TK_name || (!LJ_52 && ls->tok == TK_goto)) {
                var_new(ls, nparams++, lex_str(ls));
            } else if (ls->tok == TK_dots) {
                lj_lex_next(ls);
                fs->flags |= PROTO_VARARG;
                break;
            } else {
                err_syntax(ls, LJ_ERR_XPARAM);
            }
        } while (lex_opt(ls, ','));
    }
    var_add(ls, nparams);
    assert(fs->nactvar == nparams && "bad regalloc");
    bcreg_reserve(fs, nparams);
    lex_check(ls, ')');
    return nparams;
}

/* Forward declaration. */
static void parse_chunk(LexState *ls);

/* Parse body of a function. */
static void parse_body(LexState *ls, ExpDesc *e, int needself, BCLine line)
{
    FuncState fs, *pfs = ls->fs;
    FuncScope bl;
    assert(pfs->bcbase >= ls->bcstack.data());
    ptrdiff_t oldbase = pfs->bcbase - ls->bcstack.data();
    fs_init(ls, &fs);
    fscope_begin(&fs, &bl, 0);
    fs.linedefined = line;
    fs.numparams = (uint8_t)parse_params(ls, needself);
    fs.bcbase = pfs->bcbase + pfs->pc;
    fs.bclim = pfs->bclim - pfs->pc;
    std::ignore = bcemit_AD(&fs, BC_FUNCF, 0, 0);  /* Placeholder. */
    parse_chunk(ls);
    if (ls->tok != TK_end) lex_match(ls, TK_end, TK_function, line);
    UnlinkedCodeBlock* childUcb = fs_finish(ls, (ls->lastline = ls->linenumber));
    ls->ucbList.push_back(childUcb);
    pfs->bcbase = ls->bcstack.data() + oldbase;  /* May have been reallocated. */
    assert(ls->bcstack.size() >= static_cast<size_t>(oldbase));
    pfs->bclim = (BCPos)(ls->bcstack.size() - oldbase);
    /* Store new prototype in the constant array of the parent. */
    // TODO: this shouldn't be this hacky..
    //
    TValue tvUcb; tvUcb.m_value = reinterpret_cast<uint64_t>(childUcb);
    expr_init(e, VRELOCABLE,
              bcemit_AD(pfs, BC_FNEW, 0, tvUcb));

    if (!(pfs->flags & PROTO_CHILD)) {
        if (pfs->flags & PROTO_HAS_RETURN)
            pfs->flags |= PROTO_FIXUP_RETURN;
        pfs->flags |= PROTO_CHILD;
    }
    lj_lex_next(ls);

}

/* Parse expression list. Last expression is left open. */
static BCReg expr_list(LexState *ls, ExpDesc *v)
{
    BCReg n = 1;
    expr(ls, v);
    while (lex_opt(ls, ',')) {
        expr_tonextreg(ls->fs, v);
        expr(ls, v);
        n++;
    }
    return n;
}

/* Parse function argument list. */
static void parse_args(LexState *ls, ExpDesc *e)
{
    FuncState *fs = ls->fs;
    ExpDesc args;
    BCIns ins;
    BCReg base;
    BCLine line = ls->linenumber;
    if (ls->tok == '(') {
#if !LJ_52
        if (line != ls->lastline)
            err_syntax(ls, LJ_ERR_XAMBIG);
#endif
        lj_lex_next(ls);
        if (ls->tok == ')') {  /* f(). */
            args.k = VVOID;
        } else {
            expr_list(ls, &args);
            if (args.k == VCALL)  /* f(a, b, g()) or f(a, b, ...). */
                setbc_b(*bcptr(fs, &args), 0);  /* Pass on multiple results. */
        }
        lex_match(ls, ')', '(', line);
    } else if (ls->tok == '{') {
        expr_table(ls, &args);
    } else if (ls->tok == TK_string) {
        expr_init(&args, VKSTR, 0);
        args.u.sval = ls->tokval.As<tString>();
        lj_lex_next(ls);
    } else {
        err_syntax(ls, LJ_ERR_XFUNARG);
    }
    assert(e->k == VNONRELOC && "bad expr type");
    base = e->u.s.info;  /* Base register for call. */
    if (args.k == VCALL) {
        ins = BCINS_ABC(BC_CALLM, base, 2, args.u.s.aux - base - 1 - LJ_FR2);
    } else {
        if (args.k != VVOID)
            expr_tonextreg(fs, &args);
        ins = BCINS_ABC(BC_CALL, base, 2, fs->freereg - base - LJ_FR2);
    }
    expr_init(e, VCALL, bcemit_INS(fs, ins));
    e->u.s.aux = base;
    fs->bcbase[fs->pc - 1].line = line;
    fs->freereg = base+1;  /* Leave one result by default. */
}

/* Parse primary expression. */
static void expr_primary(LexState *ls, ExpDesc *v)
{
    FuncState *fs = ls->fs;
    /* Parse prefix expression. */
    if (ls->tok == '(') {
        BCLine line = ls->linenumber;
        lj_lex_next(ls);
        expr(ls, v);
        lex_match(ls, ')', '(', line);
        expr_discharge(ls->fs, v);
    } else if (ls->tok == TK_name || (!LJ_52 && ls->tok == TK_goto)) {
        var_lookup(ls, v);
    } else {
        err_syntax(ls, LJ_ERR_XSYMBOL);
    }
    for (;;) {  /* Parse multiple expression suffixes. */
        if (ls->tok == '.') {
            expr_field(ls, v);
        } else if (ls->tok == '[') {
            ExpDesc key;
            expr_toanyreg(fs, v);
            expr_bracket(ls, &key);
            expr_index(fs, v, &key);
        } else if (ls->tok == ':') {
            ExpDesc key;
            lj_lex_next(ls);
            expr_str(ls, &key);
            bcemit_method(fs, v, &key);
            parse_args(ls, v);
        } else if (ls->tok == '(' || ls->tok == TK_string || ls->tok == '{') {
            expr_tonextreg(fs, v);
            if (LJ_FR2) bcreg_reserve(fs, LJ_FR2);
            parse_args(ls, v);
        } else {
            break;
        }
    }
}

/* Parse simple expression. */
static void expr_simple(LexState *ls, ExpDesc *v)
{
    switch (ls->tok) {
    case TK_number:
        expr_init(v, VKNUM, 0);
        v->u.nval = ls->tokval;
        assert(v->u.nval.Is<tInt32>() || v->u.nval.Is<tDouble>());
        break;
    case TK_string:
        expr_init(v, VKSTR, 0);
        assert(ls->tokval.Is<tString>());
        v->u.sval = ls->tokval.As<tString>();
        break;
    case TK_nil:
        expr_init(v, VKNIL, 0);
        break;
    case TK_true:
        expr_init(v, VKTRUE, 0);
        break;
    case TK_false:
        expr_init(v, VKFALSE, 0);
        break;
    case TK_dots: {  /* Vararg. */
        FuncState *fs = ls->fs;
        BCReg base;
        checkcond(ls, fs->flags & PROTO_VARARG, LJ_ERR_XDOTS);
        bcreg_reserve(fs, 1);
        base = fs->freereg-1;
        expr_init(v, VCALL, bcemit_ABC(fs, BC_VARG, base, 2, fs->numparams));
        v->u.s.aux = base;
        break;
    }
    case '{':  /* Table constructor. */
        expr_table(ls, v);
        return;
    case TK_function:
        lj_lex_next(ls);
        parse_body(ls, v, 0, ls->linenumber);
        return;
    default:
        expr_primary(ls, v);
        return;
    }
    lj_lex_next(ls);
}

/* Manage syntactic levels to avoid blowing up the stack. */
static void synlevel_begin(LexState *ls)
{
    if (++ls->level >= LJ_MAX_XLEVEL)
    {
        ls->errorCode = LJ_ERR_XLEVELS;
        parser_throw(ls);
    }
}

#define synlevel_end(ls)	((ls)->level--)

/* Convert token to binary operator. */
static BinOpr token2binop(LexToken tok)
{
    switch (tok) {
    case '+':	return OPR_ADD;
    case '-':	return OPR_SUB;
    case '*':	return OPR_MUL;
    case '/':	return OPR_DIV;
    case '%':	return OPR_MOD;
    case '^':	return OPR_POW;
    case TK_concat: return OPR_CONCAT;
    case TK_ne:	return OPR_NE;
    case TK_eq:	return OPR_EQ;
    case '<':	return OPR_LT;
    case TK_le:	return OPR_LE;
    case '>':	return OPR_GT;
    case TK_ge:	return OPR_GE;
    case TK_and:	return OPR_AND;
    case TK_or:	return OPR_OR;
    default:	return OPR_NOBINOPR;
    }
}

/* Priorities for each binary operator. ORDER OPR. */
static const struct {
    uint8_t left;		/* Left priority. */
    uint8_t right;	/* Right priority. */
} priority[] = {
    {6,6}, {6,6}, {7,7}, {7,7}, {7,7},	/* ADD SUB MUL DIV MOD */
    {10,9}, {5,4},			/* POW CONCAT (right associative) */
    {3,3}, {3,3},				/* EQ NE */
    {3,3}, {3,3}, {3,3}, {3,3},		/* LT GE GT LE */
    {2,2}, {1,1}				/* AND OR */
};

#define UNARY_PRIORITY		8  /* Priority for unary operators. */

/* Forward declaration. */
static BinOpr expr_binop(LexState *ls, ExpDesc *v, uint32_t limit);

/* Parse unary expression. */
static void expr_unop(LexState *ls, ExpDesc *v)
{
    BCOp op;
    if (ls->tok == TK_not) {
        op = BC_NOT;
    } else if (ls->tok == '-') {
        op = BC_UNM;
    } else if (ls->tok == '#') {
        op = BC_LEN;
    } else {
        expr_simple(ls, v);
        return;
    }
    lj_lex_next(ls);
    expr_binop(ls, v, UNARY_PRIORITY);
    bcemit_unop(ls->fs, op, v);
}

/* Parse binary expressions with priority higher than the limit. */
static BinOpr expr_binop(LexState *ls, ExpDesc *v, uint32_t limit)
{
    BinOpr op;
    synlevel_begin(ls);
    expr_unop(ls, v);
    op = token2binop(ls->tok);
    while (op != OPR_NOBINOPR && priority[op].left > limit) {
        ExpDesc v2;
        BinOpr nextop;
        lj_lex_next(ls);
        bcemit_binop_left(ls->fs, op, v);
        /* Parse binary expression with higher priority. */
        nextop = expr_binop(ls, &v2, priority[op].right);
        bcemit_binop(ls->fs, op, v, &v2);
        op = nextop;
    }
    synlevel_end(ls);
    return op;  /* Return unconsumed binary operator (if any). */
}

/* Parse expression. */
static void expr(LexState *ls, ExpDesc *v)
{
    expr_binop(ls, v, 0);  /* Priority 0: parse whole expression. */
}

/* Assign expression to the next register. */
static void expr_next(LexState *ls)
{
    ExpDesc e;
    expr(ls, &e);
    expr_tonextreg(ls->fs, &e);
}

/* Parse conditional expression. */
static BCPos expr_cond(LexState *ls)
{
    ExpDesc v;
    expr(ls, &v);
    if (v.k == VKNIL) v.k = VKFALSE;
    bcemit_branch_t(ls->fs, &v);
    return v.f;
}

/* -- Assignments --------------------------------------------------------- */

/* List of LHS variables. */
typedef struct LHSVarList {
    ExpDesc v;			/* LHS variable. */
    struct LHSVarList *prev;	/* Link to previous LHS variable. */
} LHSVarList;

/* Eliminate write-after-read hazards for local variable assignment. */
static void assign_hazard(LexState *ls, LHSVarList *lh, const ExpDesc *v)
{
    FuncState *fs = ls->fs;
    BCReg reg = v->u.s.info;  /* Check against this variable. */
    BCReg tmp = fs->freereg;  /* Rename to this temp. register (if needed). */
    int hazard = 0;
    for (; lh; lh = lh->prev) {
        if (lh->v.k == VINDEXED) {
            if (lh->v.u.s.info == reg) {  /* t[i], t = 1, 2 */
                hazard = 1;
                lh->v.u.s.info = tmp;
            }
            if (lh->v.u.s.aux == reg) {  /* t[i], i = 1, 2 */
                hazard = 1;
                lh->v.u.s.aux = tmp;
            }
        }
    }
    if (hazard) {
        std::ignore = bcemit_AD(fs, BC_MOV, tmp, reg);  /* Rename conflicting variable. */
        bcreg_reserve(fs, 1);
    }
}

/* Adjust LHS/RHS of an assignment. */
static void assign_adjust(LexState *ls, BCReg nvars, BCReg nexps, ExpDesc *e)
{
    FuncState *fs = ls->fs;
    int32_t extra = (int32_t)nvars - (int32_t)nexps;
    if (e->k == VCALL) {
        extra++;  /* Compensate for the VCALL itself. */
        if (extra < 0) extra = 0;
        setbc_b(*bcptr(fs, e), extra+1);  /* Fixup call results. */
        if (extra > 1) bcreg_reserve(fs, (BCReg)extra-1);
    } else {
        if (e->k != VVOID)
            expr_tonextreg(fs, e);  /* Close last expression. */
        if (extra > 0) {  /* Leftover LHS are set to nil. */
            BCReg reg = fs->freereg;
            bcreg_reserve(fs, (BCReg)extra);
            bcemit_nil(fs, reg, (BCReg)extra);
        }
    }
    if (nexps > nvars)
        ls->fs->freereg -= nexps - nvars;  /* Drop leftover regs. */
}

/* Recursively parse assignment statement. */
static void parse_assignment(LexState *ls, LHSVarList *lh, BCReg nvars)
{
    ExpDesc e;
    checkcond(ls, VLOCAL <= lh->v.k && lh->v.k <= VINDEXED, LJ_ERR_XSYNTAX);
    if (lex_opt(ls, ',')) {  /* Collect LHS list and recurse upwards. */
        LHSVarList vl;
        vl.prev = lh;
        expr_primary(ls, &vl.v);
        if (vl.v.k == VLOCAL)
            assign_hazard(ls, lh, &vl.v);
        checklimit(ls->fs, ls->level + nvars, LJ_MAX_XLEVEL, "variable names exceeded limit");
        parse_assignment(ls, &vl, nvars+1);
    } else {  /* Parse RHS. */
        BCReg nexps;
        lex_check(ls, '=');
        nexps = expr_list(ls, &e);
        if (nexps == nvars) {
            if (e.k == VCALL) {
                if (bc_op(*bcptr(ls->fs, &e)) == BC_VARG) {  /* Vararg assignment. */
                    ls->fs->freereg--;
                    e.k = VRELOCABLE;
                } else {  /* Multiple call results. */
                    e.u.s.info = e.u.s.aux;  /* Base of call is not relocatable. */
                    e.k = VNONRELOC;
                }
            }
            bcemit_store(ls->fs, &lh->v, &e);
            return;
        }
        assign_adjust(ls, nvars, nexps, &e);
    }
    /* Assign RHS to LHS and recurse downwards. */
    expr_init(&e, VNONRELOC, ls->fs->freereg-1);
    bcemit_store(ls->fs, &lh->v, &e);
}

/* Parse call statement or assignment. */
static void parse_call_assign(LexState *ls)
{
    FuncState *fs = ls->fs;
    LHSVarList vl;
    expr_primary(ls, &vl.v);
    if (vl.v.k == VCALL) {  /* Function call statement. */
        setbc_b(*bcptr(fs, &vl.v), 1);  /* No results. */
    } else {  /* Start of an assignment. */
        vl.prev = NULL;
        parse_assignment(ls, &vl, 1);
    }
}

/* Parse 'local' statement. */
static void parse_local(LexState *ls)
{
    if (lex_opt(ls, TK_function)) {  /* Local function declaration. */
        ExpDesc v, b;
        FuncState *fs = ls->fs;
        var_new(ls, 0, lex_str(ls));
        expr_init(&v, VLOCAL, fs->freereg);
        v.u.s.aux = fs->varmap[fs->freereg];
        bcreg_reserve(fs, 1);
        var_add(ls, 1);
        parse_body(ls, &b, 0, ls->linenumber);
        /* bcemit_store(fs, &v, &b) without setting VSTACK_VAR_RW. */
        expr_free(fs, &b);
        expr_toreg(fs, &b, v.u.s.info);
        /* The upvalue is in scope, but the local is only valid after the store. */
        var_get(ls, fs, fs->nactvar - 1).startpc = fs->pc;
    } else {  /* Local variable declaration. */
        ExpDesc e;
        BCReg nexps, nvars = 0;
        do {  /* Collect LHS. */
            var_new(ls, nvars++, lex_str(ls));
        } while (lex_opt(ls, ','));
        if (lex_opt(ls, '=')) {  /* Optional RHS. */
            nexps = expr_list(ls, &e);
        } else {  /* Or implicitly set to nil. */
            e.k = VVOID;
            nexps = 0;
        }
        assign_adjust(ls, nvars, nexps, &e);
        var_add(ls, nvars);
    }
}

/* Parse 'function' statement. */
static void parse_func(LexState *ls, BCLine line)
{
    FuncState *fs;
    ExpDesc v, b;
    int needself = 0;
    lj_lex_next(ls);  /* Skip 'function'. */
    /* Parse function name. */
    var_lookup(ls, &v);
    while (ls->tok == '.')  /* Multiple dot-separated fields. */
        expr_field(ls, &v);
    if (ls->tok == ':') {  /* Optional colon to signify method call. */
        needself = 1;
        expr_field(ls, &v);
    }
    parse_body(ls, &b, needself, line);
    fs = ls->fs;
    bcemit_store(fs, &v, &b);
    fs->bcbase[fs->pc - 1].line = line;  /* Set line for the store. */
}

/* -- Control transfer statements ----------------------------------------- */

/* Check for end of block. */
static int parse_isend(LexToken tok)
{
    switch (tok) {
    case TK_else: case TK_elseif: case TK_end: case TK_until: case TK_eof:
        return 1;
    default:
        return 0;
    }
}

/* Parse 'return' statement. */
static void parse_return(LexState *ls)
{
    BCIns ins;
    FuncState *fs = ls->fs;
    lj_lex_next(ls);  /* Skip 'return'. */
    fs->flags |= PROTO_HAS_RETURN;
    if (parse_isend(ls->tok) || ls->tok == ';') {  /* Bare return. */
        ins = BCINS_AD(BC_RET0, 0, 1);
    } else {  /* Return with one or more values. */
        ExpDesc e;  /* Receives the _last_ expression in the list. */
        BCReg nret = expr_list(ls, &e);
        if (nret == 1) {  /* Return one result. */
            if (e.k == VCALL) {  /* Check for tail call. */
                BCIns *ip = bcptr(fs, &e);
                /* It doesn't pay off to add BC_VARGT just for 'return ...'. */
                if (bc_op(*ip) == BC_VARG) goto notailcall;
                fs->pc--;
                ins = BCINS_AD(bc_op(*ip)-BC_CALL+BC_CALLT, bc_a(*ip), bc_c(*ip));
            } else {  /* Can return the result from any register. */
                ins = BCINS_AD(BC_RET1, expr_toanyreg(fs, &e), 2);
            }
        } else {
            if (e.k == VCALL) {  /* Append all results from a call. */
notailcall:
                setbc_b(*bcptr(fs, &e), 0);
                ins = BCINS_AD(BC_RETM, fs->nactvar, e.u.s.aux - fs->nactvar);
            } else {
                expr_tonextreg(fs, &e);  /* Force contiguous registers. */
                ins = BCINS_AD(BC_RET, fs->nactvar, nret+1);
            }
        }
    }
    if (fs->flags & PROTO_CHILD)
        std::ignore = bcemit_AJ(fs, BC_UCLO, 0, 0);  /* May need to close upvalues first. */
    std::ignore = bcemit_INS(fs, ins);
}

/* Parse 'break' statement. */
static void parse_break(LexState *ls)
{
    ls->fs->bl->flags |= FSCOPE_BREAK;
    gola_new(ls, NAME_BREAK, VSTACK_GOTO, bcemit_jmp(ls->fs));
}

/* Parse 'goto' statement. */
static void parse_goto(LexState *ls)
{
    FuncState *fs = ls->fs;
    HeapPtr<HeapString> name = lex_str(ls);
    VarInfo *vl = gola_findlabel(ls, name);
    if (vl)  /* Treat backwards goto within same scope like a loop. */
        std::ignore = bcemit_AJ(fs, BC_LOOP, vl->slot, -1);  /* No BC range check. */
    fs->bl->flags |= FSCOPE_GOLA;
    gola_new(ls, name, VSTACK_GOTO, bcemit_jmp(fs));
}

/* Parse label. */
static void parse_label(LexState *ls)
{
    FuncState *fs = ls->fs;
    HeapPtr<HeapString> name;
    MSize idx;
    fs->lasttarget = fs->pc;
    fs->bl->flags |= FSCOPE_GOLA;
    lj_lex_next(ls);  /* Skip '::'. */
    name = lex_str(ls);
    if (gola_findlabel(ls, name))
    {
        ls->errorCode = LJ_ERR_XLDUP;
        parser_throw(ls);
    }
    idx = gola_new(ls, name, VSTACK_LABEL, fs->pc);
    lex_check(ls, TK_label);
    /* Recursively parse trailing statements: labels and ';' (Lua 5.2 only). */
    for (;;) {
        if (ls->tok == TK_label) {
            synlevel_begin(ls);
            parse_label(ls);
            synlevel_end(ls);
        } else if (LJ_52 && ls->tok == ';') {
            lj_lex_next(ls);
        } else {
            break;
        }
    }
    /* Trailing label is considered to be outside of scope. */
    if (parse_isend(ls->tok) && ls->tok != TK_until)
        ls->vstack[idx].slot = fs->bl->nactvar;
    gola_resolve(ls, fs->bl, idx);
}

/* -- Blocks, loops and conditional statements ---------------------------- */

/* Parse a block. */
static void parse_block(LexState *ls)
{
    FuncState *fs = ls->fs;
    FuncScope bl;
    fscope_begin(fs, &bl, 0);
    parse_chunk(ls);
    fscope_end(fs);
}

/* Parse 'while' statement. */
static void parse_while(LexState *ls, BCLine line)
{
    FuncState *fs = ls->fs;
    BCPos start, loop, condexit;
    FuncScope bl;
    lj_lex_next(ls);  /* Skip 'while'. */
    start = fs->lasttarget = fs->pc;
    condexit = expr_cond(ls);
    fscope_begin(fs, &bl, FSCOPE_LOOP);
    lex_check(ls, TK_do);
    loop = bcemit_AD(fs, BC_LOOP, fs->nactvar, 0);
    parse_block(ls);
    jmp_patch(fs, bcemit_jmp_loophint(fs), start);
    lex_match(ls, TK_end, TK_while, line);
    fscope_end(fs);
    jmp_tohere(fs, condexit);
    jmp_patchins(fs, loop, fs->pc);
}

/* Parse 'repeat' statement. */
static void parse_repeat(LexState *ls, BCLine line)
{
    FuncState *fs = ls->fs;
    BCPos loop = fs->lasttarget = fs->pc;
    BCPos condexit;
    FuncScope bl1, bl2;
    fscope_begin(fs, &bl1, FSCOPE_LOOP);  /* Breakable loop scope. */
    fscope_begin(fs, &bl2, 0);  /* Inner scope. */
    lj_lex_next(ls);  /* Skip 'repeat'. */
    std::ignore = bcemit_AD(fs, x_allow_interpreter_tier_up_to_baseline_jit ? BC_REP_LH : BC_LOOP, fs->nactvar, 0);
    parse_chunk(ls);
    lex_match(ls, TK_until, TK_repeat, line);
    condexit = expr_cond(ls);  /* Parse condition (still inside inner scope). */
    if (!(bl2.flags & FSCOPE_UPVAL)) {  /* No upvalues? Just end inner scope. */
        fscope_end(fs);
    } else {  /* Otherwise generate: cond: UCLO+JMP out, !cond: UCLO+JMP loop. */
        parse_break(ls);  /* Break from loop and close upvalues. */
        jmp_tohere(fs, condexit);
        fscope_end(fs);  /* End inner scope and close upvalues. */
        condexit = bcemit_jmp(fs);
    }
    jmp_patch(fs, condexit, loop);  /* Jump backwards if !cond. */
    jmp_patchins(fs, loop, fs->pc);
    fscope_end(fs);  /* End loop scope. */
}

enum {
    FORL_IDX, FORL_STOP, FORL_STEP, FORL_EXT
};

enum {
    VARNAME_END, VARNAME_FOR_IDX, VARNAME_FOR_STOP, VARNAME_FOR_STEP, VARNAME_FOR_GEN, VARNAME_FOR_STATE, VARNAME_FOR_CTL
};

/* Parse numeric 'for'. */
static void parse_for_num(LexState *ls, HeapPtr<HeapString> varname, BCLine line)
{
    FuncState *fs = ls->fs;
    BCReg base = fs->freereg;
    FuncScope bl;
    BCPos loop, loopend;
    /* Hidden control variables. */
    var_new_fixed(ls, FORL_IDX, VARNAME_FOR_IDX);
    var_new_fixed(ls, FORL_STOP, VARNAME_FOR_STOP);
    var_new_fixed(ls, FORL_STEP, VARNAME_FOR_STEP);
    /* Visible copy of index variable. */
    var_new(ls, FORL_EXT, varname);
    lex_check(ls, '=');
    expr_next(ls);
    lex_check(ls, ',');
    expr_next(ls);
    if (lex_opt(ls, ',')) {
        expr_next(ls);
    } else {
        std::ignore = bcemit_AD(fs, BC_KSHORT, fs->freereg, 1);  /* Default step is 1. */
        bcreg_reserve(fs, 1);
    }
    var_add(ls, 3);  /* Hidden control variables. */
    lex_check(ls, TK_do);
    loop = bcemit_AJ(fs, BC_FORI, base, NO_JMP);
    fscope_begin(fs, &bl, 0);  /* Scope for visible variables. */
    var_add(ls, 1);
    bcreg_reserve(fs, 1);
    parse_block(ls);
    fscope_end(fs);
    /* Perform loop inversion. Loop control instructions are at the end. */
    loopend = bcemit_AJ(fs, BC_FORL, base, NO_JMP);
    fs->bcbase[loopend].line = line;  /* Fix line for control ins. */
    jmp_patchins(fs, loopend, loop+1);
    jmp_patchins(fs, loop, fs->pc);
}

/* Try to predict whether the iterator is next() and specialize the bytecode.
** Detecting next() and pairs() by name is simplistic, but quite effective.
** The interpreter backs off if the check for the closure fails at runtime.
*/
static int predict_next(LexState *ls, FuncState *fs, BCPos pc)
{
    BCIns ins = fs->bcbase[pc].inst;
    HeapPtr<HeapString> name;
    switch (bc_op(ins)) {
    case BC_MOV:
        name = var_get(ls, fs, bc_d(ins)).name;
        break;
    case BC_UGET:
        name = ls->vstack[fs->uvmap[bc_d(ins)]].name;
        break;
    case BC_GGET:
    {
        TValue cst = bc_cst(ins);
        assert(cst.Is<tString>());
        name = cst.As<tString>();
        break;
    }
    default:
        return false;
    }
    return (name->m_length == 5 &&
            name->m_string[0] =='p' &&
            name->m_string[1] == 'a' &&
            name->m_string[2] =='i' &&
            name->m_string[3] =='r' &&
            name->m_string[4] =='s') ||
           (name->m_length == 4 &&
            name->m_string[0] =='n' &&
            name->m_string[1] == 'e' &&
            name->m_string[2] =='x' &&
            name->m_string[3] =='t');
}

/* Parse 'for' iterator. */
static void parse_for_iter(LexState *ls, HeapPtr<HeapString> indexname)
{
    FuncState *fs = ls->fs;
    ExpDesc e;
    BCReg nvars = 0;
    BCLine line;
    BCReg base = fs->freereg + 3;
    BCPos loop, loopend, exprpc = fs->pc;
    FuncScope bl;
    int isnext;
    /* Hidden control variables. */
    var_new_fixed(ls, nvars++, VARNAME_FOR_GEN);
    var_new_fixed(ls, nvars++, VARNAME_FOR_STATE);
    var_new_fixed(ls, nvars++, VARNAME_FOR_CTL);
    /* Visible variables returned from iterator. */
    var_new(ls, nvars++, indexname);
    while (lex_opt(ls, ','))
        var_new(ls, nvars++, lex_str(ls));
    lex_check(ls, TK_in);
    line = ls->linenumber;
    assign_adjust(ls, 3, expr_list(ls, &e), &e);
    /* The iterator needs another 3 [4] slots (func [pc] | state ctl). */
    bcreg_bump(fs, 3+LJ_FR2);
    isnext = (nvars <= 5 && predict_next(ls, fs, exprpc));
    var_add(ls, 3);  /* Hidden control variables. */
    lex_check(ls, TK_do);
    loop = bcemit_AJ(fs, isnext ? BC_ISNEXT : BC_JMP, base, NO_JMP);
    fscope_begin(fs, &bl, 0);  /* Scope for visible variables. */
    var_add(ls, nvars-3);
    bcreg_reserve(fs, nvars-3);
    parse_block(ls);
    fscope_end(fs);
    /* Perform loop inversion. Loop control instructions are at the end. */
    jmp_patchins(fs, loop, fs->pc);
    std::ignore = bcemit_ABC(fs, isnext ? BC_ITERN : BC_ITERC, base, nvars-3+1, 2+1);
    loopend = bcemit_AJ(fs, BC_ITERL, base, NO_JMP);
    fs->bcbase[loopend-1].line = line;  /* Fix line for control ins. */
    fs->bcbase[loopend].line = line;
    jmp_patchins(fs, loopend, loop+1);
}

/* Parse 'for' statement. */
static void parse_for(LexState *ls, BCLine line)
{
    FuncState *fs = ls->fs;
    HeapPtr<HeapString> varname;
    FuncScope bl;
    fscope_begin(fs, &bl, FSCOPE_LOOP);
    lj_lex_next(ls);  /* Skip 'for'. */
    varname = lex_str(ls);  /* Get first variable name. */
    if (ls->tok == '=')
        parse_for_num(ls, varname, line);
    else if (ls->tok == ',' || ls->tok == TK_in)
        parse_for_iter(ls, varname);
    else
        err_syntax(ls, LJ_ERR_XFOR);
    lex_match(ls, TK_end, TK_for, line);
    fscope_end(fs);  /* Resolve break list. */
}

/* Parse condition and 'then' block. */
static BCPos parse_then(LexState *ls)
{
    BCPos condexit;
    lj_lex_next(ls);  /* Skip 'if' or 'elseif'. */
    condexit = expr_cond(ls);
    lex_check(ls, TK_then);
    parse_block(ls);
    return condexit;
}

/* Parse 'if' statement. */
static void parse_if(LexState *ls, BCLine line)
{
    FuncState *fs = ls->fs;
    BCPos flist;
    BCPos escapelist = NO_JMP;
    flist = parse_then(ls);
    while (ls->tok == TK_elseif) {  /* Parse multiple 'elseif' blocks. */
        jmp_append(fs, &escapelist, bcemit_jmp(fs));
        jmp_tohere(fs, flist);
        flist = parse_then(ls);
    }
    if (ls->tok == TK_else) {  /* Parse optional 'else' block. */
        jmp_append(fs, &escapelist, bcemit_jmp(fs));
        jmp_tohere(fs, flist);
        lj_lex_next(ls);  /* Skip 'else'. */
        parse_block(ls);
    } else {
        jmp_append(fs, &escapelist, flist);
    }
    jmp_tohere(fs, escapelist);
    lex_match(ls, TK_end, TK_if, line);
}

/* -- Parse statements ---------------------------------------------------- */

/* Parse a statement. Returns 1 if it must be the last one in a chunk. */
static int parse_stmt(LexState *ls)
{
    BCLine line = ls->linenumber;
    switch (ls->tok) {
    case TK_if:
        parse_if(ls, line);
        break;
    case TK_while:
        parse_while(ls, line);
        break;
    case TK_do:
        lj_lex_next(ls);
        parse_block(ls);
        lex_match(ls, TK_end, TK_do, line);
        break;
    case TK_for:
        parse_for(ls, line);
        break;
    case TK_repeat:
        parse_repeat(ls, line);
        break;
    case TK_function:
        parse_func(ls, line);
        break;
    case TK_local:
        lj_lex_next(ls);
        parse_local(ls);
        break;
    case TK_return:
        parse_return(ls);
        return 1;  /* Must be last. */
    case TK_break:
        lj_lex_next(ls);
        parse_break(ls);
        return !LJ_52;  /* Must be last in Lua 5.1. */
#if LJ_52
    case ';':
        lj_lex_next(ls);
        break;
#endif
    case TK_label:
        parse_label(ls);
        break;
    case TK_goto:
        if (LJ_52 || lj_lex_lookahead(ls) == TK_name) {
            lj_lex_next(ls);
            parse_goto(ls);
            break;
        }
        /* fallthrough */
        [[fallthrough]];
    default:
        parse_call_assign(ls);
        break;
    }
    return 0;
}

/* A chunk is a list of statements optionally separated by semicolons. */
static void parse_chunk(LexState *ls)
{
    int islast = 0;
    synlevel_begin(ls);
    while (!islast && !parse_isend(ls->tok)) {
        islast = parse_stmt(ls);
        lex_opt(ls, ';');
        assert(ls->fs->framesize >= ls->fs->freereg &&
                        ls->fs->freereg >= ls->fs->nactvar &&
                    "bad regalloc");
        ls->fs->freereg = ls->fs->nactvar;  /* Free registers after each stmt. */
    }
    synlevel_end(ls);
}

/* Entry point of bytecode parser. */
UnlinkedCodeBlock* lj_parse(LexState *ls)
{
    FuncState fs;
    FuncScope bl;
    ls->chunkname = VM::GetActiveVMForCurrentThread()->m_emptyString;
    ls->level = 0;
    fs_init(ls, &fs);
    fs.linedefined = 0;
    fs.numparams = 0;
    fs.bcbase = ls->bcstack.data();
    fs.bclim = 0;
    fs.flags |= PROTO_VARARG;  /* Main chunk is always a vararg func. */
    fscope_begin(&fs, &bl, 0);
    std::ignore = bcemit_AD(&fs, BC_FUNCV, 0, 0);  /* Placeholder. */
    lj_lex_next(ls);  /* Read-ahead first token. */
    parse_chunk(ls);
    if (ls->tok != TK_eof)
        err_token(ls, TK_eof);
    UnlinkedCodeBlock* rootUcb = fs_finish(ls, ls->linenumber);
    assert((fs.prev == NULL && ls->fs == NULL) && "mismatched frame nesting");
    assert(rootUcb->m_numUpvalues == 0 && "toplevel proto has upvalues");
    ls->ucbList.push_back(rootUcb);

    // Due to how the parser is designed, the ucbList is already sorted in topological order.
    // For sanity, assert this first.
    //
#ifndef NDEBUG
    {
        std::unordered_set<UnlinkedCodeBlock*> existentUcbList;
        existentUcbList.insert(rootUcb);
        for (size_t i = ls->ucbList.size() - 1; i-- > 0; /*no-op*/)
        {
            UnlinkedCodeBlock* u = ls->ucbList[i];
            assert(u->m_parent != nullptr);
            assert(existentUcbList.count(u->m_parent));
            assert(!existentUcbList.count(u));
            existentUcbList.insert(u);
        }
    }
#endif

    // Now, perform final fix up of upvalues.
    // Populate the m_isImmutable field for all upvalues that inherits from the parent upvalue.
    //
    for (size_t ucbOrd = ls->ucbList.size() - 1; ucbOrd-- > 0; /*no-op*/)
    {
        UnlinkedCodeBlock* u = ls->ucbList[ucbOrd];
        uint32_t numUv = u->m_numUpvalues;
        for (size_t uvOrd = 0; uvOrd < numUv; uvOrd++)
        {
            UpvalueMetadata& uv = u->m_upvalueInfo[uvOrd];
            if (uv.m_isParentLocal)
            {
                assert(uv.m_immutabilityFieldFinalized);
            }
            else
            {
                uint32_t parentSlot = uv.m_slot;
                assert(!uv.m_immutabilityFieldFinalized);
                assert(parentSlot < u->m_parent->m_numUpvalues);
                UpvalueMetadata& parentUv = u->m_parent->m_upvalueInfo[parentSlot];
                assert(parentUv.m_immutabilityFieldFinalized);
                DEBUG_ONLY(uv.m_immutabilityFieldFinalized = true;)
                uv.m_isImmutable = parentUv.m_isImmutable;
            }
        }
    }

    for (UnlinkedCodeBlock* u : ls->ucbList)
    {
        // It's REALLY ugly that we have to keep around all the BytecodeBuilders until here
        // just to fix up the upvalue GETs after we have information on whether they are immutable...
        // But let's just stay simple for now: it's only the parser...
        //
        BytecodeBuilder& bw = *u->m_bytecodeBuilder;

        // Rewrite all UGET on immutable upvalue to ImmutableUpvalueGet (required for correctness!)
        //
        assert(u->m_parserUVGetFixupList != nullptr);
        for (uint32_t offset : *u->m_parserUVGetFixupList)
        {
            assert(bw.GetBytecodeKind(offset) == BCKind::UpvalueGetMutable);
            auto ops = bw.DecodeUpvalueGetMutable(offset);
            uint16_t ord = ops.ord.m_value;
            assert(ord < u->m_numUpvalues);
            assert(u->m_upvalueInfo[ord].m_immutabilityFieldFinalized);
            if (u->m_upvalueInfo[ord].m_isImmutable)
            {
                bw.ReplaceBytecode<BCKind::UpvalueGetImmutable>(offset, { .ord = ord, .output = ops.output });
            }
        }

        delete u->m_parserUVGetFixupList;
        u->m_parserUVGetFixupList = nullptr;

        // We are finally ready to emit the final bytecode...
        //
        std::pair<uint8_t*, size_t> bytecodeData = bw.GetBuiltBytecodeSequence();
        std::pair<uint64_t*, size_t> constantTableData = bw.GetBuiltConstantTable();
        if (constantTableData.second >= 0x7fff)
        {
            // TODO: gracefully handle
            fprintf(stderr, "[LOCKDOWN] Bytecode contains too many constants. Limit 32766, got %llu.\n", static_cast<unsigned long long>(constantTableData.second));
            abort();
        }

        u->m_cstTableLength = static_cast<uint32_t>(constantTableData.second);
        u->m_cstTable = constantTableData.first;

        u->m_bytecode = bytecodeData.first;
        u->m_bytecodeLength = static_cast<uint32_t>(bytecodeData.second);
        u->m_bytecodeMetadataLength = bw.GetBytecodeMetadataTotalLength();
        const auto& bmUseCounts = bw.GetBytecodeMetadataUseCountArray();
        assert(bmUseCounts.size() == x_num_bytecode_metadata_struct_kinds_);
        memcpy(u->m_bytecodeMetadataUseCounts, bmUseCounts.data(), bmUseCounts.size() * sizeof(uint16_t));

        delete u->m_bytecodeBuilder;
        u->m_bytecodeBuilder = nullptr;
    }

    return rootUcb;
}

#pragma clang diagnostic pop
