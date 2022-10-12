#pragma once

#include "common_utils.h"
#include "memory_ptr.h"
#include "vm.h"
#include "structure.h"
#include "table_object.h"

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
        r->m_globalObject = globalObject;
        r->m_numVariadicRets = 0;
        r->m_variadicRetSlotBegin = 0;
        r->m_upvalueList.m_value = 0;
        r->m_stackBegin = new TValue[x_defaultStackSlots];
        return r;
    }

    void CloseUpvalues(TValue* base);

    uint32_t m_hiddenClass;  // Always x_hiddenClassForCoroutineRuntimeContext
    HeapEntityType m_type;
    GcCellState m_cellState;

    uint16_t m_reserved1;

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
    // This is for the new interpreter. We should remove the above functions after we port everything to the new interpreter
    //
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, UnlinkedCodeBlock>>>
    static CodeBlock* WARN_UNUSED GetCodeBlock(T self, UserHeapPointer<TableObject> globalObject)
    {
        if (likely(globalObject == self->m_defaultGlobalObject))
        {
            return self->m_defaultCodeBlock;
        }
        UnlinkedCodeBlock* raw = TranslateToRawPointer(self);
        return raw->GetCodeBlockSlowPath(globalObject);
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

    UserHeapPointer<TableObject> m_defaultGlobalObject;
    CodeBlock* m_defaultCodeBlock;
    using RareGlobalObjectToCodeBlockMap = std::unordered_map<int64_t, CodeBlock*>;
    RareGlobalObjectToCodeBlockMap* m_rareGOtoCBMap;

    uint8_t* m_bytecode;
    UpvalueMetadata* m_upvalueInfo;
    uint64_t* m_cstTable;
    UnlinkedCodeBlock* m_parent;

    uint32_t m_cstTableLength;
    uint32_t m_bytecodeLength;
    uint32_t m_numUpvalues;
    uint32_t m_bytecodeMetadataLength;
    uint32_t m_stackFrameNumSlots;
    uint32_t m_numFixedArguments;
    bool m_hasVariadicArguments;
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

    static UserHeapPointer<FunctionObject> WARN_UNUSED NO_INLINE CreateAndFillUpvalues(CodeBlock* cb, CoroutineRuntimeContext* rc, TValue* stackFrameBase, HeapPtr<FunctionObject> parent);

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

inline double WARN_UNUSED ModulusWithLuaSemantics(double a, double b)
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
        if (!rhsMetamethod.IsDouble())
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
