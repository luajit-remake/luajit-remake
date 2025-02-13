#pragma once

#include "common_utils.h"
#include "dfg_reg_alloc_register_info.h"
#include "heap_ptr_utils.h"
#include "temp_arena_allocator.h"
#include "dfg_osr_exit_map_builder.h"

namespace dfg {

struct ValueNextUseInfo
{
    constexpr ValueNextUseInfo() = default;
    constexpr ValueNextUseInfo(uint32_t value) : m_value(value) { }

    constexpr ALWAYS_INLINE ValueNextUseInfo(bool isGhostUseOnly, uint32_t nextUseIndex)
    {
        m_value = 0;
        Data_GhostUseOnly::Set(m_value, isGhostUseOnly);
        Data_NextUseIndex::Set(m_value, nextUseIndex);
    }

    static constexpr ValueNextUseInfo WARN_UNUSED NoMoreUse()
    {
        return ValueNextUseInfo(true /*isGhostUseOnly*/, x_noNextUse);
    }

    bool WARN_UNUSED IsAllFutureUseGhostUse() { return Data_GhostUseOnly::Get(m_value); }
    uint32_t WARN_UNUSED GetNextUseIndex() { return Data_NextUseIndex::Get(m_value); }
    uint16_t WARN_UNUSED GetCustomDataAsU16() { return Data_CustomDataAsU16::Get(m_value); }
    uint8_t WARN_UNUSED GetCustomDataAsU8() { return Data_CustomDataAsU8::Get(m_value); }

    uint32_t WARN_UNUSED GetCompositeValueWithLowerBitsCleared()
    {
        return ValueNextUseInfo(IsAllFutureUseGhostUse(), GetNextUseIndex()).m_value;
    }

    bool HasNextUse()
    {
        TestAssertImp(GetNextUseIndex() == x_noNextUse, IsAllFutureUseGhostUse());
        bool result = (m_value < NoMoreUse().m_value);
        TestAssertIff(result, GetNextUseIndex() != x_noNextUse);
        return result;
    }

    // Note that the bit-field offsets matter: we sort DataTy as an integer directly!
    // A larger value means the value is a better candidate for eviction.
    //
    using DataTy = uint32_t;

    // If true, it means all remaining uses of this SSA value are by ShadowStore or Phantom
    // In this case, we still need to keep the value alive, but it no longer makes sense to
    // keep the value in register. Thus, this is the highest bit.
    //
    using Data_GhostUseOnly = BitFieldMember<DataTy, bool, 31 /*start*/, 1 /*width*/>;

    // The main weight value that decides which register to evict.
    //
    // We use the next nodeIndex that uses this value as criteria, which is optimal if one
    // treats all eviction to have the same cost.
    //
    // However, eviction may have different cost actually: for non-constant nodes, only at the
    // first time we evict it, we need to spill it, incurring a one-time cost. But for
    // constant-like nodes and nodes that have already been spilled once, there is no spill cost.
    // I thought a bit about if we can patch the algorithm to deal with this, but with no avail.
    // One might think the algorithm can be patched by using NextNextUse as eviction weight for nodes
    // that does not have a spill cost, but this is wrong (consider access pattern ABCABCABC...
    // where we only have two regs. The optimal solution is to always evict based on NextUse,
    // resulting in 1/2 hit rate long term. But if one uses the above NextNextUse patch, the algorithm
    // will tend to avoid spilling or prefer spilling a specific node to avoid the one-time spill cost,
    // but this results in only 1/3 hit rate long term).
    //
    // So for now, we will not attempt to leverage the fact that some nodes does not need to be spilled
    // or already have been spilled, and just always use NextUse as criterion for eviction.
    //
    using Data_NextUseIndex = BitFieldMember<DataTy, uint32_t, 12 /*start*/, 19 /*width*/>;

    static constexpr uint32_t x_noNextUse = static_cast<uint32_t>((1U << Data_NextUseIndex::BitWidth()) - 1);

    // This field is repurposed in a number of ways depending on the caller logic.
    //
    using Data_CustomDataAsU16 = BitFieldMember<DataTy, uint16_t, 0 /*start*/, 12 /*width*/>;
    using Data_CustomDataAsU8 = BitFieldMember<DataTy, uint8_t, 0 /*start*/, 8 /*width*/>;

