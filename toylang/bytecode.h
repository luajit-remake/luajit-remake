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
    static constexpr size_t x_defaultStackSlots = 10000;

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
        r->m_stackBegin = new TValue[x_defaultStackSlots];
        return r;
    }

    void CloseUpvalues(TValue* base);

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
    int32_t m_variadicRetSlotBegin;

    // The linked list head of the list of open upvalues
    //
    UserHeapPointer<Upvalue> m_upvalueList;

    // The beginning of the stack
    //
    TValue* m_stackBegin;
};

UserHeapPointer<TableObject> CreateGlobalObject(VM* vm);

template<>
inline void VMGlobalDataManager<VM>::CreateRootCoroutine()
{
    // Create global object
    //
    VM* vm = static_cast<VM*>(this);
    UserHeapPointer<TableObject> globalObject = CreateGlobalObject(vm);
    m_rootCoroutine = CoroutineRuntimeContext::Create(vm, globalObject);
}

template<>
inline HeapPtr<TableObject> VMGlobalDataManager<VM>::GetRootGlobalObject()
{
    return m_rootCoroutine->m_globalObject.As();
}

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

class UnlinkedCodeBlock;

// The constant table stores all constants in one bytecode function.
// However, we cannot know if an entry is a TValue or a UnlinkedCodeBlock pointer!
// (this is due to how LuaJIT parser is designed. It can be changed, but we don't want to bother making it more complex)
// This is fine for the mutator as the bytecode will never misuse the type.
// For the GC, we rely on the crazy fact that a user-space raw pointer always falls in [0, 2^47) (high 17 bits = 0, not 1) under practically every OS kernel,
// which mean when a pointer is interpreted as a TValue, it will always be interpreted into a double, so the GC marking
// algorithm will correctly ignore it for the marking.
//
union BytecodeConstantTableEntry
{
    BytecodeConstantTableEntry() : m_ucb(nullptr) { }
    TValue m_tv;
    UnlinkedCodeBlock* m_ucb;
};
static_assert(sizeof(BytecodeConstantTableEntry) == 8);

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
    static TValue GetConstantAsTValue(T self, int64_t ordinal)
    {
        assert(-static_cast<int64_t>(self->m_owner->m_cstTableLength) <= ordinal && ordinal < 0);
        return TCGet(GetConstantTableEnd(self)[ordinal]).m_tv;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, CodeBlock>>>
    static UnlinkedCodeBlock* GetConstantAsUnlinkedCodeBlock(T self, int64_t ordinal)
    {
        assert(-static_cast<int64_t>(self->m_owner->m_cstTableLength) <= ordinal && ordinal < 0);
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
    UpvalueMetadata* m_upvalueInfo;
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
    Type m_type;
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

    static UserHeapPointer<FunctionObject> WARN_UNUSED CreateAndFillUpvalues(UnlinkedCodeBlock* ucb, CodeBlock* cb, CoroutineRuntimeContext* rc, TValue* stackFrameBase, HeapPtr<FunctionObject> parent)
    {
        assert(cb->m_owner == ucb);
        HeapPtr<FunctionObject> r = Create(VM::GetActiveVMForCurrentThread(), cb).As();
        assert(TranslateToRawPointer(TCGet(parent->m_executable).As())->IsBytecodeFunction());
        assert(cb->m_owner->m_parent == static_cast<HeapPtr<CodeBlock>>(TCGet(parent->m_executable).As())->m_owner);
        uint32_t numUpvalues = cb->m_numUpvalues;
        UpvalueMetadata* upvalueInfo = ucb->m_upvalueInfo;
        for (uint32_t ord = 0; ord < numUpvalues; ord++)
        {
            UpvalueMetadata& uvmt = upvalueInfo[ord];
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

    static ScriptModule* WARN_UNUSED ParseFromJSON(VM* vm, const std::string& content)
    {
        return ParseFromJSON(vm, vm->GetRootGlobalObject(), content);
    }
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

// The return statement is required to fill nil up to x_minNilFillReturnValues values even if it returns less than that many values
//
constexpr uint32_t x_minNilFillReturnValues = 3;

// stack frame format:
//     [... VarArgs ...] [Header] [... Locals ...]
//                                ^
//                                stack frame pointer (sfp)
//
class alignas(8) StackFrameHeader
{
public:
    // The function corresponding to this stack frame
    // Must be first element: this is expected by call opcode
    //
    HeapPtr<FunctionObject> m_func;
    // The address of the caller stack frame (points to the END of the stack frame header)
    //
    void* m_caller;
    // The return address
    //
    void* m_retAddr;
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
static constexpr size_t x_numSlotsForStackFrameHeader = sizeof(StackFrameHeader) / sizeof(TValue);

inline void VM::LaunchScript(ScriptModule* module)
{
    CoroutineRuntimeContext* rc = GetRootCoroutine();
    CodeBlock* cb = static_cast<CodeBlock*>(TranslateToRawPointer(TCGet(module->m_defaultEntryPoint.As()->m_executable).As()));
    rc->m_codeBlock = cb;
    assert(cb->m_numFixedArguments == 0);
    StackFrameHeader* sfh = reinterpret_cast<StackFrameHeader*>(rc->m_stackBegin);
    sfh->m_caller = nullptr;
    sfh->m_retAddr = reinterpret_cast<void*>(LaunchScriptReturnEndpoint);
    sfh->m_func = module->m_defaultEntryPoint.As();
    sfh->m_callerBytecodeOffset = 0;
    sfh->m_numVariadicArguments = 0;
    void* stackbase = sfh + 1;
    cb->m_bestEntryPoint(rc, stackbase, cb->m_bytecode, 0 /*unused*/);
}

inline UserHeapPointer<FunctionObject> WARN_UNUSED GetCallTargetConsideringMetatableAndFixCallParameters(TValue* begin, uint32_t& numParams /*inout*/)
{
    TValue func = *begin;
    GetCallTargetConsideringMetatableResult res = GetCallTargetConsideringMetatable(func);
    if (likely(!res.m_invokedThroughMetatable))
    {
        return res.m_target;
    }

    assert(res.m_target.m_value != 0);
    memmove(begin + x_numSlotsForStackFrameHeader + 1, begin + x_numSlotsForStackFrameHeader, sizeof(TValue) * numParams);
    begin[x_numSlotsForStackFrameHeader] = func;
    begin[0] = TValue::CreatePointer(res.m_target);
    numParams++;
    return res.m_target;
}

#define LJR_LIB_DO_RETURN(rc, hdr, retStart, numRet)                                        \
    do {                                                                                    \
        StackFrameHeader* doReturnTmp_hdr = (hdr);                                          \
        InterpreterFn doReturnTmp_retAddr = reinterpret_cast<InterpreterFn>(                \
            doReturnTmp_hdr->m_retAddr);                                                    \
        void* doReturnTmp_retCallerSf = doReturnTmp_hdr->m_caller;                          \
        TValue* doReturnTmp_retStart = (retStart);                                          \
        uint64_t doReturnTmp_numRet = (numRet);                                             \
        uint64_t doReturnTmp_idx = doReturnTmp_numRet;                                      \
        while (doReturnTmp_idx < x_minNilFillReturnValues) {                                \
            doReturnTmp_retStart[doReturnTmp_idx] = TValue::Nil();                          \
            doReturnTmp_idx++;                                                              \
        }                                                                                   \
        [[clang::musttail]] return doReturnTmp_retAddr(                                     \
                (rc), doReturnTmp_retCallerSf,                                              \
                reinterpret_cast<uint8_t*>(doReturnTmp_retStart), doReturnTmp_numRet);      \
    } while (false)

inline TValue WARN_UNUSED MakeErrorMessage(const char* msg)
{
    return TValue::CreatePointer(VM::GetActiveVMForCurrentThread()->CreateStringObjectFromRawString(msg, static_cast<uint32_t>(strlen(msg))));
}

inline void ThrowError(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t errorObjectU64);

inline void LJR_LIB_BASE_pairs(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
    uint32_t numParams = hdr->m_numVariadicArguments;
    if (numParams < 1)
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #1 to 'pairs' (table expected, got no value)").m_value);
    }
    TValue* addr = reinterpret_cast<TValue*>(hdr) - 1;
    TValue input = *addr;
    if (!input.IsPointer(TValue::x_mivTag) || input.AsPointer<UserHeapGcObjectHeader>().As()->m_type != Type::TABLE)
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #1 to 'pairs' (table expected)").m_value);
    }

    TValue* ret = reinterpret_cast<TValue*>(sfp);
    ret[0] = VM::GetActiveVMForCurrentThread()->GetLibBaseDotNextFunctionObject();
    ret[1] = input;
    ret[2] = TValue::Nil();

    LJR_LIB_DO_RETURN(rc, hdr, ret, 3 /*numRetValues*/);
}

inline void LJR_LIB_BASE_next(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
    uint32_t numParams = hdr->m_numVariadicArguments;
    TValue* vaBegin = reinterpret_cast<TValue*>(hdr) - numParams;
    if (numParams < 1)
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #1 to 'next' (table expected, got no value)").m_value);
    }

    TValue tab = vaBegin[0];
    if (!tab.IsPointer(TValue::x_mivTag) || tab.AsPointer<UserHeapGcObjectHeader>().As()->m_type != Type::TABLE)
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #1 to 'next' (table expected)").m_value);
    }
    HeapPtr<TableObject> tabObj = tab.AsPointer<TableObject>().As();

    TValue key;
    if (numParams >= 2)
    {
        key = vaBegin[1];
    }
    else
    {
        key =  TValue::Nil();
    }

    TableObjectIterator::KeyValuePair* ret = reinterpret_cast<TableObjectIterator::KeyValuePair*>(sfp);
    bool success = TableObjectIterator::GetNextFromKey(tabObj, key, *ret /*out*/);
    if (!success)
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("invalid key to 'next'").m_value);
    }

    // Lua manual states:
    //     "When called with the last index, or with 'nil' in an empty table, 'next' returns 'nil'."
    // So when end of table is reached, this should return "nil", not "nil, nil"...
    //
    AssertImp(ret->m_key.IsNil(), ret->m_value.IsNil());
    uint64_t numReturnValues = ret->m_key.IsNil() ? 1 : 2;
    LJR_LIB_DO_RETURN(rc, hdr, reinterpret_cast<TValue*>(ret), numReturnValues);
}

inline void LJR_LIB_MATH_sqrt(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
    uint32_t numParams = hdr->m_numVariadicArguments;
    if (numParams < 1)
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #1 to 'sqrt' (number expected, got no value)").m_value);
    }
    TValue* addr = reinterpret_cast<TValue*>(hdr) - 1;
    TValue input = *addr;
    double inputDouble;
    if (input.IsDouble(TValue::x_int32Tag))
    {
        inputDouble = input.AsDouble();
    }
    else if (input.IsInt32(TValue::x_int32Tag))
    {
        inputDouble = input.AsInt32();
    }
    else
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #1 to 'sqrt' (number expected)").m_value);
    }

    double result = sqrt(inputDouble);
    *addr = TValue::CreateDouble(result);

    LJR_LIB_DO_RETURN(rc, hdr, addr, 1 /*numRetValues*/);
}

