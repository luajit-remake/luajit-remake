#pragma once

#include "deegen_engine_tier.h"
#include "misc_llvm_helper.h"
#include "x64_register_info.h"

namespace dast {

struct RegisterPinningScheme
{
public:
    static llvm::FunctionType* WARN_UNUSED GetFunctionType(llvm::LLVMContext& ctx);
    static size_t WARN_UNUSED GetFunctionTypeNumArguments();
    static llvm::Function* WARN_UNUSED CreateFunction(llvm::Module* module, const std::string& name);

    static std::unique_ptr<llvm::Module> WARN_UNUSED CreateModule(const std::string& name, llvm::LLVMContext& ctx);

    static llvm::CallInst* CreateDispatchWithAllUndefArgs(llvm::Value* target, llvm::Instruction* insertBefore);

    // Return the list of avaiable GPR/FPR that can be used to pass additional arguments to the interpreter bytecode function
    // The result is represented as a vector of argument ordinals.
    //
    static std::vector<uint64_t> WARN_UNUSED GetAvaiableGPRListForBytecodeSlowPath();
    static std::vector<uint64_t> WARN_UNUSED GetAvaiableFPRListForBytecodeSlowPath();

    // Return the argument ordinal in the function interface for the given register
    // Fire assertion if the register is not present in the function interface
    //
    static uint32_t GetArgumentOrdinalForRegister(X64Reg reg);
    static X64Reg GetRegisterForArgumentOrdinal(size_t ord);

    static const char* WARN_UNUSED GetRegisterName(size_t argOrd);

    static llvm::Value* WARN_UNUSED GetArgumentAsInt64Value(llvm::Function* interfaceFn, uint64_t argOrd, llvm::Instruction* insertBefore);
    static llvm::Value* WARN_UNUSED GetArgumentAsInt64Value(llvm::Function* interfaceFn, uint64_t argOrd, llvm::BasicBlock* insertAtEnd);

    static llvm::Value* WARN_UNUSED EmitCastI64ToArgumentType(llvm::Value* value, uint64_t argOrd, llvm::Instruction* insertBefore);
    static llvm::Value* WARN_UNUSED EmitCastI64ToArgumentType(llvm::Value* value, uint64_t argOrd, llvm::BasicBlock* insertAtEnd);

    // The Value* returned has the type of the argument in GetFunctionType()
    // To always return as Int64, use GetArgumentAsInt64Value() instead
    //
    static llvm::Value* WARN_UNUSED GetRegisterValueAtEntry(llvm::Function* interfaceFn, X64Reg reg);

    // Set argument 'argOrd' in a dispatch call to 'value'
    // The original argument must be UndefVal (which this function asserts)
    //
    static void SetExtraDispatchArgument(llvm::CallInst* callInst, uint32_t argOrd, llvm::Value* newVal);
    static void SetExtraDispatchArgument(llvm::CallInst* callInst, X64Reg reg, llvm::Value* newVal);

    // 'newVal' should be an i64, and casts are emitted appropriately to cast the value to the argument type expected by the interface
    // The cast instructions are inserted before 'callInst'
    //
    static void SetExtraDispatchArgumentWithCastFromI64(llvm::CallInst* callInst, X64Reg reg, llvm::Value* newVal);
};

struct RegisterPinnedValueBase;

namespace detail {

// The RegisterPinnedValueBase class virtual-inherit this class, and every RegisterPinnedValue class inherits RegisterPinnedValueBase
// This allows this class to collect the list of register-pinned values
//
struct RegisterPinnedValueVirtBase
{
    virtual ~RegisterPinnedValueVirtBase() = default;

    MAKE_NONCOPYABLE(RegisterPinnedValueVirtBase);
    MAKE_NONMOVABLE(RegisterPinnedValueVirtBase);

    const std::vector<RegisterPinnedValueBase*>& GetValues() const
    {
        return m_values;
    }

protected:
    RegisterPinnedValueVirtBase() = default;

    size_t AddToValueList(RegisterPinnedValueBase* value)
    {
        size_t ord = m_values.size();
        m_values.push_back(value);
        return ord;
    }

private:
    std::vector<RegisterPinnedValueBase*> m_values;
};

}   // namespace detail

// Base class for a register-pinned value
//
struct RegisterPinnedValueBase : virtual detail::RegisterPinnedValueVirtBase
{
    // In the constructor we add our 'this' pointer to the VirtBase's list, so it cannot be trivially copied or moved
    //
    MAKE_NONCOPYABLE(RegisterPinnedValueBase);
    MAKE_NONMOVABLE(RegisterPinnedValueBase);

