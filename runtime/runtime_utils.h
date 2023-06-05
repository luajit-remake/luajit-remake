#pragma once

#include "common_utils.h"
#include "lj_strfmt_num.h"
#include "lj_strscan.h"
#include "memory_ptr.h"
#include "vm.h"
#include "structure.h"
#include "table_object.h"
#include "spds_doubly_linked_list.h"
#include "baseline_jit_codegen_helper.h"

class IRNode
{
public:
    virtual ~IRNode() { }

};

class IRLogicalVariable
{
public:

};

class IRBasicBlock
{
public:
    std::vector<IRNode*> m_nodes;
    std::vector<IRNode*> m_varAtHead;
    std::vector<IRNode*> m_varAvailableAtTail;
};

class IRConstant : public IRNode
{
public:

};

class IRGetLocal : public IRNode
{
public:
    int m_slot;
    IRLogicalVariable* m_vinfo;
};

class IRSetLocal : public IRNode
{
public:
    IRNode* m_value;
    int m_slot;
    IRLogicalVariable* m_vinfo;
};

class IRAdd : public IRNode
{
public:
    IRNode* m_lhs;
    IRNode* m_rhs;
};

class IRReturn : public IRNode
{
public:
    IRNode* m_value;
};

class IRCheckIsConstant : public IRNode
{
public:
    IRNode* m_value;
    TValue m_constant;
};

class StackFrameHeader;
class CodeBlock;

class Upvalue;

inline void NO_RETURN WriteBarrierSlowPath(void* /*obj*/, uint8_t* /*cellState*/)
{
    // TODO: implement
    ReleaseAssert(false && "unimplemented");
}

template<size_t cellStateOffset, typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, uint8_t>>>
void NO_INLINE WriteBarrierSlowPathEnter(T ptr)
{
    uint8_t* raw = TranslateToRawPointer(ptr);
    WriteBarrierSlowPath(raw, raw + cellStateOffset);
}

template void NO_INLINE WriteBarrierSlowPathEnter<offsetof_member_v<&UserHeapGcObjectHeader::m_cellState>, uint8_t*, void>(uint8_t* ptr);
template void NO_INLINE WriteBarrierSlowPathEnter<offsetof_member_v<&UserHeapGcObjectHeader::m_cellState>, HeapPtr<uint8_t>, void>(HeapPtr<uint8_t> ptr);
template void NO_INLINE WriteBarrierSlowPathEnter<offsetof_member_v<&SystemHeapGcObjectHeader::m_cellState>, uint8_t*, void>(uint8_t* ptr);
template void NO_INLINE WriteBarrierSlowPathEnter<offsetof_member_v<&SystemHeapGcObjectHeader::m_cellState>, HeapPtr<uint8_t>, void>(HeapPtr<uint8_t> ptr);

template<size_t cellStateOffset, typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, uint8_t>>>
void ALWAYS_INLINE WriteBarrierImpl(T ptr)
{
    uint8_t cellState = ptr[cellStateOffset];
    constexpr uint8_t blackThreshold = 0;
    if (likely(cellState > blackThreshold))
    {
        return;
    }
    WriteBarrierSlowPathEnter<cellStateOffset>(ptr);
}

template<typename T>
void WriteBarrier(T ptr)
{
    static_assert(std::is_pointer_v<T>);
    using RawType = std::remove_pointer_t<remove_heap_ptr_t<T>>;
    static_assert(std::is_same_v<value_type_of_member_object_pointer_t<decltype(&RawType::m_cellState)>, GcCellState>);
    constexpr size_t x_offset = offsetof_member_v<&RawType::m_cellState>;
    static_assert(x_offset == offsetof_member_v<&UserHeapGcObjectHeader::m_cellState> || x_offset == offsetof_member_v<&SystemHeapGcObjectHeader::m_cellState>);
    WriteBarrierImpl<x_offset>(ReinterpretCastPreservingAddressSpace<uint8_t*>(ptr));
}

struct CoroutineStatus
{
    // Must start with the coroutine distinguish-bit set because this class occupies the ArrayType field
    //
    constexpr CoroutineStatus() : m_asValue(ArrayType::x_coroutineTypeTag) { }
    explicit constexpr CoroutineStatus(uint8_t value) : m_asValue(value) { }

    // True iff it is legal to use 'coroutine.resume' on this coroutine.
    //
    using BFM_isResumable = BitFieldMember<uint8_t, bool /*type*/, 0 /*start*/, 1 /*width*/>;
    constexpr bool IsResumable() { return BFM_isResumable::Get(m_asValue); }
    constexpr void SetResumable(bool v) { return BFM_isResumable::Set(m_asValue, v); }

    // True iff this coroutine is dead (has either finished execution or thrown an error)
    // Note that if IsDead() == true, IsResumable() must be false
    //
    using BFM_isDead = BitFieldMember<uint8_t, bool /*type*/, 1 /*start*/, 1 /*width*/>;
    constexpr bool IsDead() { return BFM_isDead::Get(m_asValue); }
    constexpr void SetDead(bool v) { return BFM_isDead::Set(m_asValue, v); }

    // To summarize, the possible states are:
    // (1) IsResumable() => suspended coroutine
    // (2) IsDead() => dead coroutine (finished execution or errored out)
    // (3) !IsResumable() && !IsDead() => a coroutine in the stack of the active coroutines
    //

    constexpr bool IsCoroutineObject() { return (m_asValue & ArrayType::x_coroutineTypeTag) > 0; }

    static constexpr CoroutineStatus CreateInitStatus()
    {
        CoroutineStatus res;
        res.SetResumable(true);
        res.SetDead(false);
        return res;
    }

    // Load the ArrayType field of any object, and use this function to validate that the object is
    // indeed a coroutine object and the coroutine object is resumable in one branch
    //
    static constexpr bool WARN_UNUSED IsCoroutineObjectAndResumable(uint8_t arrTypeField)
    {
        constexpr uint8_t maskToCheck = (ArrayType::x_coroutineTypeTag | BFM_isResumable::x_maskForGet);
        bool result = (arrTypeField & maskToCheck) == maskToCheck;
        AssertIff(result, CoroutineStatus { arrTypeField }.IsCoroutineObject() && CoroutineStatus { arrTypeField }.IsResumable());
        return result;
    }

    uint8_t m_asValue;
};
// Must be one byte because it occupies the ArrayType field
//
static_assert(sizeof(CoroutineStatus) == 1);

class alignas(8) CoroutineRuntimeContext
{
public:
    static constexpr uint32_t x_hiddenClassForCoroutineRuntimeContext = 0x10;
    static constexpr size_t x_defaultStackSlots = 4096;
    static constexpr size_t x_rootCoroutineDefaultStackSlots = 16384;
    static constexpr size_t x_stackOverflowProtectionAreaSize = 65536;
    static_assert(x_stackOverflowProtectionAreaSize % VM::x_pageSize == 0);

    static CoroutineRuntimeContext* Create(VM* vm, UserHeapPointer<TableObject> globalObject, size_t numStackSlots = x_defaultStackSlots);

    void CloseUpvalues(TValue* base);

    uint32_t m_hiddenClass;  // Always x_hiddenClassForCoroutineRuntimeContext
    HeapEntityType m_type;
    GcCellState m_cellState;
    uint8_t m_reserved1;
    CoroutineStatus m_coroutineStatus;

    // The stack base of the suspend point.
    // This field is valid for all non-dead coroutine except the currently running one.
    //
    // The call frame corresponding to the stack base must be one of the following:
    // A coroutine_resume call frame, for coroutines that suspended itself by resuming another coroutine
    // A coroutine_yield call frame, for coroutines that actively suspended itself by yielding
    // A coroutine_init dummy call frame, for couroutines that have not yet started running
    //
    // The arguments passed to reenter the coroutine should always be stored starting at m_suspendPointStackBase.
    // To reenter the coroutine, one should call the return continuation of the StackFrameHeader.
    //
    TValue* m_suspendPointStackBase;

