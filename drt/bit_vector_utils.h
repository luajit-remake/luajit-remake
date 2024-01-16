#pragma once

#include "common.h"
#include "dfg_arena.h"
#include "misc_math_helper.h"
#include "temp_arena_allocator.h"

template<typename Deleter>
struct BitVectorImpl
{
    template<typename T>
    BitVectorImpl(T&& deleter) : m_data(nullptr, std::forward<T>(deleter)), m_length(0) { }

    size_t GetAllocLength() const
    {
        return RoundUpToMultipleOf<64>(m_length) / 64;
    }

    void Clear()
    {
        memset(m_data.get(), 0, sizeof(uint64_t) * GetAllocLength());
    }

    void SetAllOne()
    {
        size_t arrLen = GetAllocLength();
        for (size_t i = 0; i + 1 < arrLen; i++)
        {
            m_data[i] = static_cast<uint64_t>(-1);
        }
        if (arrLen > 0)
        {
            size_t numUsefulBits = (m_length + 63) % 64 + 1;
            uint64_t mask = static_cast<uint64_t>(-1);
            mask >>= (64 - numUsefulBits);
            m_data[arrLen - 1] = mask;
        }
    }

    void SetBit(size_t loc)
    {
        assert(loc < m_length);
        uint64_t& val = m_data[loc / 64];
        val |= static_cast<uint64_t>(1) << (loc % 64);
    }

    void ClearBit(size_t loc)
    {
        assert(loc < m_length);
        uint64_t& val = m_data[loc / 64];
        val &= ~(static_cast<uint64_t>(1) << (loc % 64));
    }

    void SetBit(size_t loc, bool value)
    {
        if (value) { SetBit(loc); } else { ClearBit(loc); }
    }

    bool IsSet(size_t loc) const
    {
        assert(loc < m_length);
        uint64_t val = m_data[loc / 64];
        return (val & static_cast<uint64_t>(1) << (loc % 64));
    }

    template<typename OtherDeleter>
    void CopyFromEqualLengthBitVector(const BitVectorImpl<OtherDeleter>& other)
    {
        TestAssert(other.m_length == m_length);
        if (other.m_data.get() != m_data.get())
        {
            SafeMemcpy(m_data.get(), other.m_data.get(), sizeof(uint64_t) * GetAllocLength());
        }
    }

    // Iterates through each set bit in a bit vector expression of n + 1 bitvectors of equal length (the first vector is always this)
    //
    // 'func' should take argument 'size_t ord', which is the ordinal of the set bit
    // 'exprFunc' should take n + 1 uint64_t values and return uint64_t, the result of the expression
    //
    template<typename ActionFunc, typename ExprFunc, typename... OtherBvDeleters>
    void ALWAYS_INLINE ForEachSetBitInExpression(const ActionFunc& actionFunc, const ExprFunc& exprFunc, const BitVectorImpl<OtherBvDeleters>&... others) const
    {
#ifdef TESTBUILD
        {
            std::array<size_t, sizeof...(others)> lens { (others.m_length)... };
            for (size_t i = 0; i < sizeof...(others); i++) { TestAssert(lens[i] == m_length); }
        }
#endif
        size_t allocLen = GetAllocLength();
        for (size_t i = 0; i < allocLen; i++)
        {
            size_t baseOrd = i * 64;
            uint64_t exprVal = exprFunc(m_data[i], (others.m_data[i])...);
            while (exprVal != 0)
            {
                uint32_t lowestSetBit = static_cast<uint32_t>(__builtin_ctzll(exprVal));
                TestAssert((exprVal & (static_cast<uint64_t>(1) << lowestSetBit)) != 0);
                exprVal ^= static_cast<uint64_t>(1) << lowestSetBit;
                actionFunc(lowestSetBit + baseOrd);
            }
        }
    }

    template<typename Func>
    void ALWAYS_INLINE ForEachSetBit(const Func& func) const
    {
        ForEachSetBitInExpression(func, [](uint64_t val) ALWAYS_INLINE { return val; });
    }