inline void PrintTValue(FILE* fp, TValue val)
{
    if (val.IsInt32(TValue::x_int32Tag))
    {
        fprintf(fp, "%d", static_cast<int>(val.AsInt32()));
    }
    else if (val.IsDouble(TValue::x_int32Tag))
    {
        double dbl = val.AsDouble();
        char buf[x_default_tostring_buffersize_double];
        StringifyDoubleUsingDefaultLuaFormattingOptions(buf /*out*/, dbl);
        fprintf(fp, "%s", buf);
    }
    else if (val.IsMIV(TValue::x_mivTag))
    {
        MiscImmediateValue miv = val.AsMIV(TValue::x_mivTag);
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
        assert(val.IsPointer(TValue::x_mivTag));
        UserHeapGcObjectHeader* p = TranslateToRawPointer(val.AsPointer<UserHeapGcObjectHeader>().As());
        if (p->m_type == Type::STRING)
        {
            HeapString* hs = reinterpret_cast<HeapString*>(p);
            fwrite(hs->m_string, sizeof(char), hs->m_length /*length*/, fp);
        }
        else
        {
            if (p->m_type == Type::FUNCTION)
            {
                fprintf(fp, "function");
            }
            else if (p->m_type == Type::TABLE)
            {
                fprintf(fp, "table");
            }
            else if (p->m_type == Type::THREAD)
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

inline void LJR_LIB_BASE_print(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> /*bcu*/, uint64_t /*unused*/)
{
    FILE* fp = VM::GetActiveVMForCurrentThread()->GetStdout();
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
    uint32_t numElementsToPrint = hdr->m_numVariadicArguments;
    TValue* vaBegin = reinterpret_cast<TValue*>(hdr) - numElementsToPrint;
    for (uint32_t i = 0; i < numElementsToPrint; i++)
    {
        if (i > 0)
        {
            fprintf(fp, "\t");
        }
        PrintTValue(fp, vaBegin[i]);
    }
    fprintf(fp, "\n");

    LJR_LIB_DO_RETURN(rc, hdr, reinterpret_cast<TValue*>(sfp), 0 /*numRetValues*/);
}

inline void LJR_LIB_IO_write(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
{
    FILE* fp = VM::GetActiveVMForCurrentThread()->GetStdout();
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
    uint32_t numElementsToPrint = hdr->m_numVariadicArguments;
    TValue* vaBegin = reinterpret_cast<TValue*>(hdr) - numElementsToPrint;
    bool success = true;
    for (uint32_t i = 0; i < numElementsToPrint; i++)
    {
        TValue val = vaBegin[i];
        if (val.IsInt32(TValue::x_int32Tag))
        {
            char buf[x_default_tostring_buffersize_int];
            char* bufEnd = StringifyInt32UsingDefaultLuaFormattingOptions(buf /*out*/, val.AsInt32());
            size_t len = static_cast<size_t>(bufEnd - buf);
            size_t written = fwrite(buf, 1, len, fp);
            if (unlikely(len != written)) { success = false; break; }
        }
        else if (val.IsDouble(TValue::x_int32Tag))
        {
            double dbl = val.AsDouble();
            char buf[x_default_tostring_buffersize_double];
            char* bufEnd = StringifyDoubleUsingDefaultLuaFormattingOptions(buf /*out*/, dbl);
            size_t len = static_cast<size_t>(bufEnd - buf);
            size_t written = fwrite(buf, 1, len, fp);
            if (unlikely(len != written)) { success = false; break; }
        }
        else if (val.IsPointer(TValue::x_mivTag))
        {
            UserHeapGcObjectHeader* p = TranslateToRawPointer(val.AsPointer<UserHeapGcObjectHeader>().As());
            if (p->m_type == Type::STRING)
            {
                HeapString* hs = reinterpret_cast<HeapString*>(p);
                size_t written = fwrite(hs->m_string, sizeof(char), hs->m_length /*length*/, fp);
                if (unlikely(hs->m_length != written)) { success = false; break; }
            }
            else
            {
                // TODO: make error message consistent with Lua
                [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument to 'write' (string expected)").m_value);
            }
        }
        else
        {
            // TODO: make error message consistent with Lua
            [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument to 'write' (string expected)").m_value);
        }
    }

    TValue* ret = reinterpret_cast<TValue*>(sfp);
    uint64_t numRetVals;
    if (likely(success))
    {
        numRetVals = 1;
        ret[0] = TValue::CreateTrue();
    }
    else
    {
        int err = errno;
        ret[0] = TValue::Nil();
        const char* errstr = strerror(err);
        ret[1] = TValue::CreatePointer(VM::GetActiveVMForCurrentThread()->CreateStringObjectFromRawString(errstr, static_cast<uint32_t>(strlen(errstr))));
        ret[2] = TValue::CreateInt32(err, TValue::x_int32Tag);
        numRetVals = 3;
    }

    LJR_LIB_DO_RETURN(rc, hdr, ret, numRetVals);
}

void* WARN_UNUSED SetupFrameForCall(CoroutineRuntimeContext* rc, void* sfp, TValue* begin, HeapPtr<FunctionObject> target, uint32_t numFixedParamsToPass, InterpreterFn onReturn, bool passVariadicRetAsParam);

// Design for pcall/xpcall:
//     There are two return function: 'onSuccessReturn' and 'onErrorReturn'.
//     pcall/xpcall always calls the callee using 'onSuccessReturn' as the return function.
//     So if the Lua code did not encounter an error, 'onSuccessReturn' will take control, which simply generates the pcall/xpcall return values for the success case and return to parent.
//     That is, the stack looks like this:
//     [ ... Lua ... ] [ pcall ] [ Lua function being called ] [ .. more call frames .. ]
//                                 ^
//                       return = onSuccessReturn

//     If the Lua code encounters an error, we will walk the stack to find the first stack frame with 'return = onSuccessReturn'.
//     This allows us to locate the pcall/xpcall call frame.
//
//     pcall/xpcall always reserve local variable slot 0. For pcall, the slot stores 'false', and for xpcall the slot stores 'true'.
//     This allows us to know if the frame is a pcall frame or a xpcall frame.
//
//     Now, for pcall, we can simply return 'false' plus the error object.
//     For xpcall, we will locate the error handler from the xpcall call frame, then call the error handler with return = onErrorReturn.
//     That is, the stack looks like this:
//     [ ... Lua ... ] [ xpcall ] [ Lua function being called ] [ .. more call frames .. ] [ function throwing error ] [ error handler ] .. [ error handler (due to error in error handler) ]
//                                  ^                                                                                    ^                    ^
//                       return = onSuccessReturn                                                               return = onErrorReturn   return = onErrorReturn
//
//     Then 'onErrorReturn' will eventually take control.
//     The 'onErrorReturn' function will actually unwind the stack until the first xpcall call frame it encounters (identified by 'onSuccessReturn').
//     Then it generates the xpcall return values for the error case, and return to the parent of xpcall
//
inline void LJR_LIB_OnProtectedCallSuccessReturn(CoroutineRuntimeContext* rc, void* stackframe, const uint8_t* retValuesU, uint64_t numRetValues)
{
    // 'stackframe' is the stack frame for pcall/xpcall
    //
    // Return value should be 'true' plus everything returned by callee
    // Note that we reserved local variable slot 0, so 'retValuesU' must be at least at slot 2,
    // so we can overwrite 'retValuesU[-1]' without worrying about clobbering anything
    //
    TValue* previousSlot = reinterpret_cast<TValue*>(const_cast<uint8_t*>(retValuesU)) - 1;
    assert(previousSlot >= stackframe);
    *previousSlot = TValue::CreateBoolean(true);
    numRetValues++;

    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackframe);
    using RetFn = void(*)(CoroutineRuntimeContext* /*rc*/, void* /*sfp*/, uint8_t* /*retValuesStart*/, uint64_t /*numRetValues*/);
    RetFn retAddr = reinterpret_cast<RetFn>(hdr->m_retAddr);
    void* callerSf = hdr->m_caller;
    [[clang::musttail]] return retAddr(rc, callerSf, reinterpret_cast<uint8_t*>(previousSlot), numRetValues);
}

inline void LJR_LIB_OnProtectedCallErrorReturn(CoroutineRuntimeContext* rc, void* stackframe, const uint8_t* retValuesU, uint64_t numRetValues)
{
    // 'stackframe' is the stack frame for the last function throwing out the error
    // Construct the return values now. Lua 5.1 doesn't have to-be-closed variables, so we can simply overwrite at 'stackframe'
    //
    // Note that Lua discards all but the first return value from error handler, and if error handler returns no value, a nil is added
    //
    TValue* retValues;
    if (numRetValues == 0)
    {
        retValues = reinterpret_cast<TValue*>(stackframe);
        retValues[0] = TValue::CreateFalse();
        retValues[1] = TValue::Nil();
        numRetValues = 2;
    }
    else
    {
        TValue val = *reinterpret_cast<const TValue*>(retValuesU);
        retValues = reinterpret_cast<TValue*>(stackframe);
        retValues[0] = TValue::CreateFalse();
        retValues[1] = val;
        numRetValues = 2;
    }

    // Now we need to walk the stack and identify the xpcall call frame
    // This must be successful, since we won't be called unless the pcall/xpcall call frame has been identified by the error path
    //
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackframe);
    while (true)
    {
        if (hdr->m_retAddr == reinterpret_cast<void*>(LJR_LIB_OnProtectedCallSuccessReturn))
        {
            break;
        }
        hdr = reinterpret_cast<StackFrameHeader*>(hdr->m_caller) - 1;
        assert(hdr != nullptr);
    }

    // Now 'hdr' is the callee of the pcall/xpcall
    // We need to return to the caller of pcall/xpcall, so we need to walk up one more frame and return from there
    //
    hdr = reinterpret_cast<StackFrameHeader*>(hdr->m_caller) - 1;
    assert(hdr != nullptr);

    LJR_LIB_DO_RETURN(rc, hdr, retValues, numRetValues);
}

constexpr size_t x_lua_max_nested_error_count = 50;

inline TValue WARN_UNUSED MakeErrorMessageForTooManyNestedErrors()
{
    const char* errstr = "error in error handling";
    return MakeErrorMessage(errstr);
}

inline TValue WARN_UNUSED MakeErrorMessageForUnableToCall(TValue badValue)
{
    // The Lua message is "attmpt to call a (type of badValue) value"
    //
    char msg[100];
    auto makeMsg = [&](const char* ty)
    {
        snprintf(msg, 100, "attempt to call a %s value", ty);
    };
    auto makeMsg2 = [&](int d)
    {
        snprintf(msg, 100, "attempt to call a (internal type %d) value", d);
    };

    if (badValue.IsInt32(TValue::x_int32Tag))
    {
        makeMsg("number");
    }
    else if (badValue.IsDouble(TValue::x_int32Tag))
    {
        makeMsg("number");
    }
    else if (badValue.IsMIV(TValue::x_mivTag))
    {
        MiscImmediateValue miv = badValue.AsMIV(TValue::x_mivTag);
        if (miv.IsNil())
        {
            makeMsg("nil");
        }
        else
        {
            assert(miv.IsBoolean());
            makeMsg("boolean");
        }
    }
    else
    {
        assert(badValue.IsPointer(TValue::x_mivTag));
        UserHeapGcObjectHeader* p = TranslateToRawPointer(badValue.AsPointer<UserHeapGcObjectHeader>().As());
        if (p->m_type == Type::STRING)
        {
            makeMsg("string");
        }
        else if (p->m_type == Type::FUNCTION)
        {
            makeMsg("function");
        }
        else if (p->m_type == Type::TABLE)
        {
            makeMsg("table");
        }
        else if (p->m_type == Type::THREAD)
        {
            makeMsg("thread");
        }
        else
        {
            // TODO: handle userdata type
            //
            makeMsg2(static_cast<int>(p->m_type));
        }
    }
    return MakeErrorMessage(msg);
}

inline void ThrowError(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t errorObjectU64)
{
    std::ignore = bcu;  // TODO: convert bytecode location to source code location

    TValue errorObject; errorObject.m_value = errorObjectU64;

    size_t nestedErrorCount = 0;
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
    while (true)
    {
        if (hdr->m_retAddr == reinterpret_cast<void*>(LJR_LIB_OnProtectedCallSuccessReturn))
        {
            break;
        }
        if (hdr->m_retAddr == reinterpret_cast<void*>(LJR_LIB_OnProtectedCallErrorReturn))
        {
            nestedErrorCount++;
        }
        hdr = reinterpret_cast<StackFrameHeader*>(hdr->m_caller);
        if (hdr == nullptr)
        {
            break;
        }
        hdr = hdr - 1;
    }

    if (hdr == nullptr)
    {
        // There is no pcall/xpcall on the stack
        // TODO: make the output message and behavior consistent with Lua in this case
        //
        FILE* fp = VM::GetActiveVMForCurrentThread()->GetStderr();
        fprintf(fp, "Uncaught error: ");
        PrintTValue(fp, errorObject);
        fprintf(fp, "\n");
        fclose(fp);
        // Also output to stderr if the VM's stderr is redirected to somewhere else
        //
        if (fp != stderr)
        {
            fprintf(stderr, "Uncaught error: ");
            PrintTValue(stderr, errorObject);
            fprintf(stderr, "\n");
            fflush(stderr);
        }
        abort();
    }

    StackFrameHeader* protectedCallFrame = reinterpret_cast<StackFrameHeader*>(hdr->m_caller) - 1;
    assert(protectedCallFrame != nullptr);

    bool isXpcall;
    {
        TValue* locals = reinterpret_cast<TValue*>(protectedCallFrame + 1);
        assert(locals[0].IsMIV(TValue::x_mivTag) && locals[0].AsMIV(TValue::x_mivTag).IsBoolean());
        isXpcall = locals[0].AsMIV(TValue::x_mivTag).GetBooleanValue();
    }

    if (nestedErrorCount > x_lua_max_nested_error_count)
    {
        // Don't call error handler any more, treat this as if it's a pcall
        //
        errorObject = MakeErrorMessageForTooManyNestedErrors();
        goto handle_pcall;
    }

    if (isXpcall)
    {
        // We need to call error handler
        //
        // xpcall has already asserted that two arguments are passed, so we should be able to safely assume so
        //
        assert(protectedCallFrame->m_numVariadicArguments >= 2);
        TValue errHandler = *(reinterpret_cast<TValue*>(protectedCallFrame) - protectedCallFrame->m_numVariadicArguments + 1);

        // Lua 5.4 requires 'errHandler' to be a function.
        // Lua 5.1 doesn't require 'errHandler' to be a function, but ignores its metatable any way.
        // This function is not performance sensitive, so we will make it compatible for both (the xpcall API can check 'errHandler' to implement Lua 5.4 behavior)
        //
        bool isFunction = errHandler.IsPointer(TValue::x_mivTag) && errHandler.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::FUNCTION;
        if (!isFunction)
        {
            // The error handler is not callable, so attempting to call it will result in infinite error recursion
            //
            errorObject = MakeErrorMessageForTooManyNestedErrors();
            goto handle_pcall;
        }

        // Set up the call frame
        //
        UserHeapPointer<FunctionObject> handler = errHandler.AsPointer<FunctionObject>();
        uint32_t stackFrameSize;
        if (rc->m_codeBlock->IsBytecodeFunction())
        {
            stackFrameSize = rc->m_codeBlock->m_stackFrameNumSlots;
        }
        else
        {
            // ThrowError() is called from a C function.
            // Destroying all its locals should be fine, as the function should never be returned to any way
            //
            stackFrameSize = 0;
        }

        TValue* localsStart = reinterpret_cast<TValue*>(sfp);
        localsStart[stackFrameSize] = TValue::CreatePointer(handler);
        localsStart[stackFrameSize + x_numSlotsForStackFrameHeader] = errorObject;
        void* newStackFrameBase = SetupFrameForCall(
                    rc, sfp, localsStart + stackFrameSize, handler.As(),
                    1 /*numFixedParamsToPass*/, LJR_LIB_OnProtectedCallErrorReturn /*onReturn*/, false /*passVariadicRetsAsParam*/);

        HeapPtr<ExecutableCode> calleeEc = TCGet(handler.As()->m_executable).As();

        uint8_t* calleeBytecode = calleeEc->m_bytecode;
        InterpreterFn calleeFn = calleeEc->m_bestEntryPoint;
        [[clang::musttail]] return calleeFn(rc, newStackFrameBase, calleeBytecode, 0 /*unused*/);
    }
    else
    {
handle_pcall:
        // We should just return 'false' plus the error object
        //
        TValue* retValues = reinterpret_cast<TValue*>(sfp);
        retValues[0] = TValue::CreateFalse();
        retValues[1] = errorObject;

        // We need to return to the caller of 'pcall'
        //
        LJR_LIB_DO_RETURN(rc, protectedCallFrame, retValues, 2 /*numReturnValues*/);
    }
}

inline void LJR_LIB_BASE_error(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
    uint32_t numArgs = hdr->m_numVariadicArguments;
    TValue* vaBegin = reinterpret_cast<TValue*>(hdr) - numArgs;

    TValue errorObject;
    if (numArgs == 0)
    {
        errorObject = TValue::Nil();
    }
    else
    {
        errorObject = vaBegin[0];
    }

    // TODO: Lua appends source line information if 'errorObject' is a string or number
    //
    [[clang::musttail]] return ThrowError(rc, sfp, bcu, errorObject.m_value);
}

inline void LJR_LIB_BASE_xpcall(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
    uint32_t numArgs = hdr->m_numVariadicArguments;
    TValue* vaBegin = reinterpret_cast<TValue*>(hdr) - numArgs;

    if (numArgs < 2)
    {
        // Lua always complains about argument #2 even if numArgs == 0, and this error is NOT protected by this xpcall itself
        //
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #2 to 'xpcall' (value expected)").m_value);
    }

    // Any error below should be protected by the xpcall
    //

    // Write the identification boolean at local 0. This will be read by the stack walker
    //
    *reinterpret_cast<TValue*>(sfp) = TValue::CreateBoolean(true /*isXpcall*/);

    // There is one edge case: if the first parameter is not a function and cannot be called (through metatable), the error is still protected
    // and the error handler shall be called. However, since the error is not thrown from the function, but from xpcall, there is no stack
    // frame with ret = 'onSuccessReturn', so neither 'onErrorReturn' nor the 'ThrowError' could see the xpcall.
    // To fix this, we create a dummy call frame with ret = 'LJR_LIB_OnProtectedCallSuccessReturn', then invoke the error handler. This is what
    // the function below does.
    //
    auto createDummyFrameAndSetupFrameForErrorHandler = [](StackFrameHeader* curFrameHdr, UserHeapPointer<FunctionObject> errHandler, TValue errorObject) WARN_UNUSED -> TValue* /*paramForSetupCallFrame*/
    {
        // Note that the xpcall frame has one local, that's what the +1 in line below accounts for
        //
        StackFrameHeader* dummyFrameHdr = reinterpret_cast<StackFrameHeader*>(reinterpret_cast<TValue*>(curFrameHdr) + x_numSlotsForStackFrameHeader + 1);
        dummyFrameHdr->m_caller = curFrameHdr + 1;
        dummyFrameHdr->m_numVariadicArguments = 0;
        dummyFrameHdr->m_retAddr = reinterpret_cast<void*>(LJR_LIB_OnProtectedCallSuccessReturn);
        dummyFrameHdr->m_func = curFrameHdr->m_func;

        TValue* errHandlerFrameStart = reinterpret_cast<TValue*>(dummyFrameHdr + 1);
        errHandlerFrameStart[0] = TValue::CreatePointer(errHandler);
        errHandlerFrameStart[x_numSlotsForStackFrameHeader] = errorObject;
        return errHandlerFrameStart;
    };

    TValue* callStart = reinterpret_cast<TValue*>(sfp) + 1;
    *callStart = vaBegin[0];
    uint32_t numParams = 0;
    UserHeapPointer<FunctionObject> functionTarget = GetCallTargetConsideringMetatableAndFixCallParameters(callStart, numParams /*inout*/);

    if (functionTarget.m_value == 0)
    {
        // The function is not callable, so we should call the error handler.
        // Now, check if the error handler is a FunctionObject
        // Note that Lua won't consider the metatable of the error handler: it must be a function in order to be called
        //
        TValue errHandler = vaBegin[1];
        if (errHandler.IsPointer(TValue::x_mivTag) && errHandler.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::FUNCTION)
        {
            // The error handler is a function. We need to set up the dummy frame (see comments earlier) and call it
            //
            UserHeapPointer<FunctionObject> errHandlerFn = errHandler.AsPointer<FunctionObject>();
            TValue* slotStart = createDummyFrameAndSetupFrameForErrorHandler(hdr /*curFrameHdr*/, errHandlerFn, MakeErrorMessageForUnableToCall(vaBegin[0]));
            void* newStackFrameBase = SetupFrameForCall(
                        rc, slotStart, slotStart, errHandlerFn.As(),
                        1 /*numFixedParamsToPass*/, LJR_LIB_OnProtectedCallErrorReturn /*onReturn*/, false /*passVariadicRetsAsParam*/);
            HeapPtr<ExecutableCode> calleeEc = TCGet(errHandlerFn.As()->m_executable).As();
            uint8_t* calleeBytecode = calleeEc->m_bytecode;
            InterpreterFn calleeFn = calleeEc->m_bestEntryPoint;
            [[clang::musttail]] return calleeFn(rc, newStackFrameBase, calleeBytecode, 0 /*unused*/);
        }
        else
        {
            // The error handler is not a function (note that Lua ignores the metatable here)
            // So calling the error handler will result in infinite error recursion
            // So we should return false + "error in error handler"
            //
            TValue* retStart = reinterpret_cast<TValue*>(sfp);
            retStart[0] = TValue::CreateBoolean(false);
            retStart[1] = MakeErrorMessageForTooManyNestedErrors();

            LJR_LIB_DO_RETURN(rc, hdr, retStart, 2 /*numReturnValues*/);
        }
    }

    // Now we know the function is callable, call it
    //
    void* newStackFrameBase = SetupFrameForCall(rc, sfp, callStart, functionTarget.As(), numParams, LJR_LIB_OnProtectedCallSuccessReturn /*onReturn*/, false /*passVariadicRetAsParam*/);

    HeapPtr<ExecutableCode> calleeEc = TCGet(functionTarget.As()->m_executable).As();
    uint8_t* calleeBytecode = calleeEc->m_bytecode;
    InterpreterFn calleeFn = calleeEc->m_bestEntryPoint;
    [[clang::musttail]] return calleeFn(rc, newStackFrameBase, calleeBytecode, 0 /*unused*/);
}

inline void LJR_LIB_BASE_pcall(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
    uint32_t numArgs = hdr->m_numVariadicArguments;
    TValue* vaBegin = reinterpret_cast<TValue*>(hdr) - numArgs;

    if (numArgs == 0)
    {
        // This error is NOT protected by this pcall itself
        //
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #1 to 'pcall' (value expected)").m_value);
    }

    // Any error below should be protected by the pcall
    //

    // Write the identification boolean at local 0. This will be read by the stack walker
    //
    *reinterpret_cast<TValue*>(sfp) = TValue::CreateBoolean(false /*isXpcall*/);

    TValue* callStart = reinterpret_cast<TValue*>(sfp) + 1;
    *callStart = vaBegin[0];
    uint32_t numArgsToCall = numArgs - 1;
    SafeMemcpy(callStart + x_numSlotsForStackFrameHeader, vaBegin + 1, sizeof(TValue) * numArgsToCall);
    UserHeapPointer<FunctionObject> functionTarget = GetCallTargetConsideringMetatableAndFixCallParameters(callStart, numArgsToCall /*inout*/);

    if (functionTarget.m_value == 0)
    {
        TValue* retStart = reinterpret_cast<TValue*>(sfp);
        retStart[0] = TValue::CreateBoolean(false);
        retStart[1] = MakeErrorMessageForUnableToCall(vaBegin[0]);

        LJR_LIB_DO_RETURN(rc, hdr, retStart, 2 /*numReturnValues*/);
    }

    void* newStackFrameBase = SetupFrameForCall(rc, sfp, callStart, functionTarget.As(), numArgsToCall, LJR_LIB_OnProtectedCallSuccessReturn /*onReturn*/, false /*passVariadicRetAsParam*/);

    HeapPtr<ExecutableCode> calleeEc = TCGet(functionTarget.As()->m_executable).As();
    uint8_t* calleeBytecode = calleeEc->m_bytecode;
    InterpreterFn calleeFn = calleeEc->m_bestEntryPoint;
    [[clang::musttail]] return calleeFn(rc, newStackFrameBase, calleeBytecode, 0 /*unused*/);
}

inline void LJR_LIB_BASE_getmetatable(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
    uint32_t numArgs = hdr->m_numVariadicArguments;
    if (numArgs == 0)
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #1 to 'getmetatable' (value expected)").m_value);
    }

    TValue* vaBegin = reinterpret_cast<TValue*>(hdr) - numArgs;
    TValue value = vaBegin[0];
    UserHeapPointer<void> metatableMaybeNull = GetMetatableForValue(value);

    TValue* ret = reinterpret_cast<TValue*>(sfp);
    if (metatableMaybeNull.m_value == 0)
    {
        ret[0] = TValue::Nil();
    }
    else
    {
        HeapPtr<TableObject> tableObj = metatableMaybeNull.As<TableObject>();
        UserHeapPointer<HeapString> prop = VM_GetStringNameForMetatableKind(LuaMetamethodKind::ProtectedMt);
        GetByIdICInfo icInfo;
        TableObject::PrepareGetById(tableObj, prop, icInfo /*out*/);
        TValue result = TableObject::GetById(tableObj, prop.As<void>(), icInfo);
        if (result.IsNil())
        {
            ret[0] = TValue::CreatePointer(metatableMaybeNull);
        }
        else
        {
            ret[0] = result;
        }
    }

    LJR_LIB_DO_RETURN(rc, hdr, ret, 1 /*numReturnValues*/);
}

inline void LJR_LIB_DEBUG_getmetatable(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
    uint32_t numArgs = hdr->m_numVariadicArguments;
    if (numArgs == 0)
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #1 to 'getmetatable' (value expected)").m_value);
    }

    TValue* vaBegin = reinterpret_cast<TValue*>(hdr) - numArgs;
    TValue value = vaBegin[0];
    UserHeapPointer<void> metatableMaybeNull = GetMetatableForValue(value);

    TValue* ret = reinterpret_cast<TValue*>(sfp);
    if (metatableMaybeNull.m_value == 0)
    {
        ret[0] = TValue::Nil();
    }
    else
    {
        ret[0] = TValue::CreatePointer(metatableMaybeNull);
    }

    LJR_LIB_DO_RETURN(rc, hdr, ret, 1 /*numReturnValues*/);
}

inline void LJR_LIB_BASE_setmetatable(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
    uint32_t numArgs = hdr->m_numVariadicArguments;
    if (numArgs == 0)
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #1 to 'setmetatable' (table expected, got no value)").m_value);
    }

    TValue* vaBegin = reinterpret_cast<TValue*>(hdr) - numArgs;
    TValue value = vaBegin[0];
    if (!value.IsPointer(TValue::x_mivTag) || value.AsPointer<UserHeapGcObjectHeader>().As()->m_type != Type::TABLE)
    {
        // TODO: make this error message consistent with Lua
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #1 to 'setmetatable' (table expected)").m_value);
    }

    if (numArgs == 1)
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #2 to 'setmetatable' (nil or table expected)").m_value);
    }

    TValue mt = vaBegin[1];
    if (!mt.IsNil() && !(mt.IsPointer(TValue::x_mivTag) && mt.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::TABLE))
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #2 to 'setmetatable' (nil or table expected)").m_value);
    }

    VM* vm = VM::GetActiveVMForCurrentThread();
    TableObject* obj = TranslateToRawPointer(vm, value.AsPointer<TableObject>().As());

    UserHeapPointer<void> metatableMaybeNull = TableObject::GetMetatable(obj).m_result;
    if (metatableMaybeNull.m_value != 0)
    {
        HeapPtr<TableObject> existingMetatable = metatableMaybeNull.As<TableObject>();
        UserHeapPointer<HeapString> prop = VM_GetStringNameForMetatableKind(LuaMetamethodKind::ProtectedMt);
        GetByIdICInfo icInfo;
        TableObject::PrepareGetById(existingMetatable, prop, icInfo /*out*/);
        TValue result = TableObject::GetById(existingMetatable, prop.As<void>(), icInfo);
        if (!result.IsNil())
        {
            [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("cannot change a protected metatable").m_value);
        }
    }

    if (!mt.IsNil())
    {
        obj->SetMetatable(vm, mt.AsPointer());
    }
    else
    {
        obj->RemoveMetatable(vm);
    }

    // The return value of 'setmetatable' is the original object
    //
    TValue* ret = reinterpret_cast<TValue*>(sfp);
    *ret = value;

    LJR_LIB_DO_RETURN(rc, hdr, ret, 1 /*numReturnValues*/);
}

inline void LJR_LIB_DEBUG_setmetatable(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
    uint32_t numArgs = hdr->m_numVariadicArguments;

    if (numArgs <= 1)
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #2 to 'setmetatable' (nil or table expected)").m_value);
    }

    TValue* vaBegin = reinterpret_cast<TValue*>(hdr) - numArgs;
    TValue value = vaBegin[0];
    TValue mt = vaBegin[1];
    if (!mt.IsNil() && !(mt.IsPointer(TValue::x_mivTag) && mt.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::TABLE))
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #2 to 'setmetatable' (nil or table expected)").m_value);
    }

    auto setExoticMetatable = [&](UserHeapPointer<void>& metatable)
    {
        if (mt.IsNil())
        {
            metatable = UserHeapPointer<void>();
        }
        else
        {
            metatable = mt.AsPointer();
        }
    };

    if (value.IsPointer(TValue::x_mivTag))
    {
        Type ty = value.AsPointer<UserHeapGcObjectHeader>().As()->m_type;

        if (ty == Type::TABLE)
        {
            VM* vm = VM::GetActiveVMForCurrentThread();
            TableObject* obj = TranslateToRawPointer(vm, value.AsPointer<TableObject>().As());
            if (!mt.IsNil())
            {
                obj->SetMetatable(vm, mt.AsPointer());
            }
            else
            {
                obj->RemoveMetatable(vm);
            }
        }
        else if (ty == Type::STRING)
        {
            setExoticMetatable(VM::GetActiveVMForCurrentThread()->m_metatableForString);
        }
        else if (ty == Type::FUNCTION)
        {
            setExoticMetatable(VM::GetActiveVMForCurrentThread()->m_metatableForFunction);
        }
        else if (ty == Type::THREAD)
        {
            setExoticMetatable(VM::GetActiveVMForCurrentThread()->m_metatableForCoroutine);
        }
        else
        {
            // TODO: support USERDATA
            ReleaseAssert(false && "unimplemented");
        }
    }
    else if (value.IsMIV(TValue::x_mivTag))
    {
        if (value.IsNil())
        {
            setExoticMetatable(VM::GetActiveVMForCurrentThread()->m_metatableForNil);
        }
        else
        {
            assert(value.AsMIV(TValue::x_mivTag).IsBoolean());
            setExoticMetatable(VM::GetActiveVMForCurrentThread()->m_metatableForBoolean);
        }
    }
    else
    {
        assert(value.IsDouble(TValue::x_int32Tag) || value.IsInt32(TValue::x_int32Tag));
        setExoticMetatable(VM::GetActiveVMForCurrentThread()->m_metatableForNumber);
    }

    // The return value of 'debug.setmetatable' is 'true' in Lua 5.1, but the original object in Lua 5.2 and higher
    // For now we implement Lua 5.1 behavior
    //
    TValue* ret = reinterpret_cast<TValue*>(sfp);
    *ret = TValue::CreateBoolean(true);

    LJR_LIB_DO_RETURN(rc, hdr, ret, 1 /*numReturnValues*/);
}

inline void LJR_LIB_BASE_rawget(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
    uint32_t numArgs = hdr->m_numVariadicArguments;

    if (numArgs < 2)
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #2 to 'rawget' (value expected)").m_value);
    }

    TValue* vaBegin = reinterpret_cast<TValue*>(hdr) - numArgs;
    TValue base = vaBegin[0];
    TValue index = vaBegin[1];
    if (!(base.IsPointer(TValue::x_mivTag) && base.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::TABLE))
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #1 to 'rawget' (table expected)").m_value);
    }

    HeapPtr<TableObject> tableObj = base.AsPointer().As<TableObject>();
    TValue result;

    if (index.IsInt32(TValue::x_int32Tag))
    {
        GetByIntegerIndexICInfo icInfo;
        TableObject::PrepareGetByIntegerIndex(tableObj, icInfo /*out*/);
        result = TableObject::GetByIntegerIndex(tableObj, index.AsInt32(), icInfo);
    }
    else if (index.IsDouble(TValue::x_int32Tag))
    {
        double indexDouble = index.AsDouble();
        if (unlikely(IsNaN(indexDouble)))
        {
            // Indexing a table by 'NaN' for read is not an error, but always results in nil,
            // because indexing a table by 'NaN' for write is an error
            //
            result = TValue::Nil();
        }
        else
        {
            GetByIntegerIndexICInfo icInfo;
            TableObject::PrepareGetByIntegerIndex(tableObj, icInfo /*out*/);
            result = TableObject::GetByDoubleVal(tableObj, indexDouble, icInfo);
        }
    }
    else if (index.IsPointer(TValue::x_mivTag))
    {
        GetByIdICInfo icInfo;
        TableObject::PrepareGetById(tableObj, index.AsPointer(), icInfo /*out*/);
        result = TableObject::GetById(tableObj, index.AsPointer(), icInfo);
    }
    else
    {
        assert(index.IsMIV(TValue::x_mivTag));
        MiscImmediateValue miv = index.AsMIV(TValue::x_mivTag);
        if (miv.IsNil())
        {
            // Indexing a table by 'nil' for read is not an error, but always results in nil,
            // because indexing a table by 'nil' for write is an error
            //
            result = TValue::Nil();
        }
        else
        {
            assert(miv.IsBoolean());
            UserHeapPointer<HeapString> specialKey = VM_GetSpecialKeyForBoolean(miv.GetBooleanValue());

            GetByIdICInfo icInfo;
            TableObject::PrepareGetById(tableObj, specialKey, icInfo /*out*/);
            result = TableObject::GetById(tableObj, specialKey.As<void>(), icInfo);
        }
    }

    TValue* ret = reinterpret_cast<TValue*>(sfp);
    *ret = result;

    LJR_LIB_DO_RETURN(rc, hdr, ret, 1 /*numReturnValues*/);
}