    // If this coroutine is in the stack of active coroutines (that is, !IsDead() && !IsResumable()),
    // this stores the coroutine that resumed this coroutine. For the root coroutine, this is nullptr.
    // For non-active coroutines, the value in this field is undefined.
    //
    CoroutineRuntimeContext* m_parent;

    // slot [m_variadicRetSlotBegin + ord] holds variadic return value 'ord'
    // TODO: maybe this can be directly stored in CPU register since it must be consumed by immediate next bytecode?
    //
    uint32_t m_numVariadicRets;
    int32_t m_variadicRetSlotBegin;

    // The linked list head of the list of open upvalues
    //
    UserHeapPointer<Upvalue> m_upvalueList;

    // The global object of this coroutine
    //
    UserHeapPointer<TableObject> m_globalObject;

    // The beginning of the stack
    //
    TValue* m_stackBegin;
};

UserHeapPointer<TableObject> CreateGlobalObject(VM* vm);

// Base class for some executable, either an intrinsic, or a bytecode function with some fixed global object, or a user C function
//
class ExecutableCode : public SystemHeapGcObjectHeader
{
public:
    bool IsIntrinsic() const { return m_bytecode == nullptr; }
    bool IsUserCFunction() const { return reinterpret_cast<intptr_t>(m_bytecode) < 0; }
    bool IsBytecodeFunction() const { return reinterpret_cast<intptr_t>(m_bytecode) > 0; }

    // TODO: make it the real prototype
    //
    using UserCFunctionPrototype = int(*)(void*);

    UserCFunctionPrototype GetCFunctionPtr() const
    {
        assert(IsUserCFunction());
        return reinterpret_cast<UserCFunctionPrototype>(~reinterpret_cast<uintptr_t>(m_bytecode));
    }

    static SystemHeapPointer<ExecutableCode> WARN_UNUSED CreateCFunction(VM* vm, void* fn)
    {
        HeapPtr<ExecutableCode> e = vm->AllocFromSystemHeap(static_cast<uint32_t>(sizeof(ExecutableCode))).AsNoAssert<ExecutableCode>();
        SystemHeapGcObjectHeader::Populate(e);
        e->m_hasVariadicArguments = true;
        e->m_numFixedArguments = 0;
        e->m_bytecode = reinterpret_cast<uint8_t*>(~reinterpret_cast<uintptr_t>(fn));
        e->m_bestEntryPoint = fn;
        return e;
    }

    uint8_t m_reserved;

    // The # of fixed arguments and whether it accepts variadic arguments
    // User C function always have m_numFixedArguments == 0 and m_hasVariadicArguments == true
    //
    bool m_hasVariadicArguments;
    uint32_t m_numFixedArguments;

    // This is nullptr iff it is an intrinsic, and negative iff it is a user-provided C function
    // TODO: I don't think this field is needed any more..
    //
    uint8_t* m_bytecode;

    // For intrinsic, this is the entrypoint of the intrinsic function
    // For bytecode function, this is the most optimized implementation (interpreter or some JIT tier)
    // For user C function, this is a trampoline that calls the function
    // The 'codeBlock' parameter and 'curBytecode' parameter is not needed for intrinsic or JIT but we have them anyway for a unified interface
    //
    void* m_bestEntryPoint;
};
static_assert(sizeof(ExecutableCode) == 24);

class BaselineCodeBlock;
class FLOCodeBlock;

class UpvalueMetadata
{
public:
#ifndef NDEBUG
    // Whether 'm_isImmutable' field has been properly finalized, for assertion purpose only
    //
    bool m_immutabilityFieldFinalized;
#endif
    // If true, m_slot should be interpreted as the slot ordinal in parent's stack frame.
    // If false, m_slot should be interpreted as the upvalue ordinal of the parent.
    //
    bool m_isParentLocal;
    // Whether this upvalue is immutable. Currently only filled when m_isParentLocal == true.
    //
    bool m_isImmutable;
    // Where this upvalue points to.
    //
    uint32_t m_slot;
};

// Describes one entry of JIT call inline cache
//
// Each CodeBlock keeps a circular doubly-linked list of all the call IC entries caching on it,
// so that it can update the codePtr for all of them when tiering up or when the JIT code is jettisoned
// CodeBlock never invalidate any call IC. It only updates their CodePtr.
//
// If the IC is not caching on a CodeBlock (but a C function, for example), its DoublyLinkedListNode is not linked.
//
// Each call site that employs IC keeps a singly-linked list of all the IC entries it owns,
// so that it can know the cached targets, do invalidatation when transitioning from direct-call mode to closure-call mode,
// and to reclaim memory when the JIT code is jettisoned
//
// This struct always resides in the VM short-pointer data structure region
//
class JitCallInlineCacheEntry final : public SpdsDoublyLinkedListNode<JitCallInlineCacheEntry>
{
public:
    // The singly-linked list anchored at the callsite, 0 if last node
    //
    SpdsPtr<JitCallInlineCacheEntry> m_callSiteNextNode;

    // If this IC is a direct-call IC, this is the FunctionObject being cached on
    // If this IC is a closure-call IC, this is the ExecutableCode being cached on
    //
    GeneralHeapPointer<void> m_entity;

    // High 16 bits: index into the prebuilt IC trait table, to access all sorts of traits about this IC
    // Lower 48 bits: pointer to the JIT code piece
    //
    uint64_t m_taggedPtr;

    // Get the ExecutableCode of the function target cached by this IC
    //
    ExecutableCode* WARN_UNUSED GetTargetExecutableCode(VM* vm);
    ExecutableCode* WARN_UNUSED GetTargetExecutableCodeKnowingDirectCall(VM* vm);

    size_t WARN_UNUSED GetIcTraitKind()
    {
        return m_taggedPtr >> 48;
    }

    const JitCallInlineCacheTraits* WARN_UNUSED GetIcTrait()
    {
        return deegen_jit_call_inline_cache_trait_table[GetIcTraitKind()];
    }

    uint8_t* WARN_UNUSED GetJitRegionStart()
    {
        return reinterpret_cast<uint8_t*>(m_taggedPtr & ((1ULL << 48) - 1));
    }

    static JitCallInlineCacheEntry* WARN_UNUSED Create(VM* vm,
                                                       ExecutableCode* targetExecutableCode,
                                                       SpdsPtr<JitCallInlineCacheEntry> callSiteNextNode,
                                                       HeapPtr<void> entity,
                                                       size_t icTraitKind);

    // Note that this function removes the IC from the doubly-linked list (anchored at the CodeBlock) as needed,
    // but doesn't do anything about the singly-linked list (anchored at the call site)!
    //
    // So the only valid use case for this function is when a call site decides to destroy all the IC it owns.
    //
    void Destroy(VM* vm);

    // Update the target function entry point for this IC
    //
    // diff := (uint64_t)newCodePtr - (uint64_t)oldCodePtr
    //
    void UpdateTargetFunctionCodePtr(uint64_t diff)
    {
        const JitCallInlineCacheTraits* trait = GetIcTrait();
        uint8_t* jitBaseAddr = GetJitRegionStart();
        size_t numPatches = trait->m_numCodePtrUpdatePatches;
        assert(numPatches > 0);
        size_t i = 0;
        do {
            uint8_t* addr = jitBaseAddr + trait->m_codePtrPatchRecords[i].m_offset;
            if (trait->m_codePtrPatchRecords[i].m_is64)
            {
                UnalignedStore<uint64_t>(addr, UnalignedLoad<uint64_t>(addr) + diff);
            }
            else
            {
                UnalignedStore<uint32_t>(addr, UnalignedLoad<uint32_t>(addr) + static_cast<uint32_t>(diff));
            }
            i++;
        } while (unlikely(i < numPatches));
    }
};
static_assert(sizeof(JitCallInlineCacheEntry) == 24);

