#include "jit_inline_cache_utils.h"
#include "dfg_variant_traits.h"
#include "vm.h"

using namespace dfg;

extern "C" const uint8_t deegen_baseline_jit_generic_ic_jit_allocation_stepping_table[];

JitGenericInlineCacheEntry* WARN_UNUSED JitGenericInlineCacheEntry::Create(VM* vm,
                                                                           SpdsPtr<JitGenericInlineCacheEntry> nextNode,
                                                                           uint16_t icTraitKind,
                                                                           uint8_t allocationStepping)
{
    JitGenericInlineCacheEntry* entry = vm->AllocateFromSpdsRegionUninitialized<JitGenericInlineCacheEntry>();
    ConstructInPlace(entry);
    entry->m_nextNode = nextNode;
    entry->m_traitKind = icTraitKind;
    entry->m_jitRegionLengthStepping = allocationStepping;
    entry->m_jitAddr = vm->GetJITMemoryAlloc()->AllocateGivenStepping(allocationStepping);
    return entry;
}

void* WARN_UNUSED JitGenericInlineCacheSite::InsertForBaselineJIT(uint16_t traitKind)
{
    Assert(m_numEntries < x_maxJitGenericInlineCacheEntries);
    VM* vm = VM::GetActiveVMForCurrentThread();
    uint8_t allocationStepping = deegen_baseline_jit_generic_ic_jit_allocation_stepping_table[traitKind];
    JitGenericInlineCacheEntry* entry = JitGenericInlineCacheEntry::Create(vm, TCGet(m_linkedListHead), traitKind, allocationStepping);
    TCSet(m_linkedListHead, SpdsPtr<JitGenericInlineCacheEntry> { entry });
    m_numEntries++;
    return entry->m_jitAddr;
}

namespace detail {

struct ConstructGenericIcBaseTraitIdTable : DfgConstEvalForEachOpcode<ConstructGenericIcBaseTraitIdTable>
{
    template<BCKind bcKind, size_t dvOrd, size_t cgOrd>
    consteval void action()
    {
        ReleaseAssert(m_curIdx <= x_totalNumDfgUserNodeCodegenFuncs);
        ReleaseAssert(m_curTraitId < 65535);
        m_data[m_curIdx] = SafeIntegerCast<uint16_t>(m_curTraitId);

        if constexpr(bcKind == BCKind::X_END_OF_ENUM)
        {
            ReleaseAssert(m_curIdx == x_totalNumDfgUserNodeCodegenFuncs);
        }
        else
        {
            ReleaseAssert(m_curIdx < x_totalNumDfgUserNodeCodegenFuncs);
            uint16_t numCases = DfgCodegenFuncInfoFor<bcKind, dvOrd, cgOrd>::numGenericIcCases;
            ReleaseAssert(m_curTraitId + numCases < 65535);
            m_curTraitId += numCases;
            m_curIdx++;
        }
    }

    consteval ConstructGenericIcBaseTraitIdTable()
    {
        m_curIdx = 0;
        m_curTraitId = 0;
        for (size_t i = 0; i <= x_totalNumDfgUserNodeCodegenFuncs; i++) { m_data[i] = 65535; }
        RunActionForEachDfgOpcode();
        ReleaseAssert(m_curIdx == x_totalNumDfgUserNodeCodegenFuncs);
        for (size_t i = 0; i <= x_totalNumDfgUserNodeCodegenFuncs; i++) { ReleaseAssert(m_data[i] != 65535); }
    }

    size_t m_curIdx;
    size_t m_curTraitId;
    std::array<uint16_t, x_totalNumDfgUserNodeCodegenFuncs + 1> m_data;
};

}   // namespace detail

constexpr std::array<uint16_t, x_totalNumDfgUserNodeCodegenFuncs + 1>
    deegen_dfg_jit_opcode_to_generic_ic_base_trait_ord_table = ::detail::ConstructGenericIcBaseTraitIdTable().m_data;

constexpr size_t x_grandTotalDfgJitGenericIcCases = deegen_dfg_jit_opcode_to_generic_ic_base_trait_ord_table[x_totalNumDfgUserNodeCodegenFuncs];

namespace detail {

struct ConstructGenericIcAllocSteppingTable : DfgConstEvalForEachOpcode<ConstructGenericIcAllocSteppingTable>
{
    template<BCKind bcKind, size_t dvOrd, size_t cgOrd>
    consteval void action()
    {
        if constexpr(bcKind == BCKind::X_END_OF_ENUM)
        {
            ReleaseAssert(m_curTraitId == x_grandTotalDfgJitGenericIcCases);
        }
        else
        {
            constexpr size_t numTraits = DfgCodegenFuncInfoFor<bcKind, dvOrd, cgOrd>::numGenericIcCases;
            const std::array<uint8_t, numTraits>& info = DfgCodegenFuncInfoFor<bcKind, dvOrd, cgOrd>::genericIcStubAllocSteppings;
            ReleaseAssert(m_curTraitId + numTraits <= x_grandTotalDfgJitGenericIcCases);
            for (size_t idx = 0; idx < numTraits; idx++)
            {
                m_data[m_curTraitId + idx] = info[idx];
            }
            m_curTraitId += numTraits;
        }
    }

    consteval ConstructGenericIcAllocSteppingTable()
    {
        m_curTraitId = 0;
        for (size_t i = 0; i < x_grandTotalDfgJitGenericIcCases; i++) { m_data[i] = 255; }
        RunActionForEachDfgOpcode();
        ReleaseAssert(m_curTraitId == x_grandTotalDfgJitGenericIcCases);
        for (size_t i = 0; i < x_grandTotalDfgJitGenericIcCases; i++) { ReleaseAssert(m_data[i] != 255); }
    }

    size_t m_curTraitId;
    std::array<uint8_t, x_grandTotalDfgJitGenericIcCases> m_data;
};

}   // namespace detail

constexpr std::array<uint8_t, x_grandTotalDfgJitGenericIcCases>
    deegen_dfg_jit_generic_ic_jit_allocation_stepping_table = ::detail::ConstructGenericIcAllocSteppingTable().m_data;

void* WARN_UNUSED JitGenericInlineCacheSite::InsertForDfgJIT(uint16_t dfgOpOrd, uint16_t icOrdInOp)
{
    TestAssert(m_numEntries < x_maxJitGenericInlineCacheEntries);
    VM* vm = VM::GetActiveVMForCurrentThread();
    TestAssert(dfgOpOrd < x_totalNumDfgUserNodeCodegenFuncs);
    uint16_t traitOrd = deegen_dfg_jit_opcode_to_generic_ic_base_trait_ord_table[dfgOpOrd] + icOrdInOp;
    TestAssert(traitOrd < deegen_dfg_jit_opcode_to_generic_ic_base_trait_ord_table[dfgOpOrd + 1]);
    TestAssert(traitOrd < x_grandTotalDfgJitGenericIcCases);
    uint8_t allocationStepping = deegen_dfg_jit_generic_ic_jit_allocation_stepping_table[traitOrd];

    JitGenericInlineCacheEntry* entry = JitGenericInlineCacheEntry::Create(vm, TCGet(m_linkedListHead), traitOrd, allocationStepping);
    TCSet(m_linkedListHead, SpdsPtr<JitGenericInlineCacheEntry> { entry });
    m_numEntries++;
    return entry->m_jitAddr;
}
