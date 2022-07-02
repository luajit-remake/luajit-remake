#pragma once

#include "common_utils.h"
#include "memory_ptr.h"
#include "vm.h"
#include "vm_string.h"
#include "structure.h"
#include "table_object.h"

namespace ToyLang {

using namespace CommonUtils;

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

class alignas(8) CoroutineRuntimeContext
{
public:
    static constexpr uint32_t x_hiddenClassForCoroutineRuntimeContext = 0x10;

    static CoroutineRuntimeContext* Create(VM* vm, UserHeapPointer<TableObject> globalObject)
    {
        CoroutineRuntimeContext* r = TranslateToRawPointer(vm, vm->AllocFromUserHeap(static_cast<uint32_t>(sizeof(CoroutineRuntimeContext))).AsNoAssert<CoroutineRuntimeContext>());
        UserHeapGcObjectHeader::Populate(r);
        r->m_hiddenClass = x_hiddenClassForCoroutineRuntimeContext;
        r->m_codeBlock = nullptr;
        r->m_globalObject = globalObject;
        r->m_numVariadicRets = 0;
        r->m_variadicRetSlotBegin = 0;
        r->m_upvalueList.m_value = 0;
        return r;
    }

    uint32_t m_hiddenClass;  // Always x_hiddenClassForCoroutineRuntimeContext
    Type m_type;
    GcCellState m_cellState;

    uint16_t m_reserved1;

    // The CodeBlock of the current function, if interpreter
    //
    CodeBlock* m_codeBlock;

    // The global object, if interpreter
    //
    UserHeapPointer<TableObject> m_globalObject;

    // slot [m_variadicRetSlotBegin + ord] holds variadic return value 'ord'
    //
    uint32_t m_numVariadicRets;
    uint32_t m_variadicRetSlotBegin;

    // The linked list head of the list of open upvalues
    //
    UserHeapPointer<Upvalue> m_upvalueList;
};

using InterpreterFn = void(*)(CoroutineRuntimeContext* /*rc*/, RestrictPtr<void> /*stackframe*/, ConstRestrictPtr<uint8_t> /*instr*/, uint64_t /*unused*/);

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

    static SystemHeapPointer<ExecutableCode> WARN_UNUSED CreateCFunction(VM* vm, InterpreterFn fn)
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
    //
    uint8_t* m_bytecode;

    // For intrinsic, this is the entrypoint of the intrinsic function
    // For bytecode function, this is the most optimized implementation (interpreter or some JIT tier)
    // For user C function, this is a trampoline that calls the function
    // The 'codeBlock' parameter and 'curBytecode' parameter is not needed for intrinsic or JIT but we have them anyway for a unified interface
    //
    InterpreterFn m_bestEntryPoint;
};
static_assert(sizeof(ExecutableCode) == 24);

class BaselineCodeBlock;
class FLOCodeBlock;