// Describes one call site in JIT'ed code that employs inline caching
// Note that this must have a 1-byte alignment since this struct currently lives in the SlowPathData stream
//
struct __attribute__((__packed__, __aligned__(1))) JitCallInlineCacheSite
{
    static constexpr size_t x_maxEntries = x_maxJitCallInlineCacheEntries;

    // Try to keep this a zero initialization to avoid unnecessary work..
    //
    JitCallInlineCacheSite()
        : m_linkedListHead(SpdsPtr<JitCallInlineCacheEntry> { 0 })
        , m_numEntries(0)
        , m_mode(Mode::DirectCall)
        , m_bloomFilter(0)
    { }

    // The singly-linked list head of all the IC entries owned by this site
    //
    Packed<SpdsPtr<JitCallInlineCacheEntry>> m_linkedListHead;

    // The total number of IC entries
    //
    uint8_t m_numEntries;

    enum class Mode : uint8_t
    {
        DirectCall,
        ClosureCall,
        // When we transit from direct-call mode to closure-call mode, we invalidate all IC and start back from one IC (the target we just seen)
        // So the information of whether we have already seen more than one call targets is temporarily lost.
        // However, this information is useful for the higher tiers.
        // So we record this info here: it means even if there is only one IC entry, we actually have seen more.
        //
        ClosureCallWithMoreThanOneTargetObserved
    };

    // Whether this site is in direct-call or closure-call mode
    //
    Mode m_mode;

    // When in direct-call mode, a mini bloom filter recording all the ExecutableCode pointers cached by the ICs,
    // so we can usually rule out transition to closure-call mode without iterating through all the ICs
    //
    // With 2 hash functions, false positive rate with 3/4/5 existing items is 9.8% / 15.5% / 21.6% respectively
    //
    uint16_t m_bloomFilter;

    bool WARN_UNUSED ObservedNoTarget()
    {
        AssertImp(m_numEntries == 0, m_mode == Mode::DirectCall);
        return m_numEntries == 0;
    }

    bool WARN_UNUSED ObservedExactlyOneTarget()
    {
        return m_numEntries == 1 && m_mode != Mode::ClosureCallWithMoreThanOneTargetObserved;
    }

    // May only be called if m_numEntries < x_maxEntries and m_mode == DirectCall
    // This function handles everything except actually JIT'ting code
    //
    // Returns the address to populate JIT code
    //
    // Note that only dcIcTraitKind is passed in, because ccIcTraitKind for one IC site is always dcIcTraitKind + 1
    //
    // 'transitedToCCMode' will be written either 0 (false) or 1 (true), we use uint8_t instead of bool to avoid subtle C++ -> LLVM ABI issues
    //
    // Use attribute 'malloc' to teach LLVM that the returned address is noalias, which is very useful due to how our codegen function is written
    //
    __attribute__((__malloc__)) void* WARN_UNUSED InsertInDirectCallMode(uint16_t dcIcTraitKind, TValue tv, uint8_t* transitedToCCMode /*out*/);

    // May only be called if m_numEntries < x_maxEntries and m_mode != DirectCall
    // This function handles everything except actually JIT'ting code
    //
    // Returns the address to populate JIT code
    //
    // Note that the passed in IcTraitKind is the DC one, not the CC one!
    //
    __attribute__((__malloc__)) void* WARN_UNUSED InsertInClosureCallMode(uint16_t dcIcTraitKind, TValue tv);
};
static_assert(sizeof(JitCallInlineCacheSite) == 8);
static_assert(alignof(JitCallInlineCacheSite) == 1);

class UnlinkedCodeBlock;

// The constant table stores all constants in one bytecode function.
// However, we cannot know if an entry is a TValue or a UnlinkedCodeBlock pointer!
// (this is due to how LuaJIT parser is designed. It can be changed, but we don't want to bother making it more complex)
// This is fine for the mutator as the bytecode will never misuse the type.
// For the GC, we rely on the crazy fact that a user-space raw pointer always falls in [0, 2^47) (high 17 bits = 0, not 1) under practically every OS kernel,
// which mean when a pointer is interpreted as a TValue, it will always be interpreted into a double, so the GC marking
// algorithm will correctly ignore it for the marking.
//
// TODO: what is the above comment? Is it still up to date? figure out later..

// This uniquely corresponds to each pair of <UnlinkedCodeBlock, GlobalObject>
// It owns the bytecode and the corresponding metadata (the bytecode is copied from the UnlinkedCodeBlock,
// we need our own copy because we do quickening, aka., dynamic bytecode opcode specialization optimization)
//
// Layout:
// [ upvalue table and constant table ] [ CodeBlock ] [ bytecode ] [ byetecode metadata ]
//
class alignas(8) CodeBlock final : public ExecutableCode
{
public:
    static CodeBlock* WARN_UNUSED Create(VM* vm, UnlinkedCodeBlock* ucb, UserHeapPointer<TableObject> globalObject);

    static constexpr size_t GetTrailingArrayOffset()
    {
        return offsetof_member_v<&CodeBlock::m_bytecodeStream>;
    }

    uint8_t* GetBytecodeStream()
    {
        return reinterpret_cast<uint8_t*>(this) + GetTrailingArrayOffset();
    }

    uintptr_t GetBytecodeMetadataStart()
    {
        return reinterpret_cast<uintptr_t>(this) + GetTrailingArrayOffset() + RoundUpToMultipleOf<8>(m_bytecodeLength);
    }

    void UpdateBestEntryPoint(void* newEntryPoint);

    UserHeapPointer<TableObject> m_globalObject;

    uint32_t m_stackFrameNumSlots;
    uint32_t m_numUpvalues;
    uint32_t m_bytecodeLength;
    uint32_t m_bytecodeMetadataLength;

    BaselineCodeBlock* m_baselineCodeBlock;

    // When this counter becomes negative, the function will tier up to baseline JIT
    //
    int64_t m_interpreterTierUpCounter;

    FLOCodeBlock* m_floCodeBlock;

    UnlinkedCodeBlock* m_owner;

    // All JIT call inline caches that cache on this CodeBlock
    //
    SpdsDoublyLinkedList<JitCallInlineCacheEntry> m_jitCallIcList;

    uint64_t m_bytecodeStream[0];
};

// This is just x_num_bytecode_metadata_struct_kinds
// However, unfortunately we have to make it a extern const here due to header file dependency issue...
//
extern const size_t x_num_bytecode_metadata_struct_kinds_;

namespace DeegenBytecodeBuilder { class BytecodeBuilder; }

// This uniquely corresponds to a piece of source code that defines a function
//
class UnlinkedCodeBlock : public SystemHeapGcObjectHeader
{
public:
    static UnlinkedCodeBlock* WARN_UNUSED Create(VM* vm, HeapPtr<TableObject> globalObject)
    {
        size_t sizeToAllocate = RoundUpToMultipleOf<8>(GetTrailingArrayOffset() + x_num_bytecode_metadata_struct_kinds_ * sizeof(uint16_t));
        uint8_t* addressBegin = TranslateToRawPointer(vm, vm->AllocFromSystemHeap(static_cast<uint32_t>(sizeToAllocate)).AsNoAssert<uint8_t>());
        UnlinkedCodeBlock* ucb = reinterpret_cast<UnlinkedCodeBlock*>(addressBegin);
        SystemHeapGcObjectHeader::Populate(ucb);
        ucb->m_uvFixUpCompleted = false;
        ucb->m_defaultGlobalObject = globalObject;
        ucb->m_rareGOtoCBMap = nullptr;
        ucb->m_parent = nullptr;
        ucb->m_defaultCodeBlock = nullptr;
        ucb->m_parserUVGetFixupList = nullptr;
        return ucb;
    }