    DataTy m_value;
};

struct Node;

// This class records:
// 1. Where a SSA value is available right now: it could be in GPR and/or in FPR and/or on the stack.
// 2. When is the next GPR/FPR use of this SSA value
// 3. All locations on the shadow stack which current value is this SSA value (only available if this value isn't constant-like)
//
struct alignas(8) ValueRegAllocInfo
{
private:
    // This class must be allocated in DFG arena
    //
    friend Arena;

    // Initialize for a non-constant-like node
    //
    ALWAYS_INLINE ValueRegAllocInfo()
        : m_associatedShadowStackLocs()
    {
        m_compositeValue = 0;
        SetGprNextUseInfo(ValueNextUseInfo::NoMoreUse());
        SetFprNextUseInfo(ValueNextUseInfo::NoMoreUse());
        Data_GprIndex::Set(m_compositeValue, x_invalidReg);
        Data_FprIndex::Set(m_compositeValue, x_invalidReg);
        Data_PhysicalSpillLoc::Set(m_compositeValue, x_notSpilled);
        TestAssert(!IsConstantLikeNode());
    }

    // Initialize for a constant-like node
    //
    ALWAYS_INLINE ValueRegAllocInfo(Node* node, uint16_t ident)
    {
        TestAssert(node != nullptr);
        m_compositeValue = 0;
        SetGprNextUseInfo(ValueNextUseInfo::NoMoreUse());
        SetFprNextUseInfo(ValueNextUseInfo::NoMoreUse());
        Data_GprIndex::Set(m_compositeValue, x_invalidReg);
        Data_FprIndex::Set(m_compositeValue, x_invalidReg);
        Data_PhysicalSpillLoc::Set(m_compositeValue, ident);
        TestAssert(reinterpret_cast<uintptr_t>(node) % 2 == 0);
        m_cstNodeTaggedPtr = reinterpret_cast<uintptr_t>(node) | 1;
        TestAssert(IsConstantLikeNode());
    }

public:
    bool WARN_UNUSED IsAvailableInGPR()
    {
        return Data_GprIndex::Get(m_compositeValue) != x_invalidReg;
    }

    void InvalidateGPR()
    {
        TestAssert(IsAvailableInGPR());
        Data_GprIndex::Set(m_compositeValue, x_invalidReg);
    }

    // The actual GPR is x_dfg_reg_alloc_gprs[gprOrdInList]
    //
    void SetGPR(uint8_t gprOrdInList)
    {
        TestAssert(gprOrdInList < x_dfg_reg_alloc_num_gprs);
        Data_GprIndex::Set(m_compositeValue, gprOrdInList);
    }

    uint8_t WARN_UNUSED GetGprOrdInList()
    {
        TestAssert(IsAvailableInGPR());
        TestAssert(Data_GprIndex::Get(m_compositeValue) < x_dfg_reg_alloc_num_gprs);
        return Data_GprIndex::Get(m_compositeValue);
    }

    X64Reg WARN_UNUSED GetGprRegister()
    {
        return x_dfg_reg_alloc_gprs[GetGprOrdInList()];
    }

    bool WARN_UNUSED IsAvailableInFPR()
    {
        return Data_FprIndex::Get(m_compositeValue) != x_invalidReg;
    }

    void InvalidateFPR()
    {
        TestAssert(IsAvailableInFPR());
        Data_FprIndex::Set(m_compositeValue, x_invalidReg);
    }

    // The actual FPR is x_dfg_reg_alloc_fprs[fprOrdInList]
    //
    void SetFPR(uint8_t fprOrdInList)
    {
        TestAssert(fprOrdInList < x_dfg_reg_alloc_num_fprs);
        Data_FprIndex::Set(m_compositeValue, fprOrdInList);
    }

    uint8_t WARN_UNUSED GetFprOrdInList()
    {
        TestAssert(IsAvailableInFPR());
        TestAssert(Data_FprIndex::Get(m_compositeValue) < x_dfg_reg_alloc_num_fprs);
        return Data_FprIndex::Get(m_compositeValue);
    }

    X64Reg WARN_UNUSED GetFprRegister()
    {
        return x_dfg_reg_alloc_fprs[GetFprOrdInList()];
    }

    template<bool forGprState>
    bool WARN_UNUSED ALWAYS_INLINE IsAvailableInRegBank()
    {
        if constexpr(forGprState)
        {
            return IsAvailableInGPR();
        }
        else
        {
            return IsAvailableInFPR();
        }
    }