class UpvalueMetadata
{
public:
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
// For simplicity, we just make all the TValue constants and upvalue metadata as one trailing array.
// This requires UpvalueMetadata having the same size as TValue
//
static_assert(sizeof(UpvalueMetadata) == sizeof(TValue), "Must be the same size as TValue");

class UnlinkedCodeBlock;

// The constant table stores all the UpvalueMetadata and constants used in the bytecode.
// We know if an entry is a UpvalueMetadata since all the UpvalueMetadata are stored up-front.
// However, we cannot know if an entry is a TValue or a UnlinkedCodeBlock pointer!
// (this is due to how LuaJIT parser is designed. It can be changed, but we don't want to bother making it more complex)
// This is fine for the mutator as the bytecode will never misuse the type.
// For the GC, we rely on the crazy fact that a user-space raw pointer always falls in [0, 2^47) under practically every OS kernel,
// which mean when a pointer is interpreted as a TValue, it will always be interpreted into a double, so the GC marking
// algorithm will correctly ignore it for the marking.
//
union BytecodeConstantTableEntry
{
    BytecodeConstantTableEntry() : m_ucb(nullptr) { }
    UpvalueMetadata m_uv;
    TValue m_tv;
    UnlinkedCodeBlock* m_ucb;
};
static_assert(sizeof(BytecodeConstantTableEntry) == 8);

class UnlinkedCodeBlock;

// This uniquely corresponds to each pair of <UnlinkedCodeBlock, GlobalObject>
// It owns the bytecode and the corresponding metadata (the bytecode is copied from the UnlinkedCodeBlock,
// we need our own copy because we do bytecode opcode specialization optimization)
//
// Layout:
// [ upvalue table and constant table ] [ CodeBlock ] [ byetecode metadata ]
//
class alignas(8) CodeBlock final : public ExecutableCode
{
public:
    static CodeBlock* WARN_UNUSED Create(VM* vm, UnlinkedCodeBlock* ucb, UserHeapPointer<TableObject> globalObject);

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, CodeBlock>>>
    static ReinterpretCastPreservingAddressSpaceType<BytecodeConstantTableEntry*, T> GetConstantTableEnd(T self)
    {
        return ReinterpretCastPreservingAddressSpace<BytecodeConstantTableEntry*>(self);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, CodeBlock>>>
    static UpvalueMetadata GetConstant_UpvalueMetadata(T self, int64_t ordinal)
    {
        assert(-static_cast<int64_t>(self->m_numUpvalues) <= ordinal && ordinal < 0);
        return TCGet(GetConstantTableEnd(self)[ordinal]).m_uv;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, CodeBlock>>>
    static TValue GetConstant_TValue(T self, int64_t ordinal)
    {
        assert(-static_cast<int64_t>(self->m_owner->m_cstTableLength) <= ordinal && ordinal < -static_cast<int64_t>(self->m_numUpvalues));
        return TCGet(GetConstantTableEnd(self)[ordinal]).m_tv;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, CodeBlock>>>
    static UnlinkedCodeBlock* GetConstant_UnlinkedCodeBlock(T self, int64_t ordinal)
    {
        assert(-static_cast<int64_t>(self->m_owner->m_cstTableLength) <= ordinal && ordinal < -static_cast<int64_t>(self->m_numUpvalues));
        return TCGet(GetConstantTableEnd(self)[ordinal]).m_ucb;
    }

    static constexpr size_t GetTrailingArrayOffset()
    {
        return offsetof_member_v<&CodeBlock::m_bytecodeMetadata>;
    }

    UserHeapPointer<TableObject> m_globalObject;

    uint32_t m_stackFrameNumSlots;
    uint32_t m_numUpvalues;
    uint32_t m_bytecodeLength;
    uint32_t m_bytecodeMetadataLength;

    BaselineCodeBlock* m_baselineCodeBlock;
    FLOCodeBlock* m_floCodeBlock;

    UnlinkedCodeBlock* m_owner;

    uint64_t m_bytecodeMetadata[0];
};

// This uniquely corresponds to a piece of source code that defines a function
//
class UnlinkedCodeBlock
{
public:
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, UnlinkedCodeBlock>>>
    static CodeBlock* WARN_UNUSED GetCodeBlock(T self, UserHeapPointer<TableObject> globalObject)
    {
        if (likely(globalObject == self->m_defaultGlobalObject))
        {
            return self->m_defaultCodeBlock;
        }
        RareGlobalObjectToCodeBlockMap* rareMap = self->m_rareGOtoCBMap;
        if (unlikely(rareMap == nullptr))
        {
            rareMap = new RareGlobalObjectToCodeBlockMap;
            self->m_rareGOtoCBMap = rareMap;
        }
        auto iter = rareMap->find(globalObject.m_value);
        if (unlikely(iter == rareMap->end()))
        {
            VM* vm = VM::GetActiveVMForCurrentThread();
            CodeBlock* newCb = CodeBlock::Create(vm, TranslateToRawPointer(vm, self), globalObject);
            (*rareMap)[globalObject.m_value] = newCb;
            return newCb;
        }
        else
        {
            return iter->second;
        }
    }

    UserHeapPointer<TableObject> m_defaultGlobalObject;
    CodeBlock* m_defaultCodeBlock;
    using RareGlobalObjectToCodeBlockMap = std::unordered_map<int64_t, CodeBlock*>;
    RareGlobalObjectToCodeBlockMap* m_rareGOtoCBMap;

    uint8_t* m_bytecode;
    BytecodeConstantTableEntry* m_cstTable;
    UnlinkedCodeBlock* m_parent;

    uint32_t m_cstTableLength;
    uint32_t m_bytecodeLength;
    uint32_t m_numUpvalues;
    uint32_t m_numNumberConstants;
    uint32_t m_bytecodeMetadataLength;
    uint32_t m_stackFrameNumSlots;
    uint32_t m_numFixedArguments;
    bool m_hasVariadicArguments;
};

void EnterInterpreter(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/);

inline CodeBlock* WARN_UNUSED CodeBlock::Create(VM* vm, UnlinkedCodeBlock* ucb, UserHeapPointer<TableObject> globalObject)
{
    size_t sizeToAllocate = GetTrailingArrayOffset() + RoundUpToMultipleOf<8>(ucb->m_bytecodeMetadataLength) + sizeof(TValue) * ucb->m_cstTableLength;
    uint8_t* addressBegin = TranslateToRawPointer(vm, vm->AllocFromSystemHeap(static_cast<uint32_t>(sizeToAllocate)).AsNoAssert<uint8_t>());
    memcpy(addressBegin, ucb->m_cstTable, sizeof(TValue) * ucb->m_cstTableLength);

    CodeBlock* cb = reinterpret_cast<CodeBlock*>(addressBegin + sizeof(TValue) * ucb->m_cstTableLength);
    SystemHeapGcObjectHeader::Populate<ExecutableCode*>(cb);
    cb->m_hasVariadicArguments = ucb->m_hasVariadicArguments;
    cb->m_numFixedArguments = ucb->m_numFixedArguments;
    cb->m_bytecode = new uint8_t[ucb->m_bytecodeLength];
    memcpy(cb->m_bytecode, ucb->m_bytecode, ucb->m_bytecodeLength);
    cb->m_bestEntryPoint = EnterInterpreter;
    cb->m_globalObject = globalObject;
    cb->m_stackFrameNumSlots = ucb->m_stackFrameNumSlots;
    cb->m_numUpvalues = ucb->m_numUpvalues;
    cb->m_bytecodeLength = ucb->m_bytecodeLength;
    cb->m_bytecodeMetadataLength = ucb->m_bytecodeMetadataLength;
    cb->m_baselineCodeBlock = nullptr;
    cb->m_floCodeBlock = nullptr;
    cb->m_owner = ucb;
    return cb;
}

class FunctionObject;

class Upvalue
{
public:
    static HeapPtr<TValue> WARN_UNUSED GetValueAddr(HeapPtr<Upvalue> self)
    {
        return TCGet(self->m_ptr).As();
    }

    static TValue WARN_UNUSED GetValue(HeapPtr<Upvalue> self)
    {
        return TCGet(*GetValueAddr(self));
    }

    static HeapPtr<Upvalue> WARN_UNUSED CreateUpvalueImpl(UserHeapPointer<Upvalue> prev, GeneralHeapPointer<TValue> dst, bool isImmutable)
    {
        VM* vm = VM::GetActiveVMForCurrentThread();
        HeapPtr<Upvalue> r = vm->AllocFromUserHeap(static_cast<uint32_t>(sizeof(Upvalue))).AsNoAssert<Upvalue>();
        UserHeapGcObjectHeader::Populate(r);
        TCSet(r->m_ptr, dst);
        r->m_isClosed = false;
        r->m_isImmutable = isImmutable;
        TCSet(r->m_u.prev, prev);
        return r;
    }

    static HeapPtr<Upvalue> WARN_UNUSED Create(CoroutineRuntimeContext* rc, TValue* dstRaw, bool isImmutable)
    {
        GeneralHeapPointer<TValue> dst = dstRaw;
        if (rc->m_upvalueList.m_value == 0 || rc->m_upvalueList.As()->m_ptr.m_value < dst.m_value)
        {
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
            GeneralHeapPointer<TValue> curVal = TCGet(cur->m_ptr);
            UserHeapPointer<Upvalue> prev;
            while (true)
            {
                assert(!cur->m_isClosed);
                assert(dst.m_value <= curVal.m_value);
                if (curVal.m_value == dst.m_value)
                {
                    // We found an open upvalue for that slot, we are good
                    //
                    return cur;
                }

                prev = TCGet(cur->m_u.prev);
                if (prev.m_value == 0)
                {
                    // 'cur' is the last node, so we found the insertion location
                    //
                    break;
                }

                assert(!prev.As()->m_isClosed);
                GeneralHeapPointer<TValue> prevVal = TCGet(prev.As()->m_ptr);
                assert(prevVal.m_value < curVal.m_value);
                if (prevVal.m_value < dst.m_value)
                {
                    // prevVal < dst < curVal, so we found the insertion location
                    //
                    break;
                }

                cur = prev.As();
                curVal = prevVal;
            }

            assert(curVal.m_value == cur->m_ptr.m_value);
            assert(prev.m_value == cur->m_u.prev.m_value);
            assert(dst.m_value < curVal.m_value);
            assert(prev.m_value == 0 || prev.As()->m_ptr.m_value < dst.m_value);
            HeapPtr<Upvalue> newNode = CreateUpvalueImpl(prev, dst, isImmutable);
            TCSet(cur->m_u.prev, UserHeapPointer<Upvalue>(newNode));
            WriteBarrier(cur);
            return newNode;
        }
    }

    // This pointer occupies the slot for hidden class.
    // Normally this is a bit risky as it might confuse all sort of things (like IC), but upvalue is so special: it is never exposed to user,
    // so an Upvalue object will never be used as operand into any bytecode instruction other than the upvalue-dedicated ones, so we are fine.
    //
    // Points to &m_u.tv for closed upvalue, or the stack slot for open upvalue
    // All the open values are chained into a linked list (through m_u.prev) in reverse sorted order of m_ptr (i.e. absolute stack slot from high to low)
    //
    GeneralHeapPointer<TValue> m_ptr;
    Type m_type;
    GcCellState m_cellState;
    // Always equal to (m_ptr == &m_u.tv)
    //
    bool m_isClosed;
    bool m_isImmutable;

    union U {
        U() : tv() { }
        // Stores the value for closed upvalue
        //
        TValue tv;
        // Stores the linked list if the upvalue is open
        //
        UserHeapPointer<Upvalue> prev;
    } m_u;
};
static_assert(sizeof(Upvalue) == 16);

class FunctionObject
{
public:
    // Does not fill 'm_executable' or upvalue array
    //
    static UserHeapPointer<FunctionObject> WARN_UNUSED CreateImpl(VM* vm, uint16_t numUpvalues)
    {
        size_t sizeToAllocate = GetTrailingArrayOffset() + sizeof(GeneralHeapPointer<Upvalue>) * numUpvalues;
        sizeToAllocate = RoundUpToMultipleOf<8>(sizeToAllocate);
        HeapPtr<FunctionObject> r = vm->AllocFromUserHeap(static_cast<uint32_t>(sizeToAllocate)).AsNoAssert<FunctionObject>();
        UserHeapGcObjectHeader::Populate(r);

        r->m_numUpvalues = static_cast<uint16_t>(numUpvalues);
        return r;
    }

    // Does not fill upvalues
    //
    static UserHeapPointer<FunctionObject> WARN_UNUSED Create(VM* vm, CodeBlock* cb)
    {
        uint32_t numUpvalues = cb->m_numUpvalues;
        assert(numUpvalues <= std::numeric_limits<uint16_t>::max());
        UserHeapPointer<FunctionObject> r = CreateImpl(vm, static_cast<uint16_t>(numUpvalues));
        SystemHeapPointer<ExecutableCode> executable { static_cast<ExecutableCode*>(cb) };
        TCSet(r.As()->m_executable, executable);
        return r;
    }

    static UserHeapPointer<FunctionObject> WARN_UNUSED CreateCFunc(VM* vm, SystemHeapPointer<ExecutableCode> executable)
    {
        assert(TranslateToRawPointer(executable.As())->IsUserCFunction());
        UserHeapPointer<FunctionObject> r = CreateImpl(vm, 0 /*numUpvalues*/);
        TCSet(r.As()->m_executable, executable);
        return r;
    }

    static GeneralHeapPointer<Upvalue> GetUpvalue(HeapPtr<FunctionObject> self, size_t ord)
    {
        assert(ord < self->m_numUpvalues);
        return TCGet(self->m_upvalues[ord]);
    }

    static UserHeapPointer<FunctionObject> WARN_UNUSED CreateAndFillUpvalues(CodeBlock* cb, CoroutineRuntimeContext* rc, TValue* stackFrameBase, HeapPtr<FunctionObject> parent)
    {
        HeapPtr<FunctionObject> r = Create(VM::GetActiveVMForCurrentThread(), cb).As();
        assert(TranslateToRawPointer(TCGet(parent->m_executable).As())->IsBytecodeFunction());
        assert(cb->m_owner->m_parent == static_cast<HeapPtr<CodeBlock>>(TCGet(parent->m_executable).As())->m_owner);
        uint32_t numUpvalues = cb->m_numUpvalues;
        for (uint32_t ord = 0; ord < numUpvalues; ord++)
        {
            UpvalueMetadata uvmt = CodeBlock::GetConstant_UpvalueMetadata(cb, -static_cast<int32_t>(ord) - 1);
            GeneralHeapPointer<Upvalue> uv;
            if (uvmt.m_isParentLocal)
            {
                uv = Upvalue::Create(rc, stackFrameBase + uvmt.m_slot, uvmt.m_isImmutable);
            }
            else
            {
                uv = GetUpvalue(parent, uvmt.m_slot);
            }
            TCSet(r->m_upvalues[ord], uv);
        }
        return r;
    }

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
    Type m_type;
    GcCellState m_cellState;

    uint16_t m_numUpvalues;

    GeneralHeapPointer<Upvalue> m_upvalues[0];
};
static_assert(sizeof(FunctionObject) == 8);

// Corresponds to a file
//
class ScriptModule
{
public:
    std::string m_name;
    std::vector<UnlinkedCodeBlock*> m_unlinkedCodeBlocks;
    UserHeapPointer<TableObject> m_defaultGlobalObject;
    UserHeapPointer<FunctionObject> m_defaultEntryPoint;

    static ScriptModule* WARN_UNUSED ParseFromJSON(VM* vm, UserHeapPointer<TableObject> globalObject, const std::string& content);

    void EnterVM(CoroutineRuntimeContext* rc, void* stackbase);
    static void ExitVM(CoroutineRuntimeContext* /*rc*/, void* /*sfp*/, uint8_t* /*retValuesStart*/, uint64_t /*numRetValues*/) { }
};

class BytecodeSlot
{
public:
    constexpr BytecodeSlot() : m_value(x_invalidValue) { }

    static constexpr BytecodeSlot WARN_UNUSED Local(int ord)
    {
        assert(ord >= 0);
        return BytecodeSlot(ord);
    }
    static constexpr BytecodeSlot WARN_UNUSED Constant(int ord)
    {
        assert(ord < 0);
        return BytecodeSlot(ord);
    }

    bool IsInvalid() const { return m_value == x_invalidValue; }
    bool IsLocal() const { assert(!IsInvalid()); return m_value >= 0; }
    bool IsConstant() const { assert(!IsInvalid()); return m_value < 0; }

    int WARN_UNUSED LocalOrd() const { assert(IsLocal()); return m_value; }
    int WARN_UNUSED ConstantOrd() const { assert(IsConstant()); return m_value; }

    TValue WARN_UNUSED Get(CoroutineRuntimeContext* rc, void* sfp) const;

    explicit operator int() const { return m_value; }

private:
    constexpr BytecodeSlot(int value) : m_value(value) { }

    static constexpr int x_invalidValue = 0x7fffffff;
    int m_value;
} __attribute__((__packed__));

// stack frame format:
//     [... VarArgs ...] [Header] [... Locals ...]
//                                ^
//                                stack frame pointer (sfp)
//
class alignas(8) StackFrameHeader
{
public:
    // The address of the caller stack frame
    //
    StackFrameHeader* m_caller;
    // The return address
    //
    void* m_retAddr;
    // The function corresponding to this stack frame
    //
    HeapPtr<FunctionObject> m_func;
    // If the function is calling (i.e. not topmost frame), denotes the offset of the bytecode that performed the call
    //
    uint32_t m_callerBytecodeOffset;
    // Total number of variadic arguments passed to the function
    //
    uint32_t m_numVariadicArguments;

    static StackFrameHeader* GetStackFrameHeader(void* sfp)
    {
        return reinterpret_cast<StackFrameHeader*>(sfp) - 1;
    }

    static TValue* GetLocalAddr(void* sfp, BytecodeSlot slot)
    {
        assert(slot.IsLocal());
        int ord = slot.LocalOrd();
        return reinterpret_cast<TValue*>(sfp) + ord;
    }

    static TValue GetLocal(void* sfp, BytecodeSlot slot)
    {
        return *GetLocalAddr(sfp, slot);
    }
};

static_assert(sizeof(StackFrameHeader) % sizeof(TValue) == 0);
static constexpr size_t x_sizeOfStackFrameHeaderInTermsOfTValue = sizeof(StackFrameHeader) / sizeof(TValue);

inline void ScriptModule::EnterVM(CoroutineRuntimeContext* rc, void* stackStart)
{
    CodeBlock* cb = static_cast<CodeBlock*>(TranslateToRawPointer(TCGet(m_defaultEntryPoint.As()->m_executable).As()));
    rc->m_codeBlock = cb;
    assert(cb->m_numFixedArguments == 0);
    StackFrameHeader* sfh = reinterpret_cast<StackFrameHeader*>(stackStart);
    sfh->m_caller = nullptr;
    sfh->m_retAddr = reinterpret_cast<void*>(ExitVM);
    sfh->m_func = m_defaultEntryPoint.As();
    sfh->m_callerBytecodeOffset = 0;
    sfh->m_numVariadicArguments = 0;
    void* stackbase = sfh + 1;
    cb->m_bestEntryPoint(rc, stackbase, cb->m_bytecode, 0 /*unused*/);
}

inline void LJR_LIB_BASE_print(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> /*bcu*/, uint64_t /*unused*/)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
    uint32_t numElementsToPrint = hdr->m_numVariadicArguments;
    TValue* vaBegin = reinterpret_cast<TValue*>(hdr) - numElementsToPrint;
    for (uint32_t i = 0; i < numElementsToPrint; i++)
    {
        if (i > 0)
        {
            printf("\t");
        }

        TValue val = vaBegin[i];
        if (val.IsInt32(TValue::x_int32Tag))
        {
            printf("%d", static_cast<int>(val.AsInt32()));
        }
        else if (val.IsDouble(TValue::x_int32Tag))
        {
            char buf[x_dragonbox_stringify_double_buffer_length];
            dragonbox_stringify_double(val.AsDouble(), buf);
            printf("%s", buf);
        }
        else if (val.IsMIV(TValue::x_mivTag))
        {
            MiscImmediateValue miv = val.AsMIV(TValue::x_mivTag);
            if (miv.IsNil())
            {
                printf("nil");
            }
            else
            {
                assert(miv.IsBoolean());
                printf("%s", (miv.GetBooleanValue() ? "true" : "false"));
            }
        }
        else
        {
            assert(val.IsPointer(TValue::x_mivTag));
            UserHeapGcObjectHeader* p = TranslateToRawPointer(val.AsPointer<UserHeapGcObjectHeader>().As());
            if (p->m_type == Type::STRING)
            {
                HeapString* hs = reinterpret_cast<HeapString*>(p);
                fwrite(hs->m_string, sizeof(char), hs->m_length /*length*/, stdout);
            }
            else
            {
                if (p->m_type == Type::FUNCTION)
                {
                    printf("function");
                }
                else if (p->m_type == Type::TABLE)
                {
                    printf("table");
                }
                else if (p->m_type == Type::THREAD)
                {
                    printf("thread");
                }
                else
                {
                    printf("(type %d)", static_cast<int>(p->m_type));
                }
                printf(": %p", static_cast<void*>(p));
            }
        }
    }
    printf("\n");

    using RetFn = void(*)(CoroutineRuntimeContext* /*rc*/, void* /*sfp*/, uint8_t* /*retValuesStart*/, uint64_t /*numRetValues*/);
    RetFn retAddr = reinterpret_cast<RetFn>(hdr->m_retAddr);
    StackFrameHeader* callerSf = hdr->m_caller;
    [[clang::musttail]] return retAddr(rc, static_cast<void*>(callerSf), reinterpret_cast<uint8_t*>(sfp), 0 /*numRetValues*/);
}

inline UserHeapPointer<TableObject> CreateGlobalObject(VM* vm)
{
    HeapPtr<TableObject> r = TableObject::CreateEmptyGlobalObject(vm);

    auto insertField = [&](const char* propName, TValue value)
    {
        UserHeapPointer<HeapString> hs = vm->CreateStringObjectFromRawString(propName, static_cast<uint32_t>(strlen(propName)));
        PutByIdICInfo icInfo;
        TableObject::PreparePutById(r, hs /*prop*/, icInfo /*out*/);
        TableObject::PutById(r, hs.As<void>(), value, icInfo);
    };

    auto insertCFunc = [&](const char* propName, InterpreterFn func)
    {
        UserHeapPointer<FunctionObject> funcObj = FunctionObject::CreateCFunc(vm, ExecutableCode::CreateCFunction(vm, func));
        insertField(propName, TValue::CreatePointer(funcObj));
    };

    insertCFunc("print", LJR_LIB_BASE_print);
    return r;
}

inline TValue WARN_UNUSED BytecodeSlot::Get(CoroutineRuntimeContext* rc, void* sfp) const
{
    if (IsLocal())
    {
        return StackFrameHeader::GetLocal(sfp, *this);
    }
    else
    {
        int ord = ConstantOrd();
        return CodeBlock::GetConstant_TValue(rc->m_codeBlock, ord);
    }
}

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

class BytecodeToIRTransformer
{
public:
    // Remap a slot in bytecode to the physical slot for the interpreter/baseline JIT
    //
    void RemapSlot(BytecodeSlot /*slot*/)
    {

    }

    void TransformFunctionImpl(IRBasicBlock* /*bb*/)
    {

    }

    std::vector<InliningStackEntry> m_inlineStack;
};

#define OPCODE_LIST     \
    BcTableGetById,     \
    BcTablePutById,     \
    BcTableGetByVal,    \
    BcTablePutByVal,    \
    BcGlobalGet,        \
    BcGlobalPut,        \
    BcReturn,           \
    BcCall,             \
    BcNewClosure,       \
    BcMove,             \
    BcAddVV,            \
    BcSubVV,            \
    BcIsEQ,             \
    BcIsNEQ,            \
    BcIsLTVV,           \
    BcIsNLTVV,          \
    BcIsLEVV,           \
    BcIsNLEVV,          \
    BcConstant

#define macro(opcodeCppName) class opcodeCppName;
PP_FOR_EACH(macro, OPCODE_LIST)
#undef macro

#define macro(opcodeCppName) + 1
constexpr size_t x_numOpcodes = 0 PP_FOR_EACH(macro, OPCODE_LIST);
#undef macro

namespace internal
{

template<typename T>
struct opcode_for_type_impl;

#define macro(ord, opcodeCppName) template<> struct opcode_for_type_impl<opcodeCppName> { static constexpr uint8_t value = ord; };
PP_FOR_EACH_UNPACK_TUPLE(macro, PP_ZIP_TWO_LISTS((PP_NATURAL_NUMBERS_LIST), (OPCODE_LIST)))
#undef macro

}   // namespace internal

template<typename T>
constexpr uint8_t x_opcodeId = internal::opcode_for_type_impl<T>::value;

extern const InterpreterFn x_interpreter_dispatches[x_numOpcodes];

#define Dispatch(rc, stackframe, instr)                                                                                          \
    do {                                                                                                                         \
        uint8_t dispatch_nextopcode = *reinterpret_cast<const uint8_t*>(instr);                                                  \
        assert(dispatch_nextopcode < x_numOpcodes);                                                                              \
_Pragma("clang diagnostic push")                                                                                                 \
_Pragma("clang diagnostic ignored \"-Wuninitialized\"")                                                                          \
        uint64_t dispatch_unused;                                                                                                \
        [[clang::musttail]] return x_interpreter_dispatches[dispatch_nextopcode]((rc), (stackframe), (instr), dispatch_unused);  \
_Pragma("clang diagnostic pop")                                                                                                  \
    } while (false)

inline void EnterInterpreter(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
{
    Dispatch(rc, sfp, bcu);
}

// The return statement is required to fill nil up to x_minNilFillReturnValues values even if it returns less than that many values
//
constexpr uint32_t x_minNilFillReturnValues = 3;

class BcTableGetById
{
public:
    uint8_t m_opcode;
    BytecodeSlot m_base;
    BytecodeSlot m_dst;
    int32_t m_index;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcTableGetById* bc = reinterpret_cast<const BcTableGetById*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcTableGetById>);
        assert(bc->m_base.IsLocal());
        TValue tvbase = *StackFrameHeader::GetLocalAddr(sfp, bc->m_base);

        TValue tvIndex = CodeBlock::GetConstant_TValue(rc->m_codeBlock, bc->m_index);
        assert(tvIndex.IsPointer(TValue::x_mivTag));
        UserHeapPointer<HeapString> index = tvIndex.AsPointer<HeapString>();

        if (!tvbase.IsPointer(TValue::x_mivTag))
        {
            ReleaseAssert(false && "unimplemented");
        }
        else
        {
            UserHeapPointer<void> base = tvbase.AsPointer<void>();
            if (base.As<UserHeapGcObjectHeader>()->m_type != Type::TABLE)
            {
                ReleaseAssert(false && "unimplemented");
            }
            GetByIdICInfo icInfo;
            TableObject::PrepareGetById(base.As<TableObject>(), index, icInfo /*out*/);
            TValue result = TableObject::GetById(base.As<TableObject>(), index.As<void>(), icInfo);

            *StackFrameHeader::GetLocalAddr(sfp, bc->m_dst) = result;
            Dispatch(rc, sfp, bcu + sizeof(BcTableGetById));
        }
    }
} __attribute__((__packed__));

class BcTablePutById
{
public:
    uint8_t m_opcode;
    BytecodeSlot m_base;
    BytecodeSlot m_src;
    uint32_t m_index;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcTablePutById* bc = reinterpret_cast<const BcTablePutById*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcTablePutById>);
        assert(bc->m_base.IsLocal());
        TValue tvbase = *StackFrameHeader::GetLocalAddr(sfp, bc->m_base);