    static constexpr size_t GetTrailingArrayOffset()
    {
        return offsetof_member_v<&UnlinkedCodeBlock::m_bytecodeMetadataUseCounts>;
    }

    CodeBlock* WARN_UNUSED ALWAYS_INLINE GetCodeBlock(UserHeapPointer<TableObject> globalObject)
    {
        if (likely(globalObject == m_defaultGlobalObject))
        {
            assert(m_defaultCodeBlock != nullptr);
            return m_defaultCodeBlock;
        }
        return GetCodeBlockSlowPath(globalObject);
    }

    CodeBlock* WARN_UNUSED NO_INLINE GetCodeBlockSlowPath(UserHeapPointer<TableObject> globalObject)
    {
        if (unlikely(m_rareGOtoCBMap == nullptr))
        {
            m_rareGOtoCBMap = new RareGlobalObjectToCodeBlockMap;
        }
        auto iter = m_rareGOtoCBMap->find(globalObject.m_value);
        if (unlikely(iter == m_rareGOtoCBMap->end()))
        {
            VM* vm = VM::GetActiveVMForCurrentThread();
            CodeBlock* newCb = CodeBlock::Create(vm, this /*ucb*/, globalObject);
            (*m_rareGOtoCBMap)[globalObject.m_value] = newCb;
            return newCb;
        }
        else
        {
            return iter->second;
        }
    }

    void* WARN_UNUSED GetInterpreterEntryPoint();

    // For assertion purpose only
    //
    bool m_uvFixUpCompleted;
    bool m_hasVariadicArguments;
    uint32_t m_numFixedArguments;

    UserHeapPointer<TableObject> m_defaultGlobalObject;
    CodeBlock* m_defaultCodeBlock;
    using RareGlobalObjectToCodeBlockMap = std::unordered_map<int64_t, CodeBlock*>;
    RareGlobalObjectToCodeBlockMap* m_rareGOtoCBMap;

    uint8_t* m_bytecode;
    UpvalueMetadata* m_upvalueInfo;
    uint64_t* m_cstTable;
    UnlinkedCodeBlock* m_parent;

    uint32_t m_bytecodeLength;
    uint32_t m_cstTableLength;
    uint32_t m_numUpvalues;
    uint32_t m_bytecodeMetadataLength;
    uint32_t m_stackFrameNumSlots;

    // Only used during parsing. Always nullptr at runtime.
    // It doesn't have to sit in this struct but the memory consumption of this struct simply shouldn't matter.
    //
    DeegenBytecodeBuilder::BytecodeBuilder* m_bytecodeBuilder;
    std::vector<uint32_t>* m_parserUVGetFixupList;

    // The actual length of this trailing array is always x_num_bytecode_metadata_struct_kinds_
    //
    uint16_t m_bytecodeMetadataUseCounts[0];
};

// Layout:
// [ BaselineCodeBlock ] [ slowPathDataIndex ] [ slowPathData ]
//
// slowPathDataIndex:
//     SlowPathDataAndBytecodeOffset[N] where N is the # of bytecodes in this function.
//     Every SlowPathDataAndBytecodeOffset item records the offset of the bytecode / slowPathData in the
//     bytecode / slowPathData stream for one bytecode
// slowPathData:
//     It is similar to bytecode, but contains more information, which are needed for the JIT slow path
//     (e.g., the JIT code address to jump to if a branch is needed).
//
class alignas(8) BaselineCodeBlock
{
public:
    static BaselineCodeBlock* WARN_UNUSED Create(CodeBlock* cb,
                                                 uint32_t numBytecodes,
                                                 uint32_t slowPathDataStreamLength,
                                                 void* jitCodeEntry,
                                                 void* jitRegionStart,
                                                 uint32_t jitRegionSize);

    static constexpr size_t GetTrailingArrayOffset()
    {
        return offsetof_member_v<&BaselineCodeBlock::m_sbIndex>;
    }

    // The layout of this struct is currently hardcoded
    // If you change this, be sure to make corresponding changes in DeegenBytecodeBaselineJitInfo
    //
    struct alignas(8) SlowPathDataAndBytecodeOffset
    {
        // Note that this offset is relative to the BaselineCodeBlock pointer
        //
        uint32_t m_slowPathDataOffset;
        // This is the lower 32 bits of m_owner's bytecode pointer
        //
        uint32_t m_bytecodePtr32;
    };

    // Return the SlowPathData pointer for the given bytecode index (not bytecode offset!)
    //
    uint8_t* WARN_UNUSED GetSlowPathDataAtBytecodeIndex(size_t index)
    {
        assert(index < m_numBytecodes);
        size_t offset = m_sbIndex[index].m_slowPathDataOffset;
        return reinterpret_cast<uint8_t*>(this) + offset;
    }

    uint8_t* WARN_UNUSED GetSlowPathDataStreamStart()
    {
        uintptr_t addr = reinterpret_cast<uintptr_t>(this);
        addr += GetTrailingArrayOffset();
        addr += sizeof(SlowPathDataAndBytecodeOffset) * m_numBytecodes;
        return reinterpret_cast<uint8_t*>(addr);
    }

    // The bytecodePtr32 must be valid.
    //
    // For now, this is simply implemented by a O(log n) binary search.
    //
    size_t WARN_UNUSED GetBytecodeIndexFromBytecodePtrLower32Bits(uint32_t bytecodePtr32)
    {
        uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m_owner));
        uint32_t targetOffset = bytecodePtr32 - base;
        assert(m_owner->GetTrailingArrayOffset() <= targetOffset && targetOffset < m_owner->m_bytecodeLength + m_owner->GetTrailingArrayOffset());
        assert(m_numBytecodes > 0);
        size_t left = 0, right = m_numBytecodes - 1;
        while (left < right)
        {
            size_t mid = (left + right) / 2;
            uint32_t value = m_sbIndex[mid].m_bytecodePtr32 - base;
            if (targetOffset == value)
            {
                assert(m_sbIndex[mid].m_bytecodePtr32 == bytecodePtr32);
                return mid;
            }
            if (targetOffset < value)
            {
                assert(mid > 0);
                right = mid - 1;
            }
            else
            {
                left = mid + 1;
            }
        }
        assert(left == right);
        assert(m_sbIndex[left].m_bytecodePtr32 == bytecodePtr32);
        return left;
    }

    // The bytecodePtr must be a valid bytecode pointer in m_owner's bytecode stream
    //
    size_t WARN_UNUSED GetBytecodeIndexFromBytecodePtr(void* bytecodePtr)
    {
        assert(m_owner->GetBytecodeStream() <= bytecodePtr && bytecodePtr < m_owner->GetBytecodeStream() + m_owner->m_bytecodeLength);
        return GetBytecodeIndexFromBytecodePtrLower32Bits(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bytecodePtr)));
    }

    // Currently the JIT code is layouted as follow:
    //     [ Data Section ] [ FastPath Code ] [ SlowPath Code ]
    //
    void* m_jitCodeEntry;

    CodeBlock* m_owner;
    uint32_t m_numBytecodes;
    uint32_t m_slowPathDataStreamLength;

    // The JIT region is [m_jitRegionStart, m_jitRegionStart + m_jitRegionSize)
    //
    void* m_jitRegionStart;
    uint32_t m_jitRegionSize;

    SlowPathDataAndBytecodeOffset m_sbIndex[0];
};

class FunctionObject;

class Upvalue
{
public:
    static HeapPtr<Upvalue> WARN_UNUSED CreateUpvalueImpl(UserHeapPointer<Upvalue> prev, TValue* dst, bool isImmutable)
    {
        VM* vm = VM::GetActiveVMForCurrentThread();
        HeapPtr<Upvalue> r = vm->AllocFromUserHeap(static_cast<uint32_t>(sizeof(Upvalue))).AsNoAssert<Upvalue>();
        UserHeapGcObjectHeader::Populate(r);
        r->m_hiddenClass.m_value = x_hiddenClassForUpvalue;
        r->m_ptr = dst;
        r->m_isClosed = false;
        r->m_isImmutable = isImmutable;
        TCSet(r->m_prev, prev);
        return r;
    }