    template<bool forGprState>
    uint8_t WARN_UNUSED ALWAYS_INLINE GetRegBankRegOrdInList()
    {
        if constexpr(forGprState)
        {
            return GetGprOrdInList();
        }
        else
        {
            return GetFprOrdInList();
        }
    }

    template<bool forGprState>
    X64Reg WARN_UNUSED ALWAYS_INLINE GetRegBankRegister()
    {
        if constexpr(forGprState)
        {
            return GetGprRegister();
        }
        else
        {
            return GetFprRegister();
        }
    }

    template<bool forGprState>
    ValueNextUseInfo WARN_UNUSED ALWAYS_INLINE GetNextUseInfoInRegBank()
    {
        if constexpr(forGprState)
        {
            return GetGprNextUseInfo();
        }
        else
        {
            return GetFprNextUseInfo();
        }
    }

    template<bool forGprState>
    void ALWAYS_INLINE SetNextUseInfoInRegBank(ValueNextUseInfo info)
    {
        if constexpr(forGprState)
        {
            SetGprNextUseInfo(info);
        }
        else
        {
            SetFprNextUseInfo(info);
        }
    }

    template<bool forGprState>
    void ALWAYS_INLINE SetRegBankRegOrdInList(size_t regOrdInList)
    {
        if constexpr(forGprState)
        {
            SetGPR(SafeIntegerCast<uint8_t>(regOrdInList));
        }
        else
        {
            SetFPR(SafeIntegerCast<uint8_t>(regOrdInList));
        }
    }

    template<bool forGprState>
    void ALWAYS_INLINE InvalidateRegBankRegister()
    {
        if constexpr(forGprState)
        {
            InvalidateGPR();
        }
        else
        {
            InvalidateFPR();
        }
    }

    // Same as IsConstantNode() || IsSpilled(), but faster
    //
    bool ALWAYS_INLINE WARN_UNUSED IsConstantLikeNodeOrIsSpilled()
    {
        // x_noSpill (0x7fff) is not a valid constant ident (and not a valid spill location either, of course)
        // So all constant-like nodes must have m_physicalSlot != x_noSpill.
        //
        TestAssertImp(IsConstantLikeNode(), Data_PhysicalSpillLoc::Get(m_compositeValue) != x_notSpilled);
        return Data_PhysicalSpillLoc::Get(m_compositeValue) != x_notSpilled;
    }

    bool ALWAYS_INLINE WARN_UNUSED IsNonConstantAndNotSpilled()
    {
        return !IsConstantLikeNodeOrIsSpilled();
    }

    bool ALWAYS_INLINE WARN_UNUSED IsConstantLikeNode()
    {
        // For sanity, avoid breaking strict aliasing
        //
        return UnalignedLoad<uint64_t>(&m_cstNodeTaggedPtr) & 1;
    }

    Node* WARN_UNUSED GetConstantLikeNode()
    {
        TestAssert(IsConstantLikeNode());
        return reinterpret_cast<Node*>(m_cstNodeTaggedPtr ^ 1);
    }

    uint16_t WARN_UNUSED GetConstantIdentifier()
    {
        TestAssert(IsConstantLikeNode());
        return Data_PhysicalSpillLoc::Get(m_compositeValue);
    }

    bool WARN_UNUSED IsSpilled()
    {
        TestAssert(!IsConstantLikeNode());
        return Data_PhysicalSpillLoc::Get(m_compositeValue) != x_notSpilled;
    }

    uint16_t WARN_UNUSED GetPhysicalSpillSlot()
    {
        TestAssert(!IsConstantLikeNode() && IsSpilled());
        return Data_PhysicalSpillLoc::Get(m_compositeValue);
    }

    void SetPhysicalSpillSlot(uint16_t value)
    {
        TestAssert(!IsConstantLikeNode() && !IsSpilled() && value < x_notSpilled);
        Data_PhysicalSpillLoc::Set(m_compositeValue, value);
    }

    ValueNextUseInfo GetGprNextUseInfo()
    {
        return ValueNextUseInfo(
            Data_GprGhostUseOnly::Get(m_compositeValue),
            Data_GprNextUseIndex::Get(m_compositeValue));
    }

    ValueNextUseInfo GetFprNextUseInfo()
    {
        return ValueNextUseInfo(
            Data_FprGhostUseOnly::Get(m_compositeValue),
            Data_FprNextUseIndex::Get(m_compositeValue));
    }

