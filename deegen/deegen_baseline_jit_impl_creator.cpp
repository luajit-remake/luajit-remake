#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_interpreter_function_interface.h"
#include "deegen_ast_slow_path.h"
#include "tvalue_typecheck_optimization.h"
#include "deegen_type_based_hcs_helper.h"

namespace dast {

static llvm::CallInst* WARN_UNUSED CreateConstantPlaceholderForOperand(llvm::Module* module, size_t ordinal, llvm::Type* operandTy, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    std::string placeholderName = "__deegen_constant_placeholder_bytecode_operand_" + std::to_string(ordinal);
    ReleaseAssert(module->getNamedValue(placeholderName) == nullptr);
    FunctionType* fty = FunctionType::get(operandTy, {}, false /*isVarArg*/);
    Function* func = Function::Create(fty, GlobalValue::ExternalLinkage, placeholderName, module);
    ReleaseAssert(func->getName() == placeholderName);
    return CallInst::Create(func, { }, "", insertAtEnd);
}

void BaselineJitImplCreator::CreateWrapperFunction()
{
    using namespace llvm;
    LLVMContext& ctx = m_module->getContext();

    ReleaseAssert(m_wrapper == nullptr);

    // Create the wrapper function
    // We can also change the linkage of 'm_impl' back to internal now, since the wrapper function will keep it alive
    //
    ReleaseAssert(m_impl->hasExternalLinkage());
    m_impl->setLinkage(GlobalValue::InternalLinkage);
    m_wrapper = InterpreterFunctionInterface::CreateFunction(m_module.get(), m_resultFuncName);
    ReleaseAssert(m_wrapper->arg_size() == 16);

    m_wrapper->addFnAttr(Attribute::AttrKind::NoReturn);
    m_wrapper->addFnAttr(Attribute::AttrKind::NoUnwind);
    m_wrapper->addFnAttr(Attribute::AttrKind::NoInline);
    CopyFunctionAttributes(m_wrapper /*dst*/, m_impl /*src*/);

    BasicBlock* entryBlock = BasicBlock::Create(ctx, "", m_wrapper);
    BasicBlock* currentBlock = entryBlock;

    bool isNonJitSlowPath = false;

    if (m_processKind != BytecodeIrComponentKind::ReturnContinuation)
    {
        // Note that we also set parameter names here.
        // These are not required, but just to make dumps more readable
        //
        Value* coroCtx = m_wrapper->getArg(0);
        coroCtx->setName(x_coroutineCtx);
        m_valuePreserver.Preserve(x_coroutineCtx, coroCtx);

        Value* stackBase = m_wrapper->getArg(1);
        stackBase->setName(x_stackBase);
        m_valuePreserver.Preserve(x_stackBase, stackBase);

        // The bytecodePtr is only useful (and valid) for slow path since we need to use it to decode the bytecode struct and to
        // figure out where to branch back into the JIT'ed code
        //
        if (m_processKind == BytecodeIrComponentKind::QuickeningSlowPath || m_processKind == BytecodeIrComponentKind::SlowPath)
        {
            Value* curBytecode = m_wrapper->getArg(2);
            curBytecode->setName(x_curBytecode);
            m_valuePreserver.Preserve(x_curBytecode, curBytecode);
            isNonJitSlowPath = true;
        }
        else
        {
            // TODO: impl
        }

        Value* codeBlock = m_wrapper->getArg(3);
        codeBlock->setName(x_codeBlock);
        m_valuePreserver.Preserve(x_codeBlock, codeBlock);
    }
    else
    {
        m_valuePreserver.Preserve(x_coroutineCtx, m_wrapper->getArg(0));

        UnreachableInst* tmpInst = new UnreachableInst(ctx, currentBlock);
        Value* calleeStackBase = m_wrapper->getArg(1);
        Value* stackBase = CallDeegenCommonSnippet("GetCallerStackBaseFromStackBase", { calleeStackBase }, tmpInst);
        m_valuePreserver.Preserve(x_stackBase, stackBase);
        tmpInst->eraseFromParent();

        Value* retStart = m_wrapper->getArg(5);
        retStart->setName(x_retStart);
        m_valuePreserver.Preserve(x_retStart, retStart);

        Value* numRet = m_wrapper->getArg(6);
        numRet->setName(x_numRet);
        m_valuePreserver.Preserve(x_numRet, numRet);

        // The bytecodePtr is only useful (and valid) for slow path return continuation,
        // since we need to use it to decode the bytecode struct and to figure out where to branch back into the JIT'ed code
        //
        if (m_isSlowPathReturnContinuation)
        {
            // Note that the 'm_callerBytecodePtr' is stored in the callee's stack frame header, so we should pass 'calleeStackBase' here
            //
            Instruction* bytecodePtr = CreateCallToDeegenCommonSnippet(GetModule(), "GetBytecodePtrAfterReturnFromCall", { calleeStackBase }, currentBlock);
            ReleaseAssert(llvm_value_has_type<void*>(bytecodePtr));
            isNonJitSlowPath = true;
            m_valuePreserver.Preserve(x_curBytecode, bytecodePtr);
        }
        else
        {
            // TODO: impl
        }

        Instruction* codeblock = CreateCallToDeegenCommonSnippet(GetModule(), "GetCodeBlockFromStackBase", { GetStackBase() }, currentBlock);
        ReleaseAssert(llvm_value_has_type<void*>(codeblock));

        m_valuePreserver.Preserve(x_codeBlock, codeblock);
    }

    std::unordered_map<uint64_t /*operandOrd*/, uint64_t /*argOrd*/> alreadyDecodedArgs;
    if (m_processKind == BytecodeIrComponentKind::QuickeningSlowPath && m_bytecodeDef->HasQuickeningSlowPath())
    {
        alreadyDecodedArgs = TypeBasedHCSHelper::GetQuickeningSlowPathAdditionalArgs(m_bytecodeDef);
    }

    std::vector<Value*> opcodeValues;

    for (auto& operand : m_bytecodeDef->m_list)
    {
        if (alreadyDecodedArgs.count(operand->OperandOrdinal()))
        {
            opcodeValues.push_back(nullptr);
        }
        else if (!operand->SupportsGetOperandValueFromBytecodeStruct())
        {
            opcodeValues.push_back(nullptr);
        }
        else if (!isNonJitSlowPath)
        {
            // This is the fast path JIT'ed code, the operands shall be represented as a runtime constant hole
            // Note that this check has to happen after the SupportsGetOperandValueFromBytecodeStruct() check,
            // since if the operand is already known to be a static constant value, we should not use a runtime constant hole
            //
            llvm::Value* val = CreateConstantPlaceholderForOperand(GetModule(), operand->OperandOrdinal(), operand->GetSourceValueFullRepresentationType(ctx), currentBlock);
            opcodeValues.push_back(val);
        }
        else
        {
            opcodeValues.push_back(operand->GetOperandValueFromBytecodeStruct(this, currentBlock));
        }
    }

    if (m_bytecodeDef->m_hasOutputValue)
    {
        Value* outputSlot;
        if (!isNonJitSlowPath)
        {
            outputSlot = CreateConstantPlaceholderForOperand(GetModule(), 100 /*operandOrd*/, llvm_type_of<uint64_t>(ctx), currentBlock);
        }
        else
        {
            outputSlot = m_bytecodeDef->m_outputOperand->GetOperandValueFromBytecodeStruct(this, currentBlock);
        }
        ReleaseAssert(llvm_value_has_type<uint64_t>(outputSlot));
        m_valuePreserver.Preserve(x_outputSlot, outputSlot);
        outputSlot->setName(x_outputSlot);
    }

    if (m_bytecodeDef->m_hasConditionalBranchTarget)
    {
        Value* condBrTarget;
        if (!isNonJitSlowPath)
        {
            condBrTarget = CreateConstantPlaceholderForOperand(GetModule(), 101 /*operandOrd*/, llvm_type_of<int32_t>(ctx), currentBlock);
        }
        else
        {
            condBrTarget = m_bytecodeDef->m_condBrTarget->GetOperandValueFromBytecodeStruct(this, currentBlock);
        }
        ReleaseAssert(llvm_value_has_type<int32_t>(condBrTarget));
        m_valuePreserver.Preserve(x_condBrDest, condBrTarget);
        condBrTarget->setName(x_condBrDest);
    }

    ReleaseAssert(m_bytecodeDef->IsBytecodeStructLengthFinalized());

    std::vector<Value*> usageValues;
    {
        size_t ord = 0;
        for (auto& operand : m_bytecodeDef->m_list)
        {
            ReleaseAssert(operand->OperandOrdinal() == ord);
            if (alreadyDecodedArgs.count(operand->OperandOrdinal()))
            {
                size_t argOrd = alreadyDecodedArgs[operand->OperandOrdinal()];
                Value* arg = TypeBasedHCSHelper::GetBytecodeOperandUsageValueFromAlreadyDecodedArgs(m_wrapper, argOrd, currentBlock);
                usageValues.push_back(arg);
            }
            else
            {
                usageValues.push_back(operand->EmitUsageValueFromBytecodeValue(this, currentBlock, opcodeValues[ord]));
            }
            // Set name to make dump a bit more readable
            //
            usageValues.back()->setName(std::string("bc_operand_") + operand->OperandName());
            ord++;
        }
        ReleaseAssert(ord == opcodeValues.size() && ord == usageValues.size());
    }

    if (m_processKind == BytecodeIrComponentKind::SlowPath)
    {
        std::vector<Value*> extraArgs = AstSlowPath::CreateCallArgsInSlowPathWrapperFunction(static_cast<uint32_t>(usageValues.size()), m_impl, currentBlock);
        for (Value* val : extraArgs)
        {
            usageValues.push_back(val);
        }
    }

    {
        FunctionType* fty = m_impl->getFunctionType();
        ReleaseAssert(llvm_type_has_type<void>(fty->getReturnType()));
        ReleaseAssert(fty->getNumParams() == usageValues.size());
        for (size_t i = 0; i < usageValues.size(); i++)
        {
            ReleaseAssert(fty->getParamType(static_cast<uint32_t>(i)) == usageValues[i]->getType());
        }
    }

    if (m_processKind == BytecodeIrComponentKind::Main && m_bytecodeDef->HasQuickeningSlowPath())
    {
        // If we are the main function and we are a quickening bytecode, we need to check that the quickening condition holds.
        // We can only run 'm_impl' if the condition holds. If not, we must transfer control to the quickening slow path.
        //
        TypeBasedHCSHelper::GenerateCheckConditionLogic(this, usageValues, currentBlock /*inout*/);

        // At the end of 'currentBlock', we've passed all checks. Now we can call our specialized fastpath 'm_impl' which assumes these checks are true
        //
        CallInst::Create(m_impl, usageValues, "", currentBlock);
        new UnreachableInst(ctx, currentBlock);
    }
    else
    {
        // Otherwise, it's as simple as calling 'm_impl'
        //
        CallInst::Create(m_impl, usageValues, "", currentBlock);
        new UnreachableInst(ctx, currentBlock);
    }

    ValidateLLVMFunction(m_wrapper);
}

}   // namespace dast
