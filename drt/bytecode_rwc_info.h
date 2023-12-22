#pragma once

#include "common.h"
#include "tvalue.h"

// Describes something that a bytecode reads/writes/clobbers
//
struct BytecodeRWCDesc
{
    enum Type : uint8_t
    {
        Local,
        Constant,
        Range,
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

    bool IsLocal() { return m_type == BytecodeRWCDesc::Local; }
    bool IsConstant() { return m_type == BytecodeRWCDesc::Constant; }
    bool IsRange() { return m_type == BytecodeRWCDesc::Range; }
    bool IsVarArgs() { return m_type == BytecodeRWCDesc::VArgs; }
    bool IsVarRets() { return m_type == BytecodeRWCDesc::VRets; }

    static BytecodeRWCDesc ALWAYS_INLINE CreateLocal(size_t ord)
    {
        return {
            .m_type = BytecodeRWCDesc::Local,
            .m_data = { .m_i1 = SafeIntegerCast<uint32_t>(ord) }
        };
    }

    static BytecodeRWCDesc ALWAYS_INLINE CreateConstant(TValue val)
    {
        return {
            .m_type = BytecodeRWCDesc::Constant,
            .m_data = { .m_tv = val }
        };
    }

    static BytecodeRWCDesc ALWAYS_INLINE CreateRange(size_t start, int64_t len)
    {
        return {
            .m_type = BytecodeRWCDesc::Range,
            .m_data = { .m_i1 = SafeIntegerCast<uint32_t>(start), .m_i2 = SafeIntegerCast<int32_t>(len) }
        };
    }

    static BytecodeRWCDesc ALWAYS_INLINE CreateVarArgs()
    {
        return {
            .m_type = BytecodeRWCDesc::VArgs
        };
    }

    static BytecodeRWCDesc ALWAYS_INLINE CreateVarRets()
    {
        return {
            .m_type = BytecodeRWCDesc::VRets
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