inline void LJR_LIB_BASE_rawset(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
    uint32_t numArgs = hdr->m_numVariadicArguments;

    if (numArgs < 3)
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #3 to 'rawset' (value expected)").m_value);
    }

    TValue* vaBegin = reinterpret_cast<TValue*>(hdr) - numArgs;
    TValue base = vaBegin[0];
    TValue index = vaBegin[1];
    TValue newValue = vaBegin[2];
    if (!(base.IsPointer(TValue::x_mivTag) && base.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::TABLE))
    {
        [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad argument #1 to 'rawset' (table expected)").m_value);
    }

    HeapPtr<TableObject> tableObj = base.AsPointer().As<TableObject>();

    if (index.IsInt32(TValue::x_int32Tag))
    {
        TableObject::RawPutByValIntegerIndex(tableObj, index.AsInt32(), newValue);
    }
    else if (index.IsDouble(TValue::x_int32Tag))
    {
        double indexDouble = index.AsDouble();
        if (IsNaN(indexDouble))
        {
            [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("table index is NaN").m_value);
        }
        TableObject::PutByValDoubleIndex(tableObj, indexDouble, newValue);
    }
    else if (index.IsPointer(TValue::x_mivTag))
    {
        PutByIdICInfo icInfo;
        TableObject::PreparePutById(tableObj, index.AsPointer(), icInfo /*out*/);
        TableObject::PutById(tableObj, index.AsPointer(), newValue, icInfo);
    }
    else
    {
        assert(index.IsMIV(TValue::x_mivTag));
        MiscImmediateValue miv = index.AsMIV(TValue::x_mivTag);
        if (miv.IsNil())
        {
            [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("table index is nil").m_value);
        }
        assert(miv.IsBoolean());
        UserHeapPointer<HeapString> specialKey = VM_GetSpecialKeyForBoolean(miv.GetBooleanValue());

        PutByIdICInfo icInfo;
        TableObject::PreparePutById(tableObj, specialKey, icInfo /*out*/);
        TableObject::PutById(tableObj, specialKey.As<void>(), newValue, icInfo);
    }

    LJR_LIB_DO_RETURN(rc, hdr, reinterpret_cast<TValue*>(sfp), 0 /*numReturnValues*/);
}

inline UserHeapPointer<TableObject> CreateGlobalObject(VM* vm)
{
    HeapPtr<TableObject> globalObject = TableObject::CreateEmptyGlobalObject(vm);

    auto insertField = [&](HeapPtr<TableObject> r, const char* propName, TValue value)
    {
        UserHeapPointer<HeapString> hs = vm->CreateStringObjectFromRawString(propName, static_cast<uint32_t>(strlen(propName)));
        PutByIdICInfo icInfo;
        TableObject::PreparePutById(r, hs /*prop*/, icInfo /*out*/);
        TableObject::PutById(r, hs.As<void>(), value, icInfo);
    };

    auto insertCFunc = [&](HeapPtr<TableObject> r, const char* propName, InterpreterFn func) -> UserHeapPointer<FunctionObject>
    {
        UserHeapPointer<FunctionObject> funcObj = FunctionObject::CreateCFunc(vm, ExecutableCode::CreateCFunction(vm, func));
        insertField(r, propName, TValue::CreatePointer(funcObj));
        return funcObj;
    };

    auto insertObject = [&](HeapPtr<TableObject> r, const char* propName, uint8_t inlineCapacity) -> HeapPtr<TableObject>
    {
        SystemHeapPointer<Structure> initialStructure = Structure::GetInitialStructureForInlineCapacity(vm, inlineCapacity);
        UserHeapPointer<TableObject> o = TableObject::CreateEmptyTableObject(vm, TranslateToRawPointer(vm, initialStructure.As()), 0 /*initialButterflyArrayPartCapacity*/);
        insertField(r, propName, TValue::CreatePointer(o));
        return o.As();
    };

    insertField(globalObject, "_G", TValue::CreatePointer(UserHeapPointer<TableObject> { globalObject }));

    insertCFunc(globalObject, "print", LJR_LIB_BASE_print);
    insertCFunc(globalObject, "pairs", LJR_LIB_BASE_pairs);
    UserHeapPointer<FunctionObject> nextFn = insertCFunc(globalObject, "next", LJR_LIB_BASE_next);
    vm->InitLibBaseDotNextFunctionObject(TValue::CreatePointer(nextFn));
    insertCFunc(globalObject, "error", LJR_LIB_BASE_error);
    insertCFunc(globalObject, "xpcall", LJR_LIB_BASE_xpcall);
    insertCFunc(globalObject, "pcall", LJR_LIB_BASE_pcall);
    insertCFunc(globalObject, "getmetatable", LJR_LIB_BASE_getmetatable);
    insertCFunc(globalObject, "setmetatable", LJR_LIB_BASE_setmetatable);
    insertCFunc(globalObject, "rawget", LJR_LIB_BASE_rawget);
    insertCFunc(globalObject, "rawset", LJR_LIB_BASE_rawset);

    HeapPtr<TableObject> mathObj = insertObject(globalObject, "math", 32);
    insertCFunc(mathObj, "sqrt", LJR_LIB_MATH_sqrt);

    HeapPtr<TableObject> ioObj = insertObject(globalObject, "io", 16);
    insertCFunc(ioObj, "write", LJR_LIB_IO_write);

    HeapPtr<TableObject> debugObj = insertObject(globalObject, "debug", 16);
    insertCFunc(debugObj, "getmetatable", LJR_LIB_DEBUG_getmetatable);
    insertCFunc(debugObj, "setmetatable", LJR_LIB_DEBUG_setmetatable);

    return globalObject;
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
        return CodeBlock::GetConstantAsTValue(rc->m_codeBlock, ord);
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

#define OPCODE_LIST                     \
    BcUpvalueGet,                       \
    BcUpvaluePut,                       \
    BcUpvalueClose,                     \
    BcTableGetById,                     \
    BcTablePutById,                     \
    BcTableGetByVal,                    \
    BcTablePutByVal,                    \
    BcTableGetByIntegerVal,             \
    BcTablePutByIntegerVal,             \
    BcTableVariadicPutByIntegerValSeq,  \
    BcGlobalGet,                        \
    BcGlobalPut,                        \
    BcTableNew,                         \
    BcTableDup,                         \
    BcReturn,                           \
    BcCall,                             \
    BcTailCall,                         \
    BcVariadicArgsToVariadicRet,        \
    BcPutVariadicArgs,                  \
    BcCallIterator,                     \
    BcIteratorLoopBranch,               \
    BcCallNext,                         \
    BcValidateIsNextAndBranch,          \
    BcNewClosure,                       \
    BcMove,                             \
    BcFillNil,                          \
    BcIsFalsy,                          \
    BcUnaryMinus,                       \
    BcLengthOperator,                   \
    BcAdd,                              \
    BcSub,                              \
    BcMul,                              \
    BcDiv,                              \
    BcMod,                              \
    BcPow,                              \
    BcConcat,                           \
    BcIsEQ,                             \
    BcIsNEQ,                            \
    BcIsLT,                             \
    BcIsNLT,                            \
    BcIsLE,                             \
    BcIsNLE,                            \
    BcCopyAndBranchIfTruthy,            \
    BcCopyAndBranchIfFalsy,             \
    BcBranchIfTruthy,                   \
    BcBranchIfFalsy,                    \
    BcForLoopInit,                      \
    BcForLoopStep,                      \
    BcUnconditionalJump,                \
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

class BcUpvalueGet
{
public:
    using Self = BcUpvalueGet;

    BcUpvalueGet(BytecodeSlot dst, uint16_t index)
        : m_opcode(x_opcodeId<Self>), m_dst(dst), m_index(index)
    { }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        assert(bc->m_dst.IsLocal());
        assert(bc->m_index < StackFrameHeader::GetStackFrameHeader(sfp)->m_func->m_numUpvalues);
        TValue result = *TCGet(StackFrameHeader::GetStackFrameHeader(sfp)->m_func->m_upvalues[bc->m_index]).As()->m_ptr;
        *StackFrameHeader::GetLocalAddr(sfp, bc->m_dst) = result;
        Dispatch(rc, sfp, bcu + sizeof(Self));
    }

    uint8_t m_opcode;
    BytecodeSlot m_dst;
    uint16_t m_index;
} __attribute__((__packed__));

class BcUpvaluePut
{
public:
    using Self = BcUpvaluePut;

    BcUpvaluePut(BytecodeSlot src, uint16_t index)
        : m_opcode(x_opcodeId<Self>), m_src(src), m_index(index)
    { }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        assert(bc->m_index < StackFrameHeader::GetStackFrameHeader(sfp)->m_func->m_numUpvalues);
        TValue src = bc->m_src.Get(rc, sfp);
        *TCGet(StackFrameHeader::GetStackFrameHeader(sfp)->m_func->m_upvalues[bc->m_index]).As()->m_ptr = src;
        Dispatch(rc, sfp, bcu + sizeof(Self));
    }

    uint8_t m_opcode;
    BytecodeSlot m_src;
    uint16_t m_index;
} __attribute__((__packed__));

class BcUpvalueClose
{
public:
    using Self = BcUpvalueClose;

    BcUpvalueClose(BytecodeSlot base)
        : m_opcode(x_opcodeId<Self>), m_base(base), m_offset(0)
    { }

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&Self::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue* tvbase = StackFrameHeader::GetLocalAddr(sfp, bc->m_base);
        rc->CloseUpvalues(tvbase);
        Dispatch(rc, sfp, bcu + bc->m_offset);
    }

    uint8_t m_opcode;
    BytecodeSlot m_base;
    int32_t m_offset;
} __attribute__((__packed__));

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

struct PrepareMetamethodCallResult
{
    bool m_success;
    void* m_baseForNextFrame;
    HeapPtr<ExecutableCode> m_calleeEc;
};

template<auto bytecodeDst>
static void OnReturnFromStoreResultMetamethodCall(CoroutineRuntimeContext* rc, void* stackframe, const uint8_t* retValuesU, uint64_t /*numRetValues*/)
{
    static_assert(std::is_member_object_pointer_v<decltype(bytecodeDst)>);
    using C = class_type_of_member_object_pointer_t<decltype(bytecodeDst)>;
    static_assert(std::is_same_v<BytecodeSlot, value_type_of_member_object_pointer_t<decltype(bytecodeDst)>>);

    const TValue* retValues = reinterpret_cast<const TValue*>(retValuesU);
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackframe);
    HeapPtr<ExecutableCode> callerEc = TCGet(hdr->m_func->m_executable).As();
    assert(TranslateToRawPointer(callerEc)->IsBytecodeFunction());
    uint8_t* callerBytecodeStart = callerEc->m_bytecode;
    ConstRestrictPtr<uint8_t> bcu = callerBytecodeStart + hdr->m_callerBytecodeOffset;
    const C* bc = reinterpret_cast<const C*>(bcu);
    assert(bc->m_opcode == x_opcodeId<C>);
    *StackFrameHeader::GetLocalAddr(stackframe, bc->*bytecodeDst) = *retValues;
    rc->m_codeBlock = static_cast<CodeBlock*>(TranslateToRawPointer(callerEc));
    Dispatch(rc, stackframe, bcu + sizeof(C));
}

template<bool branchIfTruthy, auto bytecodeJumpDst>
static void OnReturnFromComparativeMetamethodCall(CoroutineRuntimeContext* rc, void* stackframe, const uint8_t* retValuesU, uint64_t /*numRetValues*/)
{
    static_assert(std::is_member_object_pointer_v<decltype(bytecodeJumpDst)>);
    using C = class_type_of_member_object_pointer_t<decltype(bytecodeJumpDst)>;
    static_assert(std::is_same_v<int32_t, value_type_of_member_object_pointer_t<decltype(bytecodeJumpDst)>>);

    const TValue* retValues = reinterpret_cast<const TValue*>(retValuesU);
    bool shouldBranch = (branchIfTruthy == retValues[0].IsTruthy());

    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackframe);
    HeapPtr<ExecutableCode> callerEc = TCGet(hdr->m_func->m_executable).As();
    assert(TranslateToRawPointer(callerEc)->IsBytecodeFunction());
    uint8_t* callerBytecodeStart = callerEc->m_bytecode;
    ConstRestrictPtr<uint8_t> bcu = callerBytecodeStart + hdr->m_callerBytecodeOffset;
    const C* bc = reinterpret_cast<const C*>(bcu);
    assert(bc->m_opcode == x_opcodeId<C>);
    rc->m_codeBlock = static_cast<CodeBlock*>(TranslateToRawPointer(callerEc));

    if (shouldBranch)
    {
        Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + (bc->*bytecodeJumpDst)));
    }
    else
    {
        Dispatch(rc, stackframe, bcu + sizeof(C));
    }
}

template<typename BytecodeKind>
static void OnReturnFromNewIndexMetamethodCall(CoroutineRuntimeContext* rc, void* stackframe, const uint8_t* /*retValuesU*/, uint64_t /*numRetValues*/)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackframe);
    HeapPtr<ExecutableCode> callerEc = TCGet(hdr->m_func->m_executable).As();
    assert(TranslateToRawPointer(callerEc)->IsBytecodeFunction());
    uint8_t* callerBytecodeStart = callerEc->m_bytecode;
    ConstRestrictPtr<uint8_t> bcu = callerBytecodeStart + hdr->m_callerBytecodeOffset;
    assert(reinterpret_cast<const BytecodeKind*>(bcu)->m_opcode == x_opcodeId<BytecodeKind>);
    rc->m_codeBlock = static_cast<CodeBlock*>(TranslateToRawPointer(callerEc));

    Dispatch(rc, stackframe, bcu + sizeof(BytecodeKind));
}

template<size_t numArgs>
inline PrepareMetamethodCallResult WARN_UNUSED SetupFrameForMetamethodCall(CoroutineRuntimeContext* rc, void* stackframe, const uint8_t* curBytecode, std::array<TValue, numArgs> args, TValue metamethod, InterpreterFn onReturn)
{
    assert(!metamethod.IsNil());

    TValue* start = reinterpret_cast<TValue*>(stackframe) + rc->m_codeBlock->m_stackFrameNumSlots;
    GetCallTargetConsideringMetatableResult callTarget = GetCallTargetConsideringMetatable(metamethod);
    if (callTarget.m_target.m_value == 0)
    {
        return {
            .m_success = false
        };
    }

    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackframe);
    hdr->m_callerBytecodeOffset = SafeIntegerCast<uint32_t>(curBytecode - rc->m_codeBlock->m_bytecode);

    uint32_t numParamsForMetamethodCall;
    if (unlikely(callTarget.m_invokedThroughMetatable))
    {
        start[0] = TValue::CreatePointer(callTarget.m_target);
        start[x_numSlotsForStackFrameHeader] = metamethod;
        for (size_t i = 0; i < numArgs; i++)
        {
            start[x_numSlotsForStackFrameHeader + i + 1] = args[i];
        }
        numParamsForMetamethodCall = static_cast<uint32_t>(numArgs + 1);
    }
    else
    {
        start[0] = TValue::CreatePointer(callTarget.m_target);
        for (size_t i = 0; i < numArgs; i++)
        {
            start[x_numSlotsForStackFrameHeader + i] = args[i];
        }
        numParamsForMetamethodCall = static_cast<uint32_t>(numArgs);
    }

    void* baseForNextFrame = SetupFrameForCall(rc, stackframe, start, callTarget.m_target.As(), numParamsForMetamethodCall, onReturn, false /*passVariadicRetAsParam*/);

    HeapPtr<ExecutableCode> calleeEc = TCGet(callTarget.m_target.As()->m_executable).As();
    return {
        .m_success = true,
        .m_baseForNextFrame = baseForNextFrame,
        .m_calleeEc = calleeEc
    };
}

class BcTableGetById
{
public:
    using Self = BcTableGetById;

    BcTableGetById(BytecodeSlot base, BytecodeSlot dst, int32_t index)
        : m_opcode(x_opcodeId<Self>), m_base(base), m_dst(dst), m_index(index)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_base;
    BytecodeSlot m_dst;
    int32_t m_index;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        assert(bc->m_base.IsLocal());
        TValue tvbase = *StackFrameHeader::GetLocalAddr(sfp, bc->m_base);

        TValue tvIndex = CodeBlock::GetConstantAsTValue(rc->m_codeBlock, bc->m_index);
        assert(tvIndex.IsPointer(TValue::x_mivTag));
        UserHeapPointer<HeapString> index = tvIndex.AsPointer<HeapString>();

        TValue metamethod;
        while (true)
        {
            if (!tvbase.IsPointer(TValue::x_mivTag))
            {
                goto not_table_object;
            }
            else
            {
                UserHeapPointer<void> base = tvbase.AsPointer<void>();
                if (base.As<UserHeapGcObjectHeader>()->m_type != Type::TABLE)
                {
                    goto not_table_object;
                }

                HeapPtr<TableObject> tableObj = base.As<TableObject>();
                GetByIdICInfo icInfo;
                TableObject::PrepareGetById(tableObj, index, icInfo /*out*/);
                TValue result = TableObject::GetById(tableObj, index.As<void>(), icInfo);

                if (unlikely(icInfo.m_mayHaveMetatable && result.IsNil()))
                {
                    TableObject::GetMetatableResult gmr = TableObject::GetMetatable(tableObj);
                    if (gmr.m_result.m_value != 0)
                    {
                        HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
                        if (unlikely(!TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::Index)))
                        {
                            metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::Index);
                            if (!metamethod.IsNil())
                            {
                                goto handle_metamethod;
                            }
                        }
                    }
                }

                *StackFrameHeader::GetLocalAddr(sfp, bc->m_dst) = result;
                Dispatch(rc, sfp, bcu + sizeof(Self));
            }

not_table_object:
            metamethod = GetMetamethodForValue(tvbase, LuaMetamethodKind::Index);
            if (metamethod.IsNil())
            {
                [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad type for TableGetById").m_value);
            }

handle_metamethod:
            // If 'metamethod' is a function, we should invoke the metamethod, throwing out an error if fail
            // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
            //
            if (likely(metamethod.IsPointer(TValue::x_mivTag)) && metamethod.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::FUNCTION)
            {
                PrepareMetamethodCallResult res = SetupFrameForMetamethodCall(rc, sfp, bcu, std::array { tvbase, tvIndex }, metamethod, OnReturnFromStoreResultMetamethodCall<&Self::m_dst>);
                // We already checked that 'metamethod' is a function, so it must success
                //
                assert(res.m_success);

                uint8_t* calleeBytecode = res.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = res.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, res.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }
            tvbase = metamethod;
        }
    }
} __attribute__((__packed__));

class BcTablePutById
{
public:
    using Self = BcTablePutById;

    BcTablePutById(BytecodeSlot base, BytecodeSlot src, int32_t index)
        : m_opcode(x_opcodeId<Self>), m_base(base), m_src(src), m_index(index)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_base;
    BytecodeSlot m_src;
    int32_t m_index;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        assert(bc->m_base.IsLocal());
        TValue tvbase = *StackFrameHeader::GetLocalAddr(sfp, bc->m_base);

        TValue tvIndex = CodeBlock::GetConstantAsTValue(rc->m_codeBlock, bc->m_index);
        assert(tvIndex.IsPointer(TValue::x_mivTag));
        UserHeapPointer<HeapString> index = tvIndex.AsPointer<HeapString>();
        TValue newValue = *StackFrameHeader::GetLocalAddr(sfp, bc->m_src);

        TValue metamethod;
        while (true)
        {
            if (!tvbase.IsPointer(TValue::x_mivTag))
            {
                goto not_table_object;
            }
            else
            {
                UserHeapPointer<void> base = tvbase.AsPointer<void>();
                if (base.As<UserHeapGcObjectHeader>()->m_type != Type::TABLE)
                {
                    goto not_table_object;
                }

                HeapPtr<TableObject> tableObj = base.As<TableObject>();
                PutByIdICInfo icInfo;
                TableObject::PreparePutById(tableObj, index, icInfo /*out*/);

                if (unlikely(TableObject::PutByIdNeedToCheckMetatable(tableObj, icInfo)))
                {
                    TableObject::GetMetatableResult gmr = TableObject::GetMetatable(tableObj);
                    if (gmr.m_result.m_value != 0)
                    {
                        HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
                        if (unlikely(!TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::NewIndex)))
                        {
                            metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::NewIndex);
                            if (!metamethod.IsNil())
                            {
                                goto handle_metamethod;
                            }
                        }
                    }
                }

                TableObject::PutById(tableObj, index.As<void>(), newValue, icInfo);
                Dispatch(rc, sfp, bcu + sizeof(Self));
            }

not_table_object:
            metamethod = GetMetamethodForValue(tvbase, LuaMetamethodKind::NewIndex);
            if (metamethod.IsNil())
            {
                [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad type for TablePutById").m_value);
            }

handle_metamethod:
            // If 'metamethod' is a function, we should invoke the metamethod, throwing out an error if fail
            // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
            //
            if (likely(metamethod.IsPointer(TValue::x_mivTag)) && metamethod.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::FUNCTION)
            {
                PrepareMetamethodCallResult res = SetupFrameForMetamethodCall(rc, sfp, bcu, std::array { tvbase, tvIndex, newValue }, metamethod, OnReturnFromNewIndexMetamethodCall<Self>);
                // We already checked that 'metamethod' is a function, so it must success
                //
                assert(res.m_success);

                uint8_t* calleeBytecode = res.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = res.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, res.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }
            tvbase = metamethod;
        }
    }
} __attribute__((__packed__));

class BcTableGetByVal
{
public:
    using Self = BcTableGetByVal;