        TValue tvIndex = CodeBlock::GetConstant_TValue(rc->m_codeBlock, bc->m_index);
        assert(tvIndex.IsPointer(TValue::x_mivTag));
        UserHeapPointer<HeapString> index = tvIndex.AsPointer<HeapString>();

        if (!tvbase.IsPointer(TValue::x_mivTag))
        {
            ReleaseAssert(false && "unimplemented");
        }
        else
        {
            UserHeapPointer<void> base = tvbase.AsPointer<void>();
            if (base.As<UserHeapGcObjectHeader>()->m_type != Type::TABLE)
            {
                ReleaseAssert(false && "unimplemented");
            }
            PutByIdICInfo icInfo;
            TableObject::PreparePutById(base.As<TableObject>(), index, icInfo /*out*/);
            TValue newValue = *StackFrameHeader::GetLocalAddr(sfp, bc->m_src);
            TableObject::PutById(base.As<TableObject>(), index.As<void>(), newValue, icInfo);
            Dispatch(rc, sfp, bcu + sizeof(BcTablePutById));
        }
    }
} __attribute__((__packed__));

class BcTableGetByVal
{
public:
    uint8_t m_opcode;
    BytecodeSlot m_base;
    BytecodeSlot m_index;
    BytecodeSlot m_dst;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcTableGetByVal* bc = reinterpret_cast<const BcTableGetByVal*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcTableGetByVal>);
        assert(bc->m_base.IsLocal());
        TValue tvbase = *StackFrameHeader::GetLocalAddr(sfp, bc->m_base);

        if (!tvbase.IsPointer(TValue::x_mivTag))
        {
            ReleaseAssert(false && "unimplemented");
        }
        else
        {
            UserHeapPointer<void> base = tvbase.AsPointer<void>();
            if (base.As<UserHeapGcObjectHeader>()->m_type != Type::TABLE)
            {
                ReleaseAssert(false && "unimplemented");
            }

            TValue index = *StackFrameHeader::GetLocalAddr(sfp, bc->m_index);
            TValue result;
            if (index.IsInt32(TValue::x_int32Tag))
            {
                GetByIntegerIndexICInfo icInfo;
                TableObject::PrepareGetByIntegerIndex(base.As<TableObject>(), icInfo /*out*/);
                result = TableObject::GetByIntegerIndex(base.As<TableObject>(), index.AsInt32(), icInfo);
            }
            else if (index.IsDouble(TValue::x_int32Tag))
            {
                GetByIntegerIndexICInfo icInfo;
                TableObject::PrepareGetByIntegerIndex(base.As<TableObject>(), icInfo /*out*/);
                result = TableObject::GetByDoubleVal(base.As<TableObject>(), index.AsDouble(), icInfo);
            }
            else if (index.IsPointer(TValue::x_mivTag))
            {
                GetByIdICInfo icInfo;
                TableObject::PrepareGetById(base.As<TableObject>(), index.AsPointer(), icInfo /*out*/);
                result = TableObject::GetById(base.As<TableObject>(), index.AsPointer(), icInfo);
            }
            else
            {
                ReleaseAssert(false && "unimplemented");
            }

            *StackFrameHeader::GetLocalAddr(sfp, bc->m_dst) = result;
            Dispatch(rc, sfp, bcu + sizeof(BcTableGetByVal));
        }
    }
} __attribute__((__packed__));

