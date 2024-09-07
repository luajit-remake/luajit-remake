#pragma once

#include "common.h"
#include "ptr_utils.h"
#include "jit_memory_allocator.h"
#include "memory_ptr.h"

// This struct name and member names are hardcoded as they are used by generated C++ code!
//
struct BytecodeBaselineJitTraits
{
    uint16_t m_fastPathCodeLen;
    uint16_t m_slowPathCodeLen;
    uint16_t m_dataSectionCodeLen;
    uint16_t m_slowPathDataLen;
    uint8_t m_dataSectionAlignment;
    uint8_t m_bytecodeLength;
    uint8_t m_numCondBrLatePatches;
    uint8_t m_numCallIcSites;
    uint16_t m_callIcSiteOffsetInSlowPathData;
    uint16_t m_unused;
};
// Make sure the size of this struct is a power of 2 to make addressing cheap
//
static_assert(sizeof(BytecodeBaselineJitTraits) == 16);

// The max possible m_dataSectionAlignment
// We assert this at build time, so we know this must be true at runtime
//
constexpr size_t x_baselineJitMaxPossibleDataSectionAlignment = 16;

// For now, our codegen allocator returns 16-byte-aligned memory.
// So if we want to support larger alignment, something additional logic must be done.
//
static_assert(x_baselineJitMaxPossibleDataSectionAlignment <= 16);

enum class BaselineJitCondBrLatePatchKind : uint32_t
{
    // *(uint32_t*)ptr += dstAddr
    //
    Int32,
    // ((uint32_t*)ptr)[0] = dstAddr
    // ((uint32_t*)ptr)[1] = dstBytecodeOrd
    //
    SlowPathData,
    // *(uint64_t*)ptr += dstAddr
    //
    Int64
};

struct BaselineJitCondBrLatePatchRecord
{
    uint8_t* m_ptr;
    uint32_t m_dstBytecodePtrLow32bits;
    BaselineJitCondBrLatePatchKind m_patchKind;

    void ALWAYS_INLINE Patch(uint64_t jitAddr, uint32_t bytecodeOrd)
    {
        switch (m_patchKind)
        {
        case BaselineJitCondBrLatePatchKind::Int32:
        {
            UnalignedStore<uint32_t>(m_ptr, UnalignedLoad<uint32_t>(m_ptr) + static_cast<uint32_t>(jitAddr));
            break;
        }
        case BaselineJitCondBrLatePatchKind::SlowPathData:
        {
            UnalignedStore<uint32_t>(m_ptr, static_cast<uint32_t>(jitAddr));
            UnalignedStore<uint32_t>(m_ptr + 4, static_cast<uint32_t>(bytecodeOrd));
            break;
        }
        case BaselineJitCondBrLatePatchKind::Int64: [[unlikely]]
        {
            UnalignedStore<uint64_t>(m_ptr, UnalignedLoad<uint64_t>(m_ptr) + jitAddr);
            break;
        }
        }   /* switch m_patchKind */
    }
};
static_assert(sizeof(BaselineJitCondBrLatePatchRecord) == 16);

class CodeBlock;

struct DeegenBaselineJitCodegenControlStruct
{
    // Where outputs are written to
    // Caller is responsible for correctly doing all the size computations and allocate enough space!
    //
    uint8_t* m_jitFastPathAddr;
    uint8_t* m_jitSlowPathAddr;
    uint8_t* m_jitDataSecAddr;
    BaselineJitCondBrLatePatchRecord* m_condBrPatchesArray;
    uint8_t* m_slowPathDataPtr;
    void* m_slowPathDataIndexArray;

    // Inputs:
    // The lower 32 bits of the input BaselineCodeBlock pointer
    //
    uint64_t m_baselineCodeBlock32;
    // The slowPathDataOffset for the first SlowPathData in BaselineCodeBlock
    //
    uint64_t m_initialSlowPathDataOffset;
    // Bytecode stream
    //
    uint8_t* m_bytecodeStream;

#ifndef NDEBUG
    // Assertions: in debug mode, the codegen function will populate these fields after codegen completes,
    // so the caller can assert that no buffer overflow has happened (which should never happen as long
    // as all the size computations are done correctly).
    //
    uint8_t* m_actualJitFastPathEnd;
    uint8_t* m_actualJitSlowPathEnd;
    uint8_t* m_actualJitDataSecEnd;
    BaselineJitCondBrLatePatchRecord* m_actualCondBrPatchesArrayEnd;
    uint8_t* m_actualSlowPathDataEnd;
    void* m_actualSlowPathDataIndexArrayEnd;
    uint64_t m_actualBaselineCodeBlock32End;
    uint64_t m_actualSlowPathDataOffsetEnd;
    uint8_t* m_actualBytecodeStreamEnd;
#endif
};

struct BaselineJitFunctionEntryLogicTraits
{
    using EmitterFn = void(*)(void* /*fastPathPtr*/, void* /*slowPathPtr*/, void* /*dataSecPtr*/);
    uint16_t m_fastPathCodeLen;
    uint16_t m_slowPathCodeLen;
    uint16_t m_dataSecCodeLen;
    EmitterFn m_emitterFn;
};
static_assert(sizeof(BaselineJitFunctionEntryLogicTraits) == 16);

// If the function takes <= threshold fixed parameters, it will use the specialized function entry logic implementation
// In debug mode, make the threshold smaller so there is more chance to test the generic implementation
//
constexpr size_t x_baselineJitFunctionEntrySpecializeThresholdForNonVarargsFunction = x_isDebugBuild ? 3 : 10;
constexpr size_t x_baselineJitFunctionEntrySpecializeThresholdForVarargsFunction = x_isDebugBuild ? 3 : 10;

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
                                                          uint16_t icTraitKind);

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
    void* WARN_UNUSED Insert(uint16_t traitKind);
};
static_assert(sizeof(JitGenericInlineCacheSite) == 6);

// TODO: tune
//
constexpr size_t x_maxJitGenericInlineCacheEntries = 5;

class BaselineCodeBlock;

BaselineCodeBlock* NO_INLINE deegen_baseline_jit_do_codegen(CodeBlock* cb);

struct BaselineCodeBlockAndEntryPoint
{
    // Member order hard-coded as we directly access it as (ptr, ptr) from LLVM
    //
    BaselineCodeBlock* baselineCodeBlock;
    void* entryPoint;
};

// Tier-up from interpreter to baseline JIT at a function entry
//
extern "C" BaselineCodeBlockAndEntryPoint NO_INLINE WARN_UNUSED deegen_prepare_tier_up_into_baseline_jit(CodeBlock* cb);

// Tier-up from interpreter to baseline JIT at any point within a function
// Returns the entry point corresponding to 'curBytecode'
//
extern "C" BaselineCodeBlockAndEntryPoint NO_INLINE WARN_UNUSED deegen_prepare_osr_entry_into_baseline_jit(CodeBlock* cb, void* curBytecode);