    BcTableGetByVal(BytecodeSlot base, BytecodeSlot dst, BytecodeSlot index)
        : m_opcode(x_opcodeId<Self>), m_base(base), m_dst(dst), m_index(index)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_base;
    BytecodeSlot m_dst;
    BytecodeSlot m_index;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        assert(bc->m_base.IsLocal());
        TValue tvbase = *StackFrameHeader::GetLocalAddr(sfp, bc->m_base);
        TValue index = *StackFrameHeader::GetLocalAddr(sfp, bc->m_index);
        TValue metamethod;

        while (true)
        {
            if (unlikely(!tvbase.IsPointer(TValue::x_mivTag)))
            {
                goto not_table_object;
            }
            else
            {
                UserHeapPointer<void> base = tvbase.AsPointer<void>();
                if (unlikely(base.As<UserHeapGcObjectHeader>()->m_type != Type::TABLE))
                {
                    goto not_table_object;
                }

                HeapPtr<TableObject> tableObj = base.As<TableObject>();
                TValue result;
                if (index.IsInt32(TValue::x_int32Tag))
                {
                    // TODO: we must be careful that we cannot do IC if the hidden class is CacheableDictionary
                    //
                    GetByIntegerIndexICInfo icInfo;
                    TableObject::PrepareGetByIntegerIndex(tableObj, icInfo /*out*/);
                    result = TableObject::GetByIntegerIndex(tableObj, index.AsInt32(), icInfo);
                    if (unlikely(icInfo.m_mayHaveMetatable && result.IsNil()))
                    {
                        goto check_metatable;
                    }
                }
                else if (index.IsDouble(TValue::x_int32Tag))
                {
                    double indexDouble = index.AsDouble();
                    if (unlikely(IsNaN(indexDouble)))
                    {
                        // Indexing a table by 'NaN' for read is not an error, but always results in nil,
                        // because indexing a table by 'NaN' for write is an error
                        //
                        result = TValue::Nil();
                        goto check_metatable;
                    }
                    else
                    {
                        GetByIntegerIndexICInfo icInfo;
                        TableObject::PrepareGetByIntegerIndex(tableObj, icInfo /*out*/);
                        result = TableObject::GetByDoubleVal(tableObj, indexDouble, icInfo);
                        if (unlikely(icInfo.m_mayHaveMetatable && result.IsNil()))
                        {
                            goto check_metatable;
                        }
                    }
                }
                else if (index.IsPointer(TValue::x_mivTag))
                {
                    GetByIdICInfo icInfo;
                    TableObject::PrepareGetById(tableObj, index.AsPointer(), icInfo /*out*/);
                    result = TableObject::GetById(tableObj, index.AsPointer(), icInfo);
                    if (unlikely(icInfo.m_mayHaveMetatable && result.IsNil()))
                    {
                        goto check_metatable;
                    }
                }
                else
                {
                    assert(index.IsMIV(TValue::x_mivTag));
                    MiscImmediateValue miv = index.AsMIV(TValue::x_mivTag);
                    if (miv.IsNil())
                    {
                        // Indexing a table by 'nil' for read is not an error, but always results in nil,
                        // because indexing a table by 'nil' for write is an error
                        //
                        result = TValue::Nil();
                        goto check_metatable;
                    }
                    else
                    {
                        assert(miv.IsBoolean());
                        UserHeapPointer<HeapString> specialKey = VM_GetSpecialKeyForBoolean(miv.GetBooleanValue());

                        GetByIdICInfo icInfo;
                        TableObject::PrepareGetById(tableObj, specialKey, icInfo /*out*/);
                        result = TableObject::GetById(tableObj, specialKey.As<void>(), icInfo);
                        if (unlikely(icInfo.m_mayHaveMetatable && result.IsNil()))
                        {
                            goto check_metatable;
                        }
                    }
                }

                *StackFrameHeader::GetLocalAddr(sfp, bc->m_dst) = result;
                Dispatch(rc, sfp, bcu + sizeof(Self));

check_metatable:
                TableObject::GetMetatableResult gmr = TableObject::GetMetatable(tableObj);
                if (gmr.m_result.m_value != 0)
                {
                    HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
                    if (unlikely(!TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::Index)))
                    {
                        metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::Index);
                        if (!metamethod.IsNil())
                        {
                            goto handle_metamethod;
                        }
                    }
                }
                *StackFrameHeader::GetLocalAddr(sfp, bc->m_dst) = result;
                Dispatch(rc, sfp, bcu + sizeof(Self));
            }

not_table_object:
            metamethod = GetMetamethodForValue(tvbase, LuaMetamethodKind::Index);
            if (metamethod.IsNil())
            {
                [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad type for TableGetByVal").m_value);
            }

handle_metamethod:
            // If 'metamethod' is a function, we should invoke the metamethod, throwing out an error if fail
            // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
            //
            if (likely(metamethod.IsPointer(TValue::x_mivTag)) && metamethod.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::FUNCTION)
            {
                PrepareMetamethodCallResult res = SetupFrameForMetamethodCall(rc, sfp, bcu, std::array { tvbase, index }, metamethod, OnReturnFromStoreResultMetamethodCall<&Self::m_dst>);
                // We already checked that 'metamethod' is a function, so it must success
                //
                assert(res.m_success);

                uint8_t* calleeBytecode = res.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = res.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, res.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }
            tvbase = metamethod;
        }
    }
} __attribute__((__packed__));

class BcTablePutByVal
{
public:
    using Self = BcTablePutByVal;

    BcTablePutByVal(BytecodeSlot base, BytecodeSlot src, BytecodeSlot index)
        : m_opcode(x_opcodeId<Self>), m_base(base), m_src(src), m_index(index)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_base;
    BytecodeSlot m_src;
    BytecodeSlot m_index;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
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
                TableObject::RawPutByValIntegerIndex(base.As<TableObject>(), index.AsInt32(), newValue);
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
                assert(index.IsMIV(TValue::x_mivTag));
                MiscImmediateValue miv = index.AsMIV(TValue::x_mivTag);
                if (miv.IsNil())
                {
                    ReleaseAssert(false && "unimplemented");
                }
                assert(miv.IsBoolean());
                UserHeapPointer<HeapString> specialKey = VM_GetSpecialKeyForBoolean(miv.GetBooleanValue());

                PutByIdICInfo icInfo;
                TableObject::PreparePutById(base.As<TableObject>(), specialKey, icInfo /*out*/);
                TableObject::PutById(base.As<TableObject>(), specialKey.As<void>(), newValue, icInfo);
            }

            Dispatch(rc, sfp, bcu + sizeof(Self));
        }
    }
} __attribute__((__packed__));

class BcTableGetByIntegerVal
{
public:
    using Self = BcTableGetByIntegerVal;

    BcTableGetByIntegerVal(BytecodeSlot base, BytecodeSlot dst, int16_t index)
        : m_opcode(x_opcodeId<Self>), m_base(base), m_dst(dst), m_index(index)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_base;
    BytecodeSlot m_dst;
    int16_t m_index;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        assert(bc->m_base.IsLocal());
        TValue tvbase = *StackFrameHeader::GetLocalAddr(sfp, bc->m_base);
        TValue metamethod;
        int16_t index = bc->m_index;

        while (true)
        {
            if (!tvbase.IsPointer(TValue::x_mivTag))
            {
                goto not_table_object;
            }
            else
            {
                UserHeapPointer<void> base = tvbase.AsPointer<void>();
                if (base.As<UserHeapGcObjectHeader>()->m_type != Type::TABLE)
                {
                    goto not_table_object;
                }

                HeapPtr<TableObject> tableObj = base.As<TableObject>();
                TValue result;
                GetByIntegerIndexICInfo icInfo;
                TableObject::PrepareGetByIntegerIndex(tableObj, icInfo /*out*/);
                result = TableObject::GetByIntegerIndex(tableObj, index, icInfo);

                if (unlikely(icInfo.m_mayHaveMetatable && result.IsNil()))
                {
                    TableObject::GetMetatableResult gmr = TableObject::GetMetatable(tableObj);
                    if (gmr.m_result.m_value != 0)
                    {
                        HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
                        if (unlikely(!TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::Index)))
                        {
                            metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::Index);
                            if (!metamethod.IsNil())
                            {
                                goto handle_metamethod;
                            }
                        }
                    }
                }

                *StackFrameHeader::GetLocalAddr(sfp, bc->m_dst) = result;
                Dispatch(rc, sfp, bcu + sizeof(Self));
            }

not_table_object:
            metamethod = GetMetamethodForValue(tvbase, LuaMetamethodKind::Index);
            if (metamethod.IsNil())
            {
                [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad type for TableGetByVal").m_value);
            }

handle_metamethod:
            // If 'metamethod' is a function, we should invoke the metamethod, throwing out an error if fail
            // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
            //
            if (likely(metamethod.IsPointer(TValue::x_mivTag)) && metamethod.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::FUNCTION)
            {
                PrepareMetamethodCallResult res = SetupFrameForMetamethodCall(rc, sfp, bcu, std::array { tvbase, TValue::CreateInt32(index, TValue::x_int32Tag) }, metamethod, OnReturnFromStoreResultMetamethodCall<&Self::m_dst>);
                // We already checked that 'metamethod' is a function, so it must success
                //
                assert(res.m_success);

                uint8_t* calleeBytecode = res.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = res.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, res.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }
            tvbase = metamethod;
        }
    }
} __attribute__((__packed__));

class BcTablePutByIntegerVal
{
public:
    using Self = BcTablePutByIntegerVal;

    BcTablePutByIntegerVal(BytecodeSlot base, BytecodeSlot src, int16_t index)
        : m_opcode(x_opcodeId<Self>), m_base(base), m_src(src), m_index(index)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_base;
    BytecodeSlot m_src;
    int16_t m_index;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        assert(bc->m_base.IsLocal());
        TValue tvbase = *StackFrameHeader::GetLocalAddr(sfp, bc->m_base);

        int16_t index = bc->m_index;
        TValue newValue = *StackFrameHeader::GetLocalAddr(sfp, bc->m_src);
        TValue metamethod;

        while (true)
        {
            if (!tvbase.IsPointer(TValue::x_mivTag))
            {
                goto not_table_object;
            }
            else
            {
                UserHeapPointer<void> base = tvbase.AsPointer<void>();
                if (base.As<UserHeapGcObjectHeader>()->m_type != Type::TABLE)
                {
                    goto not_table_object;
                }

                HeapPtr<TableObject> tableObj = base.As<TableObject>();

                PutByIntegerIndexICInfo icInfo;
                TableObject::PreparePutByIntegerIndex(tableObj, index, newValue, icInfo /*out*/);

                if (likely(!icInfo.m_mayHaveMetatable))
                {
                    goto no_metamethod;
                }
                else
                {
                    // Try to execute the fast checks to rule out __newindex metamethod first
                    //
                    TableObject::GetMetatableResult gmr = TableObject::GetMetatable(tableObj);
                    if (likely(gmr.m_result.m_value == 0))
                    {
                        goto no_metamethod;
                    }

                    HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
                    if (likely(TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::NewIndex)))
                    {
                        goto no_metamethod;
                    }

                    // Getting the metamethod from the metatable is more expensive than getting the index value,
                    // so get the index value and check if it's nil first
                    //
                    GetByIntegerIndexICInfo getIcInfo;
                    TableObject::PrepareGetByIntegerIndex(tableObj, getIcInfo /*out*/);
                    TValue originalVal = TableObject::GetByIntegerIndex(tableObj, index, getIcInfo);
                    if (likely(!originalVal.IsNil()))
                    {
                        goto no_metamethod;
                    }

                    metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::NewIndex);
                    if (metamethod.IsNil())
                    {
                        goto no_metamethod;
                    }

                    // Now, we know we need to invoke the metamethod
                    //
                    goto handle_metamethod;
                }

no_metamethod:
                if (!TableObject::TryPutByIntegerIndexFast(tableObj, index, newValue, icInfo))
                {
                    VM* vm = VM::GetActiveVMForCurrentThread();
                    TableObject* obj = TranslateToRawPointer(vm, tableObj);
                    obj->PutByIntegerIndexSlow(vm, index, newValue);
                }

                Dispatch(rc, sfp, bcu + sizeof(Self));
            }

not_table_object:
            metamethod = GetMetamethodForValue(tvbase, LuaMetamethodKind::NewIndex);
            if (metamethod.IsNil())
            {
                [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad type for TablePutByVal").m_value);
            }

handle_metamethod:
            // If 'metamethod' is a function, we should invoke the metamethod, throwing out an error if fail
            // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
            //
            if (likely(metamethod.IsPointer(TValue::x_mivTag)) && metamethod.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::FUNCTION)
            {
                PrepareMetamethodCallResult res = SetupFrameForMetamethodCall(rc, sfp, bcu, std::array { tvbase, TValue::CreateInt32(index, TValue::x_int32Tag), newValue }, metamethod, OnReturnFromNewIndexMetamethodCall<Self>);
                // We already checked that 'metamethod' is a function, so it must success
                //
                assert(res.m_success);

                uint8_t* calleeBytecode = res.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = res.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, res.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }

            tvbase = metamethod;
        }
    }
} __attribute__((__packed__));

class BcTableVariadicPutByIntegerValSeq
{
public:
    using Self = BcTableVariadicPutByIntegerValSeq;

    BcTableVariadicPutByIntegerValSeq(BytecodeSlot base, BytecodeSlot index)
        : m_opcode(x_opcodeId<Self>), m_base(base), m_index(index)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_base;
    BytecodeSlot m_index;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);

        TValue tvIndex = CodeBlock::GetConstantAsTValue(rc->m_codeBlock, bc->m_index.ConstantOrd());
        // For some reason LuaJIT bytecode stores the constant in the lower 32 bits of a double...
        //
        assert(tvIndex.IsDouble(TValue::x_int32Tag));
        // This should not overflow, as Lua states:
        //     "Fields of the form 'exp' are equivalent to [i] = exp, where i are consecutive numerical
        //     integers, starting with 1. Fields in the other formats do not affect this counting."
        // So in order for this index to overflow, there needs to have 2^31 terms in the table, which is impossible
        //
        int32_t indexStart = static_cast<int32_t>(tvIndex.m_value);

        // 'm_base' is guaranteed to be a table object, as this opcode only shows up in a table initializer expression
        //
        assert(StackFrameHeader::GetLocalAddr(sfp, bc->m_base)->IsPointer(TValue::x_mivTag) &&
               StackFrameHeader::GetLocalAddr(sfp, bc->m_base)->AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::TABLE);
        HeapPtr<TableObject> base = StackFrameHeader::GetLocalAddr(sfp, bc->m_base)->AsPointer<TableObject>().As();

        TValue* src = reinterpret_cast<TValue*>(sfp) + rc->m_variadicRetSlotBegin;
        int32_t numTermsToPut = static_cast<int32_t>(rc->m_numVariadicRets);
        assert(numTermsToPut != -1);
        DEBUG_ONLY(rc->m_numVariadicRets = static_cast<uint32_t>(-1);)

        // For now we simply use a naive loop of PutByIntegerVal: this isn't performance sensitive after all, so just stay simple for now
        //
        for (int32_t i = 0; i < numTermsToPut; i++)
        {
            int32_t idx = indexStart + i;
            TableObject::RawPutByValIntegerIndex(base, idx, src[i]);
        }

        Dispatch(rc, sfp, bcu + sizeof(Self));
    }
} __attribute__((__packed__));

class BcGlobalGet
{
public:
    using Self = BcGlobalGet;

    BcGlobalGet(BytecodeSlot dst, BytecodeSlot idx)
        : m_opcode(x_opcodeId<Self>), m_dst(dst), m_index(idx.ConstantOrd())
    { }

    uint8_t m_opcode;
    BytecodeSlot m_dst;
    int32_t m_index;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);

        TValue tvIndex = CodeBlock::GetConstantAsTValue(rc->m_codeBlock, bc->m_index);
        assert(tvIndex.IsPointer(TValue::x_mivTag));
        UserHeapPointer<HeapString> index = tvIndex.AsPointer<HeapString>();

        HeapPtr<TableObject> base = rc->m_globalObject.As();

retry:
        GetByIdICInfo icInfo;
        TableObject::PrepareGetById(base, index, icInfo /*out*/);
        TValue result = TableObject::GetById(base, index.As<void>(), icInfo);

        TValue metamethodBase;
        TValue metamethod;

        if (unlikely(icInfo.m_mayHaveMetatable && result.IsNil()))
        {
            TableObject::GetMetatableResult gmr = TableObject::GetMetatable(base);
            if (gmr.m_result.m_value != 0)
            {
                HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
                metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::Index);
                if (!metamethod.IsNil())
                {
                    metamethodBase = TValue::CreatePointer(UserHeapPointer<TableObject> { base });
                    goto handle_metamethod;
                }
            }
        }

        *StackFrameHeader::GetLocalAddr(sfp, bc->m_dst) = result;
        Dispatch(rc, sfp, bcu + sizeof(Self));

handle_metamethod:
        // If 'metamethod' is a function, we should invoke the metamethod, throwing out an error if fail
        // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
        //
        if (likely(metamethod.IsPointer(TValue::x_mivTag)))
        {
            Type mmType = metamethod.AsPointer<UserHeapGcObjectHeader>().As()->m_type;
            if (mmType == Type::FUNCTION)
            {
                PrepareMetamethodCallResult res = SetupFrameForMetamethodCall(rc, sfp, bcu, std::array { metamethodBase, tvIndex }, metamethod, OnReturnFromStoreResultMetamethodCall<&Self::m_dst>);
                // We already checked that 'metamethod' is a function, so it must success
                //
                assert(res.m_success);

                uint8_t* calleeBytecode = res.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = res.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, res.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }
            else if (mmType == Type::TABLE)
            {
                base = metamethod.AsPointer<TableObject>().As();
                goto retry;
            }
        }

        // Now we know 'metamethod' is not a function or pointer, so we should locate its own exotic '__index' metamethod..
        // The difference is that if the metamethod is nil, we need to throw an error
        //
        metamethodBase = metamethod;
        metamethod = GetMetamethodForValue(metamethod, LuaMetamethodKind::Index);
        if (metamethod.IsNil())
        {
            // TODO: make error message consistent with Lua
            //
            [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad type for GetById").m_value);
        }
        goto handle_metamethod;
    }
} __attribute__((__packed__));

class BcGlobalPut
{
public:
    using Self = BcGlobalPut;

    BcGlobalPut(BytecodeSlot src, BytecodeSlot idx)
        : m_opcode(x_opcodeId<Self>), m_src(src), m_index(idx.ConstantOrd())
    { }

    uint8_t m_opcode;
    BytecodeSlot m_src;
    int32_t m_index;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);

        TValue tvIndex = CodeBlock::GetConstantAsTValue(rc->m_codeBlock, bc->m_index);
        assert(tvIndex.IsPointer(TValue::x_mivTag));
        UserHeapPointer<HeapString> index = tvIndex.AsPointer<HeapString>();
        TValue newValue = *StackFrameHeader::GetLocalAddr(sfp, bc->m_src);
        TValue metamethod;
        TValue metamethodBase;

        HeapPtr<TableObject> base = rc->m_globalObject.As();

retry:
        PutByIdICInfo icInfo;
        TableObject::PreparePutById(base, index, icInfo /*out*/);

        if (unlikely(TableObject::PutByIdNeedToCheckMetatable(base, icInfo)))
        {
            TableObject::GetMetatableResult gmr = TableObject::GetMetatable(base);
            if (gmr.m_result.m_value != 0)
            {
                HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
                if (unlikely(!TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::NewIndex)))
                {
                    metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::NewIndex);
                    if (!metamethod.IsNil())
                    {
                        metamethodBase = TValue::CreatePointer(UserHeapPointer<TableObject> { base });
                        goto handle_metamethod;
                    }
                }
            }
        }

        TableObject::PutById(base, index.As<void>(), newValue, icInfo);
        Dispatch(rc, sfp, bcu + sizeof(Self));

handle_metamethod:
        // If 'metamethod' is a function, we should invoke the metamethod, throwing out an error if fail
        // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
        //
        if (likely(metamethod.IsPointer(TValue::x_mivTag)))
        {
            Type mmType = metamethod.AsPointer<UserHeapGcObjectHeader>().As()->m_type;
            if (mmType == Type::FUNCTION)
            {
                PrepareMetamethodCallResult res = SetupFrameForMetamethodCall(rc, sfp, bcu, std::array { metamethodBase, tvIndex, newValue }, metamethod, OnReturnFromNewIndexMetamethodCall<Self>);
                // We already checked that 'metamethod' is a function, so it must success
                //
                assert(res.m_success);

                uint8_t* calleeBytecode = res.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = res.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, res.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }
            else if (mmType == Type::TABLE)
            {
                base = metamethod.AsPointer<TableObject>().As();
                goto retry;
            }
        }

        // Now we know 'metamethod' is not a function or pointer, so we should locate its own exotic '__index' metamethod..
        // The difference is that if the metamethod is nil, we need to throw an error
        //
        metamethodBase = metamethod;
        metamethod = GetMetamethodForValue(metamethod, LuaMetamethodKind::NewIndex);
        if (metamethod.IsNil())
        {
            // TODO: make error message consistent with Lua
            //
            [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessage("bad type for PutById").m_value);
        }
        goto handle_metamethod;
    }
} __attribute__((__packed__));

class BcTableNew
{
public:
    using Self = BcTableNew;

    BcTableNew(BytecodeSlot dst, uint8_t inlineCapacityStepping, uint16_t arrayPartSizeHint)
        : m_opcode(x_opcodeId<Self>), m_dst(dst), m_inlineCapacityStepping(inlineCapacityStepping), m_arrayPartSizeHint(arrayPartSizeHint)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_dst;
    uint8_t m_inlineCapacityStepping;
    uint16_t m_arrayPartSizeHint;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);

        VM* vm = VM::GetActiveVMForCurrentThread();
        SystemHeapPointer<Structure> structure = Structure::GetInitialStructureForSteppingKnowingAlreadyBuilt(vm, bc->m_inlineCapacityStepping);
        UserHeapPointer<TableObject> obj = TableObject::CreateEmptyTableObject(vm, TranslateToRawPointer(vm, structure.As()), bc->m_arrayPartSizeHint);
        *StackFrameHeader::GetLocalAddr(sfp, bc->m_dst) = TValue::CreatePointer(obj);
        Dispatch(rc, sfp, bcu + sizeof(Self));
    }
} __attribute__((__packed__));

class BcTableDup
{
public:
    using Self = BcTableDup;