    void ALWAYS_INLINE SetGprNextUseInfo(ValueNextUseInfo info)
    {
        TestAssertImp(info.GetNextUseIndex() == ValueNextUseInfo::x_noNextUse, info.IsAllFutureUseGhostUse());
        Data_GprGhostUseOnly::Set(m_compositeValue, info.IsAllFutureUseGhostUse());
        Data_GprNextUseIndex::Set(m_compositeValue, info.GetNextUseIndex());
    }

    void ALWAYS_INLINE SetFprNextUseInfo(ValueNextUseInfo info)
    {
        TestAssertImp(info.GetNextUseIndex() == ValueNextUseInfo::x_noNextUse, info.IsAllFutureUseGhostUse());
        Data_FprGhostUseOnly::Set(m_compositeValue, info.IsAllFutureUseGhostUse());
        Data_FprNextUseIndex::Set(m_compositeValue, info.GetNextUseIndex());
    }

    bool ALWAYS_INLINE HasNoMoreUseInBothGprAndFpr()
    {
        return Data_GprNextUseIndex::Get(m_compositeValue) == ValueNextUseInfo::x_noNextUse &&
            Data_FprNextUseIndex::Get(m_compositeValue) == ValueNextUseInfo::x_noNextUse;
    }

    DfgOsrExitMapBuilder::DoublyLinkedListNode* GetAssociatedShadowStackLocs()
    {
        TestAssert(!IsConstantLikeNode());
        return &m_associatedShadowStackLocs;
    }

private:
    static constexpr uint8_t x_invalidReg = 15;
    static_assert(x_dfg_reg_alloc_num_gprs <= x_invalidReg && x_dfg_reg_alloc_num_fprs <= x_invalidReg);

    // Where the next GPR use of this SSA value is
    //
    using Data_GprGhostUseOnly = BitFieldMember<uint64_t, bool, 63 /*start*/, 1 /*width*/>;
    using Data_GprNextUseIndex = BitFieldMember<uint64_t, uint32_t, 44 /*start*/, 19 /*width*/>;

    // x_invalidReg means not available in GPR, otherwise available in x_dfg_reg_alloc_gprs[m_gprIndex]
    //
    using Data_GprIndex = BitFieldMember<uint64_t, uint8_t, 40 /*start*/, 4 /*width*/>;

    // Where the next FPR use of this SSA value is
    //
    using Data_FprGhostUseOnly = BitFieldMember<uint64_t, bool, 39 /*start*/, 1 /*width*/>;
    using Data_FprNextUseIndex = BitFieldMember<uint64_t, uint32_t, 20 /*start*/, 19 /*width*/>;

    // x_invalidReg means not available in FPR, otherwise available in x_dfg_reg_alloc_fprs[m_fprIndex]
    //
    using Data_FprIndex = BitFieldMember<uint64_t, uint8_t, 16 /*start*/, 4 /*width*/>;

    static_assert(Data_GprNextUseIndex::BitWidth() == ValueNextUseInfo::Data_NextUseIndex::BitWidth());
    static_assert(Data_FprNextUseIndex::BitWidth() == ValueNextUseInfo::Data_NextUseIndex::BitWidth());

    // If this value is not a constant, this stores the physical slot ordinal into the DFG frame where this value is spilled.
    // Note that this never points to the register spill area. x_notSpilled means the value is not yet spilled.
    //
    // If this value is a constant, this stores the identifier for this constant in the OSR exit log.
    //
    using Data_PhysicalSpillLoc = BitFieldMember<uint64_t, uint16_t, 0 /*start*/, 16 /*width*/>;

    static constexpr uint16_t x_notSpilled = 0x7fff;

    uint64_t m_compositeValue;

    union {
        // The doubly-linked list head that contains all shadow stack locations that currently has this SSA value
        //
        DfgOsrExitMapBuilder::DoublyLinkedListNode m_associatedShadowStackLocs;
        // For constants, we don't need to track which shadow stack locations stores this value,
        // since constants will never need to be relocated. But we need to track the constant-like node so we can codegen the materialization logic
        //
        // We use the lowest bit == 1 to tag this union (so we can tell if it is a constant-like node or not), which is unfortunately very hacky
        // (correctness is due to pointer alignment and the representation of DoublyLinkedListNode)..
        //
        uint64_t m_cstNodeTaggedPtr;
    };
    static_assert(alignof(DfgOsrExitMapBuilder::DoublyLinkedListNode) > 1, "required for pointer tagging");
};
static_assert(sizeof(ValueRegAllocInfo) == 16);

// Records a single use of an SSA value
//
struct alignas(8) ValueUseRAInfo
{
    ValueUseRAInfo()
        : m_node(nullptr)
        , m_nextUseInfo(0)
    { }

