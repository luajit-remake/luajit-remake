#pragma once

#include "common.h"
#include "deegen_for_each_bytecode_intrinsic.h"
#include "tvalue.h"

struct LocalOrConstantOrNumber
{
    enum Kind
    {
        Local,
        Constant,
        Number
    };

    bool IsLocal() { return m_kind == Kind::Local; }
    bool IsNumber() { return m_kind == Kind::Number; }
    bool IsConstant() { return m_kind == Kind::Constant; }

    size_t AsLocal() { TestAssert(IsLocal()); return m_local; }
    int64_t AsNumber() { TestAssert(IsNumber()); return m_number; }
    TValue AsConstant() { TestAssert(IsConstant()); return m_tv; }

    static LocalOrConstantOrNumber CreateLocal(size_t local)
    {
        return { .m_kind = Kind::Local, .m_local = local };
    }

    static LocalOrConstantOrNumber CreateNumber(int64_t number)
    {
        return { .m_kind = Kind::Number, .m_number = number };
    }

    static LocalOrConstantOrNumber CreateConstant(TValue tv)
    {
        return { .m_kind = Kind::Constant, .m_tv = tv };
    }

    Kind m_kind;
    union {
        size_t m_local;
        int64_t m_number;
        TValue m_tv;
    };
};

struct BytecodeIntrinsicInfo
{
#define macro2(intrinsicName, ...) struct intrinsicName { __VA_OPT__(LocalOrConstantOrNumber __VA_ARGS__;) };
#define macro(item) macro2 item
    PP_FOR_EACH(macro, DEEGEN_BYTECODE_INTRINSIC_LIST)
#undef macro
#undef macro2
};

namespace detail {

template<typename T>
constexpr size_t bytecode_intrinsic_ordinal_from_ty = []() {
    size_t res = 0;
#define macro2(intrinsicName, ...) if constexpr(std::is_same_v<BytecodeIntrinsicInfo::intrinsicName, T>) { } else { res += 1;
#define macro(item) macro2 item
    PP_FOR_EACH(macro, DEEGEN_BYTECODE_INTRINSIC_LIST)
#undef macro
#undef macro2
    static_assert(type_dependent_false<T>::value, "T is not an intrinsic type!");
#define macro(item) }
    PP_FOR_EACH(macro, DEEGEN_BYTECODE_INTRINSIC_LIST)
#undef macro
    return res;
}();

}   // namespace detail
