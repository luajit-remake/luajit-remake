#pragma once

#include "common_utils.h"
#include "runtime_utils.h"

namespace internal {

inline std::pair<bool /*success*/, double> WARN_UNUSED NO_INLINE LuaLib_ToNumberSlowPath(double tvalueAsDouble)
{
    TValue val;
    val.m_value = cxx2a_bit_cast<uint64_t>(tvalueAsDouble);
    if (val.Is<tDouble>())
    {
        return std::make_pair(true /*success*/, val.As<tDouble>());
    }

    if (val.Is<tString>())
    {
        HeapString* stringObj = val.As<tString>();
        StrScanResult ssr = TryConvertStringToDoubleWithLuaSemantics(stringObj->m_string, stringObj->m_length);
        if (ssr.fmt == StrScanFmt::STRSCAN_NUM)
        {
            return std::make_pair(true /*success*/, ssr.d);
        }
        else
        {
            return std::make_pair(false /*success*/, double());
        }
    }
    else
    {
        return std::make_pair(false /*success*/, double());
    }
}

}   // namespace internal


inline std::pair<bool /*success*/, double> WARN_UNUSED ALWAYS_INLINE LuaLib_ToNumber(TValue val)
{
    // Check tDoubleNotNaN instead of tDouble because NaN is unlikely anyway, and it avoids an expensive GPR-to-FPR mov
    //
    if (likely(val.Is<tDoubleNotNaN>()))
    {
        return std::make_pair(true /*success*/, val.As<tDouble>());
    }

    // Similarly, pass the val in FPR to slow path, to make sure the expensive GPR-to-FPR won't happen in the fast path
    //
    return internal::LuaLib_ToNumberSlowPath(val.ViewAsDouble());
}

namespace internal {

inline std::pair<bool /*success*/, double> WARN_UNUSED NO_INLINE LuaLib_TVDoubleViewToNumberSlowImpl(double tvDoubleView)
{
    TValue tv; tv.m_value = cxx2a_bit_cast<uint64_t>(tvDoubleView);
    return LuaLib_ToNumber(tv);
}

}   // namespace internal

// It's ugly: TVDoubleViewToNumberSlowImpl takes the argument in FPR because we want to
// avoid unnecessary register shuffles between FPR and GPR (LLVM is very bad at that..)
//
inline bool WARN_UNUSED ALWAYS_INLINE LuaLib_TVDoubleViewToNumberSlow(double& tvDoubleView /*inout*/)
{
    auto [success, res] = internal::LuaLib_TVDoubleViewToNumberSlowImpl(tvDoubleView);
    tvDoubleView = res;
    return success;
}

// Lua library generally performs a silent number-to-string cast if a number is passed in to an argument expected to be a string.
// The easiest approach is of course to cast the number and create a heap string object.
// However, in many cases, the string content is ephemeral, so creating a heap string object is wasteful.
// We use this ugly macro to make the converted string's buffer live on the stack instead.
//
#define GET_ARG_AS_STRING(fnName, oneIndexedArgOrd, strPtrVar, strLenVar)                                                   \
    char macro_argN2SBuf[std::max(x_default_tostring_buffersize_double, x_default_tostring_buffersize_int)];                \
    const char* strPtrVar;                                                                                                  \
    size_t strLenVar;                                                                                                       \
    {                                                                                                                       \
        TValue macro_tv = GetArg(oneIndexedArgOrd - 1);                                                                     \
        if (likely(macro_tv.Is<tString>()))                                                                                 \
        {                                                                                                                   \
            strPtrVar = reinterpret_cast<char*>(macro_tv.As<tString>()->m_string);                   \
            strLenVar = macro_tv.As<tString>()->m_length;                                                                   \
        }                                                                                                                   \
        else if (macro_tv.Is<tDouble>())                                                                                    \
        {                                                                                                                   \
            strLenVar = static_cast<size_t>(StringifyDoubleUsingDefaultLuaFormattingOptions(                                \
            macro_argN2SBuf, macro_tv.As<tDouble>()) - macro_argN2SBuf);                                                    \
            strPtrVar = macro_argN2SBuf;                                                                                    \
        }                                                                                                                   \
        else if (macro_tv.Is<tInt32>())                                                                                     \
        {                                                                                                                   \
            strLenVar = static_cast<size_t>(StringifyInt32UsingDefaultLuaFormattingOptions(                                 \
            macro_argN2SBuf, macro_tv.As<tInt32>()) - macro_argN2SBuf);                                                     \
            strPtrVar = macro_argN2SBuf;                                                                                    \
        }                                                                                                                   \
        else                                                                                                                \
        {                                                                                                                   \
            ThrowError("bad argument #" PP_STRINGIFY(oneIndexedArgOrd) " to '" PP_STRINGIFY(fnName) "' (string expected)"); \
        }                                                                                                                   \
    }                                                                                                                       \
    assert(true)        /* end with a statement so a comma can be added */