    bool IsInitialized() { return !m_node.IsNull(); }

    // Initialize info for this use, and update the use list in 'ssaValue'
    //
    void ALWAYS_INLINE Initialize(ValueRegAllocInfo* ssaValue, uint32_t useIndex, bool isGprUse, bool isGhostLikeUse, bool doNotProduceDuplicateEdge)
    {
        TestAssert(!IsInitialized());
        SetSSAValue(ssaValue);

        ValueNextUseInfo nextUseInfo;
        if (isGprUse)
        {
            nextUseInfo = ssaValue->GetGprNextUseInfo();
        }
        else
        {
            nextUseInfo = ssaValue->GetFprNextUseInfo();
        }

        // If this is a ghost-like use, it should be the last use in the GPR/FPR list.
        // since in other cases, we know the value is alive somewhere so we don't need to mark this as a use
        //
        bool isLastUse = !ssaValue->GetGprNextUseInfo().HasNextUse() && !ssaValue->GetFprNextUseInfo().HasNextUse();
        TestAssertImp(isGhostLikeUse, isLastUse);

        TestAssert(nextUseInfo.GetNextUseIndex() >= useIndex);
        if (nextUseInfo.GetNextUseIndex() == useIndex && !doNotProduceDuplicateEdge)
        {
            // This is a duplicate edge
            // We should not record this use in the use list
            //
            // This should not happen for GhostLikeUse since ShadowStore and Phantom only take on operand
            // For same reason, nextUseInfo should not be the last GhostLikeUse either. So nothing to update.
            //
            TestAssert(!isGhostLikeUse);
            TestAssert(!nextUseInfo.IsAllFutureUseGhostUse());
            SetDuplicateEdge(isGprUse);
        }
        else
        {
            SetNextUseInfo(isGprUse, isLastUse, nextUseInfo);
            if (isGprUse)
            {
                ssaValue->SetGprNextUseInfo(ValueNextUseInfo(isGhostLikeUse, useIndex));
            }
            else
            {
                ssaValue->SetFprNextUseInfo(ValueNextUseInfo(isGhostLikeUse, useIndex));
            }
        }
    }

    ValueRegAllocInfo* GetSSAValue()
    {
        TestAssert(m_node.m_value < (1U << 31));
        TestAssert(!m_node.IsNull());
        return m_node;
    }

    void SetSSAValue(ValueRegAllocInfo* value)
    {
        TestAssert(value != nullptr);
        m_node = value;
    }

    bool IsGPRUse() { return m_nextUseInfo & 1; }

    bool IsLastUse()
    {
        bool isLastUse = ((m_nextUseInfo & 2) > 0);
        TestAssertImp(IsDuplicateEdge(), !isLastUse);
        return isLastUse;
    }

    // A duplicate edge means that reg alloc must dedicate a reg for this value,
    // but the reg is not associated with an SSA value: it only holds a clone of the value of another reg (that is
    // associated with an SSA value), and after the node is executed, this reg automatically becomes scratch.
    //
    // A duplicate edge is also not considered a use (there must be a non-duplicate edge that uses that SSA value in the
    // same useIndex epoch), so GetNextUseInfo() is invalid.
    //
    bool IsDuplicateEdge() { return m_nextUseInfo & 4; }

    ValueNextUseInfo GetNextUseInfo()
    {
        TestAssert(!IsDuplicateEdge());
        return m_nextUseInfo & (~static_cast<uint32_t>(3));
    }

    void SetNextUseInfo(bool isGprUse, bool isLastUse, ValueNextUseInfo info)
    {
        TestAssertImp(isLastUse, !info.HasNextUse());
        TestAssert(info.GetCustomDataAsU16() == 0);
        m_nextUseInfo = info.m_value | (isGprUse ? 1U : 0U) | (isLastUse ? 2U : 0U);
    }

