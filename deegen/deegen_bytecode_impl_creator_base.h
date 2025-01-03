#pragma once

#include "misc_llvm_helper.h"
#include "deegen_bytecode_ir_components.h"
#include "deegen_engine_tier.h"
#include "deegen_register_pinning_scheme.h"

namespace dast {

class BytecodeVariantDefinition;

class InterpreterBytecodeImplCreator;
class BaselineJitImplCreator;
class DfgJitImplCreator;

class DeegenBytecodeImplCreatorBase
{
public:
    virtual ~DeegenBytecodeImplCreatorBase() = default;

    DeegenBytecodeImplCreatorBase(BytecodeVariantDefinition* bytecodeDef, BytecodeIrComponentKind processKind)
        : m_module(nullptr)
        , m_execFnContext(nullptr)
        , m_bytecodeDef(bytecodeDef)
        , m_processKind(processKind)
        , m_valuePreserver()
    { }

    virtual DeegenEngineTier WARN_UNUSED GetTier() const = 0;

    bool WARN_UNUSED IsReturnContinuation() const { return m_processKind == BytecodeIrComponentKind::ReturnContinuation; }
    bool WARN_UNUSED IsMainComponent() const { return m_processKind == BytecodeIrComponentKind::Main; }

    BytecodeVariantDefinition* GetBytecodeDef() const { return m_bytecodeDef; }

    ExecutorFunctionContext* GetExecFnContext()
    {
        ReleaseAssert(m_execFnContext.get() != nullptr);
        return m_execFnContext.get();
    }

    void SetExecFnContext(std::unique_ptr<ExecutorFunctionContext> val)
    {
        ReleaseAssert(m_execFnContext.get() == nullptr && val.get() != nullptr);
        m_execFnContext = std::move(val);
    }

    llvm::Module* GetModule() { return m_module.get(); }

    llvm::Value* GetCoroutineCtx() const { return m_valuePreserver.Get(x_coroutineCtx); }
    llvm::Value* GetStackBase() const { return m_valuePreserver.Get(x_stackBase); }
    llvm::Value* GetCurBytecode() const { return m_valuePreserver.Get(x_curBytecode); }

    // The two functions below are only valid for return continuations
    //
    llvm::Value* GetRetStart() const { ReleaseAssert(IsReturnContinuation()); return m_valuePreserver.Get(x_retStart); }
    llvm::Value* GetNumRet() const { ReleaseAssert(IsReturnContinuation()); return m_valuePreserver.Get(x_numRet); }

    // Only valid if the bytecode actually has an output
    //
    llvm::Value* GetOutputSlot() const { return m_valuePreserver.Get(x_outputSlot); }

    llvm::CallInst* CallDeegenCommonSnippet(const std::string& dcsName, llvm::ArrayRef<llvm::Value*> args, llvm::Instruction* insertBefore)
    {
        return CreateCallToDeegenCommonSnippet(GetModule(), dcsName, args, insertBefore);
    }

    llvm::CallInst* CallDeegenCommonSnippet(const std::string& dcsName, llvm::ArrayRef<llvm::Value*> args, llvm::BasicBlock* insertAtEnd)
    {
        return CreateCallToDeegenCommonSnippet(GetModule(), dcsName, args, insertAtEnd);
    }

    llvm::CallInst* CallDeegenRuntimeFunction(const std::string& dcsName, llvm::ArrayRef<llvm::Value*> args, llvm::Instruction* insertBefore)
    {
        return CreateCallToDeegenRuntimeFunction(GetModule(), dcsName, args, insertBefore);
    }

    bool WARN_UNUSED IsInterpreter() const { return GetTier() == DeegenEngineTier::Interpreter; }
    bool WARN_UNUSED IsBaselineJIT() const { return GetTier() == DeegenEngineTier::BaselineJIT; }
    bool WARN_UNUSED IsDfgJIT() const { return GetTier() == DeegenEngineTier::DfgJIT; }

    InterpreterBytecodeImplCreator* AsInterpreter();
    BaselineJitImplCreator* AsBaselineJIT();
    DfgJitImplCreator* AsDfgJIT();

protected:
    // For clone only
    //
    DeegenBytecodeImplCreatorBase(DeegenBytecodeImplCreatorBase* other);

    std::unique_ptr<llvm::Module> m_module;
    std::unique_ptr<ExecutorFunctionContext> m_execFnContext;
    BytecodeVariantDefinition* m_bytecodeDef;
    BytecodeIrComponentKind m_processKind;
    LLVMValuePreserver m_valuePreserver;

    static constexpr const char* x_coroutineCtx = "coroutineCtx";
    static constexpr const char* x_stackBase = "stackBase";
    static constexpr const char* x_curBytecode = "curBytecode";
    static constexpr const char* x_retStart = "retStart";
    static constexpr const char* x_numRet = "numRet";
    static constexpr const char* x_outputSlot = "outputSlot";
};

}   // namespace dast
