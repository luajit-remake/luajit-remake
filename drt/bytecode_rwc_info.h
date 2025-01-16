#pragma once

#include "common.h"
#include "tvalue.h"

// Describes something that a bytecode reads/writes/clobbers
//
struct BytecodeRWCDesc
{
    enum Type : uint8_t
    {
        LocalBV,
        LocalAny,
        ConstantBV,
        ConstantAny,
        RangeBV,
        RangeAny,
        VArgs,
        VRets
    };

    union DataTy {
        struct {
            uint32_t m_i1;
            int32_t m_i2;
        };
        TValue m_tv;
    };

    Type m_type;
    DataTy m_data;

    bool IsLocal() { return m_type == BytecodeRWCDesc::LocalBV || m_type == BytecodeRWCDesc::LocalAny; }
    bool IsConstant() { return m_type == BytecodeRWCDesc::ConstantBV || m_type == BytecodeRWCDesc::ConstantAny; }
    bool IsRange() { return m_type == BytecodeRWCDesc::RangeBV || m_type == BytecodeRWCDesc::RangeAny; }
    bool IsVarArgs() { return m_type == BytecodeRWCDesc::VArgs; }
    bool IsVarRets() { return m_type == BytecodeRWCDesc::VRets; }

    bool MaybeInvalidBoxedValue()
    {
        return m_type == BytecodeRWCDesc::LocalAny || m_type == BytecodeRWCDesc::ConstantAny || m_type == BytecodeRWCDesc::RangeAny;
    }

    static BytecodeRWCDesc ALWAYS_INLINE CreateLocal(size_t ord, bool maybeInvalidBoxedValue)
    {
        return {
            .m_type = (maybeInvalidBoxedValue ? BytecodeRWCDesc::LocalAny : BytecodeRWCDesc::LocalBV),
            .m_data = { .m_i1 = SafeIntegerCast<uint32_t>(ord) }
        };
    }

    static BytecodeRWCDesc ALWAYS_INLINE CreateConstant(TValue val, bool maybeInvalidBoxedValue)
    {
        return {
            .m_type = (maybeInvalidBoxedValue ? BytecodeRWCDesc::ConstantAny : BytecodeRWCDesc::ConstantBV),
            .m_data = { .m_tv = val }
        };
    }

    static BytecodeRWCDesc ALWAYS_INLINE CreateRange(size_t start, int64_t len, bool maybeInvalidBoxedValue)
    {
        return {
            .m_type = (maybeInvalidBoxedValue ? BytecodeRWCDesc::RangeAny : BytecodeRWCDesc::RangeBV),
            .m_data = { .m_i1 = SafeIntegerCast<uint32_t>(start), .m_i2 = SafeIntegerCast<int32_t>(len) }
        };
    }

    static BytecodeRWCDesc ALWAYS_INLINE CreateVarArgs()
    {
        return {
            .m_type = BytecodeRWCDesc::VArgs,
            .m_data = Undef<DataTy>()
        };
    }

    static BytecodeRWCDesc ALWAYS_INLINE CreateVarRets()
    {
        return {
            .m_type = BytecodeRWCDesc::VRets,
            .m_data = Undef<DataTy>()
        };
    }

    size_t GetLocalOrd()
    {
        TestAssert(IsLocal());
        return static_cast<size_t>(m_data.m_i1);
    }

    TValue GetConstant()
    {
        TestAssert(IsConstant());
        return m_data.m_tv;
    }

    size_t GetRangeStart()
    {
        TestAssert(IsRange());
        return static_cast<size_t>(m_data.m_i1);
    }

    int64_t GetRangeLength()
    {
        TestAssert(IsRange());
        return static_cast<int64_t>(m_data.m_i2);
    }
};

struct BytecodeRWCInfo
{
    template<typename... TArgs>
    ALWAYS_INLINE BytecodeRWCInfo(TArgs... args)
    {
        static_assert(sizeof...(args) <= x_maxItems, "Please raise x_maxItems to a higher value!");
        m_numItems = 0;
        construct(args...);
    }

    size_t GetNumItems() { return m_numItems; }

    BytecodeRWCDesc GetDesc(size_t i)
    {
        TestAssert(i < m_numItems);
        return {
            .m_type = m_type[i],
            .m_data = m_data[i]
        };
    }

    void ALWAYS_INLINE Append(BytecodeRWCDesc val)
    {
        TestAssert(m_numItems < x_maxItems);
        m_type[m_numItems] = val.m_type;
        m_data[m_numItems] = val.m_data;
        m_numItems++;
    }

    // Raise this as necessary
    //
    static constexpr size_t x_maxItems = 7;

    uint8_t m_numItems;
    BytecodeRWCDesc::Type m_type[x_maxItems];
    BytecodeRWCDesc::DataTy m_data[x_maxItems];

private:
    void ALWAYS_INLINE construct()
    {
        TestAssert(m_numItems <= x_maxItems);
    }

    template<typename... TArgs>
    void ALWAYS_INLINE construct(BytecodeRWCDesc val, TArgs... rest)
    {
        Append(val);
        construct(rest...);
    }
};
static_assert(sizeof(BytecodeRWCInfo) == 64);