class BcTablePutByVal
{
public:
    uint8_t m_opcode;
    BytecodeSlot m_base;
    BytecodeSlot m_index;
    BytecodeSlot m_src;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcTablePutByVal* bc = reinterpret_cast<const BcTablePutByVal*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcTablePutByVal>);
        assert(bc->m_base.IsLocal());
        TValue tvbase = *StackFrameHeader::GetLocalAddr(sfp, bc->m_base);

        if (!tvbase.IsPointer(TValue::x_mivTag))
        {
            ReleaseAssert(false && "unimplemented");
        }
        else
        {
            UserHeapPointer<void> base = tvbase.AsPointer<void>();
            if (base.As<UserHeapGcObjectHeader>()->m_type != Type::TABLE)
            {
                ReleaseAssert(false && "unimplemented");
            }

            TValue index = *StackFrameHeader::GetLocalAddr(sfp, bc->m_index);
            TValue newValue = *StackFrameHeader::GetLocalAddr(sfp, bc->m_src);
            if (index.IsInt32(TValue::x_int32Tag))
            {
                TableObject::PutByValIntegerIndex(base.As<TableObject>(), index.AsInt32(), newValue);
            }
            else if (index.IsDouble(TValue::x_int32Tag))
            {
                TableObject::PutByValDoubleIndex(base.As<TableObject>(), index.AsDouble(), newValue);
            }
            else if (index.IsPointer(TValue::x_mivTag))
            {
                PutByIdICInfo icInfo;
                TableObject::PreparePutById(base.As<TableObject>(), index.AsPointer(), icInfo /*out*/);
                TableObject::PutById(base.As<TableObject>(), index.AsPointer(), newValue, icInfo);
            }
            else
            {
                ReleaseAssert(false && "unimplemented");
            }

            Dispatch(rc, sfp, bcu + sizeof(BcTablePutByVal));
        }
    }
} __attribute__((__packed__));