    BcTableDup(BytecodeSlot dst, int32_t src)
        : m_opcode(x_opcodeId<Self>), m_dst(dst), m_src(src)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_dst;
    int32_t m_src;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);

        TValue tpl = CodeBlock::GetConstantAsTValue(rc->m_codeBlock, bc->m_src);
        assert(tpl.IsPointer(TValue::x_mivTag));
        assert(tpl.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::TABLE);
        VM* vm = VM::GetActiveVMForCurrentThread();
        TableObject* obj = TranslateToRawPointer(vm, tpl.AsPointer<TableObject>().As());
        HeapPtr<TableObject> newObject = obj->ShallowCloneTableObject(vm);
        TValue result = TValue::CreatePointer(UserHeapPointer<TableObject>(newObject));
        *StackFrameHeader::GetLocalAddr(sfp, bc->m_dst) = result;
        Dispatch(rc, sfp, bcu + sizeof(Self));
    }
} __attribute__((__packed__));

class BcReturn
{
public:
    using Self = BcReturn;

    BcReturn(bool isVariadic, uint16_t numReturnValues, BytecodeSlot slotBegin)
        : m_opcode(x_opcodeId<Self>), m_isVariadicRet(isVariadic), m_numReturnValues(numReturnValues), m_slotBegin(slotBegin)
    { }

    uint8_t m_opcode;
    bool m_isVariadicRet;
    uint16_t m_numReturnValues;
    BytecodeSlot m_slotBegin;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        assert(bc->m_slotBegin.IsLocal());
        TValue* pbegin = StackFrameHeader::GetLocalAddr(sfp, bc->m_slotBegin);
        uint32_t numRetValues = bc->m_numReturnValues;
        if (bc->m_isVariadicRet)
        {
            assert(rc->m_numVariadicRets != static_cast<uint32_t>(-1));
            TValue* pdst = pbegin + bc->m_numReturnValues;
            TValue* psrc = reinterpret_cast<TValue*>(sfp) + rc->m_variadicRetSlotBegin;
            numRetValues += rc->m_numVariadicRets;
            // TODO: it's always correct to move from left to right
            memmove(pdst, psrc, sizeof(TValue) * rc->m_numVariadicRets);
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
                pbegin[idx] = TValue::Nil();
                idx++;
            }
        }

        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
        using RetFn = void(*)(CoroutineRuntimeContext* /*rc*/, void* /*sfp*/, uint8_t* /*retValuesStart*/, uint64_t /*numRetValues*/);
        RetFn retAddr = reinterpret_cast<RetFn>(hdr->m_retAddr);
        void* callerSf = hdr->m_caller;
        [[clang::musttail]] return retAddr(rc, callerSf, reinterpret_cast<uint8_t*>(pbegin), numRetValues);
    }
} __attribute__((__packed__));

inline void* WARN_UNUSED SetupFrameForCall(CoroutineRuntimeContext* rc, void* sfp, TValue* begin, HeapPtr<FunctionObject> target, uint32_t numFixedParamsToPass, InterpreterFn onReturn, bool passVariadicRetAsParam)
{
    TValue* argEnd = begin + x_numSlotsForStackFrameHeader + numFixedParamsToPass;

    // First, if the call passes variadic ret as param, copy them to the expected location
    //
    if (unlikely(passVariadicRetAsParam))
    {
        // Fast path for the common case: the variadic ret has 1 value
        // Directly write *varRetbegin to *argEnd even if the # of variadic ret is 0 to save a branch, since this slot is overwritable any way
        //
        TValue* varRetbegin = reinterpret_cast<TValue*>(sfp) + rc->m_variadicRetSlotBegin;
        *argEnd = *varRetbegin;
        if (unlikely(rc->m_numVariadicRets > 1))
        {
            memmove(argEnd, varRetbegin, sizeof(TValue) * rc->m_numVariadicRets);
        }
        argEnd += rc->m_numVariadicRets;
    }

    HeapPtr<ExecutableCode> calleeEc = TCGet(target->m_executable).As();
    TValue* argNeeded = begin + x_numSlotsForStackFrameHeader + calleeEc->m_numFixedArguments;

    // Pad in nils if necessary
    //
    if (unlikely(argEnd < argNeeded))
    {
        TValue val = TValue::Nil();
        while (argEnd < argNeeded)
        {
            *argEnd = val;
            argEnd++;
        }
    }

    // Set up the stack frame header
    //
    bool needRelocate = calleeEc->m_hasVariadicArguments && argEnd > argNeeded;
    void* baseForNextFrame;
    if (unlikely(needRelocate))
    {
        // We need to put the stack frame header at the END of the arguments and copy the fixed argument part
        //
        StackFrameHeader* newStackFrameHdr = reinterpret_cast<StackFrameHeader*>(argEnd);
        baseForNextFrame = newStackFrameHdr + 1;
        newStackFrameHdr->m_func = target;
        newStackFrameHdr->m_retAddr = reinterpret_cast<void*>(onReturn);
        newStackFrameHdr->m_caller = sfp;
        newStackFrameHdr->m_numVariadicArguments = static_cast<uint32_t>(argEnd - argNeeded);
        SafeMemcpy(baseForNextFrame, begin + x_numSlotsForStackFrameHeader, sizeof(TValue) * calleeEc->m_numFixedArguments);
    }
    else
    {
        StackFrameHeader* newStackFrameHdr = reinterpret_cast<StackFrameHeader*>(begin);
        baseForNextFrame = newStackFrameHdr + 1;
        // m_func is already in its expected place, so just fill the other fields
        //
        newStackFrameHdr->m_retAddr = reinterpret_cast<void*>(onReturn);
        newStackFrameHdr->m_caller = sfp;
        newStackFrameHdr->m_numVariadicArguments = 0;
    }

    // This static_cast actually doesn't always hold
    // But if callee is not a bytecode function, the callee will never look at m_codeBlock, so it's fine
    //
    rc->m_codeBlock = static_cast<CodeBlock*>(TranslateToRawPointer(calleeEc));

    return baseForNextFrame;
}

class BcCall
{
public:
    using Self = BcCall;

    BcCall(bool keepVariadicRet, bool passVariadicRetAsParam, uint32_t numFixedParams, uint32_t numFixedRets, BytecodeSlot funcSlot)
        : m_opcode(x_opcodeId<Self>), m_keepVariadicRet(keepVariadicRet), m_passVariadicRetAsParam(passVariadicRetAsParam)
        , m_numFixedParams(numFixedParams), m_numFixedRets(numFixedRets), m_funcSlot(funcSlot)
    { }

    uint8_t m_opcode;
    bool m_keepVariadicRet;
    bool m_passVariadicRetAsParam;
    uint32_t m_numFixedParams;
    uint32_t m_numFixedRets;    // only used when m_keepVariadicRet == false
    BytecodeSlot m_funcSlot;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);

        hdr->m_callerBytecodeOffset = SafeIntegerCast<uint32_t>(bcu - rc->m_codeBlock->m_bytecode);

        assert(bc->m_funcSlot.IsLocal());
        TValue* begin = StackFrameHeader::GetLocalAddr(sfp, bc->m_funcSlot);

        uint32_t numParams = bc->m_numFixedParams;
        UserHeapPointer<FunctionObject> targetMaybeNull = GetCallTargetConsideringMetatableAndFixCallParameters(begin, numParams /*inout*/);
        if (targetMaybeNull.m_value == 0)
        {
            [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessageForUnableToCall(*begin).m_value);
        }

        HeapPtr<FunctionObject> target = targetMaybeNull.As();
        HeapPtr<ExecutableCode> calleeEc = TCGet(target->m_executable).As();
        void* baseForNextFrame = SetupFrameForCall(rc, sfp, begin, target, numParams, OnReturn, bc->m_passVariadicRetAsParam);

        uint8_t* calleeBytecode = calleeEc->m_bytecode;
        InterpreterFn calleeFn = calleeEc->m_bestEntryPoint;
        [[clang::musttail]] return calleeFn(rc, baseForNextFrame, calleeBytecode, 0 /*unused*/);
    }

    static void OnReturn(CoroutineRuntimeContext* rc, void* stackframe, const uint8_t* retValuesU, uint64_t numRetValues)
    {
        const TValue* retValues = reinterpret_cast<const TValue*>(retValuesU);
        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackframe);
        HeapPtr<ExecutableCode> callerEc = TCGet(hdr->m_func->m_executable).As();
        assert(TranslateToRawPointer(callerEc)->IsBytecodeFunction());
        uint8_t* callerBytecodeStart = callerEc->m_bytecode;
        ConstRestrictPtr<uint8_t> bcu = callerBytecodeStart + hdr->m_callerBytecodeOffset;
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        if (bc->m_keepVariadicRet)
        {
            rc->m_numVariadicRets = SafeIntegerCast<uint32_t>(numRetValues);
            rc->m_variadicRetSlotBegin = SafeIntegerCast<int32_t>(retValues - reinterpret_cast<TValue*>(stackframe));
        }
        else
        {
            if (bc->m_numFixedRets <= x_minNilFillReturnValues)
            {
                // TODO: it's always correct to move from left to right
                memmove(StackFrameHeader::GetLocalAddr(stackframe, bc->m_funcSlot), retValues, sizeof(TValue) * bc->m_numFixedRets);
            }
            else
            {
                TValue* dst = StackFrameHeader::GetLocalAddr(stackframe, bc->m_funcSlot);
                if (numRetValues < bc->m_numFixedRets)
                {
                    // TODO: it's always correct to move from left to right
                    memmove(dst, retValues, sizeof(TValue) * numRetValues);
                    while (numRetValues < bc->m_numFixedRets)
                    {
                        dst[numRetValues] = TValue::CreateMIV(MiscImmediateValue::CreateNil(), TValue::x_mivTag);
                        numRetValues++;
                    }
                }
                else
                {
                    // TODO: it's always correct to move from left to right
                    memmove(dst, retValues, sizeof(TValue) * bc->m_numFixedRets);
                }
            }
        }
        rc->m_codeBlock = static_cast<CodeBlock*>(TranslateToRawPointer(callerEc));
        Dispatch(rc, stackframe, bcu + sizeof(Self));
    }
} __attribute__((__packed__));

class BcTailCall
{
public:
    using Self = BcTailCall;

    BcTailCall(bool passVariadicRetAsParam, uint32_t numFixedParams, BytecodeSlot funcSlot)
        : m_opcode(x_opcodeId<Self>), m_passVariadicRetAsParam(passVariadicRetAsParam)
        , m_numFixedParams(numFixedParams), m_funcSlot(funcSlot)
    { }

    uint8_t m_opcode;
    bool m_passVariadicRetAsParam;
    uint32_t m_numFixedParams;
    BytecodeSlot m_funcSlot;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);

        hdr->m_callerBytecodeOffset = SafeIntegerCast<uint32_t>(bcu - rc->m_codeBlock->m_bytecode);

        assert(bc->m_funcSlot.IsLocal());
        TValue* begin = StackFrameHeader::GetLocalAddr(sfp, bc->m_funcSlot);
        uint32_t numParams = bc->m_numFixedParams;

        UserHeapPointer<FunctionObject> targetMaybeNull = GetCallTargetConsideringMetatableAndFixCallParameters(begin, numParams /*inout*/);
        if (targetMaybeNull.m_value == 0)
        {
            [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessageForUnableToCall(*begin).m_value);
        }

        TValue* argEnd = begin + x_numSlotsForStackFrameHeader + numParams;
        HeapPtr<FunctionObject> target = targetMaybeNull.As<FunctionObject>();

        // First, if the call passes variadic ret as param, copy them to the expected location
        //
        if (unlikely(bc->m_passVariadicRetAsParam))
        {
            // Fast path for the common case: the variadic ret has 1 value
            // Directly write *varRetbegin to *argEnd even if the # of variadic ret is 0 to save a branch, since this slot is overwritable any way
            //
            TValue* varRetbegin = reinterpret_cast<TValue*>(sfp) + rc->m_variadicRetSlotBegin;
            *argEnd = *varRetbegin;
            if (unlikely(rc->m_numVariadicRets > 1))
            {
                memmove(argEnd, varRetbegin, sizeof(TValue) * rc->m_numVariadicRets);
            }
            argEnd += rc->m_numVariadicRets;
        }

        HeapPtr<ExecutableCode> calleeEc = TCGet(target->m_executable).As();
        TValue* argNeeded = begin + x_numSlotsForStackFrameHeader + calleeEc->m_numFixedArguments;

        // Pad in nils if necessary
        //
        if (unlikely(argEnd < argNeeded))
        {
            TValue val = TValue::Nil();
            while (argEnd < argNeeded)
            {
                *argEnd = val;
                argEnd++;
            }
        }

        // Set up the stack frame header
        //
        bool needRelocate = calleeEc->m_hasVariadicArguments && argEnd > argNeeded;
        void* baseForNextFrame;
        void* callerFrameLowestAddress = reinterpret_cast<TValue*>(hdr) - hdr->m_numVariadicArguments;
        if (unlikely(needRelocate))
        {
            StackFrameHeader* newStackFrameHdr = reinterpret_cast<StackFrameHeader*>(argEnd);
            newStackFrameHdr->m_func = target;
            newStackFrameHdr->m_retAddr = hdr->m_retAddr;
            newStackFrameHdr->m_caller = hdr->m_caller;
            newStackFrameHdr->m_callerBytecodeOffset = hdr->m_callerBytecodeOffset;
            uint32_t numVarArgs = static_cast<uint32_t>(argEnd - argNeeded);
            newStackFrameHdr->m_numVariadicArguments = numVarArgs;
            SafeMemcpy(newStackFrameHdr + 1, begin + x_numSlotsForStackFrameHeader, sizeof(TValue) * calleeEc->m_numFixedArguments);
            // Now we have a fully set up new stack frame, overwrite current stack frame by moving it to the lowest address of current stack frame
            // This provides the semantics of proper tail call (no stack overflow even with unbounded # of tail calls)
            //
            memmove(callerFrameLowestAddress, reinterpret_cast<TValue*>(newStackFrameHdr) - numVarArgs, sizeof(TValue) * static_cast<size_t>(argEnd - begin));
            baseForNextFrame = reinterpret_cast<TValue*>(callerFrameLowestAddress) + numVarArgs + x_numSlotsForStackFrameHeader;
        }
        else
        {
            StackFrameHeader* newStackFrameHdr = reinterpret_cast<StackFrameHeader*>(begin);
            // m_func is already in its expected place, so just fill the other fields
            //
            newStackFrameHdr->m_retAddr = hdr->m_retAddr;
            newStackFrameHdr->m_caller = hdr->m_caller;
            newStackFrameHdr->m_callerBytecodeOffset = hdr->m_callerBytecodeOffset;
            newStackFrameHdr->m_numVariadicArguments = 0;
            // Move to lowest address, see above
            //
            memmove(callerFrameLowestAddress, newStackFrameHdr, sizeof(TValue) * static_cast<size_t>(argNeeded - begin));
            baseForNextFrame = reinterpret_cast<TValue*>(callerFrameLowestAddress) + x_numSlotsForStackFrameHeader;
        }

        // This static_cast actually doesn't always hold
        // But if callee is not a bytecode function, the callee will never look at m_codeBlock, so it's fine
        //
        rc->m_codeBlock = static_cast<CodeBlock*>(TranslateToRawPointer(calleeEc));

        uint8_t* calleeBytecode = calleeEc->m_bytecode;
        InterpreterFn calleeFn = calleeEc->m_bestEntryPoint;
        [[clang::musttail]] return calleeFn(rc, baseForNextFrame, calleeBytecode, 0 /*unused*/);
    }
} __attribute__((__packed__));

class BcVariadicArgsToVariadicRet
{
public:
    using Self = BcVariadicArgsToVariadicRet;

    BcVariadicArgsToVariadicRet()
        : m_opcode(x_opcodeId<Self>)
    { }

    uint8_t m_opcode;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        std::ignore = bc;
        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackframe);
        uint32_t numVarArgs = hdr->m_numVariadicArguments;
        rc->m_variadicRetSlotBegin = -static_cast<int32_t>(numVarArgs + x_numSlotsForStackFrameHeader);
        rc->m_numVariadicRets = numVarArgs;
        Dispatch(rc, stackframe, bcu + sizeof(Self));
    }
} __attribute__((__packed__));

class BcPutVariadicArgs
{
public:
    using Self = BcPutVariadicArgs;

    BcPutVariadicArgs(BytecodeSlot dst, uint32_t numValues)
        : m_opcode(x_opcodeId<Self>), m_dst(dst), m_numValues(numValues)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_dst;
    uint32_t m_numValues;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackframe);
        uint32_t numVarArgs = hdr->m_numVariadicArguments;
        TValue* addr = StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst);
        uint32_t numValuesWanted = bc->m_numValues;
        if (numVarArgs < numValuesWanted)
        {
            SafeMemcpy(addr, reinterpret_cast<TValue*>(hdr) - numVarArgs, sizeof(TValue) * numVarArgs);
            TValue* addrEnd = addr + numValuesWanted;
            addr += numVarArgs;
            TValue val = TValue::Nil();
            while (addr < addrEnd)
            {
                *addr = val;
                addr++;
            }
        }
        else
        {
            SafeMemcpy(addr, reinterpret_cast<TValue*>(hdr) - numVarArgs, sizeof(TValue) * numValuesWanted);
        }
        Dispatch(rc, stackframe, bcu + sizeof(Self));
    }
} __attribute__((__packed__));

class BcCallIterator
{
public:
    using Self = BcCallIterator;

    BcCallIterator(BytecodeSlot funcSlot, uint32_t numFixedRets)
        : m_opcode(x_opcodeId<Self>), m_funcSlot(funcSlot), m_numFixedRets(numFixedRets)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_funcSlot;
    uint32_t m_numFixedRets;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);

        hdr->m_callerBytecodeOffset = SafeIntegerCast<uint32_t>(bcu - rc->m_codeBlock->m_bytecode);

        assert(bc->m_funcSlot.IsLocal());
        TValue* begin = StackFrameHeader::GetLocalAddr(sfp, bc->m_funcSlot);
        begin[0] = begin[-3];
        begin[x_numSlotsForStackFrameHeader] = begin[-2];
        begin[x_numSlotsForStackFrameHeader + 1] = begin[-1];

        uint32_t numParams = 2;
        UserHeapPointer<FunctionObject> targetMaybeNull = GetCallTargetConsideringMetatableAndFixCallParameters(begin, numParams /*inout*/);
        if (targetMaybeNull.m_value == 0)
        {
            [[clang::musttail]] return ThrowError(rc, sfp, bcu, MakeErrorMessageForUnableToCall(*begin).m_value);
        }

        HeapPtr<FunctionObject> target = targetMaybeNull.As();
        HeapPtr<ExecutableCode> calleeEc = TCGet(target->m_executable).As();
        void* baseForNextFrame = SetupFrameForCall(rc, sfp, begin, target, numParams, OnReturn, false /*passVariadicRetAsParam*/);

        uint8_t* calleeBytecode = calleeEc->m_bytecode;
        InterpreterFn calleeFn = calleeEc->m_bestEntryPoint;
        [[clang::musttail]] return calleeFn(rc, baseForNextFrame, calleeBytecode, 0 /*unused*/);
    }

    static void OnReturn(CoroutineRuntimeContext* rc, void* stackframe, const uint8_t* retValuesU, uint64_t numRetValues)
    {
        const TValue* retValues = reinterpret_cast<const TValue*>(retValuesU);
        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackframe);
        HeapPtr<ExecutableCode> callerEc = TCGet(hdr->m_func->m_executable).As();
        assert(TranslateToRawPointer(callerEc)->IsBytecodeFunction());
        uint8_t* callerBytecodeStart = callerEc->m_bytecode;
        ConstRestrictPtr<uint8_t> bcu = callerBytecodeStart + hdr->m_callerBytecodeOffset;
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        if (bc->m_numFixedRets <= x_minNilFillReturnValues)
        {
            // TODO: it's always correct to move from left to right
            memmove(StackFrameHeader::GetLocalAddr(stackframe, bc->m_funcSlot), retValues, sizeof(TValue) * bc->m_numFixedRets);
        }
        else
        {
            TValue* dst = StackFrameHeader::GetLocalAddr(stackframe, bc->m_funcSlot);
            if (numRetValues < bc->m_numFixedRets)
            {
                // TODO: it's always correct to move from left to right
                memmove(dst, retValues, sizeof(TValue) * numRetValues);
                while (numRetValues < bc->m_numFixedRets)
                {
                    dst[numRetValues] = TValue::CreateMIV(MiscImmediateValue::CreateNil(), TValue::x_mivTag);
                    numRetValues++;
                }
            }
            else
            {
                // TODO: it's always correct to move from left to right
                memmove(dst, retValues, sizeof(TValue) * bc->m_numFixedRets);
            }
        }
        rc->m_codeBlock = static_cast<CodeBlock*>(TranslateToRawPointer(callerEc));
        Dispatch(rc, stackframe, bcu + sizeof(Self));
    }
} __attribute__((__packed__));

class BcIteratorLoopBranch
{
public:
    using Self = BcIteratorLoopBranch;

    BcIteratorLoopBranch(BytecodeSlot base)
        : m_opcode(x_opcodeId<Self>), m_base(base), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_base;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&Self::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue* addr = StackFrameHeader::GetLocalAddr(stackframe, bc->m_base);
        TValue val = *addr;
        if (!val.IsNil())
        {
            addr[-1] = val;
            Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
        }
        else
        {
            Dispatch(rc, stackframe, bcu + sizeof(Self));
        }
    }
} __attribute__((__packed__));

// Must have identical layout as BcCallIterator
//
class BcCallNext
{
public:
    using Self = BcCallNext;

    BcCallNext(BytecodeSlot funcSlot, uint32_t numFixedRets)
        : m_opcode(x_opcodeId<Self>), m_funcSlot(funcSlot), m_numFixedRets(numFixedRets)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_funcSlot;
    uint32_t m_numFixedRets;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue* addr = StackFrameHeader::GetLocalAddr(sfp, bc->m_funcSlot);
        TableObjectIterator* iter = reinterpret_cast<TableObjectIterator*>(addr - 3);
        HeapPtr<TableObject> table = addr[-2].AsPointer<TableObject>().As();
        TableObjectIterator::KeyValuePair kv = iter->Advance(table);
        addr[0] = kv.m_key;
        addr[1] = kv.m_value;
        if (bc->m_numFixedRets > 2)
        {
            TValue val = TValue::Nil();
            TValue* addrEnd = addr + bc->m_numFixedRets;
            addr += 2;
            while (addr < addrEnd)
            {
                *addr = val;
                addr++;
            }
        }
        Dispatch(rc, sfp, bcu + sizeof(Self));
    }
} __attribute__((__packed__));

class BcValidateIsNextAndBranch
{
public:
    using Self = BcValidateIsNextAndBranch;