    static HeapPtr<Upvalue> WARN_UNUSED CreateClosed(VM* vm, TValue val)
    {
        HeapPtr<Upvalue> r = vm->AllocFromUserHeap(static_cast<uint32_t>(sizeof(Upvalue))).AsNoAssert<Upvalue>();
        Upvalue* raw = TranslateToRawPointer(vm, r);
        UserHeapGcObjectHeader::Populate(raw);
        raw->m_hiddenClass.m_value = x_hiddenClassForUpvalue;
        raw->m_ptr = &raw->m_tv;
        raw->m_tv = val;
        raw->m_isClosed = true;
        raw->m_isImmutable = true;
        return r;
    }

    static HeapPtr<Upvalue> WARN_UNUSED Create(CoroutineRuntimeContext* rc, TValue* dst, bool isImmutable)
    {
        if (rc->m_upvalueList.m_value == 0 || rc->m_upvalueList.As()->m_ptr < dst)
        {
            // Edge case: the open upvalue list is empty, or the upvalue shall be inserted as the first element in the list
            //
            HeapPtr<Upvalue> newNode = CreateUpvalueImpl(rc->m_upvalueList /*prev*/, dst, isImmutable);
            rc->m_upvalueList = newNode;
            WriteBarrier(rc);
            return newNode;
        }
        else
        {
            // Invariant: after the loop, the node shall be inserted between 'cur' and 'prev'
            //
            HeapPtr<Upvalue> cur = rc->m_upvalueList.As();
            TValue* curVal = cur->m_ptr;
            UserHeapPointer<Upvalue> prev;
            while (true)
            {
                assert(!cur->m_isClosed);
                assert(dst <= curVal);
                if (curVal == dst)
                {
                    // We found an open upvalue for that slot, we are good
                    //
                    return cur;
                }

                prev = TCGet(cur->m_prev);
                if (prev.m_value == 0)
                {
                    // 'cur' is the last node, so we found the insertion location
                    //
                    break;
                }

                assert(!prev.As()->m_isClosed);
                TValue* prevVal = prev.As()->m_ptr;
                assert(prevVal < curVal);
                if (prevVal < dst)
                {
                    // prevVal < dst < curVal, so we found the insertion location
                    //
                    break;
                }

                cur = prev.As();
                curVal = prevVal;
            }

            assert(curVal == cur->m_ptr);
            assert(prev == TCGet(cur->m_prev));
            assert(dst < curVal);
            assert(prev.m_value == 0 || prev.As()->m_ptr < dst);
            HeapPtr<Upvalue> newNode = CreateUpvalueImpl(prev, dst, isImmutable);
            TCSet(cur->m_prev, UserHeapPointer<Upvalue>(newNode));
            WriteBarrier(cur);
            return newNode;
        }
    }

    void Close()
    {
        assert(!m_isClosed);
        assert(m_ptr != &m_tv);
        m_tv = *m_ptr;
        m_ptr = &m_tv;
        m_isClosed = true;
    }

    static constexpr int32_t x_hiddenClassForUpvalue = 0x18;

    // TODO: we could have made this structure 16 bytes instead of 32 bytes by making m_ptr a GeneralHeapPointer and takes the place of m_hiddenClass
    // (normally this is a bit risky as it might confuse all sort of things (like IC), but upvalue is so special: it is never exposed to user,
    // so an Upvalue object will never be used as operand into any bytecode instruction other than the upvalue-dedicated ones, so we are fine).
    // However, we are not doing this now because our stack is currently not placed in the VM memory range.
    //
    SystemHeapPointer<void> m_hiddenClass;
    HeapEntityType m_type;
    GcCellState m_cellState;
    // Always equal to (m_ptr == &m_u.tv)
    //
    bool m_isClosed;
    bool m_isImmutable;

    // Points to &tv for closed upvalue, or the stack slot for open upvalue
    // All the open values are chained into a linked list (through prev) in reverse sorted order of m_ptr (i.e. absolute stack slot from high to low)
    //
    TValue* m_ptr;
    // Stores the value for closed upvalue
    //
    TValue m_tv;
    // Stores the linked list if the upvalue is open
    //
    UserHeapPointer<Upvalue> m_prev;
};
static_assert(sizeof(Upvalue) == 32);

inline void CoroutineRuntimeContext::CloseUpvalues(TValue* base)
{
    VM* vm = VM::GetActiveVMForCurrentThread();
    UserHeapPointer<Upvalue> cur = m_upvalueList;
    while (cur.m_value != 0)
    {
        if (cur.As()->m_ptr < base)
        {
            break;
        }
        assert(!cur.As()->m_isClosed);
        Upvalue* uv = TranslateToRawPointer(vm, cur.As());
        cur = uv->m_prev;
        assert(cur.m_value == 0 || cur.As()->m_ptr < uv->m_ptr);
        uv->Close();
    }
    m_upvalueList = cur;
    if (cur.m_value != 0)
    {
        WriteBarrier(this);
    }
}

class FunctionObject
{
public:
    // Does not fill 'm_executable' or upvalue array
    //
    static UserHeapPointer<FunctionObject> WARN_UNUSED CreateImpl(VM* vm, uint8_t numUpvalues)
    {
        size_t sizeToAllocate = GetTrailingArrayOffset() + sizeof(TValue) * numUpvalues;
        sizeToAllocate = RoundUpToMultipleOf<8>(sizeToAllocate);
        HeapPtr<FunctionObject> r = vm->AllocFromUserHeap(static_cast<uint32_t>(sizeToAllocate)).AsNoAssert<FunctionObject>();
        UserHeapGcObjectHeader::Populate(r);

        r->m_numUpvalues = numUpvalues;
        r->m_invalidArrayType = ArrayType::x_invalidArrayType;
        return r;
    }

    // Does not fill upvalues
    //
    static UserHeapPointer<FunctionObject> WARN_UNUSED Create(VM* vm, CodeBlock* cb)
    {
        uint32_t numUpvalues = cb->m_numUpvalues;
        assert(numUpvalues <= std::numeric_limits<uint8_t>::max());
        UserHeapPointer<FunctionObject> r = CreateImpl(vm, static_cast<uint8_t>(numUpvalues));
        SystemHeapPointer<ExecutableCode> executable { static_cast<ExecutableCode*>(cb) };
        TCSet(r.As()->m_executable, executable);
        return r;
    }

    static UserHeapPointer<FunctionObject> WARN_UNUSED CreateCFunc(VM* vm, SystemHeapPointer<ExecutableCode> executable, uint8_t numUpvalues = 0)
    {
        assert(TranslateToRawPointer(executable.As())->IsUserCFunction());
        UserHeapPointer<FunctionObject> r = CreateImpl(vm, numUpvalues);
        TCSet(r.As()->m_executable, executable);
        return r;
    }

    // DEVNOTE: C library function must not use this function.
    //
    static bool ALWAYS_INLINE IsUpvalueImmutable(HeapPtr<FunctionObject> self, size_t ord)
    {
        assert(ord < self->m_numUpvalues);
        assert(TranslateToRawPointer(TCGet(self->m_executable).As())->IsBytecodeFunction());
        HeapPtr<CodeBlock> cb = static_cast<HeapPtr<CodeBlock>>(TCGet(self->m_executable).As());
        assert(cb->m_numUpvalues == self->m_numUpvalues && cb->m_owner->m_numUpvalues == self->m_numUpvalues);
        assert(cb->m_owner->m_upvalueInfo[ord].m_immutabilityFieldFinalized);
        return cb->m_owner->m_upvalueInfo[ord].m_isImmutable;
    }

