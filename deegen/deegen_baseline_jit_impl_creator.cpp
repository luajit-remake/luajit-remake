#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_interpreter_function_interface.h"
#include "deegen_ast_slow_path.h"
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

llvm::CallInst* WARN_UNUSED BaselineJitImplCreator::CreateOrGetConstantPlaceholderForOperand(size_t ordinal, llvm::Type* operandTy, int64_t lb, int64_t ub, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(operandTy->getContext(), insertAtEnd);
    CallInst* res = CreateOrGetConstantPlaceholderForOperand(ordinal, operandTy, lb, ub, dummy);
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
}

BaselineJitImplCreator::BaselineJitImplCreator(BaselineJitImplCreator::SlowPathReturnContinuationTag, BytecodeIrComponent& bic)
    : BaselineJitImplCreator(bic)
{
    ReleaseAssert(m_processKind == BytecodeIrComponentKind::ReturnContinuation);
    m_isSlowPathReturnContinuation = true;
}

std::string WARN_UNUSED DeegenPlaceholderUtils::FindFallthroughPlaceholderSymbolName(std::vector<CPRuntimeConstantNodeBase*>& rcDef)
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
    if (!found) { return ""; }
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

void BaselineJitImplCreator::DoLowering()
{
    using namespace llvm;

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

    // Now we can do the lowerings
    //
    AstBytecodeReturn::DoLoweringForBaselineJIT(this, m_wrapper);
    AstMakeCall::LowerForBaselineJIT(this, m_wrapper);
    AstReturnValueAccessor::LowerForInterpreterOrBaselineJIT(this, m_wrapper);
    DeegenAllSimpleApiLoweringPasses::LowerAllForBaselineJIT(this, m_wrapper);
    AstSlowPath::LowerAllForInterpreterOrBaselineJIT(this, m_wrapper);

#if 0

    LowerGetBytecodeMetadataPtrAPI();
    LowerInterpreterGetBytecodePtrInternalAPI(this, m_wrapper);
#endif

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
    m_stencil = DeegenStencil::ParseMainLogic(m_module->getContext(), m_stencilObjectFile);

    m_callIcInfo = slPass.m_callIcLoweringResults;

    // Note that we cannot further lower the main logic stencil to concrete copy-and-patch logic yet, because at this stage
    // we cannot determine if we are allowed to eliminate the tail jump to fallthrough. So our lowering ends here.
    //
    return;
}

}   // namespace dast
