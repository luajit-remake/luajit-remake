#pragma once

#include "common_utils.h"
#include "memory_ptr.h"
#include "jit_memory_allocator.h"

// Describes the traits of one kind of JIT IC piece
// Struct name and member names are hardcoded as they are used by generated C++ code!
//
struct alignas(4) JitCallInlineCacheTraits
{
    struct alignas(4) PatchRecord
    {
        uint16_t m_offset;
        bool m_is64;
    };
    static_assert(sizeof(PatchRecord) == 4);

    consteval JitCallInlineCacheTraits(uint8_t allocLengthStepping, bool isDirectCallMode, uint8_t numPatches)
        : m_jitCodeAllocationLengthStepping(allocLengthStepping)
        , m_isDirectCallMode(isDirectCallMode)
        , m_numCodePtrUpdatePatches(numPatches)
        , m_unused(0)
    {
        ReleaseAssert(numPatches > 0);
        ReleaseAssert(m_jitCodeAllocationLengthStepping < x_jit_mem_alloc_total_steppings);
    }

    // The allocation length stepping of the JIT code
    //
    uint8_t m_jitCodeAllocationLengthStepping;
    // Whether this IC is for direct-call mode or closure-call mode, for assertion only
    //
    bool m_isDirectCallMode;
    // Number of CodePtr update patches
    //
    uint8_t m_numCodePtrUpdatePatches;
    uint8_t m_unused;
    PatchRecord m_codePtrPatchRecords[0];
};
static_assert(sizeof(JitCallInlineCacheTraits) == 4);

template<size_t N>
struct JitCallInlineCacheTraitsHolder final : public JitCallInlineCacheTraits
{
    static_assert(N >= 1, "doesn't make sense if a call IC doesn't even have the target codePtr!");
    static_assert(N <= 255);

    using PatchRecord = JitCallInlineCacheTraits::PatchRecord;

    consteval JitCallInlineCacheTraitsHolder(uint8_t allocLengthStepping, bool isDirectCallMode, std::array<PatchRecord, N> patches)
        : JitCallInlineCacheTraits(allocLengthStepping, isDirectCallMode, static_cast<uint8_t>(N))
    {
        static_assert(offsetof_member_v<&JitCallInlineCacheTraitsHolder::m_recordsHolder> == offsetof_member_v<&JitCallInlineCacheTraits::m_codePtrPatchRecords>);
        for (size_t i = 0; i < N; i++)
        {
            ReleaseAssert(patches[i].m_offset < x_jit_mem_alloc_stepping_array[allocLengthStepping]);
            ReleaseAssert(patches[i].m_offset + (patches[i].m_is64 ? 8 : 4) <= x_jit_mem_alloc_stepping_array[allocLengthStepping]);
            m_recordsHolder[i] = patches[i];
        }
    }

    PatchRecord m_recordsHolder[N];
};

extern "C" const JitCallInlineCacheTraits* const deegen_jit_call_inline_cache_trait_table[];

// TODO: tune
//
constexpr size_t x_maxJitCallInlineCacheEntries = 3;

// Describes a Generic IC entry
// TODO: we need to think about the GC story
//
class JitGenericInlineCacheEntry
{
public:
    static JitGenericInlineCacheEntry* WARN_UNUSED Create(VM* vm,
                                                          SpdsPtr<JitGenericInlineCacheEntry> nextNode,
                                                          uint16_t icTraitKind,
                                                          uint8_t allocationStepping);

    // The singly-linked list anchored at the callsite, 0 if last node
    //
    SpdsPtr<JitGenericInlineCacheEntry> m_nextNode;
    uint16_t m_traitKind;
    uint8_t m_jitRegionLengthStepping;
    void* m_jitAddr;
};
static_assert(sizeof(JitGenericInlineCacheEntry) == 16);

// Describes an IC site
// Must have an alignment of 1 since it resides in the SlowPathData stream
//
class __attribute__((__packed__, __aligned__(1))) JitGenericInlineCacheSite
{
public:
    // Try to keep this a zero initialization to avoid unnecessary work..
    //
    JitGenericInlineCacheSite()
        : m_linkedListHead(SpdsPtr<JitGenericInlineCacheEntry> { 0 })
        , m_numEntries(0)
        , m_isInlineSlabUsed(0)
    { }

    // The linked list of all the IC entries it owns
    //
    Packed<SpdsPtr<JitGenericInlineCacheEntry>> m_linkedListHead;

    uint8_t m_numEntries;

    // Actually a bool, always 0 or 1.
    // This field needs to be accessed from LLVM, so use uint8_t to avoid ABI issues
    //
    uint8_t m_isInlineSlabUsed;

    // May only be called if m_numEntries < x_maxJitGenericInlineCacheEntries
    //
    void* WARN_UNUSED InsertForBaselineJIT(uint16_t traitKind);

    // May only be called if m_numEntries < x_maxJitGenericInlineCacheEntries
    //
    // dfgOpOrd is the absolute ord of the DfgVariant (the header field in the SlowPathData)
    // icOrdInOp is the IC ordinal inside this DfgVariant
    //
    void* WARN_UNUSED InsertForDfgJIT(uint16_t dfgOpOrd, uint16_t icOrdInOp);
};
static_assert(sizeof(JitGenericInlineCacheSite) == 6);

// TODO: tune
//
constexpr size_t x_maxJitGenericInlineCacheEntries = 5;