    virtual X64Reg GetRegisterName() = 0;

    uint32_t GetFnCtxArgOrd()
    {
        return RegisterPinningScheme::GetArgumentOrdinalForRegister(GetRegisterName());
    }

    llvm::Type* GetValueType(llvm::LLVMContext& ctx)
    {
        uint32_t argOrd = GetFnCtxArgOrd();
        llvm::FunctionType* fty = RegisterPinningScheme::GetFunctionType(ctx);
        ReleaseAssert(argOrd < fty->getFunctionNumParams());
        return fty->getParamType(argOrd);
    }

    void SetValue(llvm::Value* value)
    {
        ReleaseAssert(value != nullptr);
        ReleaseAssert(value->getType() == GetValueType(value->getContext()));
        m_value = value;
    }

    llvm::Value* GetValue() { return m_value; }

    void ClearValue()
    {
        ReleaseAssert(m_value != nullptr);
        m_value = nullptr;
    }

protected:
    RegisterPinnedValueBase()
        : m_value(nullptr)
    {
        AddToValueList(this);
    }

private:
    llvm::Value* m_value;
};

// A value pinned to a specific machine register
//
template<X64RegNTTP reg>
struct RegisterPinnedValue : RegisterPinnedValueBase
{
    RegisterPinnedValue() : RegisterPinnedValueBase() { }

    virtual X64Reg GetRegisterName() override final
    {
        return reg;
    }

    static constexpr X64Reg Reg() { return reg; }
};

namespace detail {

template<X64RegNTTP reg>
std::true_type is_inherits_register_pinned_val_tester(const RegisterPinnedValue<reg>*);

std::false_type is_inherits_register_pinned_val_tester(...);

template <typename T>
using is_inherits_register_pinned_val = decltype(is_inherits_register_pinned_val_tester(std::declval<T*>()));

template<typename T> struct is_register_pinned_value_class : std::false_type { };
template<X64RegNTTP reg> struct is_register_pinned_value_class<RegisterPinnedValue<reg>> : std::true_type { };

template<typename T>
struct is_valid_register_pinned_value : std::integral_constant<bool, !is_register_pinned_value_class<T>::value && is_inherits_register_pinned_val<T>::value> { };

}   // namespace detail

// The list of all different register-pinned values
//
// Architecture idiosyncrasies considerations in choosing registers:
//   R12 cannot be used in ModRM "r/m+disp" addressing
//   R13 and RBP cannot be used in ModRM "r/m" addressing and SIB "base+index*scale" addressing
//   RAX/RBX/RCX/RDX cannot be used by our DFG reg alloc scheme since AH/BH/CH/DH doesn't have counterparts in other registers
//   The first-eight registers (i.e., not R8-15) can sometimes result in 1-byte shorter instructions compared with R8-15
//   C callee-saved registers (RBX, RBP, R12-15) are preserved across C calls, so should be used to store important VM states
//
// So now we use the following C callee-saved registers to store the important VM states:
//   R12 as x_int32Tag tag register (isn't used in addressing or LEA at all)
//   R13 as x_mivTag tag register (isn't used in addressing, but needs r/m+disp LEA)
//   RBP as codeBlock (use first-eight reg since it's frequently used. The only case that it needs base+index*scale addressing is to access the
//       constant table, but only the interpreter needs to use index*scale to do that, the JITs don't, as they can burn in the offset directly)
//   RBX as StackBase (use first-eight reg since it's frequently used. Also takes a bad reg that doesn't work for DFG reg alloc)
//   R14 as curBytecode, R15 as CoroutineCtx (only need them to be C callee-saved)
//
// Note that the scheme is to *inherit* from RegisterPinnedValue, instead of making a type alias,
// so we can catch cases where caller asks for a register that exists but is storing something else.
//
// The coroutine context (available everywhere)
//
struct RPV_CoroContext : RegisterPinnedValue<X64Reg::R15> { };

// The stack base (available everywhere)
//
struct RPV_StackBase : RegisterPinnedValue<X64Reg::RBX> { };

// In interpreter, R14 used for curBytecode
// In baseline/DFG AOT slow path (but not JIT code), R14 used for slowPathData
// At function entry, R14 used to store #ret (forcefully cast to ptr)
//
struct RPV_CurBytecode : RegisterPinnedValue<X64Reg::R14> { };
struct RPV_JitSlowPathData : RegisterPinnedValue<X64Reg::R14> { };
struct RPV_NumArgsAsPtr : RegisterPinnedValue<X64Reg::R14> { };

// The DFG JIT code uses R14 for reg alloc, so the SaveRegStub (which DFG JIT code uses to branch to AOT code)
// takes SlowPathDataPtr in a different register to avoid pessimizing JIT code.
//
struct RPV_JitSlowPathDataForSaveRegStub : RegisterPinnedValue<X64Reg::RDX> { };

// RBP is used to store the CodeBlock for the current VM tier. Note that the type of the CodeBlock is different per-tier!
// (e.g., for interpreter this is CodeBlock, for baseline JIT this is BaselineCodeBlock)
//
// At function entry, this is the InterpreterCodeBlock but a HeapPtr (forcefully cast to ptr)
// (Note that the codeblock at function entry is always the interpreter CodeBlock even in JIT tiers!)
//
struct RPV_CodeBlock : RegisterPinnedValue<X64Reg::RBP> { };
struct RPV_InterpCodeBlockHeapPtrAsPtr : RegisterPinnedValue<X64Reg::RBP> { };

// Two tag registers
//
struct RPV_TagRegister1 : RegisterPinnedValue<X64Reg::R12> { };
struct RPV_TagRegister2 : RegisterPinnedValue<X64Reg::R13> { };

// For return continuations, RSI stores the start of the return value list
//
struct RPV_RetValsPtr : RegisterPinnedValue<X64Reg::RSI> { };

// For return continuations, RDI stores the # of return values
// At function entry, RDI stores whether this function is called by a MustTail call
//
struct RPV_NumRetVals : RegisterPinnedValue<X64Reg::RDI> { };
struct RPV_IsMustTailCall : RegisterPinnedValue<X64Reg::RDI> { };

template<typename CRTP>
struct ExecutorCtxInfoBase : virtual detail::RegisterPinnedValueVirtBase
{
    MAKE_NONCOPYABLE(ExecutorCtxInfoBase);
    MAKE_NONMOVABLE(ExecutorCtxInfoBase);

