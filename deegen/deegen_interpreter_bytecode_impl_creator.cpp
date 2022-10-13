#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_bytecode_operand.h"
#include "deegen_ast_make_call.h"
#include "deegen_ast_return.h"
#include "deegen_ast_return_value_accessor.h"
#include "deegen_interpreter_function_interface.h"
#include "deegen_ast_throw_error.h"
#include "tvalue_typecheck_optimization.h"
#include "deegen_ast_simple_lowering_utils.h"
#include "tag_register_optimization.h"
#include "llvm_fcmp_extra_optimizations.h"

#include "llvm/Linker/Linker.h"

#include "runtime_utils.h"

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

static std::string GetQuickeningSlowPathFunctionNameFromBytecodeMainFunctionName(const std::string& bytecodeFnName)
{
    return bytecodeFnName + "_quickening_slowpath";
}

bool WARN_UNUSED InterpreterBytecodeImplCreator::IsFunctionReturnContinuationOfBytecode(llvm::Function* func, const std::string& bytecodeVariantMainFuncName)
{
    std::string fnName = func->getName().str();
    std::string expectedPrefix = bytecodeVariantMainFuncName + "_retcont_";
    if (!fnName.starts_with(expectedPrefix))
    {
        return false;
    }
    fnName = fnName.substr(expectedPrefix.length());
    ReleaseAssert(fnName.length() > 0);
    for (size_t i = 0; i < fnName.length(); i++)
    {
        ReleaseAssert('0' <= fnName[i] && fnName[i] <= '9');
    }
    return true;
}

static std::unordered_map<uint64_t /*operandOrd*/, uint64_t /*argOrd*/> GetQuickeningSlowPathAdditionalArgs(BytecodeVariantDefinition* bytecodeDef)
{
    ReleaseAssert(bytecodeDef->HasQuickeningSlowPath());

    // We can only use the unused registers in InterpreterFunctionInterface, so unfortunately this is
    // currently tightly coupled with our value-passing convention of InterpreterFunctionInterface..
    //
    // The already-decoded args will be passed to the continuation using registers in the following order.
    // The order doesn't matter. But I chose the order of GPR list to stay away from the C calling conv registers,
    // in the hope that it can reduce the likelihood of register shuffling when making C calls.
    //
    std::vector<uint64_t> gprList { 8 /*R9*/, 7 /*R8*/, 5 /*RSI*/, 6 /*RDI*/ };
    std::vector<uint64_t> fprList { 10 /*XMM1*/, 11 /*XMM2*/, 12 /*XMM3*/, 13 /*XMM4*/, 14 /*XMM5*/, 15 /*XMM6*/ };

    // We will use pop_back for simplicity, so reverse the vector now
    //
    std::reverse(gprList.begin(), gprList.end());
    std::reverse(fprList.begin(), fprList.end());

    std::unordered_map<uint64_t /*operandOrd*/, uint64_t /*argOrd*/> res;
    for (auto& it : bytecodeDef->m_quickening)
    {
        TypeSpeculationMask mask = it.m_speculatedMask;
        // This is the only case that we want to (and can) use FPR to hold the TValue directly
        //
        bool shouldUseFPR = (mask == x_typeSpeculationMaskFor<tDoubleNotNaN>);
        size_t operandOrd = it.m_operandOrd;

        if (shouldUseFPR)
        {
            if (fprList.empty())
            {
                continue;
            }
            size_t argOrd = fprList.back();
            fprList.pop_back();
            res[operandOrd] = argOrd;
        }
        else
        {
            if (gprList.empty())
            {
                continue;
            }
            size_t argOrd = gprList.back();
            gprList.pop_back();
            res[operandOrd] = argOrd;
        }
    }
    return res;
}