class BcGlobalGet
{
public:
    BcGlobalGet(BytecodeSlot dst, BytecodeSlot idx)
        : m_opcode(x_opcodeId<BcGlobalGet>), m_dst(dst), m_index(idx.ConstantOrd())
    { }

    uint8_t m_opcode;
    BytecodeSlot m_dst;
    int32_t m_index;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcGlobalGet* bc = reinterpret_cast<const BcGlobalGet*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcGlobalGet>);

        TValue tvIndex = CodeBlock::GetConstant_TValue(rc->m_codeBlock, bc->m_index);
        assert(tvIndex.IsPointer(TValue::x_mivTag));
        UserHeapPointer<HeapString> index = tvIndex.AsPointer<HeapString>();

        UserHeapPointer<TableObject> base = rc->m_globalObject;
        GetByIdICInfo icInfo;
        TableObject::PrepareGetById(base.As<TableObject>(), index, icInfo /*out*/);
        TValue result = TableObject::GetById(base.As(), index.As<void>(), icInfo);

        *StackFrameHeader::GetLocalAddr(sfp, bc->m_dst) = result;
        Dispatch(rc, sfp, bcu + sizeof(BcGlobalGet));
    }
} __attribute__((__packed__));

class BcGlobalPut
{
public:
    BcGlobalPut(BytecodeSlot src, BytecodeSlot idx)
        : m_opcode(x_opcodeId<BcGlobalPut>), m_src(src), m_index(idx.ConstantOrd())
    { }