    // Get the upvalue ptr for a mutable upvalue
    //
    // DEVNOTE: C library function must not use this function.
    //
    static HeapPtr<Upvalue> ALWAYS_INLINE GetMutableUpvaluePtr(HeapPtr<FunctionObject> self, size_t ord)
    {
        assert(ord < self->m_numUpvalues);
        assert(!IsUpvalueImmutable(self, ord));
        TValue tv = TCGet(self->m_upvalues[ord]);
        assert(tv.IsPointer() && tv.GetHeapEntityType() == HeapEntityType::Upvalue);
        return tv.AsPointer().As<Upvalue>();
    }

    // Get the value of an immutable upvalue
    //
    // DEVNOTE: C library function must not use this function.
    //
    static TValue ALWAYS_INLINE GetImmutableUpvalueValue(HeapPtr<FunctionObject> self, size_t ord)
    {
        assert(ord < self->m_numUpvalues);
        assert(IsUpvalueImmutable(self, ord));
        TValue tv = TCGet(self->m_upvalues[ord]);
        assert(!(tv.IsPointer() && tv.GetHeapEntityType() == HeapEntityType::Upvalue));
        return tv;
    }

    // Get the value of an upvalue, works no matter if the upvalue is mutable or immutable.
    // Of course, this is also (quite) slow.
    //
    // DEVNOTE: C library function must not use this function.
    //
    static TValue ALWAYS_INLINE GetUpvalueValue(HeapPtr<FunctionObject> self, size_t ord)
    {
        assert(ord < self->m_numUpvalues);
        if (IsUpvalueImmutable(self, ord))
        {
            return GetImmutableUpvalueValue(self, ord);
        }
        else
        {
            HeapPtr<Upvalue> uv = GetMutableUpvaluePtr(self, ord);
            return *uv->m_ptr;
        }
    }

    static TValue GetMutableUpvaluePtrOrImmutableUpvalue(HeapPtr<FunctionObject> self, size_t ord)
    {
        assert(ord < self->m_numUpvalues);
        return TCGet(self->m_upvalues[ord]);
    }

    static UserHeapPointer<FunctionObject> WARN_UNUSED NO_INLINE CreateAndFillUpvalues(CodeBlock* cb, CoroutineRuntimeContext* rc, TValue* stackFrameBase, HeapPtr<FunctionObject> parent, size_t selfOrdinalInStackFrame);

    static constexpr size_t GetTrailingArrayOffset()
    {
        return offsetof_member_v<&FunctionObject::m_upvalues>;
    }

    // Object header
    //
    // Note that a CodeBlock defines both UnlinkedCodeBlock and GlobalObject,
    // so the upvalue list does not contain the global object (if the ExecutableCode is not a CodeBlock, then the global object doesn't matter either)
    //
    SystemHeapPointer<ExecutableCode> m_executable;
    HeapEntityType m_type;
    GcCellState m_cellState;

    uint8_t m_numUpvalues;
    // Always ArrayType::x_invalidArrayType
    //
    uint8_t m_invalidArrayType;

    // The upvalue list.
    // The interpretation of each element in the list depends on whether the upvalue is immutable
    // (this information is recorded in the UnlinkedCodeBlock's upvalue metadata list):
    //
    // 1. If the upvalue is not immutable, then the TValue must be a HeapPtr<Upvalue> object,
    //    and the value of the upvalue should be read from the Upvalue object.
    // 2. If the upvalue is immutable, then the TValue must not be a HeapPtr<Upvalue> (since upvalue
    //    objects are never exposed directly to user code). The TValue itself is simply the value of the upvalue.
    //
    TValue m_upvalues[0];
};
static_assert(sizeof(FunctionObject) == 8);

inline ExecutableCode* WARN_UNUSED JitCallInlineCacheEntry::GetTargetExecutableCode(VM* vm)
{
    AssertIff(m_entity.IsUserHeapPointer(), GetIcTrait()->m_isDirectCallMode);
    ExecutableCode* ec;
    if (m_entity.IsUserHeapPointer())
    {
        assert(m_entity.As<UserHeapGcObjectHeader>()->m_type == HeapEntityType::Function);
        ec = TranslateToRawPointer(vm, TCGet(m_entity.As<FunctionObject>()->m_executable).As());
    }
    else
    {
        assert(m_entity.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::ExecutableCode);
        ec = TranslateToRawPointer(vm, m_entity.As<ExecutableCode>());
    }
    AssertIff(IsOnDoublyLinkedList(this), ec->IsBytecodeFunction());
    return ec;
}

inline ExecutableCode* WARN_UNUSED JitCallInlineCacheEntry::GetTargetExecutableCodeKnowingDirectCall(VM* vm)
{
    assert(GetIcTrait()->m_isDirectCallMode);
    assert(m_entity.IsUserHeapPointer());
    assert(m_entity.As<UserHeapGcObjectHeader>()->m_type == HeapEntityType::Function);
    ExecutableCode* ec = TranslateToRawPointer(vm, TCGet(m_entity.As<FunctionObject>()->m_executable).As());
    AssertIff(IsOnDoublyLinkedList(this), ec->IsBytecodeFunction());
    return ec;
}

// Corresponds to a file
//
class ScriptModule
{
public:
    std::string m_name;
    std::vector<UnlinkedCodeBlock*> m_unlinkedCodeBlocks;
    UserHeapPointer<TableObject> m_defaultGlobalObject;
    UserHeapPointer<FunctionObject> m_defaultEntryPoint;

    static std::unique_ptr<ScriptModule> WARN_UNUSED LegacyParseScriptFromJSONBytecodeDump(VM* vm, UserHeapPointer<TableObject> globalObject, const std::string& content);
};

// The return statement is required to fill nil up to x_minNilFillReturnValues values even if it returns less than that many values
//
constexpr uint32_t x_minNilFillReturnValues = 3;

inline void PrintTValue(FILE* fp, TValue val)
{
    if (val.IsInt32())
    {
        fprintf(fp, "%d", static_cast<int>(val.AsInt32()));
    }
    else if (val.IsDouble())
    {
        double dbl = val.AsDouble();
        char buf[x_default_tostring_buffersize_double];
        StringifyDoubleUsingDefaultLuaFormattingOptions(buf /*out*/, dbl);
        fprintf(fp, "%s", buf);
    }
    else if (val.IsMIV())
    {
        MiscImmediateValue miv = val.AsMIV();
        if (miv.IsNil())
        {
            fprintf(fp, "nil");
        }
        else
        {
            assert(miv.IsBoolean());
            fprintf(fp, "%s", (miv.GetBooleanValue() ? "true" : "false"));
        }
    }
    else
    {
        assert(val.IsPointer());
        UserHeapGcObjectHeader* p = TranslateToRawPointer(val.AsPointer<UserHeapGcObjectHeader>().As());
        if (p->m_type == HeapEntityType::String)
        {
            HeapString* hs = reinterpret_cast<HeapString*>(p);
            fwrite(hs->m_string, sizeof(char), hs->m_length /*length*/, fp);
        }
        else
        {
            if (p->m_type == HeapEntityType::Function)
            {
                fprintf(fp, "function");
            }
            else if (p->m_type == HeapEntityType::Table)
            {
                fprintf(fp, "table");
            }
            else if (p->m_type == HeapEntityType::Thread)
            {
                fprintf(fp, "thread");
            }
            else
            {
                fprintf(fp, "(type %d)", static_cast<int>(p->m_type));
            }
            fprintf(fp, ": %p", static_cast<void*>(p));
        }
    }
}

TValue WARN_UNUSED MakeErrorMessage(const char* msg);

constexpr size_t x_lua_max_nested_error_count = 50;