InterpreterBytecodeImplCreator::InterpreterBytecodeImplCreator(BytecodeVariantDefinition* bytecodeDef, llvm::Function* implTmp, ProcessKind processKind)
    : m_bytecodeDef(bytecodeDef)
    , m_module(nullptr)
    , m_processKind(processKind)
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

    // For m_processKind != Main, our caller should have set up the desired function name for us
    // Otherwise, we should rename by ourselves.
    //
    if (m_processKind == ProcessKind::Main)
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
    if (m_processKind == ProcessKind::Main)
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
            m_allRetConts.push_back(std::make_unique<InterpreterBytecodeImplCreator>(m_bytecodeDef, targetRc, ProcessKind::ReturnContinuation));
        }

        // Process the quickening slow path if needed
        //
        if (m_bytecodeDef->HasQuickeningSlowPath())
        {
            // Temporarily change 'm_impl' to the function name for the slowpath
            //
            std::string oldName = m_impl->getName().str();
            std::string desiredName = GetQuickeningSlowPathFunctionNameFromBytecodeMainFunctionName(m_resultFuncName) + "_impl";
            m_impl->setName(desiredName);
            ReleaseAssert(m_impl->getName().str() == desiredName);

            // Construct the slowpath creator. This clones the module in the process, and the impl function in the cloned module has the new name, as desired.
            //
            m_quickeningSlowPath = std::make_unique<InterpreterBytecodeImplCreator>(m_bytecodeDef, m_impl, ProcessKind::QuickeningSlowPath);

            // Revert the name
            //
            m_impl->setName(oldName);
            ReleaseAssert(m_impl->getName().str() == oldName);
        }
    }

    // Now, we can start processing our own module
    //

    // At this stage we can drop the bytecode definition global symbol, which will render all bytecode definitions dead.
    // We temporarily change 'm_impl' to external linkage to keep it alive, then run DCE to strip the dead symbols,
    // so that all bytecode defintions except 'm_impl' are stripped and later processing is faster
    //
    std::string implNameBak = m_impl->getName().str();
    BytecodeVariantDefinition::RemoveUsedAttributeOfBytecodeDefinitionGlobalSymbol(m_module.get());
    ReleaseAssert(m_impl->getLinkage() == GlobalValue::InternalLinkage);
    m_impl->setLinkage(GlobalValue::ExternalLinkage);

    RunLLVMDeadGlobalElimination(m_module.get());

    // Sanity check 'm_impl' is still there
    //
    ReleaseAssert(m_module->getFunction(implNameBak) == m_impl);

    // Before changing 'm_impl' back to internal linkage, run the general inlining pass to
    // inline general (non-API) functions into 'm_impl' and canonicalize it
    // (if we change it to internal linkage now, it will be dead and get removed)
    //
    DesugarAndSimplifyLLVMModule(m_module.get(), DesugaringLevel::InlineGeneralFunctions);
    ReleaseAssert(m_module->getFunction(implNameBak) == m_impl);

    // Now change the linkage back to internal and create the wrapper function
    //
    m_impl->setLinkage(GlobalValue::InternalLinkage);
    m_wrapper = InterpreterFunctionInterface::CreateFunction(m_module.get(), m_resultFuncName);
    ReleaseAssert(m_wrapper->arg_size() == 16);

    // Set up the function attributes
    // TODO: add alias attributes to parameters
    //
    m_wrapper->addFnAttr(Attribute::AttrKind::NoReturn);
    m_wrapper->addFnAttr(Attribute::AttrKind::NoUnwind);
    if (m_processKind == ProcessKind::QuickeningSlowPath)
    {
        m_wrapper->addFnAttr(Attribute::AttrKind::NoInline);
    }
    CopyFunctionAttributes(m_wrapper /*dst*/, m_impl /*src*/);

    BasicBlock* entryBlock = BasicBlock::Create(ctx, "", m_wrapper);
    BasicBlock* currentBlock = entryBlock;

    if (m_processKind != ProcessKind::ReturnContinuation)
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

        Value* curBytecode = m_wrapper->getArg(2);
        curBytecode->setName(x_curBytecode);
        m_valuePreserver.Preserve(x_curBytecode, curBytecode);

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

    std::unordered_map<uint64_t /*operandOrd*/, uint64_t /*argOrd*/> alreadyDecodedArgs;
    if (m_processKind == ProcessKind::QuickeningSlowPath && m_bytecodeDef->HasQuickeningSlowPath())
    {
        alreadyDecodedArgs = GetQuickeningSlowPathAdditionalArgs(m_bytecodeDef);
    }

    std::vector<Value*> opcodeValues;
    for (auto& operand : m_bytecodeDef->m_list)
    {
        if (alreadyDecodedArgs.count(operand->OperandOrdinal()))
        {
            opcodeValues.push_back(nullptr);
        }
        else
        {
            opcodeValues.push_back(operand->GetOperandValueFromBytecodeStruct(this, currentBlock));
        }
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
            ReleaseAssert(operand->OperandOrdinal() == ord);
            if (alreadyDecodedArgs.count(operand->OperandOrdinal()))
            {
                size_t argOrd = alreadyDecodedArgs[operand->OperandOrdinal()];
                ReleaseAssert(argOrd < m_wrapper->arg_size());
                Value* arg = m_wrapper->getArg(static_cast<uint32_t>(argOrd));
                if (llvm_value_has_type<double>(arg))
                {
                    Instruction* dblToI64 = new BitCastInst(arg, llvm_type_of<uint64_t>(ctx), "", currentBlock);
                    usageValues.push_back(dblToI64);
                }
                else if (llvm_value_has_type<void*>(arg))
                {
                    Instruction* ptrToI64 = new PtrToIntInst(arg, llvm_type_of<uint64_t>(ctx), "", currentBlock);
                    usageValues.push_back(ptrToI64);
                }
                else
                {
                    ReleaseAssert(llvm_value_has_type<uint64_t>(arg));
                    usageValues.push_back(arg);
                }
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

    {
        FunctionType* fty = m_impl->getFunctionType();
        ReleaseAssert(llvm_type_has_type<void>(fty->getReturnType()));
        ReleaseAssert(fty->getNumParams() == usageValues.size());
        for (size_t i = 0; i < usageValues.size(); i++)
        {
            ReleaseAssert(fty->getParamType(static_cast<uint32_t>(i)) == usageValues[i]->getType());
        }
    }

    if (m_processKind == ProcessKind::Main && m_bytecodeDef->HasQuickeningSlowPath())
    {
        // If we are the main function and we are a quickening bytecode, we need to check that the quickening condition holds.
        // We can only run 'm_impl' if the condition holds. If not, we must transfer control to the quickening slow path.
        //

        // First let's create the basic block that fallbacks to the slow path
        //
        BasicBlock* slowpathBB;
        {
            std::string slowpathName = GetQuickeningSlowPathFunctionNameFromBytecodeMainFunctionName(m_resultFuncName);
            Function* slowpathFn = InterpreterFunctionInterface::CreateFunction(m_module.get(), slowpathName);
            slowpathFn->addFnAttr(Attribute::AttrKind::NoReturn);
            slowpathFn->addFnAttr(Attribute::AttrKind::NoInline);
            ReleaseAssert(slowpathFn->getName() == slowpathName);
            slowpathBB = BasicBlock::Create(ctx, "slowpath", m_wrapper);
            Instruction* tmp = new UnreachableInst(ctx, slowpathBB);
            InterpreterFunctionInterface::CreateDispatchToBytecode(slowpathFn, GetCoroutineCtx(), GetStackBase(), GetCurBytecode(), GetCodeBlock(), tmp /*insertBefore*/);
            CallInst* callInst = dyn_cast<CallInst>(tmp->getPrevNode()->getPrevNode());
            ReleaseAssert(callInst != nullptr);
            ReleaseAssert(callInst->getCalledFunction() == slowpathFn);
            tmp->eraseFromParent();
            std::unordered_map<uint64_t /*operandOrd*/, uint64_t /*argOrd*/> extraArgs = GetQuickeningSlowPathAdditionalArgs(m_bytecodeDef);
            for (auto& it : extraArgs)
            {
                uint64_t operandOrd = it.first;
                ReleaseAssert(operandOrd < usageValues.size());
                uint64_t argOrd = it.second;
                ReleaseAssert(argOrd < slowpathFn->arg_size());
                Type* desiredArgTy = slowpathFn->getArg(static_cast<uint32_t>(argOrd))->getType();
                Value* srcValue = usageValues[operandOrd];
                ReleaseAssert(llvm_value_has_type<uint64_t>(srcValue));
                Value* argValue;
                if (llvm_type_has_type<double>(desiredArgTy))
                {
                    argValue = new BitCastInst(srcValue, llvm_type_of<double>(ctx), "", callInst /*insertBefore*/);
                }
                else if (llvm_type_has_type<void*>(desiredArgTy))
                {
                    argValue = new IntToPtrInst(srcValue, llvm_type_of<void*>(ctx), "", callInst /*insertBefore*/);
                }
                else
                {
                    ReleaseAssert(llvm_type_has_type<uint64_t>(desiredArgTy));
                    argValue = srcValue;
                }
                ReleaseAssert(argValue->getType() == desiredArgTy);
                ReleaseAssert(dyn_cast<UndefValue>(callInst->getArgOperand(static_cast<uint32_t>(argOrd))) != nullptr);
                callInst->setArgOperand(static_cast<uint32_t>(argOrd), argValue);
            }
        }

        // Now, create each check of the quickening condition
        //
        Function* expectIntrin = Intrinsic::getDeclaration(m_module.get(), Intrinsic::expect, { Type::getInt1Ty(ctx) });
        TypeCheckFunctionSelector tcFnSelector(m_module.get());

        for (auto& it : m_bytecodeDef->m_quickening)
        {
            size_t operandOrd = it.m_operandOrd;
            ReleaseAssert(operandOrd < usageValues.size());
            TypeSpeculationMask mask = it.m_speculatedMask;
            TypeCheckFunctionSelector::QueryResult res = tcFnSelector.Query(mask, x_typeSpeculationMaskFor<tTop>);
            ReleaseAssert(res.m_opKind == TypeCheckFunctionSelector::QueryResult::CallFunction);
            Function* callee = res.m_func;
            ReleaseAssert(callee != nullptr && callee->arg_size() == 1 && llvm_value_has_type<uint64_t>(callee->getArg(0)) && llvm_type_has_type<bool>(callee->getReturnType()));
            CallInst* checkPassed = CallInst::Create(callee, { usageValues[operandOrd] }, "", currentBlock);
            ReleaseAssert(llvm_value_has_type<bool>(checkPassed));
            CallInst::Create(expectIntrin, { checkPassed, CreateLLVMConstantInt<bool>(ctx, true) }, "", currentBlock);

            BasicBlock* newBB = BasicBlock::Create(ctx, "", m_wrapper);
            BranchInst::Create(newBB /*ifTrue*/, slowpathBB /*ifFalse*/, checkPassed /*cond*/, currentBlock);
            currentBlock = newBB;
        }

        // We've passed all checks. Now we can call our specialized fastpath 'm_impl' which assumes these checks are true
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

std::unique_ptr<llvm::Module> WARN_UNUSED InterpreterBytecodeImplCreator::ProcessBytecode(BytecodeVariantDefinition* bytecodeDef, llvm::Function* impl)
{
    // DEVNOTE: if you change this function, you likely need to correspondingly change how we process return continuations in DoLowering()
    //
    InterpreterBytecodeImplCreator ifi(bytecodeDef, impl, ProcessKind::Main);
    return ifi.DoOptimizationAndLowering();
}

void InterpreterBytecodeImplCreator::DoOptimization()
{
    using namespace llvm;
    ReleaseAssert(!m_generated);
    ReleaseAssert(m_impl != nullptr);

    // Run TValue typecheck strength reduction
    //
    DesugarAndSimplifyLLVMModule(m_module.get(), DesugarUpToExcluding(DesugaringLevel::TypeSpecialization));
    if (m_processKind == ProcessKind::Main)
    {
        if (m_bytecodeDef->HasQuickeningSlowPath())
        {
            // In this case, we are the fast path
            //
            TValueTypecheckOptimizationPass::DoOptimizationForBytecodeQuickeningFastPath(m_bytecodeDef, m_impl);
        }
        else
        {
            TValueTypecheckOptimizationPass::DoOptimizationForBytecode(m_bytecodeDef, m_impl);
        }
    }
    else if (m_processKind == ProcessKind::QuickeningSlowPath)
    {
        TValueTypecheckOptimizationPass::DoOptimizationForBytecodeQuickeningSlowPath(m_bytecodeDef, m_impl);
    }
    else
    {
        ReleaseAssert(m_processKind == ProcessKind::ReturnContinuation);
        TValueTypecheckOptimizationPass::DoOptimizationForBytecode(m_bytecodeDef, m_impl);
    }
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
    if (m_processKind == ProcessKind::Main && m_bytecodeDef->HasQuickeningSlowPath())
    {
        std::string slowPathFnName = GetQuickeningSlowPathFunctionNameFromBytecodeMainFunctionName(m_resultFuncName);
        Function* slowPathFn = m_module->getFunction(slowPathFnName);
        ReleaseAssert(slowPathFn != nullptr);
        ReleaseAssert(slowPathFn->hasFnAttribute(Attribute::AttrKind::NoReturn));
        slowPathFn->removeFnAttr(Attribute::AttrKind::NoReturn);
    }

    // Remove the value preserver annotations so optimizer can work fully
    //
    m_valuePreserver.Cleanup();

    // Now, having lowered everything, we can run the tag register optimization pass
    //
    // The tag register optimization pass is supposed to be run after all API calls have been inlined, so lower all the API calls
    //
    DesugarAndSimplifyLLVMModule(m_module.get(), DesugaringLevel::Top);

    // Now, run the tag register optimization pass
    //
    RunTagRegisterOptimizationPass(m_wrapper);

    // Run LLVM optimization pass
    //
    RunLLVMOptimizePass(m_module.get());

    // Run our homebrewed simple rewrite passes (targetting some insufficiencies of LLVM's optimizations of FCmp) after the main LLVM optimization pass
    //
    DeegenExtraLLVMOptPass_FuseTwoNaNChecksIntoOne(m_module.get());

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

    if (m_processKind == ProcessKind::Main)
    {
        // If we are the main function, we need to link in all the return continuations, and possibly the quickening slowpath
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

        // Link in the quickening slow path if needed
        //
        ReleaseAssertIff(m_bytecodeDef->HasQuickeningSlowPath(), m_quickeningSlowPath.get() != nullptr);
        if (m_bytecodeDef->HasQuickeningSlowPath())
        {
            std::unique_ptr<Module> spModule = m_quickeningSlowPath->DoOptimizationAndLowering();
            std::string expectedSpName = m_quickeningSlowPath->m_resultFuncName;
            ReleaseAssert(expectedSpName == GetQuickeningSlowPathFunctionNameFromBytecodeMainFunctionName(m_resultFuncName));
            ReleaseAssert(spModule->getFunction(expectedSpName) != nullptr);
            ReleaseAssert(!spModule->getFunction(expectedSpName)->empty());

            Linker linker(*m_module.get());
            // linkInModule returns true on error
            //
            ReleaseAssert(linker.linkInModule(std::move(spModule)) == false);

            ReleaseAssert(m_module->getFunction(expectedSpName) != nullptr);
            ReleaseAssert(!m_module->getFunction(expectedSpName)->empty());
        }

        // Now, having linked in all return continuations and the quickening slow path, we can strip dead return continuations by
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