    BcValidateIsNextAndBranch(BytecodeSlot base)
        : m_opcode(x_opcodeId<Self>), m_base(base), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_base;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&Self::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue* addr = StackFrameHeader::GetLocalAddr(stackframe, bc->m_base);
        bool validateOK = false;
        // Check 1: addr[-3] is the true 'next' function
        //
        if (addr[-3].m_value == VM::GetActiveVMForCurrentThread()->GetLibBaseDotNextFunctionObject().m_value)
        {
            // Check 2: addr[-2] is a table
            TValue v = addr[-2];
            if (v.IsPointer(TValue::x_mivTag) && v.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::TABLE)
            {
                // Check 3: addr[-1] is nil
                //
                if (addr[-1].IsNil())
                {
                    validateOK = true;
                }
            }
        }
        if (validateOK)
        {
            // Overwrite addr[-3] with TableObjectIterator
            //
            ConstructInPlace(reinterpret_cast<TableObjectIterator*>(addr - 3));
            // Overwrite the opcode of 'ITERC' to 'ITERN'
            //
            assert(bcu[bc->m_offset] == x_opcodeId<BcCallIterator> || bcu[bc->m_offset] == x_opcodeId<BcCallNext>);
            const_cast<uint8_t*>(bcu)[bc->m_offset] = x_opcodeId<BcCallNext>;
            _Pragma("clang diagnostic push")
            _Pragma("clang diagnostic ignored \"-Wuninitialized\"")
            uint64_t dispatch_unused;
            [[clang::musttail]] return BcCallNext::Execute(rc, stackframe, bcu + bc->m_offset, dispatch_unused);
            _Pragma("clang diagnostic pop")

        }
        else
        {
            // Overwrite the opcode of 'ITERN' to 'ITERC'
            //
            assert(bcu[bc->m_offset] == x_opcodeId<BcCallIterator> || bcu[bc->m_offset] == x_opcodeId<BcCallNext>);
            const_cast<uint8_t*>(bcu)[bc->m_offset] = x_opcodeId<BcCallIterator>;
            _Pragma("clang diagnostic push")
            _Pragma("clang diagnostic ignored \"-Wuninitialized\"")
            uint64_t dispatch_unused;
            [[clang::musttail]] return BcCallIterator::Execute(rc, stackframe, bcu + bc->m_offset, dispatch_unused);
            _Pragma("clang diagnostic pop")
        }
    }
} __attribute__((__packed__));

class BcNewClosure
{
public:
    using Self = BcNewClosure;

    BcNewClosure(BytecodeSlot src, BytecodeSlot dst)
        : m_opcode(x_opcodeId<Self>), m_src(src), m_dst(dst)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_src;
    BytecodeSlot m_dst;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        UnlinkedCodeBlock* ucb = CodeBlock::GetConstantAsUnlinkedCodeBlock(rc->m_codeBlock, bc->m_src.ConstantOrd());
        CodeBlock* cb = UnlinkedCodeBlock::GetCodeBlock(ucb, rc->m_globalObject);
        UserHeapPointer<FunctionObject> func = FunctionObject::CreateAndFillUpvalues(ucb, cb, rc, reinterpret_cast<TValue*>(stackframe), StackFrameHeader::GetStackFrameHeader(stackframe)->m_func);
        *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = TValue::CreatePointer(func);
        Dispatch(rc, stackframe, bcu + sizeof(Self));
    }
} __attribute__((__packed__));

class BcMove
{
public:
    using Self = BcMove;

    BcMove(BytecodeSlot src, BytecodeSlot dst)
        : m_opcode(x_opcodeId<Self>), m_src(src), m_dst(dst)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_src;
    BytecodeSlot m_dst;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue src = bc->m_src.Get(rc, stackframe);
        *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = src;
        Dispatch(rc, stackframe, bcu + sizeof(Self));
    }
} __attribute__((__packed__));

class BcFillNil
{
public:
    using Self = BcFillNil;

    BcFillNil(BytecodeSlot firstSlot, uint32_t numSlotsToFill)
        : m_opcode(x_opcodeId<Self>), m_firstSlot(firstSlot), m_numSlotsToFill(numSlotsToFill)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_firstSlot;
    uint32_t m_numSlotsToFill;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue val = TValue::Nil();
        TValue* begin = StackFrameHeader::GetLocalAddr(stackframe, bc->m_firstSlot);
        TValue* end = begin + bc->m_numSlotsToFill;
        while (begin < end)
        {
            *begin = val;
            begin++;
        }
        Dispatch(rc, stackframe, bcu + sizeof(Self));
    }
} __attribute__((__packed__));

class BcIsFalsy
{
public:
    using Self = BcIsFalsy;

    BcIsFalsy(BytecodeSlot src, BytecodeSlot dst)
        : m_opcode(x_opcodeId<Self>), m_src(src), m_dst(dst)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_src;
    BytecodeSlot m_dst;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue src = *StackFrameHeader::GetLocalAddr(stackframe, bc->m_src);
        *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = TValue::CreateBoolean(!src.IsTruthy());
        Dispatch(rc, stackframe, bcu + sizeof(Self));
    }
} __attribute__((__packed__));

class BcUnaryMinus
{
public:
    using Self = BcUnaryMinus;

    BcUnaryMinus(BytecodeSlot src, BytecodeSlot dst)
        : m_opcode(x_opcodeId<Self>), m_src(src), m_dst(dst)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_src;
    BytecodeSlot m_dst;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue src = *StackFrameHeader::GetLocalAddr(stackframe, bc->m_src);
        if (src.IsDouble(TValue::x_int32Tag))
        {
            double result = -src.AsDouble();
            *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = TValue::CreateDouble(result);
            Dispatch(rc, stackframe, bcu + sizeof(Self));
        }
        else
        {
            if (src.IsPointer(TValue::x_mivTag) && src.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::STRING)
            {
                HeapPtr<HeapString> stringObj = src.AsPointer<HeapString>().As();
                StrScanResult ssr = TryConvertStringToDoubleWithLuaSemantics(TranslateToRawPointer(stringObj->m_string), stringObj->m_length);
                if (ssr.fmt == StrScanFmt::STRSCAN_NUM)
                {
                    double result = -ssr.d;
                    *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = TValue::CreateDouble(result);
                    Dispatch(rc, stackframe, bcu + sizeof(Self));
                }
            }

            UserHeapPointer<void> metatableMaybeNull = GetMetatableForValue(src);
            if (metatableMaybeNull.m_value == 0)
            {
                // TODO: make this error consistent with Lua
                //
                [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("Invalid types for unary minus").m_value);
            }

            HeapPtr<TableObject> metatable = metatableMaybeNull.As<TableObject>();
            GetByIdICInfo icInfo;
            TableObject::PrepareGetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Unm), icInfo /*out*/);
            TValue metamethod = TableObject::GetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Unm).As<void>(), icInfo);

            if (metamethod.IsNil())
            {
                // TODO: make this error consistent with Lua
                //
                [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("Invalid types for unary minus").m_value);
            }

            PrepareMetamethodCallResult res =  SetupFrameForMetamethodCall(rc, stackframe, bcu, std::array { src /*lhs*/, src /*rhs*/ }, metamethod, OnReturnFromStoreResultMetamethodCall<&Self::m_dst>);
            if (!res.m_success)
            {
                // Metamethod exists but is not callable, throw error
                //
                [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessageForUnableToCall(metamethod).m_value);
            }

            uint8_t* calleeBytecode = res.m_calleeEc->m_bytecode;
            InterpreterFn calleeFn = res.m_calleeEc->m_bestEntryPoint;
            [[clang::musttail]] return calleeFn(rc, res.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
        }
    }
} __attribute__((__packed__));

class BcLengthOperator
{
public:
    using Self = BcLengthOperator;

    BcLengthOperator(BytecodeSlot src, BytecodeSlot dst)
        : m_opcode(x_opcodeId<Self>), m_src(src), m_dst(dst)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_src;
    BytecodeSlot m_dst;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue src = *StackFrameHeader::GetLocalAddr(stackframe, bc->m_src);
        if (src.IsPointer(TValue::x_mivTag))
        {
            Type ty = src.AsPointer<UserHeapGcObjectHeader>().As()->m_type;
            if (ty == Type::STRING)
            {
                HeapPtr<HeapString> s = src.AsPointer<HeapString>().As();
                *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = TValue::CreateDouble(s->m_length);
                Dispatch(rc, stackframe, bcu + sizeof(Self));
            }
            else if (ty == Type::TABLE)
            {
                // In Lua 5.1, the primitive length operator is always used, even if there exists a 'length' metamethod
                // But in Lua 5.2+, the 'length' metamethod takes precedence, so this needs to be changed once we add support for Lua 5.2+
                //
                HeapPtr<TableObject> s = src.AsPointer<TableObject>().As();
                uint32_t result = TableObject::GetTableLengthWithLuaSemantics(s);
                *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = TValue::CreateDouble(result);
                Dispatch(rc, stackframe, bcu + sizeof(Self));
            }
        }

        UserHeapPointer<void> metatableMaybeNull = GetMetatableForValue(src);
        if (metatableMaybeNull.m_value == 0)
        {
            // TODO: make this error consistent with Lua
            //
            [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("Invalid types for length").m_value);
        }

        HeapPtr<TableObject> metatable = metatableMaybeNull.As<TableObject>();
        GetByIdICInfo icInfo;
        TableObject::PrepareGetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Len), icInfo /*out*/);
        TValue metamethod = TableObject::GetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Len).As<void>(), icInfo);

        if (metamethod.IsNil())
        {
            // TODO: make this error consistent with Lua
            //
            [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("Invalid types for length").m_value);
        }

        // Lua idiosyncrasy: in Lua 5.1, for unary minus, the parameter passed to metamethod is 'src, src',
        // but for 'length', the parameter is only 'src'.
        // Lua 5.2+ unified the behavior and always pass 'src, src', but for now we are targeting Lua 5.1.
        //
        PrepareMetamethodCallResult res =  SetupFrameForMetamethodCall(rc, stackframe, bcu, std::array { src /*lhs*/, TValue::Nil() /*rhs*/ }, metamethod, OnReturnFromStoreResultMetamethodCall<&Self::m_dst>);
        if (!res.m_success)
        {
            // Metamethod exists but is not callable, throw error
            //
            [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessageForUnableToCall(metamethod).m_value);
        }

        uint8_t* calleeBytecode = res.m_calleeEc->m_bytecode;
        InterpreterFn calleeFn = res.m_calleeEc->m_bestEntryPoint;
        [[clang::musttail]] return calleeFn(rc, res.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
    }
} __attribute__((__packed__));

// Lua allows crazy things like "1 " + " 0xf " (which yields 16): string can be silently converted to number (ignoring whitespace) when
// performing an arithmetic operation. This function does this job. 'func' must be a lambda (double, double) -> double
//
template<typename Func>
std::optional<double> WARN_UNUSED TryDoBinaryOperationConsideringStringConversion(TValue lhs, TValue rhs, const Func& func)
{
    double lhsNumber;
    if (lhs.IsDouble(TValue::x_int32Tag))
    {
        lhsNumber = lhs.AsDouble();
    }
    else if (lhs.IsPointer(TValue::x_mivTag) && lhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::STRING)
    {
        HeapPtr<HeapString> stringObj = lhs.AsPointer<HeapString>().As();
        StrScanResult ssr = TryConvertStringToDoubleWithLuaSemantics(TranslateToRawPointer(stringObj->m_string), stringObj->m_length);
        if (ssr.fmt == StrScanFmt::STRSCAN_NUM)
        {
            lhsNumber = ssr.d;
        }
        else
        {
            return {};
        }
    }
    else
    {
        return {};
    }

    double rhsNumber;
    if (rhs.IsDouble(TValue::x_int32Tag))
    {
        rhsNumber = rhs.AsDouble();
    }
    else if (rhs.IsPointer(TValue::x_mivTag) && rhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::STRING)
    {
        HeapPtr<HeapString> stringObj = rhs.AsPointer<HeapString>().As();
        StrScanResult ssr = TryConvertStringToDoubleWithLuaSemantics(TranslateToRawPointer(stringObj->m_string), stringObj->m_length);
        if (ssr.fmt == StrScanFmt::STRSCAN_NUM)
        {
            rhsNumber = ssr.d;
        }
        else
        {
            return {};
        }
    }
    else
    {
        return {};
    }

    double result = func(lhsNumber, rhsNumber);
    return result;
}

template<LuaMetamethodKind mtKind>
TValue WARN_UNUSED GetMetamethodForBinaryArithmeticOperation(TValue lhs, TValue rhs)
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
template<LuaMetamethodKind mtKind>
TValue WARN_UNUSED GetMetamethodFromMetatableForComparisonOperation(HeapPtr<TableObject> lhsMetatable, HeapPtr<TableObject> rhsMetatable)
{
    TValue lhsMetamethod;
    {
        // For 'eq', if either table doesn't have metamethod, the result is 'false', so it's a probable case that worth a fast path.
        // For 'le' or 'lt', however, the behavior is to throw error, so the situtation becomes just the opposite: it's unlikely the table doesn't have metamethod.
        //
        if constexpr(mtKind == LuaMetamethodKind::Eq)
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
        if constexpr(mtKind == LuaMetamethodKind::Eq)
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

    assert(!lhsMetamethod.IsInt32(TValue::x_int32Tag) && "unimplemented");
    assert(!rhsMetamethod.IsInt32(TValue::x_int32Tag) && "unimplemented");

    // Now, perform a primitive comparison of lhsMetamethod and rhsMetamethod
    //
    if (unlikely(lhsMetamethod.IsDouble(TValue::x_int32Tag)))
    {
        if (!rhsMetamethod.IsDouble(TValue::x_int32Tag))
        {
            return TValue::Nil();
        }
        // If both values are double, we must do a floating point comparison,
        // otherwise we will fail on edge cases like negative zero (-0 == 0) and NaN (NaN != NaN)
        //
        if (UnsafeFloatEqual(lhsMetamethod.AsDouble(), rhsMetamethod.AsDouble()))
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

class BcAdd
{
public:
    using Self = BcAdd;

    BcAdd(BytecodeSlot lhs, BytecodeSlot rhs, BytecodeSlot result)
        : m_opcode(x_opcodeId<Self>), m_lhs(lhs), m_rhs(rhs), m_result(result)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    BytecodeSlot m_result;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(lhs.AsDouble() + rhs.AsDouble());
            Dispatch(rc, stackframe, bcu + sizeof(Self));
        }
        else
        {
            TValue metamethod;

            if (likely(lhs.IsPointer(TValue::x_mivTag) && lhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::TABLE))
            {
                HeapPtr<TableObject> tableObj = lhs.AsPointer<TableObject>().As();
                TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                if (result.m_result.m_value != 0)
                {
                    HeapPtr<TableObject> metatable = result.m_result.As<TableObject>();
                    GetByIdICInfo icInfo;
                    TableObject::PrepareGetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Add), icInfo /*out*/);
                    metamethod = TableObject::GetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Add).As<void>(), icInfo);
                    if (likely(!metamethod.IsNil()))
                    {
                        goto do_metamethod_call;
                    }
                }
            }

            // Handle case that lhs/rhs are number or string that can be converted to number
            //
            {
                std::optional<double> res = TryDoBinaryOperationConsideringStringConversion(lhs, rhs, [](double l, double r) { return l + r; });
                if (res)
                {
                    *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(res.value());
                    Dispatch(rc, stackframe, bcu + sizeof(Self));
                }
            }

            // Now we know we will need to call metamethod, determine the metamethod to call
            //
            // TODO: this could have been better since we already know lhs is not a table with metatable
            //
            metamethod = GetMetamethodForBinaryArithmeticOperation<LuaMetamethodKind::Add>(lhs, rhs);
            if (metamethod.IsNil())
            {
                // TODO: make this error consistent with Lua
                //
                [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("Invalid types for arithmetic add").m_value);
            }

do_metamethod_call:
            {
                // We've found the (non-nil) metamethod to call
                //
                PrepareMetamethodCallResult res = SetupFrameForMetamethodCall(rc, stackframe, bcu, std::array{ lhs, rhs }, metamethod, OnReturnFromStoreResultMetamethodCall<&Self::m_result>);
                if (!res.m_success)
                {
                    // Metamethod exists but is not callable, throw error
                    //
                    [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessageForUnableToCall(metamethod).m_value);
                }

                uint8_t* calleeBytecode = res.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = res.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, res.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }
        }
    }
} __attribute__((__packed__));

class BcSub
{
public:
    using Self = BcSub;

    BcSub(BytecodeSlot lhs, BytecodeSlot rhs, BytecodeSlot result)
        : m_opcode(x_opcodeId<Self>), m_lhs(lhs), m_rhs(rhs), m_result(result)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    BytecodeSlot m_result;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(lhs.AsDouble() - rhs.AsDouble());
            Dispatch(rc, stackframe, bcu + sizeof(Self));
        }
        else
        {
            TValue metamethod;

            if (likely(lhs.IsPointer(TValue::x_mivTag) && lhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::TABLE))
            {
                HeapPtr<TableObject> tableObj = lhs.AsPointer<TableObject>().As();
                TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                if (result.m_result.m_value != 0)
                {
                    HeapPtr<TableObject> metatable = result.m_result.As<TableObject>();
                    GetByIdICInfo icInfo;
                    TableObject::PrepareGetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Sub), icInfo /*out*/);
                    metamethod = TableObject::GetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Sub).As<void>(), icInfo);
                    if (likely(!metamethod.IsNil()))
                    {
                        goto do_metamethod_call;
                    }
                }
            }

            // Handle case that lhs/rhs are number or string that can be converted to number
            //
            {
                std::optional<double> res = TryDoBinaryOperationConsideringStringConversion(lhs, rhs, [](double l, double r) { return l - r; });
                if (res)
                {
                    *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(res.value());
                    Dispatch(rc, stackframe, bcu + sizeof(Self));
                }
            }

            // Now we know we will need to call metamethod, determine the metamethod to call
            //
            // TODO: this could have been better since we already know lhs is not a table with metatable
            //
            metamethod = GetMetamethodForBinaryArithmeticOperation<LuaMetamethodKind::Sub>(lhs, rhs);
            if (metamethod.IsNil())
            {
                // TODO: make this error consistent with Lua
                //
                [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("Invalid types for arithmetic sub").m_value);
            }

do_metamethod_call:
            {
                // We've found the (non-nil) metamethod to call
                //
                PrepareMetamethodCallResult res = SetupFrameForMetamethodCall(rc, stackframe, bcu, std::array { lhs, rhs }, metamethod, OnReturnFromStoreResultMetamethodCall<&Self::m_result>);
                if (!res.m_success)
                {
                    // Metamethod exists but is not callable, throw error
                    //
                    [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessageForUnableToCall(metamethod).m_value);
                }

                uint8_t* calleeBytecode = res.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = res.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, res.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }
        }
    }
} __attribute__((__packed__));

class BcMul
{
public:
    using Self = BcMul;

    BcMul(BytecodeSlot lhs, BytecodeSlot rhs, BytecodeSlot result)
        : m_opcode(x_opcodeId<Self>), m_lhs(lhs), m_rhs(rhs), m_result(result)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    BytecodeSlot m_result;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(lhs.AsDouble() * rhs.AsDouble());
            Dispatch(rc, stackframe, bcu + sizeof(Self));
        }
        else
        {
            TValue metamethod;

            if (likely(lhs.IsPointer(TValue::x_mivTag) && lhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::TABLE))
            {
                HeapPtr<TableObject> tableObj = lhs.AsPointer<TableObject>().As();
                TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                if (result.m_result.m_value != 0)
                {
                    HeapPtr<TableObject> metatable = result.m_result.As<TableObject>();
                    GetByIdICInfo icInfo;
                    TableObject::PrepareGetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Mul), icInfo /*out*/);
                    metamethod = TableObject::GetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Mul).As<void>(), icInfo);
                    if (likely(!metamethod.IsNil()))
                    {
                        goto do_metamethod_call;
                    }
                }
            }

            // Handle case that lhs/rhs are number or string that can be converted to number
            //
            {
                std::optional<double> res = TryDoBinaryOperationConsideringStringConversion(lhs, rhs, [](double l, double r) { return l * r; });
                if (res)
                {
                    *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(res.value());
                    Dispatch(rc, stackframe, bcu + sizeof(Self));
                }
            }

            // Now we know we will need to call metamethod, determine the metamethod to call
            //
            // TODO: this could have been better since we already know lhs is not a table with metatable
            //
            metamethod = GetMetamethodForBinaryArithmeticOperation<LuaMetamethodKind::Mul>(lhs, rhs);
            if (metamethod.IsNil())
            {
                // TODO: make this error consistent with Lua
                //
                [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("Invalid types for arithmetic mul").m_value);
            }

do_metamethod_call:
            {
                // We've found the (non-nil) metamethod to call
                //
                PrepareMetamethodCallResult res = SetupFrameForMetamethodCall(rc, stackframe, bcu, std::array { lhs, rhs }, metamethod, OnReturnFromStoreResultMetamethodCall<&Self::m_result>);
                if (!res.m_success)
                {
                    // Metamethod exists but is not callable, throw error
                    //
                    [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessageForUnableToCall(metamethod).m_value);
                }

                uint8_t* calleeBytecode = res.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = res.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, res.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }
        }
    }
} __attribute__((__packed__));

class BcDiv
{
public:
    using Self = BcDiv;

    BcDiv(BytecodeSlot lhs, BytecodeSlot rhs, BytecodeSlot result)
        : m_opcode(x_opcodeId<Self>), m_lhs(lhs), m_rhs(rhs), m_result(result)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    BytecodeSlot m_result;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(lhs.AsDouble() / rhs.AsDouble());
            Dispatch(rc, stackframe, bcu + sizeof(Self));
        }
        else
        {
            TValue metamethod;

            if (likely(lhs.IsPointer(TValue::x_mivTag) && lhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::TABLE))
            {
                HeapPtr<TableObject> tableObj = lhs.AsPointer<TableObject>().As();
                TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                if (result.m_result.m_value != 0)
                {
                    HeapPtr<TableObject> metatable = result.m_result.As<TableObject>();
                    GetByIdICInfo icInfo;
                    TableObject::PrepareGetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Div), icInfo /*out*/);
                    metamethod = TableObject::GetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Div).As<void>(), icInfo);
                    if (likely(!metamethod.IsNil()))
                    {
                        goto do_metamethod_call;
                    }
                }
            }

            // Handle case that lhs/rhs are number or string that can be converted to number
            //
            {
                std::optional<double> res = TryDoBinaryOperationConsideringStringConversion(lhs, rhs, [](double l, double r) { return l / r; });
                if (res)
                {
                    *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(res.value());
                    Dispatch(rc, stackframe, bcu + sizeof(Self));
                }
            }

            // Now we know we will need to call metamethod, determine the metamethod to call
            //
            // TODO: this could have been better since we already know lhs is not a table with metatable
            //
            metamethod = GetMetamethodForBinaryArithmeticOperation<LuaMetamethodKind::Div>(lhs, rhs);
            if (metamethod.IsNil())
            {
                // TODO: make this error consistent with Lua
                //
                [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("Invalid types for arithmetic div").m_value);
            }