inline TValue WARN_UNUSED MakeErrorMessageForTooManyNestedErrors()
{
    const char* errstr = "error in error handling";
    return MakeErrorMessage(errstr);
}

TValue WARN_UNUSED MakeErrorMessageForUnableToCall(TValue badValue);

// The varg part of each inlined function can always
// be represented as a list of locals plus a suffix of the original function's varg
//
class InlinedFunctionVarArgRepresentation
{
public:
    // The prefix ordinals
    //
    std::vector<int> m_prefix;
    // The suffix of the original function's varg beginning at that ordinal (inclusive)
    //
    int m_suffix;
};

class InliningStackEntry
{
public:
    // The base ordinal of stack frame header
    //
    int m_baseOrd;
    // Number of fixed arguments for this function
    //
    int m_numArguments;
    // Number of locals for this function
    //
    int m_numLocals;
    // Varargs of this function
    //
    InlinedFunctionVarArgRepresentation m_varargs;

};

inline TValue WARN_UNUSED GetMetamethodFromMetatable(HeapPtr<TableObject> metatable, LuaMetamethodKind mtKind)
{
    GetByIdICInfo icInfo;
    TableObject::PrepareGetById(metatable, VM_GetStringNameForMetatableKind(mtKind), icInfo /*out*/);
    return TableObject::GetById(metatable, VM_GetStringNameForMetatableKind(mtKind).As<void>(), icInfo);
}

inline TValue WARN_UNUSED GetMetamethodForValue(TValue value, LuaMetamethodKind mtKind)
{
    UserHeapPointer<void> metatableMaybeNull = GetMetatableForValue(value);
    if (metatableMaybeNull.m_value != 0)
    {
        HeapPtr<TableObject> metatable = metatableMaybeNull.As<TableObject>();
        return GetMetamethodFromMetatable(metatable, mtKind);
    }
    else
    {
        return TValue::Nil();
    }
}

// This is the official Lua 5.3/5.4 implementation of the modulus operator.
// Note that the semantics of the below implementation is different from the Lua 5.1/5.2 implementation
// This implementation is here for future reference only, since we currently target Lua 5.1
//
inline double WARN_UNUSED ModulusWithLuaSemantics_PUCLuaReference_5_3(double a, double b)
{
    // Quoted from PUC Lua llimits.h:320:
    //     modulo: defined as 'a - floor(a/b)*b'; the direct computation
    //     using this definition has several problems with rounding errors,
    //     so it is better to use 'fmod'. 'fmod' gives the result of
    //     'a - trunc(a/b)*b', and therefore must be corrected when
    //     'trunc(a/b) ~= floor(a/b)'. That happens when the division has a
    //     non-integer negative result: non-integer result is equivalent to
    //     a non-zero remainder 'm'; negative result is equivalent to 'a' and
    //     'b' with different signs, or 'm' and 'b' with different signs
    //     (as the result 'm' of 'fmod' has the same sign of 'a').
    //
    double m = fmod(a, b);
    if ((m > 0) ? b < 0 : (m < 0 && b > 0)) m += b;
    return m;
}

// This is the official Lua 5.1/5.2 implementation of the modulus operator.
// It involves a call into the math library, which is slow.
// This implementation here is for reference and test purpose only.
// Internally, we use LuaJIT's a better implementation in hand-coded assembly (see a few lines below).
//
inline double WARN_UNUSED ModulusWithLuaSemantics_PUCLuaReference_5_1(double a, double b)
{
    // Code from PUC Lua 5.2 luaconf.h:436
    //
    return a - floor(a / b) * b;
}

// LuaJIT's more optimized implementation of Lua modulus operator.
// Note that this implementation is compatible with Lua 5.1/5.2, but not Lua 5.3/5.4.
//
// The assembly code is adapted from LuaJIT vm_x64.dasc
//
// I realized that LuaJIT used this implementation only because it supports legacy CPU without
// SSE4's roundsd instruction..
// Since SSE4 is released back in 2006 and supported by most processors after 2008, it should be
// reasonable to assume that we have SSE4 support.
//
// However, it seems like Lua 5.3/5.4's fmod semantics cannot be implemented by roundsd,
// so I keep this assembly around for now in case we need to adapt it for fmod later..
//
inline double WARN_UNUSED ALWAYS_INLINE ModulusWithLuaSemantics_5_1_NoSSE4(double a, double b)
{
    double fpr1, fpr2, fpr3, fpr4;
    uint64_t gpr1;
    asm (
        "movapd %[x0], %[x5];"                      // x5 = a
        "divsd %[x1], %[x0];"                       // x0 = a / b

        "movabsq $0x7FFFFFFFFFFFFFFF, %[r0];"       // x2 = bitcast<double>(0x7FFFFFFFFFFFFFFFULL)
        "movq %[r0], %[x2];"                        //

        "movabsq $0x4330000000000000, %[r0];"       // x3 = (double)2^52
        "movq %[r0], %[x3];"                        //

        "movapd %[x0], %[x4];"                      // x4 = abs(a / b)
        "andpd %[x2], %[x4];"                       //

        "ucomisd %[x4], %[x3];"                     // if (2**52 <= abs(a / b)) goto 1
        "jbe 1f;"                                   //

        "andnpd %[x0], %[x2];"                      // x2 = signmask(a / b)

        "addsd %[x3], %[x4];"                       // x4 = abs(a / b) + 2^52 - 2^52
        "subsd %[x3], %[x4];"                       //

        "orpd %[x2], %[x4];"                        // x4 = copysign(x4, x2)

        "movabsq $0x3ff0000000000000, %[r0];"       // x4 -= (x0 < x4) ? 1.0 : 0.0
        "movq %[r0], %[x2];"                        //
        "cmpltsd %[x4], %[x0];"                     //
        "andpd %[x2], %[x0];"                       //
        "subsd %[x0], %[x4];"                       //

        "movapd %[x5], %[x0];"                      // result = a - x4 * b
        "mulsd %[x4], %[x1];"                       //
        "subsd %[x1], %[x0];"                       //

        "jmp 2f;"

        "1:;"

        "mulsd %[x0], %[x1];"                       // result = a - (a / b) * b
        "movapd %[x5], %[x0];"                      //
        "subsd %[x1], %[x0];"                       //

        "2:;"
        :
            [x0] "+x"(a) /*inout*/,
            [x1] "+x"(b) /*inout*/,
            [x2] "=&x"(fpr1) /*scratch*/,
            [x3] "=&x"(fpr2) /*scratch*/,
            [x4] "=&x"(fpr3) /*scratch*/,
            [x5] "=&x"(fpr4) /*scratch*/,
            [r0] "=&r"(gpr1) /*scratch*/
        :   /*no read-only input*/
        :  "cc" /*clobber*/);

    return a;
}

inline double ALWAYS_INLINE WARN_UNUSED ModulusWithLuaSemantics(double a, double b)
{
    return ModulusWithLuaSemantics_PUCLuaReference_5_1(a, b);
}

// A wrapper around libm pow that provides a fastpath if the exponent is an integer that fits in [-128, 127).
// If not, it runs ~3% slower than libm pow due to the extra check.
//
double WARN_UNUSED math_fast_pow(double b, double ex);

struct DoBinaryOperationConsideringStringConversionResult
{
    bool success;
    double result;
};

