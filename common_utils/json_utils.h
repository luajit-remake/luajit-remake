#pragma once

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation-unknown-command"
#pragma clang diagnostic ignored "-Wcomma"
#pragma clang diagnostic ignored "-Wswitch-enum"
#pragma clang diagnostic ignored "-Wtautological-overlap-compare"
#pragma clang diagnostic ignored "-Wcovered-switch-default"

#include "thirdparty_json.hpp"

#pragma clang diagnostic pop

using json = nlohmann::json;

#include "common.h"
#include "misc_math_helper.h"

namespace ToyLang
{

template<typename T>
T JSONCheckedGet(json& j, const char* prop)
{
    static_assert(std::is_same_v<T, std::string> || std::is_same_v<T, bool> || std::is_integral_v<T> || std::is_same_v<T, double>);
    TestAssert(j.is_object());
    TestAssert(j.count(prop));
    auto it = j.find(prop);
    if constexpr(std::is_same_v<T, std::string>)
    {
        TestAssert(it->is_string());
        return it->get<std::string>();
    }
    else if constexpr(std::is_integral_v<T> && !std::is_same_v<T, bool>)
    {
        if (it->is_number_integer())
        {
            int64_t val = it->get<int64_t>();
            return SafeIntegerCast<T>(val);
        }
        else
        {
            TestAssert(it->is_number_unsigned());
            uint64_t val = it->get<uint64_t>();
            return SafeIntegerCast<T>(val);
        }
    }
    else if constexpr(std::is_same_v<T, double>)
    {
        if (it->is_number_integer())
        {
            int64_t val = it->get<int64_t>();
            double d = static_cast<double>(val);
            TestAssert(static_cast<int64_t>(d) == val);
            return d;
        }
        else if (it->is_number_unsigned())
        {
            uint64_t val = it->get<uint64_t>();
            double d = static_cast<double>(val);
            TestAssert(static_cast<uint64_t>(d) == val);
            return d;
        }
        else
        {
            TestAssert(it->is_number_float());
            double val = it->get<double>();
            return val;
        }
    }
    else
    {
        static_assert(std::is_same_v<T, bool>);
        TestAssert(it->is_boolean());
        return it->get<bool>();
    }
}

}   // namespace ToyLang
