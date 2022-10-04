#pragma once

#include "common_utils.h"

#define LJ_OPCODE_LIST    \
    ISLT,   \
    ISGE,   \
    ISLE,   \
    ISGT,   \
    ISEQV,  \
    ISNEV,  \
    ISEQS,  \
    ISNES,  \
    ISEQN,  \
    ISNEN,  \
    ISEQP,  \
    ISNEP,  \
    ISTC,   \
    ISFC,   \
    IST,    \
    ISF,    \
    ISTYPE, \
    ISNUM,  \
    MOV,    \
    NOT,    \
    UNM,    \
    LEN,    \
    ADDVN,  \
    SUBVN,  \
    MULVN,  \
    DIVVN,  \
    MODVN,  \
    ADDNV,  \
    SUBNV,  \
    MULNV,  \
    DIVNV,  \
    MODNV,  \
    ADDVV,  \
    SUBVV,  \
    MULVV,  \
    DIVVV,  \
    MODVV,  \
    POW,    \
    CAT,    \
    KSTR,   \
    KCDATA, \
    KSHORT, \
    KNUM,   \
    KPRI,   \
    KNIL,   \
    UGET,   \
    USETV,  \
    USETS,  \
    USETN,  \
    USETP,  \
    UCLO,   \
    FNEW,   \
    TNEW,   \
    TDUP,   \
    GGET,   \
    GSET,   \
    TGETV,  \
    TGETS,  \
    TGETB,  \
    TGETR,  \
    TSETV,  \
    TSETS,  \
    TSETB,  \
    TSETM,  \
    TSETR,  \
    CALLM,  \
    CALL,   \
    CALLMT, \
    CALLT,  \
    ITERC,  \
    ITERN,  \
    VARG,   \
    ISNEXT, \
    RETM,   \
    RET,    \
    RET0,   \
    RET1,   \
    FORI,   \
    JFORI,  \
    FORL,   \
    IFORL,  \
    JFORL,  \
    ITERL,  \
    IITERL, \
    JITERL, \
    LOOP,   \
    ILOOP,  \
    JLOOP,  \
    JMP,    \
    FUNCF,  \
    IFUNCF, \
    JFUNCF, \
    FUNCV,  \
    IFUNCV, \
    JFUNCV, \
    FUNCC,  \
    FUNCCW

enum class LJOpcode
{
    LJ_OPCODE_LIST
};

#define macro(ljopcode) + 1
constexpr size_t x_numLJOpcodes = 0 PP_FOR_EACH(macro, LJ_OPCODE_LIST);
#undef macro

constexpr const char* x_LJOpcodeStrings[x_numLJOpcodes + 1] = {
#define macro(ljopcode) PP_STRINGIFY(ljopcode),
    PP_FOR_EACH(macro, LJ_OPCODE_LIST)
#undef macro
    ""
};

inline LJOpcode WARN_UNUSED GetOpcodeFromString(const std::string& s)
{
    for (uint32_t i = 0; i < x_numLJOpcodes; i++)
    {
        if (s == x_LJOpcodeStrings[i])
        {
            return static_cast<LJOpcode>(i);
        }
    }
    fprintf(stderr, "Bad opcode \"%s\"!\n", s.c_str());
    abort();
}