    std::map<uint32_t /*argOrd*/, llvm::Value*> GetProvidedArgumentMap() const
    {
        std::map<uint32_t, llvm::Value*> r;
        for (RegisterPinnedValueBase* e : GetValues())
        {
            uint32_t argOrd = e->GetFnCtxArgOrd();
            llvm::Value* val = e->GetValue();
            ReleaseAssert(!r.count(argOrd));
            ReleaseAssert(val != nullptr);
            r[argOrd] = val;
        }
        return r;
    }

    // Note that the interface does not contain the reg alloc registers, so this will return false for such registers
    //
    bool WARN_UNUSED IsRegisterUsedInInterface(X64Reg reg) const
    {
        for (RegisterPinnedValueBase* e : GetValues())
        {
            if (e->GetRegisterName() == reg)
            {
                return true;
            }
        }
        return false;
    }

    template<typename RPVName>
    CRTP& Set(llvm::Value* value)
    {
        static_assert(HasValue<RPVName>());
        CRTP* crtp = dynamic_cast<CRTP*>(this);
        ReleaseAssert(crtp != nullptr);
        crtp->RPVName::SetValue(value);
        return *crtp;
    }

    // Dispatch to the destination address. Asserts that all context values needed for the target interface has been populated.
    // Note that AOT slow path and DFG reg-alloc-enabled JIT code may take additional arguments.
    // These arguments are not specified here: caller should populate them as needed by themselves.
    //
    llvm::CallInst* Dispatch(llvm::Value* target, llvm::Instruction* insertBefore) const
    {
        using namespace llvm;
        ReleaseAssert(dynamic_cast<const CRTP*>(this) != nullptr);
        if (isa<Function>(target))
        {
            ReleaseAssert(cast<Function>(target)->getFunctionType() == RegisterPinningScheme::GetFunctionType(target->getContext()));
        }

        std::map<uint32_t, llvm::Value*> argMap = GetProvidedArgumentMap();
        CallInst* callInst = RegisterPinningScheme::CreateDispatchWithAllUndefArgs(target, insertBefore);
        for (auto& it : argMap)
        {
            uint32_t argOrd = it.first;
            Value* val = it.second;
            ReleaseAssert(argOrd < callInst->arg_size());
            ReleaseAssert(argOrd < callInst->getFunctionType()->getNumParams());
            ReleaseAssert(callInst->getFunctionType()->getParamType(argOrd) == val->getType());
            ReleaseAssert(val != nullptr);
            callInst->setArgOperand(argOrd, val);
        }
        return callInst;
    }