do_metamethod_call:
            {
                // We've found the (non-nil) metamethod to call
                //
                PrepareMetamethodCallResult res = SetupFrameForMetamethodCall(rc, stackframe, bcu, std::array { lhs, rhs }, metamethod, OnReturnFromStoreResultMetamethodCall<&Self::m_result>);
                if (!res.m_success)
                {
                    // Metamethod exists but is not callable, throw error
                    //
                    [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessageForUnableToCall(metamethod).m_value);
                }

                uint8_t* calleeBytecode = res.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = res.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, res.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }
        }
    }
} __attribute__((__packed__));

class BcMod
{
public:
    using Self = BcMod;

    BcMod(BytecodeSlot lhs, BytecodeSlot rhs, BytecodeSlot result)
        : m_opcode(x_opcodeId<Self>), m_lhs(lhs), m_rhs(rhs), m_result(result)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    BytecodeSlot m_result;

    static double WARN_UNUSED ModulusWithLuaSemantics(double a, double b)
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

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(ModulusWithLuaSemantics(lhs.AsDouble(), rhs.AsDouble()));
            Dispatch(rc, stackframe, bcu + sizeof(Self));
        }
        else
        {
            TValue metamethod;

            if (likely(lhs.IsPointer(TValue::x_mivTag) && lhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::TABLE))
            {
                HeapPtr<TableObject> tableObj = lhs.AsPointer<TableObject>().As();
                TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                if (result.m_result.m_value != 0)
                {
                    HeapPtr<TableObject> metatable = result.m_result.As<TableObject>();
                    GetByIdICInfo icInfo;
                    TableObject::PrepareGetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Mod), icInfo /*out*/);
                    metamethod = TableObject::GetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Mod).As<void>(), icInfo);
                    if (likely(!metamethod.IsNil()))
                    {
                        goto do_metamethod_call;
                    }
                }
            }

            // Handle case that lhs/rhs are number or string that can be converted to number
            //
            {
                std::optional<double> res = TryDoBinaryOperationConsideringStringConversion(lhs, rhs, ModulusWithLuaSemantics);
                if (res)
                {
                    *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(res.value());
                    Dispatch(rc, stackframe, bcu + sizeof(Self));
                }
            }

            // Now we know we will need to call metamethod, determine the metamethod to call
            //
            // TODO: this could have been better since we already know lhs is not a table with metatable
            //
            metamethod = GetMetamethodForBinaryArithmeticOperation<LuaMetamethodKind::Mod>(lhs, rhs);
            if (metamethod.IsNil())
            {
                // TODO: make this error consistent with Lua
                //
                [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("Invalid types for arithmetic mod").m_value);
            }

do_metamethod_call:
            {
                // We've found the (non-nil) metamethod to call
                //
                PrepareMetamethodCallResult res = SetupFrameForMetamethodCall(rc, stackframe, bcu, std::array { lhs, rhs }, metamethod, OnReturnFromStoreResultMetamethodCall<&Self::m_result>);
                if (!res.m_success)
                {
                    // Metamethod exists but is not callable, throw error
                    //
                    [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessageForUnableToCall(metamethod).m_value);
                }

                uint8_t* calleeBytecode = res.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = res.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, res.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }
        }
    }
} __attribute__((__packed__));

class BcPow
{
public:
    using Self = BcPow;

    BcPow(BytecodeSlot lhs, BytecodeSlot rhs, BytecodeSlot result)
        : m_opcode(x_opcodeId<Self>), m_lhs(lhs), m_rhs(rhs), m_result(result)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    BytecodeSlot m_result;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue lhs = *StackFrameHeader::GetLocalAddr(stackframe, bc->m_lhs);
        TValue rhs = *StackFrameHeader::GetLocalAddr(stackframe, bc->m_rhs);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(pow(lhs.AsDouble(), rhs.AsDouble()));
            Dispatch(rc, stackframe, bcu + sizeof(Self));
        }
        else
        {
            TValue metamethod;

            if (likely(lhs.IsPointer(TValue::x_mivTag) && lhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::TABLE))
            {
                HeapPtr<TableObject> tableObj = lhs.AsPointer<TableObject>().As();
                TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                if (result.m_result.m_value != 0)
                {
                    HeapPtr<TableObject> metatable = result.m_result.As<TableObject>();
                    GetByIdICInfo icInfo;
                    TableObject::PrepareGetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Pow), icInfo /*out*/);
                    metamethod = TableObject::GetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Pow).As<void>(), icInfo);
                    if (likely(!metamethod.IsNil()))
                    {
                        goto do_metamethod_call;
                    }
                }
            }

            // Handle case that lhs/rhs are number or string that can be converted to number
            //
            {
                std::optional<double> res = TryDoBinaryOperationConsideringStringConversion(lhs, rhs, [](double l, double r) { return pow(l, r); });
                if (res)
                {
                    *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(res.value());
                    Dispatch(rc, stackframe, bcu + sizeof(Self));
                }
            }

            // Now we know we will need to call metamethod, determine the metamethod to call
            //
            // TODO: this could have been better since we already know lhs is not a table with metatable
            //
            metamethod = GetMetamethodForBinaryArithmeticOperation<LuaMetamethodKind::Pow>(lhs, rhs);
            if (metamethod.IsNil())
            {
                // TODO: make this error consistent with Lua
                //
                [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("Invalid types for arithmetic pow").m_value);
            }

do_metamethod_call:
            {
                // We've found the (non-nil) metamethod to call
                //
                PrepareMetamethodCallResult res = SetupFrameForMetamethodCall(rc, stackframe, bcu, std::array { lhs, rhs }, metamethod, OnReturnFromStoreResultMetamethodCall<&Self::m_result>);
                if (!res.m_success)
                {
                    // Metamethod exists but is not callable, throw error
                    //
                    [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessageForUnableToCall(metamethod).m_value);
                }

                uint8_t* calleeBytecode = res.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = res.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, res.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }
        }
    }
} __attribute__((__packed__));

class BcConcat
{
public:
    using Self = BcConcat;

    BcConcat(BytecodeSlot dst, BytecodeSlot begin, uint32_t num)
        : m_opcode(x_opcodeId<Self>), m_dst(dst), m_begin(begin), m_num(num)
    {
        assert(num >= 2);
    }

    uint8_t m_opcode;
    BytecodeSlot m_dst;
    BytecodeSlot m_begin;
    uint32_t m_num;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue* begin = StackFrameHeader::GetLocalAddr(stackframe, bc->m_begin);
        uint32_t num = bc->m_num;
        bool success = true;
        bool needNumberConversion = false;
        for (uint32_t i = 0; i < num; i++)
        {
            TValue val = begin[i];
            if (val.IsPointer(TValue::x_mivTag))
            {
                if (val.AsPointer<UserHeapGcObjectHeader>().As()->m_type != Type::STRING)
                {
                    success = false;
                    break;
                }
                continue;
            }
            if (val.IsDouble(TValue::x_int32Tag) || val.IsInt32(TValue::x_int32Tag))
            {
                needNumberConversion = true;
            }
        }

        if (likely(success))
        {
            VM* vm = VM::GetActiveVMForCurrentThread();
            if (needNumberConversion)
            {
                for (uint32_t i = 0; i < num; i++)
                {
                    TValue val = begin[i];
                    if (val.IsDouble(TValue::x_int32Tag))
                    {
                        double dbl = val.AsDouble();
                        char buf[x_default_tostring_buffersize_double];
                        char* bufEnd = StringifyDoubleUsingDefaultLuaFormattingOptions(buf /*out*/, dbl);
                        begin[i] = TValue::CreatePointer(vm->CreateStringObjectFromRawString(buf, static_cast<uint32_t>(bufEnd - buf)));
                    }
                    else if (val.IsInt32(TValue::x_int32Tag))
                    {
                        char buf[x_default_tostring_buffersize_int];
                        char* bufEnd = StringifyInt32UsingDefaultLuaFormattingOptions(buf /*out*/, val.AsInt32());
                        begin[i] = TValue::CreatePointer(vm->CreateStringObjectFromRawString(buf, static_cast<uint32_t>(bufEnd - buf)));
                    }
                }
            }

            TValue result = TValue::CreatePointer(vm->CreateStringObjectFromConcatenation(begin, num));
            *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = result;
            Dispatch(rc, stackframe, bcu + sizeof(Self));
        }

        // Need to call metamethod
        // Note that this must be executed from right to left (this semantic is expected by Lua)
        // Do primitive concatenation until we found the first location to call metamethod
        //
        ScanForMetamethodCallResult fsr = ScanForMetamethodCall(begin, static_cast<int32_t>(bc->m_num) - 2, begin[bc->m_num - 1] /*initialValue*/);
        assert(!fsr.m_exhausted);

        // Store the next slot to concat on metamethod return on the last slot of the values to concat
        // This slot is clobberable (confirmed by checking LuaJIT source code).
        //
        begin[bc->m_num - 1] = TValue::CreateInt32(fsr.m_endOffset, TValue::x_int32Tag);

        // Call metamethod
        //
        TValue metamethod = GetMetamethodForBinaryArithmeticOperation<LuaMetamethodKind::Concat>(fsr.m_lhsValue, fsr.m_rhsValue);
        if (metamethod.IsNil())
        {
            // TODO: make this error consistent with Lua
            //
            [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("Invalid types for concat").m_value);
        }

        PrepareMetamethodCallResult pmcr = SetupFrameForMetamethodCall(rc, stackframe, bcu, std::array { fsr.m_lhsValue, fsr.m_rhsValue }, metamethod, OnMetamethodReturn);
        if (!pmcr.m_success)
        {
            // Metamethod exists but is not callable, throw error
            //
            [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessageForUnableToCall(metamethod).m_value);
        }

        uint8_t* calleeBytecode = pmcr.m_calleeEc->m_bytecode;
        InterpreterFn calleeFn = pmcr.m_calleeEc->m_bestEntryPoint;
        [[clang::musttail]] return calleeFn(rc, pmcr.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
    }

    struct ScanForMetamethodCallResult
    {
        // If true, 'm_lhsValue' stores the final value, and no more metamethod call happens
        // If false, 'm_lhsValue' and 'm_rhsValue' shall be passed to metamethod call
        //
        bool m_exhausted;
        // The slot offset minus one where 'm_lhsValue' is found, if m_exhausted == false
        //
        int32_t m_endOffset;
        // The value pair for metamethod call
        //
        TValue m_lhsValue;
        TValue m_rhsValue;
    };

    static std::optional<UserHeapPointer<HeapString>> TryGetStringOrConvertNumberToString(TValue value)
    {
        if (value.IsPointer(TValue::x_mivTag))
        {
            if (value.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::STRING)
            {
                return value.AsPointer<HeapString>();
            }
        }
        if (value.IsDouble(TValue::x_int32Tag))
        {
            char buf[x_default_tostring_buffersize_double];
            char* bufEnd = StringifyDoubleUsingDefaultLuaFormattingOptions(buf /*out*/, value.AsDouble());
            return VM::GetActiveVMForCurrentThread()->CreateStringObjectFromRawString(buf, static_cast<uint32_t>(bufEnd - buf));
        }
        else if (value.IsInt32(TValue::x_int32Tag))
        {
            char buf[x_default_tostring_buffersize_int];
            char* bufEnd = StringifyInt32UsingDefaultLuaFormattingOptions(buf /*out*/, value.AsInt32());
            return VM::GetActiveVMForCurrentThread()->CreateStringObjectFromRawString(buf, static_cast<uint32_t>(bufEnd - buf));
        }
        else
        {
            return {};
        }
    }

    // Try to execute loop: while startOffset != -1: curValue = base[startOffset] .. curValue, startOffset -= 1
    // Until the loop ends or it encounters an expression where a metamethod call is needed.
    // Returns the final value if the loop ends, or the value pair for the metamethod call.
    //
    static ScanForMetamethodCallResult WARN_UNUSED ScanForMetamethodCall(TValue* base, int32_t startOffset, TValue curValue)
    {
        assert(startOffset >= 0);

        std::optional<UserHeapPointer<HeapString>> optStr = TryGetStringOrConvertNumberToString(curValue);
        if (!optStr)
        {
            return {
                .m_exhausted = false,
                .m_endOffset = startOffset - 1,
                .m_lhsValue = base[startOffset],
                .m_rhsValue = curValue
            };
        }

        // This is tricky: curString and curValue are out of sync at the beginning of the loop (curValue might be number while curString must be string),
        // but are always kept in sync in every later iteration (see end of the loop below), and the return inside the loop returns 'curValue'.
        // This is required, because if no concatenation happens before metamethod call,
        // the metamethod should see the original parameter, not the coerced-to-string parameter.
        //
        UserHeapPointer<HeapString> curString = optStr.value();
        while (startOffset >= 0)
        {
            std::optional<UserHeapPointer<HeapString>> lhs = TryGetStringOrConvertNumberToString(base[startOffset]);
            if (!lhs)
            {
                return {
                    .m_exhausted = false,
                    .m_endOffset = startOffset - 1,
                    .m_lhsValue = base[startOffset],
                    .m_rhsValue = curValue
                };
            }

            startOffset--;
            TValue tmp[2];
            tmp[0] = TValue::CreatePointer(lhs.value());
            tmp[1] = TValue::CreatePointer(curString);
            curString = VM::GetActiveVMForCurrentThread()->CreateStringObjectFromConcatenation(tmp, 2 /*len*/);
            curValue = TValue::CreatePointer(curString);
        }

        return {
            .m_exhausted = true,
            .m_lhsValue = curValue
        };
    }

    static void OnMetamethodReturn(CoroutineRuntimeContext* rc, void* stackframe, const uint8_t* retValuesU, uint64_t /*numRetValues*/)
    {
        const TValue* retValues = reinterpret_cast<const TValue*>(retValuesU);
        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackframe);
        HeapPtr<ExecutableCode> callerEc = TCGet(hdr->m_func->m_executable).As();
        assert(TranslateToRawPointer(callerEc)->IsBytecodeFunction());
        uint8_t* callerBytecodeStart = callerEc->m_bytecode;
        ConstRestrictPtr<uint8_t> bcu = callerBytecodeStart + hdr->m_callerBytecodeOffset;
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        rc->m_codeBlock = static_cast<CodeBlock*>(TranslateToRawPointer(callerEc));

        TValue* begin = StackFrameHeader::GetLocalAddr(stackframe, bc->m_begin);

        TValue curValue = *retValues;

        assert(begin[bc->m_num - 1].IsInt32(TValue::x_int32Tag));
        int32_t nextSlotToConcat = begin[bc->m_num - 1].AsInt32();
        assert(nextSlotToConcat >= -1 && nextSlotToConcat < static_cast<int32_t>(bc->m_num) - 2);
        if (nextSlotToConcat == -1)
        {
            *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = curValue;
            Dispatch(rc, stackframe, bcu + sizeof(Self));
        }
        else
        {
            ScanForMetamethodCallResult fsr = ScanForMetamethodCall(begin, nextSlotToConcat, curValue);
            if (fsr.m_exhausted)
            {
                *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = fsr.m_lhsValue;
                Dispatch(rc, stackframe, bcu + sizeof(Self));
            }
            else
            {
                begin[bc->m_num - 1] = TValue::CreateInt32(fsr.m_endOffset, TValue::x_int32Tag);

                // Call metamethod
                //
                TValue metamethod = GetMetamethodForBinaryArithmeticOperation<LuaMetamethodKind::Concat>(fsr.m_lhsValue, fsr.m_rhsValue);
                if (metamethod.IsNil())
                {
                    // TODO: make this error consistent with Lua
                    //
                    [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("Invalid types for concat").m_value);
                }

                PrepareMetamethodCallResult pmcr = SetupFrameForMetamethodCall(rc, stackframe, bcu, std::array { fsr.m_lhsValue, fsr.m_rhsValue }, metamethod, OnMetamethodReturn);
                if (!pmcr.m_success)
                {
                    // Metamethod exists but is not callable, throw error
                    //
                    [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessageForUnableToCall(metamethod).m_value);
                }

                uint8_t* calleeBytecode = pmcr.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = pmcr.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, pmcr.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }
        }
    }
} __attribute__((__packed__));

class BcIsLT
{
public:
    using Self = BcIsLT;

    BcIsLT(BytecodeSlot lhs, BytecodeSlot rhs)
        : m_opcode(x_opcodeId<Self>), m_lhs(lhs), m_rhs(rhs), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&Self::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag)))
        {
            if (unlikely(!rhs.IsDouble(TValue::x_int32Tag)))
            {
                goto fail;
            }
            if (lhs.AsDouble() < rhs.AsDouble())
            {
                Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
            }
            else
            {
                Dispatch(rc, stackframe, bcu + sizeof(Self));
            }
        }
        else
        {
            TValue metamethod;
            if (lhs.IsPointer(TValue::x_mivTag))
            {
                if (!rhs.IsPointer(TValue::x_mivTag))
                {
                    goto fail;
                }
                Type lhsTy = lhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type;
                Type rhsTy = rhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type;
                if (unlikely(lhsTy != rhsTy))
                {
                    goto fail;
                }
                if (lhsTy == Type::STRING)
                {
                    VM* vm = VM::GetActiveVMForCurrentThread();
                    HeapString* lhsString = TranslateToRawPointer(vm, lhs.AsPointer<HeapString>().As());
                    HeapString* rhsString = TranslateToRawPointer(vm, rhs.AsPointer<HeapString>().As());
                    int cmpRes = lhsString->Compare(rhsString);
                    if (cmpRes < 0)
                    {
                        Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
                    }
                    else
                    {
                        Dispatch(rc, stackframe, bcu + sizeof(Self));
                    }
                }
                if (lhsTy == Type::TABLE)
                {
                    HeapPtr<TableObject> lhsMetatable;
                    {
                        HeapPtr<TableObject> tableObj = lhs.AsPointer<TableObject>().As();
                        TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                        if (result.m_result.m_value == 0)
                        {
                            goto fail;
                        }
                        lhsMetatable = result.m_result.As<TableObject>();
                    }

                    HeapPtr<TableObject> rhsMetatable;
                    {
                        HeapPtr<TableObject> tableObj = rhs.AsPointer<TableObject>().As();
                        TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                        if (result.m_result.m_value == 0)
                        {
                            goto fail;
                        }
                        rhsMetatable = result.m_result.As<TableObject>();
                    }

                    metamethod = GetMetamethodFromMetatableForComparisonOperation<LuaMetamethodKind::Lt>(lhsMetatable, rhsMetatable);
                    if (metamethod.IsNil())
                    {
                        goto fail;
                    }
                    goto do_metamethod_call;
                }

                assert(lhsTy != Type::USERDATA && "unimplemented");

                metamethod = GetMetamethodForValue(lhs, LuaMetamethodKind::Lt);
                if (metamethod.IsNil())
                {
                    goto fail;
                }
                goto do_metamethod_call;
            }

            assert(!lhs.IsInt32(TValue::x_int32Tag) && "unimplemented");

            {
                assert(lhs.IsMIV(TValue::x_mivTag));
                if (!rhs.IsMIV(TValue::x_mivTag))
                {
                    goto fail;
                }
                // Must be both 'nil', or both 'boolean', in order to consider metatable
                //
                if (lhs.IsNil() != rhs.IsNil())
                {
                    goto fail;
                }
                metamethod = GetMetamethodForValue(lhs, LuaMetamethodKind::Lt);
                if (metamethod.IsNil())
                {
                    goto fail;
                }
                goto do_metamethod_call;
            }

do_metamethod_call:
            {
                PrepareMetamethodCallResult pmcr = SetupFrameForMetamethodCall(rc, stackframe, bcu, std::array { lhs, rhs }, metamethod, OnReturnFromComparativeMetamethodCall<true /*branchIfTruthy*/, &Self::m_offset>);

                if (!pmcr.m_success)
                {
                    // Metamethod exists but is not callable, throw error
                    //
                    [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessageForUnableToCall(metamethod).m_value);
                }

                uint8_t* calleeBytecode = pmcr.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = pmcr.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, pmcr.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }
        }
fail:
        // TODO: make this error consistent with Lua
        //
        [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("Invalid types for less than").m_value);
    }
} __attribute__((__packed__));

// IsNLT x y is different from IsLE y x in the presense of NaN (in other words, !(x < y) is not always equal to y <= x)
// because if x or y is NaN, the former gives true while latter gives false
// For same reason we need IsNLE
//
class BcIsNLT
{
public:
    using Self = BcIsNLT;

