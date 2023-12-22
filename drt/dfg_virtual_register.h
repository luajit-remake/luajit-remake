#pragma once

#include "common_utils.h"
#include "temp_arena_allocator.h"

namespace dfg {

// The bytecode local ordinal might not equal the local variable ordinal in DFG due to inlining
// As such, we use this wrapper class to prevent accidental use of bytecode local ordinal,
// which is a plain size_t, as a local variable (i.e., virtual register) ordinal in DFG.
//
// This distinction between VirtualRegister and Local exists mostly only in DFG frontend.
// After the initial IR graph is built, we normally do not need to worry about their difference.
//
struct VirtualRegister
{
    VirtualRegister() = default;
    explicit VirtualRegister(size_t value) : m_value(value) { }
    size_t Value() const { return m_value; }

private:
    size_t m_value;
};

// Similar to above, the bytecode local ordinal might not equal the interpreter slot ordinal due to inlining
// So we use this wrapper class as a safety measure
//
struct InterpreterSlot
{
    InterpreterSlot() = default;
    explicit InterpreterSlot(size_t value) : m_value(value) { }
    size_t Value() const { return m_value; }

private:
    size_t m_value;
};

// Handles allocation and deallocation of virtual register ordinals
//
// Internally, this is just a free list that allows us to recycle unused register ordinals,
// to keep the total number of virtual registers small if possible
//
struct VirtualRegisterAllocator
{
    VirtualRegisterAllocator(TempArenaAllocator& alloc)
        : m_freeList(alloc)
        , m_firstAvailableOrdinal(0)
    { }

    VirtualRegister WARN_UNUSED Allocate()
    {
        if (!m_freeList.empty())
        {
            uint32_t result = m_freeList.back();
            m_freeList.pop_back();
            return VirtualRegister(result);
        }
        else
        {
            size_t result = m_firstAvailableOrdinal;
            m_firstAvailableOrdinal++;
            return VirtualRegister(result);
        }
    }

    void Deallocate(VirtualRegister vreg)
    {
        uint32_t ord = SafeIntegerCast<uint32_t>(vreg.Value());
#ifdef TESTBUILD
        for (uint32_t k : m_freeList) { TestAssert(ord != k); }
#endif
        TestAssert(ord < m_firstAvailableOrdinal);
        m_freeList.push_back(ord);
    }

    void CopyStateTo(VirtualRegisterAllocator& other /*out*/) const
    {
        other.m_freeList = m_freeList;
        other.m_firstAvailableOrdinal = m_firstAvailableOrdinal;
    }

    uint32_t GetVirtualRegisterVectorLength() const
    {
        return SafeIntegerCast<uint32_t>(m_firstAvailableOrdinal);
    }

    const TempVector<uint32_t>& GetFreeList() const    // for debug only
    {
        return m_freeList;
    }

private:
    TempVector<uint32_t> m_freeList;
    size_t m_firstAvailableOrdinal;
};

}   // namespace dfg