    llvm::CallInst* Dispatch(llvm::Value* target, llvm::BasicBlock* insertAtEnd) const
    {
        using namespace llvm;
        ReleaseAssert(insertAtEnd != nullptr);
        LLVMContext& ctx = insertAtEnd->getContext();
        Instruction* dummyInst = new UnreachableInst(ctx, insertAtEnd);
        CallInst* res = Dispatch(target, dummyInst /*insertBefore*/);
        dummyInst->eraseFromParent();
        return res;
    }

    template<typename RPVName>
    static constexpr bool HasValue()
    {
        static_assert(detail::is_valid_register_pinned_value<RPVName>::value);
        static_assert(std::is_base_of_v<ExecutorCtxInfoBase<CRTP>, CRTP>);
        return std::is_base_of_v<RPVName, CRTP>;
    }

protected:
    ExecutorCtxInfoBase() = default;
};

struct InterpreterInterface final
    : ExecutorCtxInfoBase<InterpreterInterface>
    , RPV_CoroContext
    , RPV_StackBase
    , RPV_CurBytecode
    , RPV_CodeBlock
    , RPV_TagRegister1
    , RPV_TagRegister2
{ };

struct JitGeneratedCodeInterface final
    : ExecutorCtxInfoBase<JitGeneratedCodeInterface>
    , RPV_CoroContext
    , RPV_StackBase
    , RPV_CodeBlock
    , RPV_TagRegister1
    , RPV_TagRegister2
{ };

struct JitAOTSlowPathInterface final
    : ExecutorCtxInfoBase<JitAOTSlowPathInterface>
    , RPV_CoroContext
    , RPV_StackBase
    , RPV_CodeBlock
    , RPV_JitSlowPathData
    , RPV_TagRegister1
    , RPV_TagRegister2
{ };

// This is the AOT stub that wraps a real AOT SlowPath.
// DFG JIT code with reg alloc will dispatch to this stub, and this stub will save the registers and dispatch to the real AOT SlowPath
// This "intermediate" step is needed because AOT slow paths may call each other,
// but the process of saving the registers should only happen once when the JIT code branches to AOT code
//
struct JitAOTSlowPathSaveRegStubInterface final
    : ExecutorCtxInfoBase<JitAOTSlowPathSaveRegStubInterface>
    , RPV_CoroContext
    , RPV_StackBase
    , RPV_CodeBlock
    , RPV_JitSlowPathDataForSaveRegStub
    , RPV_TagRegister1
    , RPV_TagRegister2
{ };

struct ReturnContinuationInterface final
    : ExecutorCtxInfoBase<ReturnContinuationInterface>
    , RPV_CoroContext
    , RPV_StackBase
    , RPV_RetValsPtr
    , RPV_NumRetVals
    , RPV_TagRegister1
    , RPV_TagRegister2
{ };

struct FunctionEntryInterface final
    : ExecutorCtxInfoBase<FunctionEntryInterface>
    , RPV_CoroContext
    , RPV_StackBase
    , RPV_NumArgsAsPtr
    , RPV_InterpCodeBlockHeapPtrAsPtr
    , RPV_IsMustTailCall
    , RPV_TagRegister1
    , RPV_TagRegister2
{ };

struct ExecutorFunctionContext
{
    MAKE_NONCOPYABLE(ExecutorFunctionContext);
    MAKE_NONMOVABLE(ExecutorFunctionContext);

    bool IsFunctionNull() { return m_func == nullptr; }
    llvm::Function* GetFunction() { ReleaseAssert(!IsFunctionNull()); return m_func; }

