#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_bytecode_operand.h"
#include "deegen_ast_make_call.h"
#include "deegen_ast_return.h"
#include "deegen_ast_return_value_accessor.h"
#include "deegen_interpreter_function_interface.h"
#include "deegen_ast_throw_error.h"
#include "tvalue_typecheck_optimization.h"
#include "deegen_ast_simple_lowering_utils.h"

#include "llvm/Linker/Linker.h"

// TODO: extract out needed logic
#include "bytecode.h"

namespace dast {

struct ReturnContinuationFinder
{
    ReturnContinuationFinder(llvm::Function* from)
    {
        m_count = 0;
        std::vector<AstMakeCall> list = AstMakeCall::GetAllUseInFunction(from);
        for (AstMakeCall& amc : list)
        {
            dfs(amc.m_continuation);
        }
        // We disallow the entry function itself to also be a continuation,
        // since the entry function cannot access return values while the continuation function can.
        //
        ReleaseAssert(!m_labelMap.count(from));
    }

    std::unordered_map<llvm::Function*, size_t> m_labelMap;
    size_t m_count;

private:
    void dfs(llvm::Function* cur)
    {
        if (cur == nullptr || m_labelMap.count(cur))
        {
            return;
        }
        m_labelMap[cur] = m_count;
        m_count++;

        std::vector<AstMakeCall> list = AstMakeCall::GetAllUseInFunction(cur);
        for (AstMakeCall& amc : list)
        {
            dfs(amc.m_continuation);
        }
    }
};

std::string WARN_UNUSED InterpreterBytecodeImplCreator::GetInterpreterBytecodeFunctionCName(BytecodeVariantDefinition* bytecodeDef)
{
    return std::string("__deegen_interpreter_op_") + bytecodeDef->m_bytecodeName + "_" + std::to_string(bytecodeDef->m_variantOrd);
}

std::string WARN_UNUSED InterpreterBytecodeImplCreator::GetInterpreterBytecodeReturnContinuationFunctionCName(BytecodeVariantDefinition* bytecodeDef, size_t rcOrd)
{
    return GetInterpreterBytecodeFunctionCName(bytecodeDef) + "_retcont_" + std::to_string(rcOrd);
}

InterpreterBytecodeImplCreator::InterpreterBytecodeImplCreator(BytecodeVariantDefinition* bytecodeDef, llvm::Function* implTmp, bool isReturnContinuation)
    : m_bytecodeDef(bytecodeDef)
    , m_module(nullptr)
    , m_isReturnContinuation(isReturnContinuation)
    , m_impl(nullptr)
    , m_wrapper(nullptr)
    , m_valuePreserver()
    , m_generated(false)
{
    using namespace llvm;
    m_module = llvm::CloneModule(*implTmp->getParent());
    m_impl = m_module->getFunction(implTmp->getName());
    ReleaseAssert(m_impl != nullptr);

    if (m_impl->getLinkage() != GlobalValue::InternalLinkage)
    {
        // We require the implementation function to be marked 'static', so they can be automatically dropped
        // after we finished the transformation and made them dead
        //
        fprintf(stderr, "The implementation function of the bytecode (or any of its return continuation) must be marked 'static'!\n");
        abort();
    }

    LLVMContext& ctx = m_module->getContext();

    // For isReturnContinuation, our caller should have set up the desired function name for us
    // For !isReturnContinuation, we should rename by ourselves.
    //
    if (!isReturnContinuation)
    {
        m_resultFuncName = GetInterpreterBytecodeFunctionCName(m_bytecodeDef);
        std::string implFuncName = m_resultFuncName + "_impl";
        ReleaseAssert(m_module->getNamedValue(implFuncName) == nullptr);
        m_impl->setName(implFuncName);
    }
    else
    {
        std::string implFuncName = m_impl->getName().str();
        ReleaseAssert(implFuncName.ends_with("_impl"));
        m_resultFuncName = implFuncName.substr(0, implFuncName.length() - strlen("_impl"));
    }

    // First step: if we are the main function (i.e., not return continuation), we shall parse out all the needed return
    // continuations, in preparation of processing each of them later
    //
    if (!m_isReturnContinuation)
    {
        // Find all the return continuations, give each of them a unique name, and create the declarations.
        // This is necessary for us to later link them together.
        //
        ReturnContinuationFinder rcFinder(m_impl);
        std::vector<Function*> rcList;
        rcList.resize(rcFinder.m_count, nullptr);
        ReleaseAssert(rcFinder.m_labelMap.size() == rcFinder.m_count);
        for (auto& it : rcFinder.m_labelMap)
        {
            ReleaseAssert(it.second < rcList.size());
            Function* rc = it.first;
            std::string rcFinalName = GetInterpreterBytecodeReturnContinuationFunctionCName(m_bytecodeDef, it.second);
            std::string rcImplName = rcFinalName + "_impl";
            ReleaseAssert(m_module->getNamedValue(rcImplName) == nullptr);
            rc->setName(rcImplName);
            ReleaseAssert(rcList[it.second] == nullptr);
            rcList[it.second] = rc;
            ReleaseAssert(m_module->getNamedValue(rcFinalName) == nullptr);
        }

        // After all the renaming and function declarations, create the InterpreterBytecodeImplCreator class so we can process each of the return continuation later
        // Note that creating the InterpreterBytecodeImplCreator also clones the module, so we want to do it after all the renaming but before our own processing begins
        //
        for (Function* targetRc : rcList)
        {
            ReleaseAssert(targetRc != nullptr);
            m_allRetConts.push_back(std::make_unique<InterpreterBytecodeImplCreator>(m_bytecodeDef, targetRc, true /*isReturnContinuation*/));
        }
    }

    // Now, we can start processing our own module
    //
    {
        FunctionType* fty = InterpreterFunctionInterface::GetType(ctx);
        ReleaseAssert(m_module->getNamedValue(m_resultFuncName) == nullptr);
        m_wrapper = Function::Create(fty, GlobalValue::LinkageTypes::ExternalLinkage, m_resultFuncName, m_module.get());
        ReleaseAssert(m_wrapper->getName() == m_resultFuncName);
        m_wrapper->setDSOLocal(true);
    }

    // Set parameter names just to make dumps more readable
    //
    ReleaseAssert(m_wrapper->arg_size() == 5);
    m_wrapper->getArg(0)->setName(x_coroutineCtx);
    m_wrapper->getArg(1)->setName(x_stackBase);
    if (m_isReturnContinuation)
    {
        m_wrapper->getArg(2)->setName(x_retStart);
        m_wrapper->getArg(3)->setName(x_numRet);
    }
    else
    {
        m_wrapper->getArg(2)->setName(x_curBytecode);
        m_wrapper->getArg(3)->setName(x_codeBlock);
    }

    // Set up the function attributes
    // TODO: add alias attributes to parameters
    //
    m_wrapper->addFnAttr(Attribute::AttrKind::NoReturn);
    m_wrapper->addFnAttr(Attribute::AttrKind::NoUnwind);
    CopyFunctionAttributes(m_wrapper /*dst*/, m_impl /*src*/);

    BasicBlock* entryBlock = BasicBlock::Create(ctx, "", m_wrapper);
    BasicBlock* currentBlock = entryBlock;

    if (!m_isReturnContinuation)
    {
        m_valuePreserver.Preserve(x_coroutineCtx, m_wrapper->getArg(0));
        m_valuePreserver.Preserve(x_stackBase, m_wrapper->getArg(1));
        m_valuePreserver.Preserve(x_curBytecode, m_wrapper->getArg(2));
        m_valuePreserver.Preserve(x_codeBlock, m_wrapper->getArg(3));
    }
    else
    {
        m_valuePreserver.Preserve(x_coroutineCtx, m_wrapper->getArg(0));

        UnreachableInst* tmpInst = new UnreachableInst(ctx, currentBlock);
        Value* calleeStackBase = m_wrapper->getArg(1);
        Value* stackBase = CallDeegenCommonSnippet("GetCallerStackBaseFromStackBase", { calleeStackBase }, tmpInst);
        m_valuePreserver.Preserve(x_stackBase, stackBase);
        tmpInst->eraseFromParent();

        m_valuePreserver.Preserve(x_retStart, m_wrapper->getArg(2));

        PtrToIntInst* numRet = new PtrToIntInst(m_wrapper->getArg(3), llvm_type_of<uint64_t>(ctx), "" /*name*/, currentBlock);
        m_valuePreserver.Preserve(x_numRet, numRet);

        Function* getCbFunc = LinkInDeegenCommonSnippet(m_module.get(), "GetCodeBlockFromStackBase");
        ReleaseAssert(getCbFunc->arg_size() == 1 && llvm_type_has_type<void*>(getCbFunc->getFunctionType()->getParamType(0)));
        ReleaseAssert(llvm_type_has_type<void*>(getCbFunc->getFunctionType()->getReturnType()));
        Instruction* codeblock = CallInst::Create(getCbFunc, { GetStackBase() }, "" /*name*/, currentBlock);

        Function* getBytecodePtrFunc = LinkInDeegenCommonSnippet(m_module.get(), "GetBytecodePtrAfterReturnFromCall");
        ReleaseAssert(getBytecodePtrFunc->arg_size() == 2 &&
                      llvm_type_has_type<void*>(getBytecodePtrFunc->getFunctionType()->getParamType(0)) &&
                      llvm_type_has_type<void*>(getBytecodePtrFunc->getFunctionType()->getParamType(1)));
        ReleaseAssert(llvm_type_has_type<void*>(getBytecodePtrFunc->getFunctionType()->getReturnType()));
        // Note that the 'm_callerBytecodeOffset' is stored in the callee's stack frame header, so we should pass 'calleeStackBase' here
        //
        Instruction* bytecodePtr = CallInst::Create(getBytecodePtrFunc, { calleeStackBase, codeblock }, "" /*name*/, currentBlock);

        m_valuePreserver.Preserve(x_codeBlock, codeblock);
        m_valuePreserver.Preserve(x_curBytecode, bytecodePtr);
    }

    std::vector<Value*> opcodeValues;
    for (auto& operand : m_bytecodeDef->m_list)
    {
        opcodeValues.push_back(operand->GetOperandValueFromBytecodeStruct(this, currentBlock));
    }

    if (m_bytecodeDef->m_hasOutputValue)
    {
        Value* outputSlot = m_bytecodeDef->m_outputOperand->GetOperandValueFromBytecodeStruct(this, currentBlock);
        ReleaseAssert(llvm_value_has_type<uint64_t>(outputSlot));
        m_valuePreserver.Preserve(x_outputSlot, outputSlot);
        outputSlot->setName(x_outputSlot);
    }

    if (m_bytecodeDef->m_hasConditionalBranchTarget)
    {
        Value* condBrTarget = m_bytecodeDef->m_condBrTarget->GetOperandValueFromBytecodeStruct(this, currentBlock);
        ReleaseAssert(llvm_value_has_type<int32_t>(condBrTarget));
        m_valuePreserver.Preserve(x_condBrDest, condBrTarget);
        condBrTarget->setName(x_condBrDest);
    }

    std::vector<Value*> usageValues;
    {
        size_t ord = 0;
        for (auto& operand : m_bytecodeDef->m_list)
        {
            usageValues.push_back(operand->EmitUsageValueFromBytecodeValue(this, currentBlock, opcodeValues[ord]));
            // Set name to make dump a bit more readable
            //
            usageValues.back()->setName(std::string("bc_operand_") + operand->OperandName());
            ord++;
        }
        ReleaseAssert(ord == opcodeValues.size() && ord == usageValues.size());
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

    CallInst::Create(m_impl, usageValues, "", currentBlock);
    new UnreachableInst(ctx, currentBlock);

    ValidateLLVMFunction(m_wrapper);

    // At this stage, we can drop the bytecode definition global symbol, which will render all bytecode definitions except ourselves dead.
    // We then run DCE to strip the dead symbols, so that later processing is faster
    //
    std::string implNameBak = m_impl->getName().str();
    std::string wrapperNameBak = m_wrapper->getName().str();
    BytecodeVariantDefinition::RemoveUsedAttributeOfBytecodeDefinitionGlobalSymbol(m_module.get());
    RunLLVMDeadGlobalElimination(m_module.get());

    // Sanity check the functions we are processing are still there
    //
    ReleaseAssert(m_module->getFunction(implNameBak) == m_impl);
    ReleaseAssert(m_module->getFunction(wrapperNameBak) == m_wrapper);
}

std::unique_ptr<llvm::Module> WARN_UNUSED InterpreterBytecodeImplCreator::ProcessBytecode(BytecodeVariantDefinition* bytecodeDef, llvm::Function* impl)
{
    // DEVNOTE: if you change this function, you likely need to correspondingly change how we process return continuations in DoLowering()
    //
    InterpreterBytecodeImplCreator ifi(bytecodeDef, impl, false /*isReturnContinuation*/);
    return ifi.DoOptimizationAndLowering();
}

void InterpreterBytecodeImplCreator::DoOptimization()
{
    using namespace llvm;
    ReleaseAssert(!m_generated);
    ReleaseAssert(m_impl != nullptr);
    TValueTypecheckOptimizationPass::DoOptimizationForBytecode(m_bytecodeDef, m_impl);
}

std::unique_ptr<llvm::Module> WARN_UNUSED InterpreterBytecodeImplCreator::DoLowering()
{
    using namespace llvm;
    ReleaseAssert(!m_generated);
    m_generated = true;

    // Inline 'm_impl' into 'm_wrapper'
    //
    if (m_impl->hasFnAttribute(Attribute::AttrKind::NoInline))
    {
        m_impl->removeFnAttr(Attribute::AttrKind::NoInline);
    }
    m_impl->addFnAttr(Attribute::AttrKind::AlwaysInline);
    m_impl->setLinkage(GlobalValue::InternalLinkage);

    DesugarAndSimplifyLLVMModule(m_module.get(), DesugaringLevel::PerFunctionSimplifyOnly);
    m_impl = nullptr;

    m_valuePreserver.RefreshAfterTransform();

    // Now we can do the lowerings
    //
    AstBytecodeReturn::LowerForInterpreter(this, m_wrapper);
    AstMakeCall::LowerForInterpreter(this, m_wrapper);
    AstReturnValueAccessor::LowerForInterpreter(this, m_wrapper);
    DeegenAllSimpleApiLoweringPasses::LowerAllForInterpreter(this, m_wrapper);

    // All lowerings are complete.
    // Remove the NoReturn attribute since all pseudo no-return API calls have been replaced to dispatching tail calls
    //
    m_wrapper->removeFnAttr(Attribute::AttrKind::NoReturn);

    // Remove the value preserver annotations so optimizer can work fully
    //
    m_valuePreserver.Cleanup();

    // Run optimization pass
    //
    RunLLVMOptimizePass(m_module.get());

    // After the optimization pass, change the linkage of everything to 'external' before extraction
    // This is fine: our caller will fix up the linkage for us.
    //
    for (Function& func : *m_module.get())
    {
        func.setLinkage(GlobalValue::ExternalLinkage);
        func.setComdat(nullptr);
    }

    // Sanity check that 'm_wrapper' is there, and extract it
    //
    ReleaseAssert(m_module->getFunction(m_resultFuncName) == m_wrapper);
    ReleaseAssert(!m_wrapper->empty());
    m_module = ExtractFunction(m_module.get(), m_resultFuncName);
    // After the extract, 'm_wrapper' is invalidated since a new module is returned. Refresh its value.
    //
    m_wrapper = m_module->getFunction(m_resultFuncName);
    ReleaseAssert(m_wrapper != nullptr);

    if (!m_isReturnContinuation)
    {
        // If we are the main function, we also need to link in all the return continuations
        // Note that some of the return continuations could be dead (due to optimizations), however, since return continuations
        // may arbitrarily call each other, we cannot know a return continuation is dead until we have linked in all the return continuations.
        // So first we need to link in all return continuations.
        //
        for (size_t rcOrdinal = 0; rcOrdinal < m_allRetConts.size(); rcOrdinal++)
        {
            std::unique_ptr<Module> rcModule = m_allRetConts[rcOrdinal]->DoOptimizationAndLowering();
            std::string expectedRcName = m_allRetConts[rcOrdinal]->m_resultFuncName;
            ReleaseAssert(expectedRcName == GetInterpreterBytecodeReturnContinuationFunctionCName(m_bytecodeDef, rcOrdinal));
            ReleaseAssert(rcModule->getFunction(expectedRcName) != nullptr);
            ReleaseAssert(!rcModule->getFunction(expectedRcName)->empty());
            // Optimization pass may have stripped the return continuation function if it's not directly used by the main function
            // But if it exists, it should always be a declaration at this point
            //
            if (m_module->getFunction(expectedRcName) != nullptr)
            {
                ReleaseAssert(m_module->getFunction(expectedRcName)->empty());
            }

            Linker linker(*m_module.get());
            // linkInModule returns true on error
            //
            ReleaseAssert(linker.linkInModule(std::move(rcModule)) == false);

            ReleaseAssert(m_module->getFunction(expectedRcName) != nullptr);
            ReleaseAssert(!m_module->getFunction(expectedRcName)->empty());
        }

        // Now, having linked in all return continuations, we can strip dead return continuations by
        // changing the linkage of all return continuations to internal and run the DCE pass.
        //
        for (size_t rcOrdinal = 0; rcOrdinal < m_allRetConts.size(); rcOrdinal++)
        {
            std::string rcName = m_allRetConts[rcOrdinal]->m_resultFuncName;
            Function* func = m_module->getFunction(rcName);
            ReleaseAssert(func != nullptr);
            ReleaseAssert(!func->empty());
            ReleaseAssert(func->hasExternalLinkage());
            func->setLinkage(GlobalValue::InternalLinkage);
        }
        RunLLVMDeadGlobalElimination(m_module.get());

        // In theory it's fine to leave them with internal linkage now, since they are exclusively
        // used by our bytecode, but some of our callers expects them to have external linkage.
        // So we will change the surviving functions back to external linkage after DCE.
        //
        for (size_t rcOrdinal = 0; rcOrdinal < m_allRetConts.size(); rcOrdinal++)
        {
            std::string rcName = m_allRetConts[rcOrdinal]->m_resultFuncName;
            Function* func = m_module->getFunction(rcName);
            if (func != nullptr)
            {
                ReleaseAssert(func->hasInternalLinkage());
                func->setLinkage(GlobalValue::ExternalLinkage);
            }
            else
            {
                ReleaseAssert(m_module->getNamedValue(rcName) == nullptr);
            }
        }
    }
    else
    {
        ReleaseAssert(m_allRetConts.size() == 0);
    }

    ReleaseAssert(m_module->getFunction(m_resultFuncName) == m_wrapper);
    m_wrapper = nullptr;
    return std::move(m_module);
}

}   // namespace dast
