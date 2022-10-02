#pragma once

#include "common_utils.h"
#include "heap_ptr_utils.h"

namespace DeegenBytecodeBuilder
{

// Refers to a local (a bytecode slot in current function frame)
//
struct Local
{
    explicit Local(uint64_t ord) : m_localOrd(ord) { }
    uint64_t m_localOrd;
};

// Refers to a constant table entry
//
struct CsTab
{
    explicit CsTab(uint64_t ord) : m_csTableOrd(ord) { }
    uint64_t m_csTableOrd;
};

struct LocalOrCsTab
{
    LocalOrCsTab(Local v) : m_isLocal(true), m_ord(v.m_localOrd) { }
    LocalOrCsTab(CsTab v) : m_isLocal(false), m_ord(v.m_csTableOrd) { }

    bool m_isLocal;
    uint64_t m_ord;
};

template<typename T>
struct ForbidUninitialized
{
    ForbidUninitialized(T v) : m_value(v) { }
    T m_value;
};

class BytecodeBuilderBase
{
    MAKE_NONCOPYABLE(BytecodeBuilderBase);
    MAKE_NONMOVABLE(BytecodeBuilderBase);
public:
    friend class BranchTargetPopulator;

    static constexpr size_t x_numExtraPaddingAtEnd = 4;

    std::pair<uint8_t*, size_t> GetBuiltBytecodeSequence()
    {
        size_t len = GetCurBytecodeLength();
        // Add a few bytes so that tentative decoding of the next bytecode won't segfault..
        //
        uint8_t* res = new uint8_t[len + x_numExtraPaddingAtEnd];
        memcpy(res, GetBytecodeStart(), len);
        memset(res + len, 0, x_numExtraPaddingAtEnd);
        return std::make_pair(res, len);
    }

protected:
    BytecodeBuilderBase()
    {
        constexpr size_t initialSize = 128;
        m_bufferBegin = new uint8_t[initialSize];
        m_bufferCur = m_bufferBegin;
        m_bufferEnd = m_bufferBegin + initialSize;
    }

    ~BytecodeBuilderBase()
    {
        if (m_bufferBegin != nullptr)
        {
            delete [] m_bufferBegin;
            m_bufferBegin = nullptr;
        }
    }

    uint8_t* Reserve(size_t size)
    {
        if (likely(m_bufferEnd - m_bufferCur >= static_cast<ssize_t>(size)))
        {
            return m_bufferCur;
        }
        size_t cap = static_cast<size_t>(m_bufferEnd - m_bufferBegin);
        size_t current = static_cast<size_t>(m_bufferCur - m_bufferBegin);
        size_t needed = current + size;
        while (cap < needed) { cap *= 2; }
        uint8_t* newArray = new uint8_t[cap];
        memcpy(newArray, m_bufferBegin, current);
        delete [] m_bufferBegin;
        m_bufferBegin = newArray;
        m_bufferCur = newArray + current;
        m_bufferEnd = newArray + cap;
        return m_bufferCur;
    }

    void MarkWritten(size_t size)
    {
        m_bufferCur += size;
        assert(m_bufferCur <= m_bufferEnd);
    }

    uint8_t* GetBytecodeStart()
    {
        return m_bufferBegin;
    }

    size_t GetCurBytecodeLength()
    {
        return static_cast<size_t>(m_bufferCur - m_bufferBegin);
    }

private:
    uint8_t* m_bufferBegin;
    uint8_t* m_bufferCur;
    uint8_t* m_bufferEnd;
};

class BranchTargetPopulator
{
public:
    BranchTargetPopulator(uint64_t fillOffset, uint64_t bytecodeBaseOffset)
        : m_fillOffset(fillOffset), m_bytecodeBaseOffset(bytecodeBaseOffset)
    { }

    void PopulateBranchTarget(BytecodeBuilderBase& builder, uint64_t bytecodeLoc)
    {
        int64_t diff = static_cast<int64_t>(bytecodeLoc - m_bytecodeBaseOffset);
        // TODO: we likely need to support larger offset size in the future, but for now just stick with int16_t
        //
        using ValueType = int16_t;
        if (unlikely(diff < std::numeric_limits<ValueType>::min() || diff > std::numeric_limits<ValueType>::max()))
        {
            fprintf(stderr, "[LOCKDOWN] Branch bytecode exceeded maximum branch offset limit. Maybe make your function smaller?\n");
            abort();
        }
        ValueType val = static_cast<ValueType>(diff);
        uint8_t* base = builder.GetBytecodeStart();
        UnalignedStore<ValueType>(base + m_fillOffset, val);
    }

private:
    uint64_t m_fillOffset;
    uint64_t m_bytecodeBaseOffset;
};

}   // namespace DeegenBytecodeBuilder