// Lua allows crazy things like "1 " + " 0xf " (which yields 16): string can be silently converted to number (ignoring whitespace) when
// performing an arithmetic operation. This function does this job. 'func' must be a lambda (double, double) -> double
//
inline DoBinaryOperationConsideringStringConversionResult WARN_UNUSED NO_INLINE TryDoBinaryOperationConsideringStringConversion(TValue lhs, TValue rhs, LuaMetamethodKind opKind)
{
    if (likely(!lhs.Is<tString>() && !rhs.Is<tString>()))
    {
        return { .success = false };
    }

    double lhsNumber;
    if (lhs.Is<tDouble>())
    {
        lhsNumber = lhs.As<tDouble>();
    }
    else if (lhs.Is<tString>())
    {
        HeapPtr<HeapString> stringObj = lhs.AsPointer<HeapString>().As();
        StrScanResult ssr = TryConvertStringToDoubleWithLuaSemantics(TranslateToRawPointer(stringObj->m_string), stringObj->m_length);
        if (ssr.fmt == StrScanFmt::STRSCAN_NUM)
        {
            lhsNumber = ssr.d;
        }
        else
        {
            return { .success = false };
        }
    }
    else
    {
        return { .success = false };
    }

    double rhsNumber;
    if (rhs.Is<tDouble>())
    {
        rhsNumber = rhs.As<tDouble>();
    }
    else if (rhs.Is<tString>())
    {
        HeapPtr<HeapString> stringObj = rhs.AsPointer<HeapString>().As();
        StrScanResult ssr = TryConvertStringToDoubleWithLuaSemantics(TranslateToRawPointer(stringObj->m_string), stringObj->m_length);
        if (ssr.fmt == StrScanFmt::STRSCAN_NUM)
        {
            rhsNumber = ssr.d;
        }
        else
        {
            return { .success = false };
        }
    }
    else
    {
        return { .success = false };
    }

    // This is fine: string to integer coercion is already slow enough..
    // And this string-to-int coercion behavior is just some historical garbage of Lua that probably nobody ever used
    //
    switch (opKind)
    {
    case LuaMetamethodKind::Add:
        return { .success = true, .result = lhsNumber + rhsNumber };
    case LuaMetamethodKind::Sub:
        return { .success = true, .result = lhsNumber - rhsNumber };
    case LuaMetamethodKind::Mul:
        return { .success = true, .result = lhsNumber * rhsNumber };
    case LuaMetamethodKind::Div:
        return { .success = true, .result = lhsNumber / rhsNumber };
    case LuaMetamethodKind::Mod:
        return { .success = true, .result = ModulusWithLuaSemantics(lhsNumber, rhsNumber) };
    case LuaMetamethodKind::Pow:
        return { .success = true, .result = pow(lhsNumber, rhsNumber) };
    default:
        assert(false);
        __builtin_unreachable();
    }
}

inline TValue WARN_UNUSED GetMetamethodForBinaryArithmeticOperation(TValue lhs, TValue rhs, LuaMetamethodKind mtKind)
{
    {
        UserHeapPointer<void> lhsMetatableMaybeNull = GetMetatableForValue(lhs);
        if (lhsMetatableMaybeNull.m_value != 0)
        {
            HeapPtr<TableObject> metatable = lhsMetatableMaybeNull.As<TableObject>();
            GetByIdICInfo icInfo;
            TableObject::PrepareGetById(metatable, VM_GetStringNameForMetatableKind(mtKind), icInfo /*out*/);
            TValue metamethod = TableObject::GetById(metatable, VM_GetStringNameForMetatableKind(mtKind).As<void>(), icInfo);
            if (!metamethod.IsNil())
            {
                return metamethod;
            }
        }
    }

    {
        UserHeapPointer<void> rhsMetatableMaybeNull = GetMetatableForValue(rhs);
        if (rhsMetatableMaybeNull.m_value == 0)
        {
            return TValue::Nil();
        }

        HeapPtr<TableObject> metatable = rhsMetatableMaybeNull.As<TableObject>();
        GetByIdICInfo icInfo;
        TableObject::PrepareGetById(metatable, VM_GetStringNameForMetatableKind(mtKind), icInfo /*out*/);
        return TableObject::GetById(metatable, VM_GetStringNameForMetatableKind(mtKind).As<void>(), icInfo);
    }
}

// For EQ/NEQ operator:
//     For Lua 5.1 & 5.2, the metamethod is only called if both are table or both are full userdata, AND both share the same metamethod
//     For Lua 5.3+, the restriction "both share the same metamethod" is removed.
// For LE/LT/GE/GT operator:
//     For Lua 5.1, the metamethod is called if both are same type, are not number/string, and both share the same metamethod
//     For Lua 5.2+, the restriction "both have the same type and both share the same metamethod" is removed.
//
template<bool supportsQuicklyRuleOutMetamethod>
TValue WARN_UNUSED GetMetamethodFromMetatableForComparisonOperation(HeapPtr<TableObject> lhsMetatable, HeapPtr<TableObject> rhsMetatable, LuaMetamethodKind mtKind)
{
    TValue lhsMetamethod;
    {
        // For 'eq', if either table doesn't have metamethod, the result is 'false', so it's a probable case that worth a fast path.
        // For 'le' or 'lt', however, the behavior is to throw error, so the situtation becomes just the opposite: it's unlikely the table doesn't have metamethod.
        //
        if constexpr(supportsQuicklyRuleOutMetamethod)
        {
            if (TableObject::TryQuicklyRuleOutMetamethod(lhsMetatable, mtKind))
            {
                return TValue::Nil();
            }
        }
        GetByIdICInfo icInfo;
        TableObject::PrepareGetById(lhsMetatable, VM_GetStringNameForMetatableKind(mtKind), icInfo /*out*/);
        lhsMetamethod = TableObject::GetById(lhsMetatable, VM_GetStringNameForMetatableKind(mtKind).As<void>(), icInfo);
        if (lhsMetamethod.IsNil())
        {
            return lhsMetamethod;
        }
    }

    TValue rhsMetamethod;
    {
        if constexpr(supportsQuicklyRuleOutMetamethod)
        {
            if (TableObject::TryQuicklyRuleOutMetamethod(rhsMetatable, mtKind))
            {
                return TValue::Nil();
            }
        }
        GetByIdICInfo icInfo;
        TableObject::PrepareGetById(rhsMetatable, VM_GetStringNameForMetatableKind(mtKind), icInfo /*out*/);
        rhsMetamethod = TableObject::GetById(rhsMetatable, VM_GetStringNameForMetatableKind(mtKind).As<void>(), icInfo);
    }

    assert(!lhsMetamethod.IsInt32() && "unimplemented");
    assert(!rhsMetamethod.IsInt32() && "unimplemented");

    // Now, perform a primitive comparison of lhsMetamethod and rhsMetamethod
    //
    if (unlikely(lhsMetamethod.IsDouble()))
    {
        // If both values are double, we must do a floating point comparison,
        // otherwise we will fail on edge cases like negative zero (-0 == 0) and NaN (NaN != NaN)
        // If rhs is not double, ViewAsDouble() gives NaN and the below comparison will also fail as expected.
        //
        if (UnsafeFloatEqual(lhsMetamethod.AsDouble(), rhsMetamethod.ViewAsDouble()))
        {
            return lhsMetamethod;
        }
        else
        {
            return TValue::Nil();
        }
    }
    else
    {
        // Now we know 'lhsMetamethod' is not a double
        // So it's safe to perform a bit comparison
        // Note that the comparison here doesn't involve methamethod or string-to-number coercion, so doing a bit comparison is correct.
        //
        if (lhsMetamethod.m_value == rhsMetamethod.m_value)
        {
            return lhsMetamethod;
        }
        else
        {
            return TValue::Nil();
        }
    }
}

inline TValue WARN_UNUSED NO_INLINE __attribute__((__preserve_most__)) GetNewIndexMetamethodFromTableObject(HeapPtr<TableObject> tableObj)
{
    TableObject::GetMetatableResult gmr = TableObject::GetMetatable(tableObj);
    if (unlikely(gmr.m_result.m_value != 0))
    {
        HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
        if (unlikely(!TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::NewIndex)))
        {
            return GetMetamethodFromMetatable(metatable, LuaMetamethodKind::NewIndex);
        }
    }
    return TValue::Create<tNil>();
}