    template<typename OtherDeleter, typename Func>
    void ALWAYS_INLINE ForEachSetBitThatIsClearInOther(const BitVectorImpl<OtherDeleter>& other, const Func& func) const
    {
        auto expr = [](uint64_t v1, uint64_t v2) ALWAYS_INLINE
        {
            return v1 & (~v2);
        };
        ForEachSetBitInExpression(func, expr, other);
    }

    std::unique_ptr<uint64_t[], Deleter> m_data;
    size_t m_length;

protected:
    template<typename T>
    void ALWAYS_INLINE ResetImpl(size_t len, T&& allocator)
    {
        static_assert(std::is_convertible_v<T&&, std::function<uint64_t*(size_t)>>);
        size_t allocLen = RoundUpToMultipleOf<64>(len) / 64;
        m_length = len;
        m_data.reset(allocator(allocLen));
        Clear();
    }

    // Deleter must be no-op
    //
    void InitializeForBitVectorView(uint64_t* data, size_t length)
    {
        m_data.reset(data);
        m_length = length;
    }
};

namespace bvdetail {

struct StdDeleter
{
    void operator()(uint64_t* p) { delete [] p; }
};

struct NoOpDeleter
{
    void operator()(uint64_t*) { }
};

}   // namespace bvdetail

struct BitVector : BitVectorImpl<bvdetail::StdDeleter>
{
    BitVector() : BitVectorImpl<bvdetail::StdDeleter>(bvdetail::StdDeleter()) { }

    void Reset(size_t len)
    {
        ResetImpl(len, [](size_t l) ALWAYS_INLINE { return new uint64_t[l]; });
    }
};

struct DBitVector : BitVectorImpl<bvdetail::NoOpDeleter>
{
    DBitVector() : BitVectorImpl<bvdetail::NoOpDeleter>(bvdetail::NoOpDeleter()) { }

    void Reset(size_t len)
    {
        ResetImpl(len, [](size_t l) ALWAYS_INLINE { return dfg::DfgAlloc()->AllocateArray<uint64_t>(l); });
    }
};

struct TempBitVector : BitVectorImpl<bvdetail::NoOpDeleter>
{
    TempBitVector() : BitVectorImpl<bvdetail::NoOpDeleter>(bvdetail::NoOpDeleter()) { }

    void Reset(TempArenaAllocator& alloc, size_t len)
    {
        ResetImpl(len, [&](size_t l) ALWAYS_INLINE { return alloc.AllocateArray<uint64_t>(l); });
    }
};

// This is invalidated if Reset is called on the owner
//
struct BitVectorView : BitVectorImpl<bvdetail::NoOpDeleter>
{
    template<typename Deleter>
    BitVectorView(BitVectorImpl<Deleter>& bv)
        : BitVectorImpl<bvdetail::NoOpDeleter>(bvdetail::NoOpDeleter())
    {
        InitializeForBitVectorView(bv.m_data.get(), bv.m_length);
    }

    // Make it copyable and movable
    // Note that the copy and move constructors doesn't respect constness (e.g., you get a writable view by copying a const view),
    // but we are not being very strict about constness throughout the codebase anyway..
    //
    BitVectorView(const BitVectorView& bv)
        : BitVectorImpl<bvdetail::NoOpDeleter>(bvdetail::NoOpDeleter())
    {
        InitializeForBitVectorView(bv.m_data.get(), bv.m_length);
    }

    BitVectorView& operator=(const BitVectorView& bv)
    {
        InitializeForBitVectorView(bv.m_data.get(), bv.m_length);
        return *this;
    }

    BitVectorView(BitVectorView&& bv)
        : BitVectorImpl<bvdetail::NoOpDeleter>(bvdetail::NoOpDeleter())
    {
        InitializeForBitVectorView(bv.m_data.get(), bv.m_length);
    }

    BitVectorView& operator=(BitVectorView&& bv)
    {
        InitializeForBitVectorView(bv.m_data.get(), bv.m_length);
        return *this;
    }
};
