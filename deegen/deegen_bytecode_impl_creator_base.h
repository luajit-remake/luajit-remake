#pragma once

#include "misc_llvm_helper.h"
#include "deegen_bytecode_ir_components.h"

namespace dast {

class BytecodeVariantDefinition;

class DeegenBytecodeImplCreatorBase
{
public:
    virtual ~DeegenBytecodeImplCreatorBase() = default;

    DeegenBytecodeImplCreatorBase(BytecodeVariantDefinition* bytecodeDef, BytecodeIrComponentKind processKind)
        : m_bytecodeDef(bytecodeDef)
        , m_processKind(processKind)
        , m_valuePreserver()
    { }

    bool IsReturnContinuation() const { return m_processKind == BytecodeIrComponentKind::ReturnContinuation; }

    BytecodeVariantDefinition* GetBytecodeDef() const { return m_bytecodeDef; }

    virtual llvm::Module* GetModule() const = 0;

    llvm::Value* GetCoroutineCtx() const { return m_valuePreserver.Get(x_coroutineCtx); }
    llvm::Value* GetStackBase() const { return m_valuePreserver.Get(x_stackBase); }
    llvm::Value* GetCurBytecode() const { return m_valuePreserver.Get(x_curBytecode); }
    llvm::Value* GetCodeBlock() const { return m_valuePreserver.Get(x_codeBlock); }

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

    llvm::CallInst* CallDeegenRuntimeFunction(const std::string& dcsName, llvm::ArrayRef<llvm::Value*> args, llvm::Instruction* insertBefore)
    {
        return CreateCallToDeegenRuntimeFunction(GetModule(), dcsName, args, insertBefore);
    }

protected:
    BytecodeVariantDefinition* m_bytecodeDef;
    BytecodeIrComponentKind m_processKind;
    LLVMValuePreserver m_valuePreserver;

    static constexpr const char* x_coroutineCtx = "coroutineCtx";
    static constexpr const char* x_stackBase = "stackBase";
    static constexpr const char* x_curBytecode = "curBytecode";
    static constexpr const char* x_codeBlock = "codeBlock";
    static constexpr const char* x_retStart = "retStart";
    static constexpr const char* x_numRet = "numRet";
    static constexpr const char* x_outputSlot = "outputSlot";
};

}   // namespace dast
