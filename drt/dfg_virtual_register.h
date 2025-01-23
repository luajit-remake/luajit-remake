#pragma once

#include "common_utils.h"
#include "dfg_arena.h"
#include "temp_arena_allocator.h"

namespace dfg {

// Describes an absolute DFG local ordinal.
//
// The bytecode local ordinal might not equal the local variable ordinal in DFG due to inlining
// As such, we use this wrapper class to prevent accidental use of bytecode local ordinal,
// which is a plain size_t, as a local variable (i.e., virtual register) ordinal in DFG.
//
// This distinction between "VirtualRegister" and "local" in DFG exists mostly only in DFG frontend.
// After the initial IR graph is built, we normally do not need to worry about their difference.
//
struct VirtualRegister
{
    VirtualRegister() = default;
    explicit VirtualRegister(size_t value) : m_value(value) { }
    size_t Value() const { return m_value; }

    bool ALWAYS_INLINE operator==(const VirtualRegister&) const = default;

private:
    size_t m_value;
};

// Describes an absolute interpreter slot ordinal (frameBase[slot] is where this value is stored in interpreter stack).
//
// Similar to above, the bytecode local ordinal might not equal the interpreter slot ordinal due to inlining
// So we use this wrapper class as a safety measure
//
struct InterpreterSlot
{
    InterpreterSlot() = default;
    explicit InterpreterSlot(size_t value) : m_value(value) { }
    size_t Value() const { return m_value; }

    bool ALWAYS_INLINE operator==(const InterpreterSlot&) const = default;

private:
    size_t m_value;
};

struct Node;

// Describes a location in a (root or inlined) interpreter call frame.
// InlinedCallFrame can translate this to the absolute VirtualRegister and InterpreterSlot ordinal
//
struct InterpreterFrameLocation
{
    InterpreterFrameLocation() = default;

    static InterpreterFrameLocation WARN_UNUSED Local(size_t localOrd)
    {
        return InterpreterFrameLocation(SafeIntegerCast<int32_t>(localOrd));
    }

    static InterpreterFrameLocation WARN_UNUSED VarArg(size_t varArgOrd)
    {
        return InterpreterFrameLocation(x_firstVarArgSlot - SafeIntegerCast<int32_t>(varArgOrd));
    }

    static InterpreterFrameLocation WARN_UNUSED FunctionObjectLoc()
    {
        return InterpreterFrameLocation(x_functionObjectSlot);
    }

    static InterpreterFrameLocation WARN_UNUSED NumVarArgsLoc()
    {
        return InterpreterFrameLocation(x_numVarArgsSlot);
    }

    bool IsLocal() { return m_value >= 0; }
    uint32_t LocalOrd() { TestAssert(IsLocal()); return static_cast<uint32_t>(m_value); }

    bool IsVarArg() { return m_value <= x_firstVarArgSlot; }
    uint32_t VarArgOrd() { TestAssert(IsVarArg()); return static_cast<uint32_t>(x_firstVarArgSlot - m_value); }

    bool IsFunctionObjectLoc() { return m_value == x_functionObjectSlot; }
    bool IsNumVarArgsLoc() { return m_value == x_numVarArgsSlot; }

    friend bool operator==(const InterpreterFrameLocation& a, const InterpreterFrameLocation& b)
    {
        return a.m_value == b.m_value;
    }

private:
    constexpr static int32_t x_functionObjectSlot = -1;
    constexpr static int32_t x_numVarArgsSlot = -2;
    constexpr static int32_t x_firstVarArgSlot = -3;

    explicit InterpreterFrameLocation(int32_t value) : m_value(value) { }
    int32_t m_value;
};

// DFG has a consistent 1:1 mapping between VirtualRegister and InterpreterSlot in each function.
// At a basic block boundary, the value stored in an interpreter slot (in the shadow stack) must be one of the following:
// 1. Anything (including invalid bitpatterns) if the interpreter slot is dead in the bytecode.
// 2. The same value as in the VirtualRegister that corresponds to this interpreter slot.
// 3. If there is no VirtualRegister corresponding to this interpreter slot, a fixed constant.
//
struct VirtualRegisterMappingInfo
{
    VirtualRegisterMappingInfo() : m_value(x_uninitialized) { }

    // For assertion purpose only
    //
    bool IsInitialized() { return m_value != x_uninitialized; }

    bool IsLive()
    {
        TestAssert(IsInitialized());
        // Currently interpreter slots that do not have a corresponding virtual register
        // are only used for constant values in the stack frame header, which is always live.
        //
        return m_value != x_dead;
    }

    // This might only be true for stack frame header slots
    // It means that the interpreter slot is not mapped to any virtual register, so it can only store statically-known constant values
    //
    bool IsUmmapedToAnyVirtualReg()
    {
        TestAssert(IsLive());
        return m_value & (1U << 31);
    }

    Node* GetConstantValue()
    {
        TestAssert(IsUmmapedToAnyVirtualReg());
        uint32_t ptrVal = m_value ^ (1U << 31);
        return ArenaPtr<Node>(ptrVal);
    }

    VirtualRegister GetVirtualRegister()
    {
        TestAssert(!IsUmmapedToAnyVirtualReg());
        return VirtualRegister(m_value);
    }

    static VirtualRegisterMappingInfo WARN_UNUSED VReg(VirtualRegister vreg)
    {
        VirtualRegisterMappingInfo res = VirtualRegisterMappingInfo(SafeIntegerCast<uint32_t>(vreg.Value()));
        TestAssert(res.GetVirtualRegister().Value() == vreg.Value());
        return res;
    }

    // This slot is always holding a fixed statically-known constant value
    //
    static VirtualRegisterMappingInfo WARN_UNUSED Unmapped(Node* node)
    {
#ifdef TESTBUILD
        AssertIsConstantOrUnboxedConstantNode(node);
#endif
        ArenaPtr<Node> ptrVal(node);
        TestAssert(ptrVal.m_value < (1U << 31));
        uint32_t val = ptrVal.m_value | (1U << 31);
        TestAssert(val != x_dead && val != x_uninitialized);
        return VirtualRegisterMappingInfo(val);
    }

    static VirtualRegisterMappingInfo WARN_UNUSED Dead()
    {
        return VirtualRegisterMappingInfo(x_dead);
    }

#ifdef TESTBUILD
    // Ugly: due to header file dependency the implementation must be put in a CPP file.. Fortunately this is only an assertion
    //
    static void AssertIsConstantOrUnboxedConstantNode(Node* node);
#endif

private:
    // The information has not been initialized properly. For assertion purpose only: It's a bug to see this.
    //
    static constexpr uint32_t x_uninitialized = static_cast<uint32_t>(-1);
    // The interpreter slot is dead, whatever value (including invalid boxed value bitpatterns) should not affect the interpreter
    //
    static constexpr uint32_t x_dead = static_cast<uint32_t>(-2);

    explicit VirtualRegisterMappingInfo(uint32_t value) : m_value(value) { }

    // If the highest bit is 0, it means the slot is live, and m_value is the VirtualRegister ordinal
    // If the highest bit is 1:
    //     If m_value is x_dead or x_uninitialized, it just means what these senital values said.
    //     Otherwise, it means the interpreter slot is always a constant, and the lower 31 bits should be
    //     interpreted as an ArenaPtr<Node>, which is the constant node (note that the bit pattern of the
    //     lower 31 bits for x_dead and x_uninitialized is never a valid ArenaPtr<Node>, so there's no confusion).
    //
    uint32_t m_value;
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
