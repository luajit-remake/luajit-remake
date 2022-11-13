#pragma once

#include "common_utils.h"
#include "runtime_utils.h"

namespace LuaLib {

inline std::pair<bool /*success*/, double> WARN_UNUSED NO_INLINE ToNumberSlowPath(double tvalueAsDouble)
{
    TValue val;
    val.m_value = cxx2a_bit_cast<uint64_t>(tvalueAsDouble);
    if (val.Is<tDouble>())
    {
        return std::make_pair(true /*success*/, val.As<tDouble>());
    }

    if (val.Is<tString>())
    {
        HeapPtr<HeapString> stringObj = val.As<tString>();
        StrScanResult ssr = TryConvertStringToDoubleWithLuaSemantics(TranslateToRawPointer(stringObj->m_string), stringObj->m_length);
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

inline std::pair<bool /*success*/, double> WARN_UNUSED ALWAYS_INLINE ToNumber(TValue val)
{
    // Check tDoubleNotNaN instead of tDouble because NaN is unlikely anyway, and it avoids an expensive GPR-to-FPR mov
    //
    if (likely(val.Is<tDoubleNotNaN>()))
    {
        return std::make_pair(true /*success*/, val.As<tDouble>());
    }

    // Similarly, pass the val in FPR to slow path, to make sure the expensive GPR-to-FPR won't happen in the fast path
    //
    return ToNumberSlowPath(val.ViewAsDouble());
}

inline std::pair<bool /*success*/, double> WARN_UNUSED NO_INLINE TVDoubleViewToNumberSlowImpl(double tvDoubleView)
{
    TValue tv; tv.m_value = cxx2a_bit_cast<uint64_t>(tvDoubleView);
    return ToNumber(tv);
}

// It's ugly: TVDoubleViewToNumberSlowImpl takes the argument in FPR because we want to
// avoid unnecessary register shuffles between FPR and GPR (LLVM is very bad at that..)
//
inline bool WARN_UNUSED ALWAYS_INLINE TVDoubleViewToNumberSlow(double& tvDoubleView /*inout*/)
{
    auto [success, res] = TVDoubleViewToNumberSlowImpl(tvDoubleView);
    tvDoubleView = res;
    return success;
}

}   // namespace LuaLib