    uint8_t m_opcode;
    BytecodeSlot m_src;
    int32_t m_index;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcGlobalPut* bc = reinterpret_cast<const BcGlobalPut*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcGlobalPut>);

        TValue tvIndex = CodeBlock::GetConstant_TValue(rc->m_codeBlock, bc->m_index);
        assert(tvIndex.IsPointer(TValue::x_mivTag));
        UserHeapPointer<HeapString> index = tvIndex.AsPointer<HeapString>();
        TValue newValue = *StackFrameHeader::GetLocalAddr(sfp, bc->m_src);

        UserHeapPointer<TableObject> base = rc->m_globalObject;
        PutByIdICInfo icInfo;
        TableObject::PreparePutById(base.As<TableObject>(), index, icInfo /*out*/);
        TableObject::PutById(base.As(), index.As<void>(), newValue, icInfo);

        Dispatch(rc, sfp, bcu + sizeof(BcGlobalPut));
    }
} __attribute__((__packed__));


class BcReturn
{
public:
    BcReturn(bool isVariadic, uint16_t numReturnValues, BytecodeSlot slotBegin)
        : m_opcode(x_opcodeId<BcReturn>), m_isVariadicRet(isVariadic), m_numReturnValues(numReturnValues), m_slotBegin(slotBegin)
    { }

    uint8_t m_opcode;
    bool m_isVariadicRet;
    uint16_t m_numReturnValues;
    BytecodeSlot m_slotBegin;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcReturn* bc = reinterpret_cast<const BcReturn*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcReturn>);
        assert(bc->m_slotBegin.IsLocal());
        TValue* pbegin = StackFrameHeader::GetLocalAddr(sfp, bc->m_slotBegin);
        uint32_t numRetValues = bc->m_numReturnValues;
        if (bc->m_isVariadicRet)
        {
            assert(rc->m_numVariadicRets != static_cast<uint32_t>(-1));
            TValue* pdst = pbegin + bc->m_numReturnValues;
            TValue* psrc = reinterpret_cast<TValue*>(sfp) + rc->m_variadicRetSlotBegin;
            numRetValues += rc->m_numVariadicRets;
            SafeMemcpy(pdst, psrc, sizeof(TValue) * rc->m_numVariadicRets);
        }
        // No matter we consumed variadic ret or not, it is no longer valid after the return
        //
        DEBUG_ONLY(rc->m_numVariadicRets = static_cast<uint32_t>(-1);)

        // Fill nil up to x_minNilFillReturnValues values
        // TODO: we can also just do a vectorized write
        //
        {
            uint32_t idx = numRetValues;
            while (idx < x_minNilFillReturnValues)
            {
                pbegin[idx] = TValue::CreateMIV(MiscImmediateValue::CreateNil(), TValue::x_mivTag);
                idx++;
            }
        }

        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
        using RetFn = void(*)(CoroutineRuntimeContext* /*rc*/, void* /*sfp*/, uint8_t* /*retValuesStart*/, uint64_t /*numRetValues*/);
        RetFn retAddr = reinterpret_cast<RetFn>(hdr->m_retAddr);
        StackFrameHeader* callerSf = hdr->m_caller;
        [[clang::musttail]] return retAddr(rc, static_cast<void*>(callerSf), reinterpret_cast<uint8_t*>(pbegin), numRetValues);
    }
} __attribute__((__packed__));

class BcCall
{
public:
    BcCall(bool keepVariadicRet, bool passVariadicRetAsParam, uint32_t numFixedParams, uint32_t numFixedRets, BytecodeSlot funcSlot)
        : m_opcode(x_opcodeId<BcCall>), m_keepVariadicRet(keepVariadicRet), m_passVariadicRetAsParam(passVariadicRetAsParam)
        , m_numFixedParams(numFixedParams), m_numFixedRets(numFixedRets), m_funcSlot(funcSlot)
    { }

