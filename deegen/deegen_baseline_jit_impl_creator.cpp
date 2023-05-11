#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_ast_inline_cache.h"
#include "deegen_interpreter_function_interface.h"
#include "deegen_ast_slow_path.h"
#include "deegen_stencil_reserved_placeholder_ords.h"
#include "llvm_fcmp_extra_optimizations.h"
#include "tvalue_typecheck_optimization.h"
#include "deegen_type_based_hcs_helper.h"
#include "tag_register_optimization.h"
#include "deegen_ast_return.h"
#include "deegen_ast_make_call.h"
#include "deegen_ast_simple_lowering_utils.h"
#include "deegen_ast_return_value_accessor.h"
#include "deegen_stencil_lowering_pass.h"
#include "invoke_clang_helper.h"
#include "llvm_override_option.h"
#include "llvm/Linker/Linker.h"

namespace dast {

static std::string WARN_UNUSED GetRawRuntimeConstantPlaceholderName(size_t ordinal)
{
    return std::string("__deegen_constant_placeholder_bytecode_operand_") + std::to_string(ordinal);
}

llvm::CallInst* WARN_UNUSED DeegenPlaceholderUtils::CreateConstantPlaceholderForOperand(llvm::Module* module, size_t ordinal, llvm::Type* operandTy, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    std::string placeholderName = GetRawRuntimeConstantPlaceholderName(ordinal);
    ReleaseAssert(module->getNamedValue(placeholderName) == nullptr);
    FunctionType* fty = FunctionType::get(operandTy, {}, false /*isVarArg*/);
    Function* func = Function::Create(fty, GlobalValue::ExternalLinkage, placeholderName, module);
    ReleaseAssert(func->getName() == placeholderName);
    func->addFnAttr(Attribute::AttrKind::NoUnwind);
    func->addFnAttr(Attribute::AttrKind::ReadNone);
    func->addFnAttr(Attribute::AttrKind::WillReturn);
    return CallInst::Create(func, { }, "", insertBefore);
}

llvm::CallInst* WARN_UNUSED BaselineJitImplCreator::CreateConstantPlaceholderForOperand(size_t ordinal, llvm::Type* operandTy, int64_t lb, int64_t ub, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    CallInst* ci = DeegenPlaceholderUtils::CreateConstantPlaceholderForOperand(GetModule(), ordinal, operandTy, insertBefore);
    m_stencilRcInserter.AddRawRuntimeConstant(ordinal, lb, ub);
    return ci;
}

llvm::CallInst* WARN_UNUSED BaselineJitImplCreator::CreateConstantPlaceholderForOperand(size_t ordinal, llvm::Type* operandTy, int64_t lb, int64_t ub, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(operandTy->getContext(), insertAtEnd);
    CallInst* res = CreateConstantPlaceholderForOperand(ordinal, operandTy, lb, ub, dummy);
    dummy->eraseFromParent();
    return res;
}

llvm::CallInst* WARN_UNUSED BaselineJitImplCreator::CreateOrGetConstantPlaceholderForOperand(size_t ordinal, llvm::Type* operandTy, int64_t lb, int64_t ub, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    std::string placeholderName = GetRawRuntimeConstantPlaceholderName(ordinal);
    if (GetModule()->getNamedValue(placeholderName) == nullptr)
    {
        return CreateConstantPlaceholderForOperand(ordinal, operandTy, lb, ub, insertBefore);
    }

    Function* func = GetModule()->getFunction(placeholderName);
    ReleaseAssert(func != nullptr);
    ReleaseAssert(func->getReturnType() == operandTy);
    ReleaseAssert(func->arg_size() == 0);

    m_stencilRcInserter.WidenRange(ordinal, lb, ub);
    return CallInst::Create(func, { }, "", insertBefore);
}

std::pair<llvm::CallInst*, size_t /*ord*/> WARN_UNUSED BaselineJitImplCreator::CreateGenericIcStateCapturePlaceholder(llvm::Type* ty, int64_t lb, int64_t ub, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(ty->getContext(), insertAtEnd);
    auto res = CreateGenericIcStateCapturePlaceholder(ty, lb, ub, dummy);
    dummy->eraseFromParent();
    return res;
}

std::pair<llvm::CallInst*, size_t /*ord*/> WARN_UNUSED BaselineJitImplCreator::CreateGenericIcStateCapturePlaceholder(llvm::Type* ty, int64_t lb, int64_t ub, llvm::Instruction* insertBefore)
{
    size_t resultOrd = m_numGenericIcCaptures;
    m_numGenericIcCaptures++;
    llvm::CallInst* inst = CreateConstantPlaceholderForOperand(resultOrd + 200, ty, lb, ub, insertBefore);
    return std::make_pair(inst, resultOrd);
}

llvm::CallInst* WARN_UNUSED BaselineJitImplCreator::CreateOrGetConstantPlaceholderForOperand(size_t ordinal, llvm::Type* operandTy, int64_t lb, int64_t ub, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(operandTy->getContext(), insertAtEnd);
    CallInst* res = CreateOrGetConstantPlaceholderForOperand(ordinal, operandTy, lb, ub, dummy);
    dummy->eraseFromParent();
    return res;
}

llvm::Value* WARN_UNUSED BaselineJitImplCreator::GetSlowPathDataOffsetFromJitFastPath(llvm::Instruction* insertBefore)
{
    using namespace llvm;
    ReleaseAssert(!IsBaselineJitSlowPath());
    return CreateOrGetConstantPlaceholderForOperand(103 /*ordinal*/,
                                                    llvm_type_of<uint64_t>(insertBefore->getContext()),
                                                    1 /*lowerBound*/,
                                                    StencilRuntimeConstantInserter::GetLowAddrRangeUB(),
                                                    insertBefore);
}

llvm::Value* WARN_UNUSED BaselineJitImplCreator::GetSlowPathDataOffsetFromJitFastPath(llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(insertAtEnd->getContext(), insertAtEnd);
    Value* res = GetSlowPathDataOffsetFromJitFastPath(dummy);
    dummy->eraseFromParent();
    return res;
}

BaselineJitImplCreator::BaselineJitImplCreator(BytecodeIrComponent& bic)
    : DeegenBytecodeImplCreatorBase(bic.m_bytecodeDef, bic.m_processKind)
{
    using namespace llvm;
    m_module = CloneModule(*bic.m_module.get());
    m_impl = m_module->getFunction(bic.m_impl->getName());
    ReleaseAssert(m_impl != nullptr);
    ReleaseAssert(m_impl->getName() == bic.m_impl->getName());
    m_wrapper = nullptr;
    m_resultFuncName = bic.m_identFuncName;
    m_generated = false;
    m_isSlowPathReturnContinuation = false;
    m_numGenericIcCaptures = 0;
}

BaselineJitImplCreator::BaselineJitImplCreator(BaselineJitImplCreator::SlowPathReturnContinuationTag, BytecodeIrComponent& bic)
    : BaselineJitImplCreator(bic)
{
    ReleaseAssert(m_processKind == BytecodeIrComponentKind::ReturnContinuation);
    m_isSlowPathReturnContinuation = true;
}

size_t WARN_UNUSED DeegenPlaceholderUtils::FindFallthroughPlaceholderOrd(const std::vector<CPRuntimeConstantNodeBase*>& rcDef)
{
    bool found = false;
    size_t ord = static_cast<size_t>(-1);
    for (size_t i = 0; i < rcDef.size(); i++)
    {
        CPRuntimeConstantNodeBase* def = rcDef[i];
        if (def->IsRawRuntimeConstant() && dynamic_cast<CPRawRuntimeConstant*>(def)->m_label == 101 /*fallthroughTarget*/)
        {
            ReleaseAssert(!found);
            found = true;
            ord = i;
        }
    }
    if (!found) { return static_cast<size_t>(-1); }
    return ord;
}

std::string WARN_UNUSED DeegenPlaceholderUtils::FindFallthroughPlaceholderSymbolName(const std::vector<CPRuntimeConstantNodeBase*>& rcDef)
{
    size_t ord = FindFallthroughPlaceholderOrd(rcDef);
    if (ord == static_cast<size_t>(-1))
    {
        return "";
    }
    return std::string("__deegen_cp_placeholder_") + std::to_string(ord);
}

std::string WARN_UNUSED BaselineJitImplCreator::GetRcPlaceholderNameForFallthrough()
{
    ReleaseAssert(!IsBaselineJitSlowPath());
    return DeegenPlaceholderUtils::FindFallthroughPlaceholderSymbolName(m_stencilRcDefinitions);
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

        Value* codeBlock = m_wrapper->getArg(3);
        codeBlock->setName(x_codeBlock);
        m_valuePreserver.Preserve(x_codeBlock, codeBlock);

        // The BaselineJitSlowPathData is only useful (and valid) for slow path
        //
        if (IsBaselineJitSlowPath())
        {
            Value* jitSlowPathData = m_wrapper->getArg(2);
            jitSlowPathData->setName(x_jitSlowPathData);
            isNonJitSlowPath = true;
            m_valuePreserver.Preserve(x_jitSlowPathData, jitSlowPathData);
        }
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


        if (IsBaselineJitSlowPath())
        {
            // Decode the CodeBlock from the stack frame header
            //
            Instruction* codeBlock = CreateCallToDeegenCommonSnippet(GetModule(), "GetCodeBlockFromStackBase", { GetStackBase() }, currentBlock);
            ReleaseAssert(llvm_value_has_type<void*>(codeBlock));

            m_valuePreserver.Preserve(x_codeBlock, codeBlock);

            // Decode BaselineJitSlowPathData, which is only useful (and valid) for slow path
            // Furthermore, since this is the return continuation, we must decode this value from the stack frame header
            //
            // Note that the 'm_callerBytecodePtr' is stored in the callee's stack frame header, so we should pass 'calleeStackBase' here
            //
            Instruction* slowPathDataPtr = CreateCallToDeegenCommonSnippet(GetModule(), "GetBaselineJitSlowpathDataAfterSlowCall", { calleeStackBase, codeBlock }, currentBlock);
            ReleaseAssert(llvm_value_has_type<void*>(slowPathDataPtr));
            isNonJitSlowPath = true;
            m_valuePreserver.Preserve(x_jitSlowPathData, slowPathDataPtr);
        }
        else
        {
            // For the JIT fast path, we only need to set up the CodeBlock, which is hardcoded as a stencil hole
            // For now since we don't support encoding 64-bit constant, just translate from HeapPtr..
            //
            Value* val = CreateConstantPlaceholderForOperand(104 /*ordinal*/,
                                                             llvm_type_of<uint64_t>(ctx),
                                                             1,
                                                             m_stencilRcInserter.GetLowAddrRangeUB(),
                                                             currentBlock);
            Value* codeBlockHeapPtr = new IntToPtrInst(val, llvm_type_of<HeapPtr<void>>(ctx), "", currentBlock);
            Value* codeBlock = CreateCallToDeegenCommonSnippet(GetModule(), "SimpleTranslateToRawPointer", { codeBlockHeapPtr }, currentBlock);
            ReleaseAssert(llvm_value_has_type<void*>(codeBlock));
            m_valuePreserver.Preserve(x_codeBlock, codeBlock);
        }
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
            llvm::Value* val = CreateConstantPlaceholderForOperand(operand->OperandOrdinal(),
                                                                   operand->GetSourceValueFullRepresentationType(ctx),
                                                                   operand->ValueLowerBound(),
                                                                   operand->ValueUpperBound(),
                                                                   currentBlock);
            opcodeValues.push_back(val);
        }
        else
        {
            opcodeValues.push_back(operand->GetOperandValueFromBaselineJitSlowPathData(this, currentBlock));
        }
    }

    if (m_bytecodeDef->m_hasOutputValue)
    {
        Value* outputSlot;
        if (!isNonJitSlowPath)
        {
            outputSlot = CreateConstantPlaceholderForOperand(100 /*operandOrd*/,
                                                             llvm_type_of<uint64_t>(ctx),
                                                             0 /*valueLowerBound*/,
                                                             1048576 /*valueUpperBound*/,
                                                             currentBlock);
        }
        else
        {
            outputSlot = m_bytecodeDef->m_outputOperand->GetOperandValueFromBaselineJitSlowPathData(this, currentBlock);
        }
        ReleaseAssert(llvm_value_has_type<uint64_t>(outputSlot));
        m_valuePreserver.Preserve(x_outputSlot, outputSlot);
        outputSlot->setName(x_outputSlot);
    }

    {
        Value* fallthroughTarget;
        if (!isNonJitSlowPath)
        {
            fallthroughTarget = CreateConstantPlaceholderForOperand(101 /*operandOrd*/,
                                                                    llvm_type_of<void*>(ctx),
                                                                    1 /*valueLowerBound*/,
                                                                    m_stencilRcInserter.GetLowAddrRangeUB(),
                                                                    currentBlock);
        }
        else
        {
            fallthroughTarget = m_bytecodeDef->GetFallthroughCodePtrForBaselineJit(GetJitSlowPathData(), currentBlock);
        }
        ReleaseAssert(llvm_value_has_type<void*>(fallthroughTarget));
        m_valuePreserver.Preserve(x_fallthroughDest, fallthroughTarget);
        fallthroughTarget->setName(x_fallthroughDest);
    }

    if (m_bytecodeDef->m_hasConditionalBranchTarget)
    {
        Value* condBrTarget;
        if (!isNonJitSlowPath)
        {
            condBrTarget = CreateConstantPlaceholderForOperand(102 /*operandOrd*/,
                                                               llvm_type_of<void*>(ctx),
                                                               1 /*valueLowerBound*/,
                                                               m_stencilRcInserter.GetLowAddrRangeUB(),
                                                               currentBlock);
        }
        else
        {
            condBrTarget = m_bytecodeDef->GetCondBrTargetCodePtrForBaselineJit(GetJitSlowPathData(), currentBlock);
        }
        ReleaseAssert(llvm_value_has_type<void*>(condBrTarget));
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

void BaselineJitImplCreator::DoLowering(BytecodeIrInfo* bii, const DeegenGlobalBytecodeTraitAccessor& gbta)
{
    using namespace llvm;

    LLVMContext& ctx = m_module->getContext();

    ReleaseAssert(!m_generated);
    m_generated = true;

    ReleaseAssert(m_bytecodeDef->IsBytecodeStructLengthFinalized());

    // Create the wrapper function 'm_wrapper'
    //
    CreateWrapperFunction();
    ReleaseAssert(m_wrapper != nullptr);

    // Inline 'm_impl' into 'm_wrapper'
    //
    if (m_impl->hasFnAttribute(Attribute::AttrKind::NoInline))
    {
        m_impl->removeFnAttr(Attribute::AttrKind::NoInline);
    }
    m_impl->addFnAttr(Attribute::AttrKind::AlwaysInline);
    m_impl->setLinkage(GlobalValue::InternalLinkage);

    DesugarAndSimplifyLLVMModule(m_module.get(), DesugaringLevel::PerFunctionSimplifyOnlyAggresive);
    RunLLVMDeadGlobalElimination(m_module.get());
    m_impl = nullptr;

    m_valuePreserver.RefreshAfterTransform();

    std::vector<AstInlineCache::BaselineJitLLVMLoweringResult> icLLRes;

    if (IsMainComponent())
    {
        std::vector<AstInlineCache> allIcUses = AstInlineCache::GetAllUseInFunction(m_wrapper);
        size_t globalIcTraitOrdBase = gbta.GetGenericIcEffectTraitBaseOrdinal(m_bytecodeDef->GetBytecodeIdName());
        for (size_t i = 0; i < allIcUses.size(); i++)
        {
            icLLRes.push_back(allIcUses[i].DoLoweringForBaselineJit(this, i, globalIcTraitOrdBase));
            globalIcTraitOrdBase += allIcUses[i].m_totalEffectKinds;
        }
        ReleaseAssert(globalIcTraitOrdBase == gbta.GetGenericIcEffectTraitBaseOrdinal(m_bytecodeDef->GetBytecodeIdName()) + gbta.GetNumTotalGenericIcEffectKinds(m_bytecodeDef->GetBytecodeIdName()));
    }

    // Now we can do the lowerings
    //
    AstBytecodeReturn::DoLoweringForBaselineJIT(this, m_wrapper);
    AstMakeCall::LowerForBaselineJIT(this, m_wrapper);
    AstReturnValueAccessor::LowerForInterpreterOrBaselineJIT(this, m_wrapper);
    DeegenAllSimpleApiLoweringPasses::LowerAllForBaselineJIT(this, m_wrapper);
    AstSlowPath::LowerAllForInterpreterOrBaselineJIT(this, m_wrapper);

    // Lower the remaining function APIs from the generic IC
    //
    if (!IsBaselineJitSlowPath())
    {
        LowerInterpreterGetBytecodePtrInternalAPI(this, m_wrapper);
        AstInlineCache::LowerIcPtrGetterFunctionForBaselineJit(this, m_wrapper);
    }

    // All lowerings are complete.
    // Remove the NoReturn attribute since all pseudo no-return API calls have been replaced to dispatching tail calls
    //
    m_wrapper->removeFnAttr(Attribute::AttrKind::NoReturn);
    if (m_processKind == BytecodeIrComponentKind::Main && m_bytecodeDef->HasQuickeningSlowPath())
    {
        std::string slowPathFnName = BytecodeIrInfo::GetQuickeningSlowPathFuncName(m_bytecodeDef);
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

    DesugarAndSimplifyLLVMModule(m_module.get(), DesugaringLevel::PerFunctionSimplifyOnlyAggresive);

    // Run the stencil runtime constant insertion pass if this function is for the JIT
    //
    if (!IsBaselineJitSlowPath())
    {
        m_stencilRcDefinitions = m_stencilRcInserter.RunOnFunction(m_wrapper);
    }

    // Run LLVM optimization pass
    //
    RunLLVMOptimizePass(m_module.get());

    // Run our homebrewed simple rewrite passes (targetting some insufficiencies of LLVM's optimizations of FCmp) after the main LLVM optimization pass
    //
    DeegenExtraLLVMOptPass_FuseTwoNaNChecksIntoOne(m_module.get());
    DeegenExtraLLVMOptPass_FuseNaNAndCmpCheckIntoOne(m_module.get());

    ReleaseAssert(m_module->getFunction(m_resultFuncName) == m_wrapper);

    // After the optimization pass, change the linkage of everything to 'external' before extraction
    // This is fine: for non-JIT slow path, our caller will fix up the linkage for us.
    // For JIT, we will extract the target function into a stencil, so the linkage doesn't matter.
    //
    for (Function& func : *m_module.get())
    {
        func.setLinkage(GlobalValue::ExternalLinkage);
        func.setComdat(nullptr);
    }

    // Sanity check that 'm_wrapper' is there
    //
    ReleaseAssert(m_module->getFunction(m_resultFuncName) == m_wrapper);
    ReleaseAssert(!m_wrapper->empty());

    // Extract all the IC body functions
    //
    std::unique_ptr<Module> icBodyModule;
    if (icLLRes.size() > 0)
    {
        std::vector<std::string> fnNames;
        for (auto& item : icLLRes)
        {
            ReleaseAssert(m_module->getFunction(item.m_bodyFnName) != nullptr);
            fnNames.push_back(item.m_bodyFnName);
        }
        icBodyModule = ExtractFunctions(m_module.get(), fnNames);
    }

    m_module = ExtractFunction(m_module.get(), m_resultFuncName);

    // After the extract, 'm_wrapper' is invalidated since a new module is returned. Refresh its value.
    //
    m_wrapper = m_module->getFunction(m_resultFuncName);
    ReleaseAssert(m_wrapper != nullptr);

    if (IsBaselineJitSlowPath())
    {
        // For non-JIT slow path, we are done at this point. For JIT, we need to do further processing.
        //
        return;
    }

    // Run the stencil lowering pass in preparation for stencil generation
    //
    DeegenStencilLoweringPass slPass = DeegenStencilLoweringPass::RunIrRewritePhase(m_wrapper, GetRcPlaceholderNameForFallthrough());

    // Compile the function to ASM (.s) file
    // TODO: ideally we should think about properly setting function and loop alignments
    // Currently we simply ignore alignments, which is fine for now since currently our none of our JIT fast path contains loops.
    // But this will break down once any loop is introduced. To fix this, we need to be aware of the aligned BBs and manually generate NOPs.
    //
    std::string asmFile = CompileLLVMModuleToAssemblyFile(
        m_module.get(),
        llvm::Reloc::Static,
        llvm::CodeModel::Small,
        [](TargetOptions& opt) {
            // This is the option that is equivalent to the clang -fdata-sections flag
            // Put each data symbol into a separate data section so our stencil creation pass can produce more efficient result
            //
            opt.DataSections = true;
        });

    // Run the ASM phase of the stencil lowering pass
    //
    slPass.RunAsmRewritePhase(asmFile);
    asmFile = slPass.m_primaryPostTransformAsmFile;

    m_stencilPreTransformAsmFile = std::move(slPass.m_rawInputFileForAudit);

    m_stencilPostTransformAsmFile = asmFile;

    // Compile the final ASM file to object file
    //
    m_stencilObjectFile = CompileAssemblyFileToObjectFile(asmFile, " -fno-pic -fno-pie ");

    // Parse object file into copy-and-patch stencil
    //
    m_stencil = DeegenStencil::ParseMainLogic(ctx, m_stencilObjectFile);

    // Generate generic IC codegen implementations
    // TODO: the logic below should be moved to ast_inline_cache.cpp
    //
    ReleaseAssert(slPass.m_genericIcLoweringResults.size() == icLLRes.size());
    if (icLLRes.size() > 0)
    {
        bool hasFastPathReturnContinuation = false;
        for (auto& rc : bii->m_allRetConts)
        {
            if (!rc->IsReturnContinuationUsedBySlowPathOnly())
            {
                hasFastPathReturnContinuation = true;
            }
        }

        size_t fallthroughPlaceholderOrd = DeegenPlaceholderUtils::FindFallthroughPlaceholderOrd(GetStencilRcDefinitions());

        std::map<uint64_t /*globalOrd*/, uint64_t /*icSize*/> genericIcSizeMap;
        std::vector<std::string> genericIcAuditInfo;
        size_t globalIcTraitOrdBase = gbta.GetGenericIcEffectTraitBaseOrdinal(m_bytecodeDef->GetBytecodeIdName());
        for (size_t icUsageOrd = 0; icUsageOrd < slPass.m_genericIcLoweringResults.size(); icUsageOrd++)
        {
            std::string auditInfo;
            AstInlineCache::BaselineJitAsmLoweringResult& slRes = slPass.m_genericIcLoweringResults[icUsageOrd];
            AstInlineCache::BaselineJitLLVMLoweringResult& llRes = icLLRes[icUsageOrd];
            ReleaseAssert(slRes.m_uniqueOrd == icUsageOrd);

            size_t smcRegionOffset = m_stencil.RetrieveLabelDistanceComputationResult(slRes.m_symbolNameForSMCLabelOffset);
            size_t smcRegionLen = m_stencil.RetrieveLabelDistanceComputationResult(slRes.m_symbolNameForSMCRegionLength);
            size_t icMissSlowPathOffset = m_stencil.RetrieveLabelDistanceComputationResult(slRes.m_symbolNameForIcMissLogicLabelOffset);

            ReleaseAssert(smcRegionLen == 5);

            // Figure out if the IC may qualify for inline slab optimization
            // For now, for simplicity, we only enable inline slab optimization if the SMC region is at the tail position,
            // and the bytecode has no fast path return continuations (in other words, the SMC region can fallthrough
            // directly to the next bytecode). This is only for simplicity because that's all the use case we have for now.
            //
            bool shouldConsiderInlineSlabOpt = !hasFastPathReturnContinuation;
            ReleaseAssert(smcRegionOffset < m_stencil.m_fastPathCode.size());
            ReleaseAssert(smcRegionOffset + smcRegionLen <= m_stencil.m_fastPathCode.size());
            if (smcRegionOffset + smcRegionLen != m_stencil.m_fastPathCode.size())
            {
                shouldConsiderInlineSlabOpt = false;
            }

            size_t ultimateInlineSlabSize = static_cast<size_t>(-1);
            size_t inlineSlabIcMissPatchableJumpEndOffsetInFastPath = static_cast<size_t>(-1);
            std::map<uint64_t /*ordInLLRes*/, DeegenStencil> allInlineSlabEffects;

            if (shouldConsiderInlineSlabOpt)
            {
                std::unordered_set<uint64_t /*ordInLLRes*/> effectOrdsWithTailJumpRemoved;
                for (size_t k = 0; k < llRes.m_effectPlaceholderDesc.size(); k++)
                {
                    // TODO: We should attempt to transform the logic so that it fallthroughs to the next bytecode
                    //
                    std::string icLogicObjFile = CompileAssemblyFileToObjectFile(slRes.m_icLogicAsm[k], " -fno-pic -fno-pie ");
                    DeegenStencil icStencil = DeegenStencil::ParseIcLogic(ctx, icLogicObjFile, m_stencil.m_sectionToPdoOffsetMap);

                    if (icStencil.m_privateDataObject.m_bytes.size() > 0)
                    {
                        // The stencil has a data section, don't consider it for inline slab
                        //
                        continue;
                    }

                    // Figure out if the stencil ends with a jump to the next bytecode, if yes, strip it
                    //
                    ReleaseAssert(icStencil.m_icPathCode.size() >= 5);
                    if (fallthroughPlaceholderOrd != static_cast<size_t>(-1))
                    {
                        bool found = false;
                        std::vector<uint8_t>& code = icStencil.m_icPathCode;
                        std::vector<RelocationRecord>& relos = icStencil.m_icPathRelos;
                        std::vector<RelocationRecord>::iterator relocToRemove = relos.end();
                        for (auto it = relos.begin(); it != relos.end(); it++)
                        {
                            RelocationRecord& rr = *it;
                            if (rr.m_offset == code.size() - 4 &&
                                (rr.m_relocationType == ELF::R_X86_64_PLT32 || rr.m_relocationType == ELF::R_X86_64_PC32) &&
                                rr.m_symKind == RelocationRecord::SymKind::StencilHole &&
                                rr.m_stencilHoleOrd == fallthroughPlaceholderOrd)
                            {
                                ReleaseAssert(rr.m_addend == -4);
                                ReleaseAssert(!found);
                                found = true;
                                relocToRemove = it;
                            }
                        }
                        if (found)
                        {
                            ReleaseAssert(relocToRemove != relos.end());
                            relos.erase(relocToRemove);
                            ReleaseAssert(code[code.size() - 5] == 0xe9 /*jmp*/);
                            code.resize(code.size() - 5);

                            ReleaseAssert(!effectOrdsWithTailJumpRemoved.count(k));
                            effectOrdsWithTailJumpRemoved.insert(k);
                        }
                    }

                    ReleaseAssert(!allInlineSlabEffects.count(k));
                    allInlineSlabEffects[k] = std::move(icStencil);
                }

                // Determine whether we shall enable inline slab, and if yes, its size
                //
                // Currently the heuristic is as follows:
                // 1. At least 60% of the total # effects must fit in the inline slab.
                // 2. We find the smallest size that satisfies (1), say S.
                // 3. If S > maxS, give up. Currently maxS = 64.
                // 4. Set T = min(S + tolerance_factor, maxS), where currently tolerance_factor = 12.
                //    This allows IC slightly larger than the threshold to fit in the inline slab as well.
                // 5. Set final inline slab size to be the max IC size <= T.
                //
                const double x_inlineSlabMinCoverage = 0.6;
                const size_t x_maxAllowedInlineSlabSize = 64;
                const size_t x_inlineSlabExtraToleranceBytes = 12;

                size_t minEffectsToCover = static_cast<size_t>(static_cast<double>(llRes.m_effectPlaceholderDesc.size()) * x_inlineSlabMinCoverage);
                if (minEffectsToCover < 1) { minEffectsToCover = 1; }
                if (allInlineSlabEffects.size() < minEffectsToCover)
                {
                    // The # of IC effects qualified for inline slab is not sufficient, give up
                    //
                    allInlineSlabEffects.clear();
                }
                else
                {
                    std::vector<uint64_t> icSizeList;
                    for (auto& it : allInlineSlabEffects)
                    {
                        icSizeList.push_back(it.second.m_icPathCode.size());
                    }
                    std::sort(icSizeList.begin(), icSizeList.end());

                    ReleaseAssert(icSizeList.size() >= minEffectsToCover);
                    size_t inlineSlabSize = icSizeList[minEffectsToCover - 1];

                    // The inline slab still must at least be able to accomodate a patchable jump
                    // This should be trivially true, because the IC check/branch miss logic is already longer than that
                    //
                    ReleaseAssert(inlineSlabSize >= 5);

                    if (inlineSlabSize > x_maxAllowedInlineSlabSize)
                    {
                        // Give up
                        //
                        allInlineSlabEffects.clear();
                    }
                    else
                    {
                        inlineSlabSize = std::min(inlineSlabSize + x_inlineSlabExtraToleranceBytes, x_maxAllowedInlineSlabSize);

                        size_t maxIcSizeInInlineSlab = 0;
                        for (size_t icSize : icSizeList)
                        {
                            if (icSize <= inlineSlabSize)
                            {
                                maxIcSizeInInlineSlab = std::max(maxIcSizeInInlineSlab, icSize);
                            }
                        }
                        inlineSlabSize = maxIcSizeInInlineSlab;

                        // Remove all IC that won't fit in the inline slab
                        //
                        {
                            std::vector<uint64_t> icIndexToRemove;
                            for (auto& it : allInlineSlabEffects)
                            {
                                if (it.second.m_icPathCode.size() > inlineSlabSize)
                                {
                                    icIndexToRemove.push_back(it.first);
                                }
                            }

                            for (uint64_t indexToRemove : icIndexToRemove)
                            {
                                ReleaseAssert(allInlineSlabEffects.count(indexToRemove));
                                allInlineSlabEffects.erase(allInlineSlabEffects.find(indexToRemove));
                            }
                        }

                        // Sanity check the decision makes sense
                        //
                        ReleaseAssert(inlineSlabSize >= 5);
                        ReleaseAssert(inlineSlabSize <= x_maxAllowedInlineSlabSize);
                        ReleaseAssert(allInlineSlabEffects.size() >= minEffectsToCover);

                        // Pad nops to make the code length equal to the inline slab size
                        //
                        for (auto& it : allInlineSlabEffects)
                        {
                            std::vector<uint8_t>& code = it.second.m_icPathCode;

                            ReleaseAssert(code.size() <= inlineSlabSize);
                            size_t nopBegin = code.size();
                            size_t bytesToFill = inlineSlabSize - nopBegin;
                            code.resize(inlineSlabSize);

                            if (bytesToFill >= 16 && effectOrdsWithTailJumpRemoved.count(it.first))
                            {
                                // The NOP sequence will actually be executed as we removed the tail jump.
                                // Executing >= 16 bytes of NOP might not make sense (I forgot the exact source but I believe
                                // this is the threshold used by GCC). So in that case do a short jump directly.
                                //
                                uint8_t* buf = code.data() + nopBegin;
                                buf[0] = 0xeb;      // imm8 jmp
                                ReleaseAssert(2 <= bytesToFill && bytesToFill <= 129);
                                buf[1] = SafeIntegerCast<uint8_t>(bytesToFill - 2);
                                nopBegin += 2;
                                bytesToFill -= 2;
                            }

                            FillAddressRangeWithX64MultiByteNOPs(code.data() + nopBegin, bytesToFill);
                        }

                        // Figure out the location of the patchable jump for IC miss in the inline slab
                        // They are guaranteed to have the same offset in any IC effect due to how we generated the logic
                        //
                        size_t patchableJmpOperandLocInIcPath = static_cast<size_t>(-1);
                        for (auto& it : allInlineSlabEffects)
                        {
                            std::vector<RelocationRecord>& relocs = it.second.m_icPathRelos;
                            size_t offset = static_cast<size_t>(-1);
                            for (RelocationRecord& rr : relocs)
                            {
                                if (rr.m_symKind == RelocationRecord::SymKind::StencilHole &&
                                    rr.m_stencilHoleOrd == CP_PLACEHOLDER_IC_MISS_DEST)
                                {
                                    ReleaseAssert(rr.m_relocationType == ELF::R_X86_64_PLT32 || rr.m_relocationType == ELF::R_X86_64_PC32);
                                    ReleaseAssert(rr.m_addend == -4);
                                    ReleaseAssert(offset == static_cast<size_t>(-1));
                                    offset = rr.m_offset;
                                    ReleaseAssert(offset != static_cast<size_t>(-1));
                                }
                            }
                            ReleaseAssert(offset != static_cast<size_t>(-1));
                            if (patchableJmpOperandLocInIcPath == static_cast<size_t>(-1))
                            {
                                patchableJmpOperandLocInIcPath = offset;
                            }
                            else
                            {
                                ReleaseAssert(patchableJmpOperandLocInIcPath == offset);
                            }
                        }
                        ReleaseAssert(patchableJmpOperandLocInIcPath != static_cast<size_t>(-1));

                        // Pad NOPs for the main logic's SMC region
                        // Since we already asserted that the SMC region is at the end of the function,
                        // it's simply populating NOPs at the end, as it won't break any jump targets etc
                        //
                        {
                            std::vector<uint8_t>& code = m_stencil.m_fastPathCode;

                            ReleaseAssert(inlineSlabSize >= smcRegionLen);
                            ReleaseAssert(smcRegionOffset + smcRegionLen == code.size());

                            size_t numBytesToPad = inlineSlabSize - smcRegionLen;
                            size_t offsetStart = code.size();
                            code.resize(offsetStart + numBytesToPad);
                            FillAddressRangeWithX64MultiByteNOPs(code.data() + offsetStart, numBytesToPad);

                            smcRegionLen = inlineSlabSize;
                            ReleaseAssert(smcRegionOffset + smcRegionLen == code.size());
                        }

                        // Declare success
                        //
                        ultimateInlineSlabSize = inlineSlabSize;
                        inlineSlabIcMissPatchableJumpEndOffsetInFastPath = smcRegionOffset + patchableJmpOperandLocInIcPath + 4;

                        ReleaseAssert(smcRegionOffset < inlineSlabIcMissPatchableJumpEndOffsetInFastPath - 4);
                        ReleaseAssert(inlineSlabIcMissPatchableJumpEndOffsetInFastPath <= smcRegionOffset + smcRegionLen);
                    }
                }
            }

            ReleaseAssert(llRes.m_effectPlaceholderDesc.size() == slRes.m_icLogicAsm.size());

            AstInlineCache::InlineSlabInfo inlineSlabInfo {
                .m_hasInlineSlab = (ultimateInlineSlabSize != static_cast<size_t>(-1)),
                .m_smcRegionOffset = smcRegionOffset,
                .m_smcRegionLength = smcRegionLen,
                .m_inlineSlabPatchableJumpEndOffsetInFastPath = inlineSlabIcMissPatchableJumpEndOffsetInFastPath,
                .m_icMissLogicOffsetInSlowPath = icMissSlowPathOffset
            };

            // Generate codegen logic for each inline slab
            //
            std::string inlineSlabAuditLog;
            ReleaseAssertIff(ultimateInlineSlabSize != static_cast<size_t>(-1), !allInlineSlabEffects.empty());
            for (size_t k = 0; k < llRes.m_effectPlaceholderDesc.size(); k++)
            {
                size_t icGlobalOrd = llRes.m_effectPlaceholderDesc[k].m_globalOrd;
                std::string isIcQualifyForInlineSlabFnName = x_jit_check_generic_ic_fits_in_inline_slab_placeholder_fn_prefix + std::to_string(icGlobalOrd);
                Function* isIcQualifyForInlineSlabFn = icBodyModule->getFunction(isIcQualifyForInlineSlabFnName);
                ReleaseAssert(isIcQualifyForInlineSlabFn != nullptr);
                ReleaseAssert(isIcQualifyForInlineSlabFn->empty());
                ReleaseAssert(llvm_type_has_type<bool>(isIcQualifyForInlineSlabFn->getReturnType()) && isIcQualifyForInlineSlabFn->arg_size() == 1);

                isIcQualifyForInlineSlabFn->setLinkage(GlobalValue::InternalLinkage);
                isIcQualifyForInlineSlabFn->addFnAttr(Attribute::AlwaysInline);

                if (allInlineSlabEffects.count(k))
                {
                    DeegenStencil& icStencil = allInlineSlabEffects[k];

                    ReleaseAssert(ultimateInlineSlabSize != static_cast<size_t>(-1) && inlineSlabIcMissPatchableJumpEndOffsetInFastPath != static_cast<size_t>(-1));

                    // Create the implementation for isIcQualifyForInlineSlabFn
                    // It should check if the inline slab is already used, and do updates correspondingly.
                    //
                    {
                        BasicBlock* bb = BasicBlock::Create(ctx, "", isIcQualifyForInlineSlabFn);
                        Value* icSite = isIcQualifyForInlineSlabFn->getArg(0);
                        ReleaseAssert(llvm_value_has_type<void*>(icSite));
                        Value* isInlineSlabUsedAddr = GetElementPtrInst::CreateInBounds(
                            llvm_type_of<uint8_t>(ctx), icSite,
                            {
                                CreateLLVMConstantInt<uint64_t>(ctx, offsetof_member_v<&JitGenericInlineCacheSite::m_isInlineSlabUsed>)
                            }, "", bb);
                        static_assert(std::is_same_v<uint8_t, typeof_member_t<&JitGenericInlineCacheSite::m_isInlineSlabUsed>>);
                        Value* isInlineSlabUsedU8 = new LoadInst(llvm_type_of<uint8_t>(ctx), isInlineSlabUsedAddr, "", false /*isVolatile*/, Align(1), bb);
                        Value* isInlineSlabUsed = new ICmpInst(*bb, ICmpInst::ICMP_NE, isInlineSlabUsedU8, CreateLLVMConstantInt<uint8_t>(ctx, 0));

                        BasicBlock* trueBB = BasicBlock::Create(ctx, "", isIcQualifyForInlineSlabFn);
                        BasicBlock* falseBB = BasicBlock::Create(ctx, "", isIcQualifyForInlineSlabFn);
                        BranchInst::Create(trueBB, falseBB, isInlineSlabUsed, bb);

                        // If the inline slab is already used, we can't do anything
                        //
                        ReturnInst::Create(ctx, CreateLLVMConstantInt<bool>(ctx, false), trueBB);

                        // Otherwise, we should set it to used, increment # of IC, and return true
                        //
                        new StoreInst(CreateLLVMConstantInt<uint8_t>(ctx, 1), isInlineSlabUsedAddr, falseBB);
                        Value* numExistingIcAddr = GetElementPtrInst::CreateInBounds(
                            llvm_type_of<uint8_t>(ctx), icSite,
                            {
                                CreateLLVMConstantInt<uint64_t>(ctx, offsetof_member_v<&JitGenericInlineCacheSite::m_numEntries>)
                            }, "", falseBB);
                        static_assert(std::is_same_v<uint8_t, typeof_member_t<&JitGenericInlineCacheSite::m_numEntries>>);
                        Value* numExistingIc = new LoadInst(llvm_type_of<uint8_t>(ctx), numExistingIcAddr, "", false /*isVolatile*/, Align(1), falseBB);
                        Value* newNumExistingIcVal = BinaryOperator::Create(BinaryOperator::Add, numExistingIc, CreateLLVMConstantInt<uint8_t>(ctx, 1), "", falseBB);
                        new StoreInst(newNumExistingIcVal, numExistingIcAddr, falseBB);

                        ReturnInst::Create(ctx, CreateLLVMConstantInt<bool>(ctx, true), falseBB);
                    }

                    {
                        // Create the implementation of the codegen function
                        //
                        AstInlineCache::BaselineJitCodegenResult cgRes = AstInlineCache::CreateJitIcCodegenImplementation(
                            this,
                            llRes.m_effectPlaceholderDesc[k] /*icInfo*/,
                            icStencil,
                            inlineSlabInfo,
                            icUsageOrd,
                            true /*isCodegenForInlineSlab*/);

                        ReleaseAssert(cgRes.m_icSize == smcRegionLen);

                        ReleaseAssert(icBodyModule.get() != nullptr);
                        ReleaseAssert(icBodyModule->getFunction(cgRes.m_resultFnName) != nullptr);
                        ReleaseAssert(icBodyModule->getFunction(cgRes.m_resultFnName)->empty());

                        // Link the codegen impl function into the bodyFn module
                        // Do not reuse linker because we are also modifying the module by ourselves
                        //
                        {
                            Linker linker(*icBodyModule.get());
                            ReleaseAssert(linker.linkInModule(std::move(cgRes.m_module)) == false);
                        }

                        // Dump audit log
                        //
                        {
                            ReleaseAssert(icGlobalOrd >= globalIcTraitOrdBase);
                            std::string disAsmForAuditPfx = "# IC Effect Kind #" + std::to_string(icGlobalOrd - globalIcTraitOrdBase);
                            disAsmForAuditPfx += " (inline slab version, global ord = " + std::to_string(icGlobalOrd) + ", code len = " + std::to_string(cgRes.m_icSize) + "):\n\n";
                            inlineSlabAuditLog += disAsmForAuditPfx + cgRes.m_disasmForAudit;
                        }
                    }
                }
                else
                {
                    // This IC effect does not qualify for inline slab optimization
                    //
                    BasicBlock* bb = BasicBlock::Create(ctx, "", isIcQualifyForInlineSlabFn);
                    ReturnInst::Create(ctx, CreateLLVMConstantInt<bool>(ctx, false), bb);
                }
            }

            // Generate logic for each outlined IC case
            //
            ReleaseAssert(icBodyModule.get() != nullptr);
            Linker linker(*icBodyModule.get());

            for (size_t k = 0; k < llRes.m_effectPlaceholderDesc.size(); k++)
            {
                std::string icLogicObjFile = CompileAssemblyFileToObjectFile(slRes.m_icLogicAsm[k], " -fno-pic -fno-pie ");
                DeegenStencil icStencil = DeegenStencil::ParseIcLogic(ctx, icLogicObjFile, m_stencil.m_sectionToPdoOffsetMap);

                AstInlineCache::BaselineJitCodegenResult cgRes = AstInlineCache::CreateJitIcCodegenImplementation(
                    this,
                    llRes.m_effectPlaceholderDesc[k] /*icInfo*/,
                    icStencil,
                    inlineSlabInfo,
                    icUsageOrd,
                    false /*isCodegenForInlineSlab*/);

                ReleaseAssert(icBodyModule->getFunction(cgRes.m_resultFnName) != nullptr);
                ReleaseAssert(icBodyModule->getFunction(cgRes.m_resultFnName)->empty());
                ReleaseAssert(linker.linkInModule(std::move(cgRes.m_module)) == false);

                size_t icGlobalOrd = llRes.m_effectPlaceholderDesc[k].m_globalOrd;
                ReleaseAssert(icGlobalOrd >= globalIcTraitOrdBase);
                std::string disAsmForAuditPfx = "# IC Effect Kind #" + std::to_string(icGlobalOrd - globalIcTraitOrdBase);
                disAsmForAuditPfx += " (global ord = " + std::to_string(icGlobalOrd) + ", code len = " + std::to_string(cgRes.m_icSize) + "):\n\n";
                auditInfo += disAsmForAuditPfx + cgRes.m_disasmForAudit;

                ReleaseAssert(!genericIcSizeMap.count(icGlobalOrd));
                genericIcSizeMap[icGlobalOrd] = cgRes.m_icSize;
            }

            auditInfo += inlineSlabAuditLog;

            auditInfo += "# SMC region offset = " + std::to_string(smcRegionOffset) + ", length = " + std::to_string(smcRegionLen) + "\n";
            auditInfo += "# IC miss slow path offset = " + std::to_string(icMissSlowPathOffset) + "\n";
            auditInfo += std::string("# Has Inline Slab = ") + (inlineSlabInfo.m_hasInlineSlab ? "true" : "false");
            if (inlineSlabInfo.m_hasInlineSlab)
            {
                ReleaseAssert(inlineSlabInfo.m_inlineSlabPatchableJumpEndOffsetInFastPath > inlineSlabInfo.m_smcRegionOffset);
                auditInfo += ", Inline Slab Patchable Jump End Offset = " + std::to_string(inlineSlabInfo.m_inlineSlabPatchableJumpEndOffsetInFastPath - inlineSlabInfo.m_smcRegionOffset);
            }
            auditInfo += "\n\n";

            globalIcTraitOrdBase += llRes.m_effectPlaceholderDesc.size();
            genericIcAuditInfo.push_back(auditInfo);
        }
        ReleaseAssert(globalIcTraitOrdBase == gbta.GetGenericIcEffectTraitBaseOrdinal(m_bytecodeDef->GetBytecodeIdName()) + gbta.GetNumTotalGenericIcEffectKinds(m_bytecodeDef->GetBytecodeIdName()));

        for (auto& it : genericIcSizeMap)
        {
            m_genericIcLoweringResult.m_icTraitInfo.push_back({
                .m_ordInTraitTable = it.first,
                .m_allocationLength = it.second
            });
        }

        // Change all the IC codegen implementations to internal and always_inline
        //
        for (Function& fn : *icBodyModule.get())
        {
            if (fn.getName().startswith(x_jit_codegen_ic_impl_placeholder_fn_prefix) && !fn.empty())
            {
                ReleaseAssert(fn.hasExternalLinkage());
                ReleaseAssert(!fn.hasFnAttribute(Attribute::NoInline));
                fn.setLinkage(GlobalValue::InternalLinkage);
                fn.addFnAttr(Attribute::AlwaysInline);
            }
        }

        RunLLVMOptimizePass(icBodyModule.get());

        // Assert that all the IC codegen implementation functions are gone:
        // they should either be inlined, or never used and eliminated (for IC that does not qualify for inline slab)
        //
        // And assert that all the check-can-use-inline-slab functions are gone: they must have been inlined
        //
        for (Function& fn : *icBodyModule.get())
        {
            ReleaseAssert(!fn.getName().startswith(x_jit_codegen_ic_impl_placeholder_fn_prefix));
            ReleaseAssert(!fn.getName().startswith(x_jit_check_generic_ic_fits_in_inline_slab_placeholder_fn_prefix));
        }

        // Generate the final generic IC audit log
        //
        {
            std::string finalAuditLog;
            ReleaseAssert(genericIcAuditInfo.size() == icLLRes.size());
            for (size_t i = 0; i < genericIcAuditInfo.size(); i++)
            {
                if (genericIcAuditInfo.size() > 1)
                {
                    finalAuditLog += "# IC Info (IC body = " + icLLRes[i].m_bodyFnName + ")\n\n";
                }
                finalAuditLog += genericIcAuditInfo[i];
            }
            {
                std::unique_ptr<Module> clonedModule = CloneModule(*icBodyModule.get());
                finalAuditLog += CompileLLVMModuleToAssemblyFile(clonedModule.get(), Reloc::Static, CodeModel::Small);
            }
            m_genericIcLoweringResult.m_disasmForAudit = finalAuditLog;
        }

        m_genericIcLoweringResult.m_icBodyModule = std::move(icBodyModule);
    }

    m_callIcInfo = slPass.m_callIcLoweringResults;

    // Note that we cannot further lower the main logic stencil to concrete copy-and-patch logic yet, because at this stage
    // we cannot determine if we are allowed to eliminate the tail jump to fallthrough. So our lowering ends here.
    //
    return;
}

}   // namespace dast