    BcIsNLT(BytecodeSlot lhs, BytecodeSlot rhs)
        : m_opcode(x_opcodeId<Self>), m_lhs(lhs), m_rhs(rhs), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&Self::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag)))
        {
            if (unlikely(!rhs.IsDouble(TValue::x_int32Tag)))
            {
                goto fail;
            }
            if (!(lhs.AsDouble() < rhs.AsDouble()))
            {
                Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
            }
            else
            {
                Dispatch(rc, stackframe, bcu + sizeof(Self));
            }
        }
        else
        {
            TValue metamethod;
            if (lhs.IsPointer(TValue::x_mivTag))
            {
                if (!rhs.IsPointer(TValue::x_mivTag))
                {
                    goto fail;
                }
                Type lhsTy = lhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type;
                Type rhsTy = rhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type;
                if (unlikely(lhsTy != rhsTy))
                {
                    goto fail;
                }
                if (lhsTy == Type::STRING)
                {
                    VM* vm = VM::GetActiveVMForCurrentThread();
                    HeapString* lhsString = TranslateToRawPointer(vm, lhs.AsPointer<HeapString>().As());
                    HeapString* rhsString = TranslateToRawPointer(vm, rhs.AsPointer<HeapString>().As());
                    int cmpRes = lhsString->Compare(rhsString);
                    if (!(cmpRes < 0))
                    {
                        Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
                    }
                    else
                    {
                        Dispatch(rc, stackframe, bcu + sizeof(Self));
                    }
                }
                if (lhsTy == Type::TABLE)
                {
                    HeapPtr<TableObject> lhsMetatable;
                    {
                        HeapPtr<TableObject> tableObj = lhs.AsPointer<TableObject>().As();
                        TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                        if (result.m_result.m_value == 0)
                        {
                            goto fail;
                        }
                        lhsMetatable = result.m_result.As<TableObject>();
                    }

                    HeapPtr<TableObject> rhsMetatable;
                    {
                        HeapPtr<TableObject> tableObj = rhs.AsPointer<TableObject>().As();
                        TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                        if (result.m_result.m_value == 0)
                        {
                            goto fail;
                        }
                        rhsMetatable = result.m_result.As<TableObject>();
                    }

                    metamethod = GetMetamethodFromMetatableForComparisonOperation<LuaMetamethodKind::Lt>(lhsMetatable, rhsMetatable);
                    if (metamethod.IsNil())
                    {
                        goto fail;
                    }
                    goto do_metamethod_call;
                }

                assert(lhsTy != Type::USERDATA && "unimplemented");

                metamethod = GetMetamethodForValue(lhs, LuaMetamethodKind::Lt);
                if (metamethod.IsNil())
                {
                    goto fail;
                }
                goto do_metamethod_call;
            }

            assert(!lhs.IsInt32(TValue::x_int32Tag) && "unimplemented");

            {
                assert(lhs.IsMIV(TValue::x_mivTag));
                if (!rhs.IsMIV(TValue::x_mivTag))
                {
                    goto fail;
                }
                // Must be both 'nil', or both 'boolean', in order to consider metatable
                //
                if (lhs.IsNil() != rhs.IsNil())
                {
                    goto fail;
                }
                metamethod = GetMetamethodForValue(lhs, LuaMetamethodKind::Lt);
                if (metamethod.IsNil())
                {
                    goto fail;
                }
                goto do_metamethod_call;
            }

do_metamethod_call:
            {
                PrepareMetamethodCallResult pmcr = SetupFrameForMetamethodCall(rc, stackframe, bcu, std::array { lhs, rhs }, metamethod, OnReturnFromComparativeMetamethodCall<false /*branchIfTruthy*/, &Self::m_offset>);

                if (!pmcr.m_success)
                {
                    // Metamethod exists but is not callable, throw error
                    //
                    [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessageForUnableToCall(metamethod).m_value);
                }

                uint8_t* calleeBytecode = pmcr.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = pmcr.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, pmcr.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }
        }
fail:
        // TODO: make this error consistent with Lua
        //
        [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("Invalid types for less than").m_value);
    }
} __attribute__((__packed__));

class BcIsLE
{
public:
    using Self = BcIsLE;

    BcIsLE(BytecodeSlot lhs, BytecodeSlot rhs)
        : m_opcode(x_opcodeId<Self>), m_lhs(lhs), m_rhs(rhs), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&Self::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag)))
        {
            if (unlikely(!rhs.IsDouble(TValue::x_int32Tag)))
            {
                goto fail;
            }
            if (lhs.AsDouble() <= rhs.AsDouble())
            {
                Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
            }
            else
            {
                Dispatch(rc, stackframe, bcu + sizeof(Self));
            }
        }
        else
        {
            TValue metamethod;
            if (lhs.IsPointer(TValue::x_mivTag))
            {
                if (!rhs.IsPointer(TValue::x_mivTag))
                {
                    goto fail;
                }
                Type lhsTy = lhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type;
                Type rhsTy = rhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type;
                if (unlikely(lhsTy != rhsTy))
                {
                    goto fail;
                }
                if (lhsTy == Type::STRING)
                {
                    VM* vm = VM::GetActiveVMForCurrentThread();
                    HeapString* lhsString = TranslateToRawPointer(vm, lhs.AsPointer<HeapString>().As());
                    HeapString* rhsString = TranslateToRawPointer(vm, rhs.AsPointer<HeapString>().As());
                    int cmpRes = lhsString->Compare(rhsString);
                    if (cmpRes <= 0)
                    {
                        Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
                    }
                    else
                    {
                        Dispatch(rc, stackframe, bcu + sizeof(Self));
                    }
                }
                if (lhsTy == Type::TABLE)
                {
                    HeapPtr<TableObject> lhsMetatable;
                    {
                        HeapPtr<TableObject> tableObj = lhs.AsPointer<TableObject>().As();
                        TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                        if (result.m_result.m_value == 0)
                        {
                            goto fail;
                        }
                        lhsMetatable = result.m_result.As<TableObject>();
                    }

                    HeapPtr<TableObject> rhsMetatable;
                    {
                        HeapPtr<TableObject> tableObj = rhs.AsPointer<TableObject>().As();
                        TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                        if (result.m_result.m_value == 0)
                        {
                            goto fail;
                        }
                        rhsMetatable = result.m_result.As<TableObject>();
                    }

                    metamethod = GetMetamethodFromMetatableForComparisonOperation<LuaMetamethodKind::Le>(lhsMetatable, rhsMetatable);
                    if (metamethod.IsNil())
                    {
                        goto fail;
                    }
                    goto do_metamethod_call;
                }

                assert(lhsTy != Type::USERDATA && "unimplemented");

                metamethod = GetMetamethodForValue(lhs, LuaMetamethodKind::Le);
                if (metamethod.IsNil())
                {
                    goto fail;
                }
                goto do_metamethod_call;
            }

            assert(!lhs.IsInt32(TValue::x_int32Tag) && "unimplemented");

            {
                assert(lhs.IsMIV(TValue::x_mivTag));
                if (!rhs.IsMIV(TValue::x_mivTag))
                {
                    goto fail;
                }
                // Must be both 'nil', or both 'boolean', in order to consider metatable
                //
                if (lhs.IsNil() != rhs.IsNil())
                {
                    goto fail;
                }
                metamethod = GetMetamethodForValue(lhs, LuaMetamethodKind::Le);
                if (metamethod.IsNil())
                {
                    goto fail;
                }
                goto do_metamethod_call;
            }

do_metamethod_call:
            {
                PrepareMetamethodCallResult pmcr = SetupFrameForMetamethodCall(rc, stackframe, bcu, std::array { lhs, rhs }, metamethod, OnReturnFromComparativeMetamethodCall<true /*branchIfTruthy*/, &Self::m_offset>);

                if (!pmcr.m_success)
                {
                    // Metamethod exists but is not callable, throw error
                    //
                    [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessageForUnableToCall(metamethod).m_value);
                }

                uint8_t* calleeBytecode = pmcr.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = pmcr.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, pmcr.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }
        }
fail:
        // TODO: make this error consistent with Lua
        //
        [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("Invalid types for less than").m_value);
    }
} __attribute__((__packed__));

class BcIsNLE
{
public:
    using Self = BcIsNLE;

    BcIsNLE(BytecodeSlot lhs, BytecodeSlot rhs)
        : m_opcode(x_opcodeId<Self>), m_lhs(lhs), m_rhs(rhs), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&Self::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag)))
        {
            if (unlikely(!rhs.IsDouble(TValue::x_int32Tag)))
            {
                goto fail;
            }
            if (!(lhs.AsDouble() <= rhs.AsDouble()))
            {
                Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
            }
            else
            {
                Dispatch(rc, stackframe, bcu + sizeof(Self));
            }
        }
        else
        {
            TValue metamethod;
            if (lhs.IsPointer(TValue::x_mivTag))
            {
                if (!rhs.IsPointer(TValue::x_mivTag))
                {
                    goto fail;
                }
                Type lhsTy = lhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type;
                Type rhsTy = rhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type;
                if (unlikely(lhsTy != rhsTy))
                {
                    goto fail;
                }
                if (lhsTy == Type::STRING)
                {
                    VM* vm = VM::GetActiveVMForCurrentThread();
                    HeapString* lhsString = TranslateToRawPointer(vm, lhs.AsPointer<HeapString>().As());
                    HeapString* rhsString = TranslateToRawPointer(vm, rhs.AsPointer<HeapString>().As());
                    int cmpRes = lhsString->Compare(rhsString);
                    if (!(cmpRes <= 0))
                    {
                        Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
                    }
                    else
                    {
                        Dispatch(rc, stackframe, bcu + sizeof(Self));
                    }
                }
                if (lhsTy == Type::TABLE)
                {
                    HeapPtr<TableObject> lhsMetatable;
                    {
                        HeapPtr<TableObject> tableObj = lhs.AsPointer<TableObject>().As();
                        TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                        if (result.m_result.m_value == 0)
                        {
                            goto fail;
                        }
                        lhsMetatable = result.m_result.As<TableObject>();
                    }

                    HeapPtr<TableObject> rhsMetatable;
                    {
                        HeapPtr<TableObject> tableObj = rhs.AsPointer<TableObject>().As();
                        TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                        if (result.m_result.m_value == 0)
                        {
                            goto fail;
                        }
                        rhsMetatable = result.m_result.As<TableObject>();
                    }

                    metamethod = GetMetamethodFromMetatableForComparisonOperation<LuaMetamethodKind::Le>(lhsMetatable, rhsMetatable);
                    if (metamethod.IsNil())
                    {
                        goto fail;
                    }
                    goto do_metamethod_call;
                }

                assert(lhsTy != Type::USERDATA && "unimplemented");

                metamethod = GetMetamethodForValue(lhs, LuaMetamethodKind::Le);
                if (metamethod.IsNil())
                {
                    goto fail;
                }
                goto do_metamethod_call;
            }

            assert(!lhs.IsInt32(TValue::x_int32Tag) && "unimplemented");

            {
                assert(lhs.IsMIV(TValue::x_mivTag));
                if (!rhs.IsMIV(TValue::x_mivTag))
                {
                    goto fail;
                }
                // Must be both 'nil', or both 'boolean', in order to consider metatable
                //
                if (lhs.IsNil() != rhs.IsNil())
                {
                    goto fail;
                }
                metamethod = GetMetamethodForValue(lhs, LuaMetamethodKind::Le);
                if (metamethod.IsNil())
                {
                    goto fail;
                }
                goto do_metamethod_call;
            }

do_metamethod_call:
            {
                PrepareMetamethodCallResult pmcr = SetupFrameForMetamethodCall(rc, stackframe, bcu, std::array { lhs, rhs }, metamethod, OnReturnFromComparativeMetamethodCall<false /*branchIfTruthy*/, &Self::m_offset>);

                if (!pmcr.m_success)
                {
                    // Metamethod exists but is not callable, throw error
                    //
                    [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessageForUnableToCall(metamethod).m_value);
                }

                uint8_t* calleeBytecode = pmcr.m_calleeEc->m_bytecode;
                InterpreterFn calleeFn = pmcr.m_calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, pmcr.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
            }
        }
fail:
        // TODO: make this error consistent with Lua
        //
        [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("Invalid types for less than").m_value);
    }
} __attribute__((__packed__));

class BcIsEQ
{
public:
    using Self = BcIsEQ;

    BcIsEQ(BytecodeSlot lhs, BytecodeSlot rhs)
        : m_opcode(x_opcodeId<Self>), m_lhs(lhs), m_rhs(rhs), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&Self::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
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
                Dispatch(rc, stackframe, bcu + sizeof(Self));
            }
        }
        else
        {
            if (lhs.m_value == rhs.m_value)
            {
                Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
            }

            assert(!lhs.IsInt32(TValue::x_int32Tag) && "unimplemented");
            assert(!rhs.IsInt32(TValue::x_int32Tag) && "unimplemented");

            if (lhs.IsPointer(TValue::x_mivTag) && rhs.IsPointer(TValue::x_mivTag))
            {
                // Consider metamethod call
                //
                Type lhsTy = lhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type;
                Type rhsTy = rhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type;

                if (likely(lhsTy == Type::TABLE && rhsTy == Type::TABLE))
                {
                    HeapPtr<TableObject> lhsMetatable;
                    {
                        HeapPtr<TableObject> tableObj = lhs.AsPointer<TableObject>().As();
                        TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                        if (result.m_result.m_value == 0)
                        {
                            goto not_equal;
                        }
                        lhsMetatable = result.m_result.As<TableObject>();
                    }

                    HeapPtr<TableObject> rhsMetatable;
                    {
                        HeapPtr<TableObject> tableObj = rhs.AsPointer<TableObject>().As();
                        TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                        if (result.m_result.m_value == 0)
                        {
                            goto not_equal;
                        }
                        rhsMetatable = result.m_result.As<TableObject>();
                    }

                    TValue metamethod = GetMetamethodFromMetatableForComparisonOperation<LuaMetamethodKind::Eq>(lhsMetatable, rhsMetatable);
                    if (metamethod.IsNil())
                    {
                        goto not_equal;
                    }

                    PrepareMetamethodCallResult pmcr = SetupFrameForMetamethodCall(rc, stackframe, bcu, std::array { lhs, rhs }, metamethod, OnReturnFromComparativeMetamethodCall<true /*branchIfTruthy*/, &Self::m_offset>);

                    if (!pmcr.m_success)
                    {
                        // Metamethod exists but is not callable, throw error
                        //
                        [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessageForUnableToCall(metamethod).m_value);
                    }

                    uint8_t* calleeBytecode = pmcr.m_calleeEc->m_bytecode;
                    InterpreterFn calleeFn = pmcr.m_calleeEc->m_bestEntryPoint;
                    [[clang::musttail]] return calleeFn(rc, pmcr.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
                }
            }
not_equal:
            Dispatch(rc, stackframe, bcu + sizeof(Self));
        }
    }
} __attribute__((__packed__));

class BcIsNEQ
{
public:
    using Self = BcIsNEQ;

    BcIsNEQ(BytecodeSlot lhs, BytecodeSlot rhs)
        : m_opcode(x_opcodeId<Self>), m_lhs(lhs), m_rhs(rhs), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&Self::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        TValue lhs = bc->m_lhs.Get(rc, stackframe);
        TValue rhs = bc->m_rhs.Get(rc, stackframe);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            if (!UnsafeFloatEqual(lhs.AsDouble(), rhs.AsDouble()))
            {
                Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
            }
            else
            {
                Dispatch(rc, stackframe, bcu + sizeof(Self));
            }
        }
        else
        {
            if (lhs.m_value == rhs.m_value)
            {
                Dispatch(rc, stackframe, bcu + sizeof(Self));
            }

            assert(!lhs.IsInt32(TValue::x_int32Tag) && "unimplemented");
            assert(!rhs.IsInt32(TValue::x_int32Tag) && "unimplemented");

            if (lhs.IsPointer(TValue::x_mivTag) && rhs.IsPointer(TValue::x_mivTag))
            {
                // Consider metamethod call
                //
                Type lhsTy = lhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type;
                Type rhsTy = rhs.AsPointer<UserHeapGcObjectHeader>().As()->m_type;

                if (likely(lhsTy == Type::TABLE && rhsTy == Type::TABLE))
                {
                    HeapPtr<TableObject> lhsMetatable;
                    {
                        HeapPtr<TableObject> tableObj = lhs.AsPointer<TableObject>().As();
                        TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                        if (result.m_result.m_value == 0)
                        {
                            goto not_equal;
                        }
                        lhsMetatable = result.m_result.As<TableObject>();
                    }

                    HeapPtr<TableObject> rhsMetatable;
                    {
                        HeapPtr<TableObject> tableObj = rhs.AsPointer<TableObject>().As();
                        TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                        if (result.m_result.m_value == 0)
                        {
                            goto not_equal;
                        }
                        rhsMetatable = result.m_result.As<TableObject>();
                    }

                    TValue metamethod = GetMetamethodFromMetatableForComparisonOperation<LuaMetamethodKind::Eq>(lhsMetatable, rhsMetatable);
                    if (metamethod.IsNil())
                    {
                        goto not_equal;
                    }

                    PrepareMetamethodCallResult pmcr = SetupFrameForMetamethodCall(rc, stackframe, bcu, std::array { lhs, rhs }, metamethod, OnReturnFromComparativeMetamethodCall<false /*branchIfTruthy*/, &Self::m_offset>);

                    if (!pmcr.m_success)
                    {
                        // Metamethod exists but is not callable, throw error
                        //
                        [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessageForUnableToCall(metamethod).m_value);
                    }

                    uint8_t* calleeBytecode = pmcr.m_calleeEc->m_bytecode;
                    InterpreterFn calleeFn = pmcr.m_calleeEc->m_bestEntryPoint;
                    [[clang::musttail]] return calleeFn(rc, pmcr.m_baseForNextFrame, calleeBytecode, 0 /*unused*/);
                }
            }
not_equal:
            Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
        }
    }
} __attribute__((__packed__));

class BcCopyAndBranchIfTruthy
{
public:
    using Self = BcCopyAndBranchIfTruthy;

    BcCopyAndBranchIfTruthy(BytecodeSlot dst, BytecodeSlot src)
        : m_opcode(x_opcodeId<Self>), m_dst(dst), m_src(src), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_dst;
    BytecodeSlot m_src;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&Self::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);

        TValue src = *StackFrameHeader::GetLocalAddr(stackframe, bc->m_src);
        *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = src;

        if (src.IsTruthy())
        {
            Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
        }
        else
        {
            Dispatch(rc, stackframe, bcu + sizeof(Self));
        }
    }
} __attribute__((__packed__));

class BcCopyAndBranchIfFalsy
{
public:
    using Self = BcCopyAndBranchIfFalsy;

    BcCopyAndBranchIfFalsy(BytecodeSlot dst, BytecodeSlot src)
        : m_opcode(x_opcodeId<Self>), m_dst(dst), m_src(src), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_dst;
    BytecodeSlot m_src;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&Self::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);

        TValue src = *StackFrameHeader::GetLocalAddr(stackframe, bc->m_src);
        *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = src;

        if (!src.IsTruthy())
        {
            Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
        }
        else
        {
            Dispatch(rc, stackframe, bcu + sizeof(Self));
        }
    }
} __attribute__((__packed__));

class BcBranchIfTruthy
{
public:
    using Self = BcBranchIfTruthy;

    BcBranchIfTruthy(BytecodeSlot src)
        : m_opcode(x_opcodeId<Self>), m_src(src), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_src;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&Self::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);

        TValue src = *StackFrameHeader::GetLocalAddr(stackframe, bc->m_src);
        if (src.IsTruthy())
        {
            Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
        }
        else
        {
            Dispatch(rc, stackframe, bcu + sizeof(Self));
        }
    }
} __attribute__((__packed__));

class BcBranchIfFalsy
{
public:
    using Self = BcBranchIfFalsy;

    BcBranchIfFalsy(BytecodeSlot src)
        : m_opcode(x_opcodeId<Self>), m_src(src), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_src;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&Self::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);

        TValue src = *StackFrameHeader::GetLocalAddr(stackframe, bc->m_src);
        if (!src.IsTruthy())
        {
            Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
        }
        else
        {
            Dispatch(rc, stackframe, bcu + sizeof(Self));
        }
    }
} __attribute__((__packed__));

class BcForLoopInit
{
public:
    using Self = BcForLoopInit;

    BcForLoopInit(BytecodeSlot base)
        : m_opcode(x_opcodeId<Self>), m_base(base), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_base;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&Self::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        double vals[3];
        TValue* addr = StackFrameHeader::GetLocalAddr(stackframe, bc->m_base);
        for (uint32_t i = 0; i < 3; i++)
        {
            TValue v = addr[i];
            if (v.IsDouble(TValue::x_int32Tag))
            {
                vals[i] = v.AsDouble();
            }
            else if (v.IsPointer(TValue::x_mivTag) && v.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::STRING)
            {
                HeapPtr<HeapString> hs = v.AsPointer<HeapString>().As();
                uint8_t* str = TranslateToRawPointer(hs->m_string);
                uint32_t len = hs->m_length;
                StrScanResult r = TryConvertStringToDoubleWithLuaSemantics(str, len);
                if (r.fmt == STRSCAN_ERROR)
                {
                    [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("'for' loop range must be a number").m_value);
                }
                assert(r.fmt == STRSCAN_NUM);
                vals[i] = r.d;
                addr[i] = TValue::CreateDouble(r.d);
            }
            else
            {
                [[clang::musttail]] return ThrowError(rc, stackframe, bcu, MakeErrorMessage("'for' loop range must be a number").m_value);
            }
        }

        bool loopConditionSatisfied = (vals[2] > 0 && vals[0] <= vals[1]) || (vals[2] <= 0 && vals[0] >= vals[1]);
        if (!loopConditionSatisfied)
        {
            Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
        }
        else
        {
            addr[3] = TValue::CreateDouble(vals[0]);
            Dispatch(rc, stackframe, bcu + sizeof(Self));
        }
    }
} __attribute__((__packed__));

class BcForLoopStep
{
public:
    using Self = BcForLoopStep;

    BcForLoopStep(BytecodeSlot base)
        : m_opcode(x_opcodeId<Self>), m_base(base), m_offset(0)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_base;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&Self::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        double vals[3];
        TValue* addr = StackFrameHeader::GetLocalAddr(stackframe, bc->m_base);

        vals[0] = addr[0].AsDouble();
        vals[1] = addr[1].AsDouble();
        vals[2] = addr[2].AsDouble();

        vals[0] += vals[2];
        bool loopConditionSatisfied = (vals[2] > 0 && vals[0] <= vals[1]) || (vals[2] <= 0 && vals[0] >= vals[1]);
        if (loopConditionSatisfied)
        {
            TValue v = TValue::CreateDouble(vals[0]);
            addr[0] = v;
            addr[3] = v;
            Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
        }
        else
        {
            Dispatch(rc, stackframe, bcu + sizeof(Self));
        }
    }
} __attribute__((__packed__));

class BcUnconditionalJump
{
public:
    using Self = BcUnconditionalJump;

    BcUnconditionalJump()
        : m_opcode(x_opcodeId<Self>), m_offset(0)
    { }

    uint8_t m_opcode;
    int32_t m_offset;

    static constexpr int32_t OffsetOfJump()
    {
        return static_cast<int32_t>(offsetof_member_v<&Self::m_offset>);
    }

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
    }
} __attribute__((__packed__));

class BcConstant
{
public:
    using Self = BcConstant;

    BcConstant(BytecodeSlot dst, TValue value)
        : m_opcode(x_opcodeId<Self>), m_dst(dst), m_value(value)
    { }

    uint8_t m_opcode;
    BytecodeSlot m_dst;
    Packed<TValue> m_value;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const Self* bc = reinterpret_cast<const Self*>(bcu);
        assert(bc->m_opcode == x_opcodeId<Self>);
        *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = TCGet(bc->m_value);
        Dispatch(rc, stackframe, bcu + sizeof(Self));
    }
} __attribute__((__packed__));

}   // namespace ToyLang