    uint8_t m_opcode;
    bool m_keepVariadicRet;
    bool m_passVariadicRetAsParam;
    uint32_t m_numFixedParams;
    uint32_t m_numFixedRets;    // only used when m_keepVariadicRet == false
    BytecodeSlot m_funcSlot;   // params are [m_funcSlot + 1, ... m_funcSlot + m_numFixedParams]

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcCall* bc = reinterpret_cast<const BcCall*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcCall>);
        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);

        HeapPtr<ExecutableCode> callerEc = TCGet(hdr->m_func->m_executable).As();
        assert(TranslateToRawPointer(callerEc)->IsBytecodeFunction());
        HeapPtr<CodeBlock> callerCb = static_cast<HeapPtr<CodeBlock>>(callerEc);
        uint8_t* callerBytecodeStart = callerCb->m_bytecode;
        hdr->m_callerBytecodeOffset = SafeIntegerCast<uint32_t>(bcu - callerBytecodeStart);

        assert(bc->m_funcSlot.IsLocal());
        TValue* begin = StackFrameHeader::GetLocalAddr(sfp, bc->m_funcSlot);
        TValue func = *begin;
        begin++;

        if (func.IsPointer(TValue::x_mivTag))
        {
            if (func.AsPointer().As<UserHeapGcObjectHeader>()->m_type == Type::FUNCTION)
            {
                HeapPtr<FunctionObject> target = func.AsPointer().As<FunctionObject>();

                TValue* sfEnd = reinterpret_cast<TValue*>(sfp) + callerCb->m_stackFrameNumSlots;
                TValue* baseForNextFrame = sfEnd + x_sizeOfStackFrameHeaderInTermsOfTValue;

                uint32_t numFixedArgsToPass = bc->m_numFixedParams;
                uint32_t totalArgs = numFixedArgsToPass;
                if (bc->m_passVariadicRetAsParam)
                {
                    totalArgs += rc->m_numVariadicRets;
                }

                HeapPtr<ExecutableCode> calleeEc = TCGet(target->m_executable).As();

                uint32_t numCalleeExpectingArgs = calleeEc->m_numFixedArguments;
                bool calleeTakesVarArgs = calleeEc->m_hasVariadicArguments;

                // If the callee takes varargs and it is not empty, set up the varargs
                //
                if (unlikely(calleeTakesVarArgs))
                {
                    uint32_t actualNumVarArgs = 0;
                    if (totalArgs > numCalleeExpectingArgs)
                    {
                        actualNumVarArgs = totalArgs - numCalleeExpectingArgs;
                        baseForNextFrame += actualNumVarArgs;
                    }

                    // First, if we need to pass varret, move the whole varret to the correct position
                    //
                    if (bc->m_passVariadicRetAsParam)
                    {
                        TValue* varRetbegin = reinterpret_cast<TValue*>(sfp) + rc->m_variadicRetSlotBegin;
                        // TODO: over-moving is fine
                        memmove(baseForNextFrame + numFixedArgsToPass, varRetbegin, sizeof(TValue) * rc->m_numVariadicRets);
                    }

                    // Now, copy the fixed args to the correct position
                    //
                    SafeMemcpy(baseForNextFrame, begin, sizeof(TValue) * numFixedArgsToPass);

                    // Now, set up the vararg part
                    //
                    if (totalArgs > numCalleeExpectingArgs)
                    {
                        SafeMemcpy(sfEnd, baseForNextFrame + numCalleeExpectingArgs, sizeof(TValue) * (totalArgs - numCalleeExpectingArgs));
                    }

                    // Finally, set up the numVarArgs field in the frame header
                    //
                    StackFrameHeader* sfh = reinterpret_cast<StackFrameHeader*>(baseForNextFrame) - 1;
                    sfh->m_numVariadicArguments = actualNumVarArgs;
                }
                else
                {
                    // First, if we need to pass varret, move the whole varret to the correct position, up to the number of args the callee accepts
                    //
                    if (bc->m_passVariadicRetAsParam)
                    {
                        if (numCalleeExpectingArgs > numFixedArgsToPass)
                        {
                            TValue* varRetbegin = reinterpret_cast<TValue*>(sfp) + rc->m_variadicRetSlotBegin;
                            // TODO: over-moving is fine
                            memmove(baseForNextFrame + numFixedArgsToPass, varRetbegin, sizeof(TValue) * std::min(rc->m_numVariadicRets, numCalleeExpectingArgs - numFixedArgsToPass));
                        }
                    }

                    // Now, copy the fixed args to the correct position, up to the number of args the callee accepts
                    //
                    SafeMemcpy(baseForNextFrame, begin, sizeof(TValue) * std::min(numFixedArgsToPass, numCalleeExpectingArgs));
                }

                // Finally, pad in nils if necessary
                //
                if (totalArgs < numCalleeExpectingArgs)
                {
                    TValue* p = baseForNextFrame + totalArgs;
                    TValue* end = baseForNextFrame + numCalleeExpectingArgs;
                    while (p < end)
                    {
                        *p = TValue::CreateMIV(MiscImmediateValue::CreateNil(), TValue::x_mivTag);
                        p++;
                    }
                }

                // Set up the stack frame header
                //
                StackFrameHeader* sfh = reinterpret_cast<StackFrameHeader*>(baseForNextFrame) - 1;
                sfh->m_caller = reinterpret_cast<StackFrameHeader*>(sfp);
                sfh->m_retAddr = reinterpret_cast<void*>(OnReturn);
                sfh->m_func = target;

                // This static_cast actually doesn't always hold
                // But if callee is not a bytecode function, the callee will never look at m_codeBlock, so it's fine
                //
                rc->m_codeBlock = static_cast<CodeBlock*>(TranslateToRawPointer(calleeEc));

                _Pragma("clang diagnostic push")
                _Pragma("clang diagnostic ignored \"-Wuninitialized\"")
                uint64_t unused;
                uint8_t* calleeBytecode = calleeEc->m_bytecode;
                InterpreterFn calleeFn = calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, baseForNextFrame, calleeBytecode, unused);
                _Pragma("clang diagnostic pop")
            }
            else
            {
                assert(false && "unimplemented");
            }
        }
        else
        {
            assert(false && "unimplemented");
        }
    }

    static void OnReturn(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> retValuesU, uint64_t numRetValues)
    {
        const TValue* retValues = reinterpret_cast<const TValue*>(retValuesU);
        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackframe);
        HeapPtr<ExecutableCode> callerEc = TCGet(hdr->m_func->m_executable).As();
        assert(TranslateToRawPointer(callerEc)->IsBytecodeFunction());
        uint8_t* callerBytecodeStart = callerEc->m_bytecode;
        ConstRestrictPtr<uint8_t> bcu = callerBytecodeStart + hdr->m_callerBytecodeOffset;
        const BcCall* bc = reinterpret_cast<const BcCall*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcCall>);
        if (bc->m_keepVariadicRet)
        {
            rc->m_numVariadicRets = SafeIntegerCast<uint32_t>(numRetValues);
            rc->m_variadicRetSlotBegin = SafeIntegerCast<uint32_t>(retValues - reinterpret_cast<TValue*>(stackframe));
        }
        else
        {
            if (bc->m_numFixedRets <= x_minNilFillReturnValues)
            {
                SafeMemcpy(StackFrameHeader::GetLocalAddr(stackframe, bc->m_funcSlot), retValues, sizeof(TValue) * bc->m_numFixedRets);
            }
            else
            {
                TValue* dst = StackFrameHeader::GetLocalAddr(stackframe, bc->m_funcSlot);
                if (numRetValues < bc->m_numFixedRets)
                {
                    SafeMemcpy(dst, retValues, sizeof(TValue) * numRetValues);
                    while (numRetValues < bc->m_numFixedRets)
                    {
                        dst[numRetValues] = TValue::CreateMIV(MiscImmediateValue::CreateNil(), TValue::x_mivTag);
                        numRetValues++;
                    }
                }
                else
                {
                    SafeMemcpy(dst, retValues, sizeof(TValue) * bc->m_numFixedRets);
                }
            }
        }
        rc->m_codeBlock = static_cast<CodeBlock*>(TranslateToRawPointer(callerEc));
        Dispatch(rc, stackframe, bcu + sizeof(BcCall));
    }
} __attribute__((__packed__));

class BcNewClosure
{
public:
    BcNewClosure(BytecodeSlot src, BytecodeSlot dst)
        : m_opcode(x_opcodeId<BcNewClosure>), m_src(src), m_dst(dst)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_src;
    BytecodeSlot m_dst;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcNewClosure* bc = reinterpret_cast<const BcNewClosure*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcNewClosure>);
        UnlinkedCodeBlock* ucb = CodeBlock::GetConstant_UnlinkedCodeBlock(rc->m_codeBlock, bc->m_src.ConstantOrd());
        CodeBlock* cb = UnlinkedCodeBlock::GetCodeBlock(ucb, rc->m_globalObject);
        UserHeapPointer<FunctionObject> func = FunctionObject::CreateAndFillUpvalues(cb, rc, reinterpret_cast<TValue*>(stackframe), StackFrameHeader::GetStackFrameHeader(stackframe)->m_func);
        *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = TValue::CreatePointer(func);
        Dispatch(rc, stackframe, bcu + sizeof(BcNewClosure));
    }
} __attribute__((__packed__));