    void SetFunction(llvm::Function* func)
    {
        ReleaseAssert(m_func == nullptr && func != nullptr);
        ReleaseAssert(func->getFunctionType() == RegisterPinningScheme::GetFunctionType(func->getContext()));
        m_func = func;
    }

    void ResetFunction(llvm::Function* func)
    {
        ReleaseAssert(m_func != nullptr && func != nullptr);
        ReleaseAssert(func->getFunctionType() == RegisterPinningScheme::GetFunctionType(func->getContext()));
        m_func = func;
    }

    llvm::Function* CreateFunction(llvm::Module* module, const std::string& funcName)
    {
        ReleaseAssert(m_func == nullptr);
        m_func = RegisterPinningScheme::CreateFunction(module, funcName);
        ReleaseAssert(m_func->getFunctionType() == RegisterPinningScheme::GetFunctionType(m_func->getContext()));
        return m_func;
    }

    // coroContext, tagRegister1 and tagRegister2 always exist in every function,
    // and they are almost always passed to the continuation unchanged.
    // So we abstract them out to deduplicate some common logic.
    //
    template<typename TargetInterfaceTy>
    TargetInterfaceTy& WARN_UNUSED PrepareDispatch()
    {
        static_assert(std::is_base_of_v<ExecutorCtxInfoBase<TargetInterfaceTy>, TargetInterfaceTy>);
        static_assert(std::is_base_of_v<RPV_CoroContext, TargetInterfaceTy>);
        static_assert(std::is_base_of_v<RPV_TagRegister1, TargetInterfaceTy>);
        static_assert(std::is_base_of_v<RPV_TagRegister2, TargetInterfaceTy>);

        // Ugly: ownership of 'info' is in 'm_memoryHolder', see comments on m_memoryHolder.
        //
        TargetInterfaceTy* info = new TargetInterfaceTy;
        m_memoryHolder.push_back(std::unique_ptr<detail::RegisterPinnedValueVirtBase>(info));
        info->RPV_CoroContext::SetValue(GetValueAtEntry<RPV_CoroContext>());
        info->RPV_TagRegister1::SetValue(GetValueAtEntry<RPV_TagRegister1>());
        info->RPV_TagRegister2::SetValue(GetValueAtEntry<RPV_TagRegister2>());
        return *info;
    }

    // Returns the value passed in to the function.
    // Fires assertion if the value is not available for this type of function context.
    //
    template<typename RPVName>
    llvm::Value* WARN_UNUSED GetValueAtEntry()
    {
        ReleaseAssert(HasValue<RPVName>());
        return UnsafeGetCtxValImpl<RPVName>();
    }

    static std::unique_ptr<ExecutorFunctionContext> WARN_UNUSED Create(DeegenEngineTier engineTier, bool isJitCode, bool isReturnContinuation)
    {
        return std::unique_ptr<ExecutorFunctionContext>(
            new ExecutorFunctionContext(
                engineTier,
                isJitCode,
                isReturnContinuation,
                false /*isFunctionEntry*/,
                false /*isDfgSaveRegStubForAOT*/));
    }

    static std::unique_ptr<ExecutorFunctionContext> WARN_UNUSED CreateForFunctionEntry(DeegenEngineTier engineTier)
    {
        return std::unique_ptr<ExecutorFunctionContext>(
            new ExecutorFunctionContext(
                engineTier,
                engineTier != DeegenEngineTier::Interpreter /*isJitCode*/,
                false /*isReturnContinuation*/,
                true /*isFunctionEntry*/,
                false /*isDfgSaveRegStubForAOT*/));
    }

    static std::unique_ptr<ExecutorFunctionContext> WARN_UNUSED CreateForDfgAOTSaveRegStub()
    {
        return std::unique_ptr<ExecutorFunctionContext>(
            new ExecutorFunctionContext(
                DeegenEngineTier::DfgJIT,
                false /*isJitCode*/,
                false /*isReturnContinuation*/,
                false /*isFunctionEntry*/,
                true /*isDfgSaveRegStubForAOT*/));
    }