    void SetDuplicateEdge(bool isGprUse)
    {
        m_nextUseInfo = (isGprUse ? 1U : 0U) | 4U;
    }

private:
    ArenaPtr<ValueRegAllocInfo> m_node;
    // Bit 0 of this value is repurposed to represent if this is a GPR use or a FPR use (1 = GPR, 0 = FPR)
    // Bit 1 of this value is repurposed to represent if this is the last use (considering both GPR and FPR) of this SSA value
    // Bit 2 of this value is repurposed to represent if this is a duplicate edge. If so, the remaining bits are invalid.
    //
    uint32_t m_nextUseInfo;
};
static_assert(sizeof(ValueUseRAInfo) == 8);

enum class DfgCodegenFuncOrd : uint16_t;

// [ NodeRegAllocInfo ] [ m_numFixedSSAInputs ] [ m_numRangeSSAInputs ] [ m_numChecks ]
//
// Conceptually the uses happen in 3 steps:
// 1. We use all SSA inputs corresponding to the range operand (to store them to the correct stack location)
// 2. We use all the inputs that need a check (to check them)
// 3. We use all SSA inputs corresponding to the fixed operands (to execute the main logic)
//
struct alignas(8) NodeRegAllocInfo
{
private:
    friend TempArenaAllocator;

    NodeRegAllocInfo()
        : m_numRangeSSAInputs(static_cast<uint16_t>(-1))
        , m_numChecks(static_cast<uint16_t>(-1))
        , m_numFixedSSAInputs(static_cast<uint16_t>(-1))
        , m_isGhostLikeNode(false)
        , m_isShadowStoreNode(false)
    { }

public:
    std::span<ValueUseRAInfo> WARN_UNUSED GetRangedOperands()
    {
        TestAssert(m_numFixedSSAInputs != static_cast<uint16_t>(-1) && m_numRangeSSAInputs != static_cast<uint16_t>(-1));
        return { m_operands + m_numFixedSSAInputs, m_numRangeSSAInputs };
    }

    std::span<ValueUseRAInfo> WARN_UNUSED GetFixedOperands()
    {
        TestAssert(m_numFixedSSAInputs != static_cast<uint16_t>(-1));
        return { m_operands, m_numFixedSSAInputs };
    }

    ValueUseRAInfo* ALWAYS_INLINE GetInputRAInfo(size_t inputOrd)
    {
        TestAssert(inputOrd < m_numFixedSSAInputs);
        return m_operands + inputOrd;
    }

    std::span<ValueUseRAInfo> WARN_UNUSED GetCheckOperands()
    {
        TestAssert(m_numFixedSSAInputs != static_cast<uint16_t>(-1) && m_numRangeSSAInputs != static_cast<uint16_t>(-1) && m_numChecks != static_cast<uint16_t>(-1));
        return { m_operands + m_numFixedSSAInputs + m_numRangeSSAInputs, m_numChecks };
    }

    // The list containing everything (fixed + range + checks), should only be used to process death events
    //
    std::span<ValueUseRAInfo> WARN_UNUSED GetAllOperands()
    {
        TestAssert(m_numFixedSSAInputs != static_cast<uint16_t>(-1) && m_numRangeSSAInputs != static_cast<uint16_t>(-1) && m_numChecks != static_cast<uint16_t>(-1));
        return { m_operands, static_cast<size_t>(m_numFixedSSAInputs + m_numRangeSSAInputs + m_numChecks) };
    }

    static constexpr size_t TrailingArrayOffset()
    {
        return offsetof_member_v<&NodeRegAllocInfo::m_operands>;
    }

    // Note that the trailing array is not constructed
    //
    static NodeRegAllocInfo* WARN_UNUSED Create(TempArenaAllocator& alloc, uint16_t numFixedOperands, uint16_t numRangedOperands, uint16_t numChecks)
    {
        size_t trailingArrayNumElements = numFixedOperands + numRangedOperands + numChecks;
        NodeRegAllocInfo* r = alloc.AllocateObjectWithTrailingBuffer<NodeRegAllocInfo>(sizeof(ValueUseRAInfo) * trailingArrayNumElements);
        r->m_numRangeSSAInputs = numRangedOperands;
        r->m_numFixedSSAInputs = numFixedOperands;
        r->m_numChecks = numChecks;
        return r;
    }

    uint16_t m_numRangeSSAInputs;
    uint16_t m_numChecks;
    uint16_t m_numFixedSSAInputs;

    // True if this is a ShadowStore, ShadowStoreUndefToRange, or Phantom
    //
    bool m_isGhostLikeNode;
    bool m_isShadowStoreNode;

    ValueUseRAInfo m_operands[0];
};
static_assert(sizeof(NodeRegAllocInfo) == 8);

}   // namespace dfg