class BcMove
{
public:
    BcMove(BytecodeSlot src, BytecodeSlot dst)
        : m_opcode(x_opcodeId<BcMove>), m_src(src), m_dst(dst)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_src;
    BytecodeSlot m_dst;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcMove* bc = reinterpret_cast<const BcMove*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcMove>);
        TValue src = bc->m_src.Get(rc, stackframe);
        *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = src;
        Dispatch(rc, stackframe, bcu + sizeof(BcMove));
    }
} __attribute__((__packed__));

class BcAddVV
{
public:
    BcAddVV(BytecodeSlot lhs, BytecodeSlot rhs, BytecodeSlot result)
        : m_opcode(x_opcodeId<BcAddVV>), m_lhs(lhs), m_rhs(rhs), m_result(result)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    BytecodeSlot m_result;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcAddVV* bc = reinterpret_cast<const BcAddVV*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcAddVV>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(lhs.AsDouble() + rhs.AsDouble());
            Dispatch(rc, stackframe, bcu + sizeof(BcAddVV));
        }
        else
        {
            assert(false && "unimplemented");
        }
    }
} __attribute__((__packed__));

class BcSubVV
{
public:
    BcSubVV(BytecodeSlot lhs, BytecodeSlot rhs, BytecodeSlot result)
        : m_opcode(x_opcodeId<BcSubVV>), m_lhs(lhs), m_rhs(rhs), m_result(result)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    BytecodeSlot m_result;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcSubVV* bc = reinterpret_cast<const BcSubVV*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcSubVV>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(lhs.AsDouble() - rhs.AsDouble());
            Dispatch(rc, stackframe, bcu + sizeof(BcSubVV));
        }
        else
        {
            assert(false && "unimplemented");
        }
    }
} __attribute__((__packed__));

class BcIsLTVV
{
public:
    BcIsLTVV(BytecodeSlot lhs, BytecodeSlot rhs)
        : m_opcode(x_opcodeId<BcIsLTVV>), m_lhs(lhs), m_rhs(rhs), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&BcIsLTVV::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcIsLTVV* bc = reinterpret_cast<const BcIsLTVV*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcIsLTVV>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            if (lhs.AsDouble() < rhs.AsDouble())
            {
                Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
            }
            else
            {
                Dispatch(rc, stackframe, bcu + sizeof(BcIsLTVV));
            }
        }
        else
        {
            assert(false && "unimplemented");
        }
    }
} __attribute__((__packed__));

// IsNLT x y is different from IsLE y x in the presense of NaN (in other words, !(x < y) is not always equal to y <= x)
// because if x or y is NaN, the former gives true while latter gives false
// For same reason we need IsNLE
//
class BcIsNLTVV
{
public:
    BcIsNLTVV(BytecodeSlot lhs, BytecodeSlot rhs)
        : m_opcode(x_opcodeId<BcIsNLTVV>), m_lhs(lhs), m_rhs(rhs), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&BcIsNLTVV::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcIsNLTVV* bc = reinterpret_cast<const BcIsNLTVV*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcIsNLTVV>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            if (!(lhs.AsDouble() < rhs.AsDouble()))
            {
                Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
            }
            else
            {
                Dispatch(rc, stackframe, bcu + sizeof(BcIsNLTVV));
            }
        }
        else
        {
            assert(false && "unimplemented");
        }
    }
} __attribute__((__packed__));

class BcIsLEVV
{
public:
    BcIsLEVV(BytecodeSlot lhs, BytecodeSlot rhs)
        : m_opcode(x_opcodeId<BcIsLEVV>), m_lhs(lhs), m_rhs(rhs), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&BcIsLEVV::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcIsLEVV* bc = reinterpret_cast<const BcIsLEVV*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcIsLEVV>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            if (lhs.AsDouble() <= rhs.AsDouble())
            {
                Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
            }
            else
            {
                Dispatch(rc, stackframe, bcu + sizeof(BcIsLEVV));
            }
        }
        else
        {
            assert(false && "unimplemented");
        }
    }
} __attribute__((__packed__));

class BcIsNLEVV
{
public:
    BcIsNLEVV(BytecodeSlot lhs, BytecodeSlot rhs)
        : m_opcode(x_opcodeId<BcIsNLEVV>), m_lhs(lhs), m_rhs(rhs), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&BcIsNLEVV::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcIsNLEVV* bc = reinterpret_cast<const BcIsNLEVV*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcIsNLEVV>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            if (!(lhs.AsDouble() <= rhs.AsDouble()))
            {
                Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
            }
            else
            {
                Dispatch(rc, stackframe, bcu + sizeof(BcIsNLEVV));
            }
        }
        else
        {
            assert(false && "unimplemented");
        }
    }
} __attribute__((__packed__));

class BcIsEQ
{
public:
    BcIsEQ(BytecodeSlot lhs, BytecodeSlot rhs)
        : m_opcode(x_opcodeId<BcIsEQ>), m_lhs(lhs), m_rhs(rhs), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&BcIsEQ::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcIsEQ* bc = reinterpret_cast<const BcIsEQ*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcIsEQ>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            if (UnsafeFloatEqual(lhs.AsDouble(), rhs.AsDouble()))
            {
                Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
            }
            else
            {
                Dispatch(rc, stackframe, bcu + sizeof(BcIsEQ));
            }
        }
        else
        {
            // This is problematic..
            //
            if (lhs.m_value == rhs.m_value)
            {
                Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
            }
            else
            {
                Dispatch(rc, stackframe, bcu + sizeof(BcIsEQ));
            }
        }
    }
} __attribute__((__packed__));

class BcIsNEQ
{
public:
    BcIsNEQ(BytecodeSlot lhs, BytecodeSlot rhs)
        : m_opcode(x_opcodeId<BcIsNEQ>), m_lhs(lhs), m_rhs(rhs), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&BcIsNEQ::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcIsNEQ* bc = reinterpret_cast<const BcIsNEQ*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcIsNEQ>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            if (UnsafeFloatEqual(lhs.AsDouble(), rhs.AsDouble()))
            {
                Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
            }
            else
            {
                Dispatch(rc, stackframe, bcu + sizeof(BcIsNEQ));
            }
        }
        else
        {
            // This is problematic..
            //
            if (lhs.m_value == rhs.m_value)
            {
                Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
            }
            else
            {
                Dispatch(rc, stackframe, bcu + sizeof(BcIsNEQ));
            }
        }
    }
} __attribute__((__packed__));

class BcConstant
{
public:
    BcConstant(BytecodeSlot dst, TValue value)
        : m_opcode(x_opcodeId<BcConstant>), m_dst(dst), m_value(value)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_dst;
    Packed<TValue> m_value;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcConstant* bc = reinterpret_cast<const BcConstant*>(bcu);
        assert(bc->m_opcode == x_opcodeId<BcConstant>);
        *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = TCGet(bc->m_value);
        Dispatch(rc, stackframe, bcu + sizeof(BcConstant));
    }
} __attribute__((__packed__));

}   // namespace ToyLang