    std::unique_ptr<ExecutorFunctionContext> WARN_UNUSED Clone(llvm::Module* newModule)
    {
        using namespace llvm;

        std::unique_ptr<ExecutorFunctionContext> r(
            new ExecutorFunctionContext(m_engineTier, m_isJitCode, m_isReturnContinuation, m_isFunctionEntry, m_isDfgSaveRegStubForAOT));

        if (!IsFunctionNull())
        {
            Function* fn = newModule->getFunction(GetFunction()->getName());
            ReleaseAssert(fn != nullptr);
            r->SetFunction(fn);
        }
        return r;
    }

private:
    ExecutorFunctionContext(DeegenEngineTier engineTier, bool isJitCode, bool isReturnContinuation, bool isFunctionEntry, bool isDfgSaveRegStubForAOT)
        : m_func(nullptr)
        , m_engineTier(engineTier)
        , m_isJitCode(isJitCode)
        , m_isReturnContinuation(isReturnContinuation)
        , m_isFunctionEntry(isFunctionEntry)
        , m_isDfgSaveRegStubForAOT(isDfgSaveRegStubForAOT)
    {
        ReleaseAssertImp(m_engineTier == DeegenEngineTier::Interpreter, !m_isJitCode);
        ReleaseAssertImp(m_isDfgSaveRegStubForAOT, m_engineTier == DeegenEngineTier::DfgJIT && !m_isJitCode);

        size_t numTrue = (m_isReturnContinuation ? 1 : 0) + (m_isFunctionEntry ? 1 : 0) + (m_isDfgSaveRegStubForAOT ? 1 : 0);
        ReleaseAssert(numTrue <= 1);
    }

    template<typename RPVName, typename InterfaceTy>
    constexpr bool HasValueImpl()
    {
        static_assert(std::is_base_of_v<ExecutorCtxInfoBase<InterfaceTy>, InterfaceTy>);
        return InterfaceTy::template HasValue<RPVName>();
    }

    template<typename RPVName>
    bool HasValue()
    {
        if (m_isFunctionEntry)
        {
            // All function entry have the same interface, regardless of engine tier
            //
            return HasValueImpl<RPVName, FunctionEntryInterface>();
        }

        if (m_isReturnContinuation)
        {
            // All return continuations have the same interface, regardless of the engine tier
            //
            return HasValueImpl<RPVName, ReturnContinuationInterface>();
        }

        if (m_isDfgSaveRegStubForAOT)
        {
            // The DFG SaveRegStub for AOT slow path has interface JitAOTSlowPathSaveRegStubInterface
            //
            return HasValueImpl<RPVName, JitAOTSlowPathSaveRegStubInterface>();
        }

        switch (m_engineTier)
        {
        case DeegenEngineTier::Interpreter:
        {
            ReleaseAssert(!m_isJitCode);
            return HasValueImpl<RPVName, InterpreterInterface>();
        }
        case DeegenEngineTier::BaselineJIT:
        case DeegenEngineTier::DfgJIT:
        {
            if (m_isJitCode)
            {
                return HasValueImpl<RPVName, JitGeneratedCodeInterface>();
            }
            else
            {
                return HasValueImpl<RPVName, JitAOTSlowPathInterface>();
            }
        }
        }   /*switch*/
    }

    template<typename RPVName>
    llvm::Value* UnsafeGetCtxValImpl()
    {
        static_assert(detail::is_valid_register_pinned_value<RPVName>::value);
        X64Reg reg = RPVName::Reg();
        uint32_t argOrd = RegisterPinningScheme::GetArgumentOrdinalForRegister(reg);
        ReleaseAssert(argOrd < GetFunction()->arg_size());
        return GetFunction()->getArg(argOrd);
    }

    llvm::Function* m_func;
    DeegenEngineTier m_engineTier;
    bool m_isJitCode;
    bool m_isReturnContinuation;
    bool m_isFunctionEntry;
    bool m_isDfgSaveRegStubForAOT;
    // Currently ExecutorCtxInfoBase is not movable/copyable, which causes issues with PrepareDispatch().
    // We can fix this by correctly implement the move/copy constructor for ExecutorCtxInfoBase, but this is a bit tricky,
    // and also limits how the API can be used due to limitations of temporary objects.
    // Since performance doesn't matter here, we simply allocate the object and se this memory holder to keep these
    // temporary objects alive.
    //
    std::vector<std::unique_ptr<detail::RegisterPinnedValueVirtBase>> m_memoryHolder;
};

}   // namespace dast
