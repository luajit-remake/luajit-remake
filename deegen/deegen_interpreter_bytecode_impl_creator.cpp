#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_bytecode_operand.h"
#include "deegen_ast_make_call.h"
#include "deegen_ast_return.h"
#include "deegen_ast_return_value_accessor.h"
#include "deegen_interpreter_function_interface.h"
#include "deegen_ast_throw_error.h"
#include "deegen_options.h"
#include "tvalue_typecheck_optimization.h"
#include "deegen_ast_simple_lowering_utils.h"
#include "tag_register_optimization.h"
#include "llvm_fcmp_extra_optimizations.h"
#include "deegen_ast_inline_cache.h"
#include "deegen_ast_slow_path.h"
#include "deegen_type_based_hcs_helper.h"
#include "vm_base_pointer_optimization.h"

#include "llvm/Linker/Linker.h"

#include "runtime_utils.h"

namespace dast {

void InterpreterBytecodeImplCreator::CreateWrapperFunction()
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

    // Set up the function attributes
    // TODO: add alias attributes to parameters
    //
    m_wrapper->addFnAttr(Attribute::AttrKind::NoReturn);
    m_wrapper->addFnAttr(Attribute::AttrKind::NoUnwind);
    if (m_processKind == BytecodeIrComponentKind::QuickeningSlowPath || m_processKind == BytecodeIrComponentKind::SlowPath)
    {
        m_wrapper->addFnAttr(Attribute::AttrKind::NoInline);
    }
    CopyFunctionAttributes(m_wrapper /*dst*/, m_impl /*src*/);

    BasicBlock* entryBlock = BasicBlock::Create(ctx, "", m_wrapper);
    BasicBlock* currentBlock = entryBlock;

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

        Value* curBytecode = m_wrapper->getArg(2);
        curBytecode->setName(x_curBytecode);
        m_valuePreserver.Preserve(x_curBytecode, curBytecode);

        Value* codeBlock = m_wrapper->getArg(5);
        codeBlock->setName(x_interpreterCodeBlock);
        m_valuePreserver.Preserve(x_interpreterCodeBlock, codeBlock);
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

        // Note that the 'm_callerBytecodePtr' is stored in the callee's stack frame header, so we should pass 'calleeStackBase' here
        //
        Instruction* bytecodePtr = CreateCallToDeegenCommonSnippet(GetModule(), "GetBytecodePtrAfterReturnFromCall", { calleeStackBase }, currentBlock);
        ReleaseAssert(llvm_value_has_type<void*>(bytecodePtr));

        Instruction* codeblock = CreateCallToDeegenCommonSnippet(GetModule(), "GetCodeBlockFromStackBase", { GetStackBase() }, currentBlock);
        ReleaseAssert(llvm_value_has_type<void*>(codeblock));

        m_valuePreserver.Preserve(x_interpreterCodeBlock, codeblock);
        m_valuePreserver.Preserve(x_curBytecode, bytecodePtr);
    }

    if (m_processKind == BytecodeIrComponentKind::Main && m_bytecodeDef->m_isInterpreterToBaselineJitOsrEntryPoint && x_allow_interpreter_tier_up_to_baseline_jit)
    {
        BasicBlock* tierUpBB = BasicBlock::Create(ctx, "", m_wrapper);
        {
            // Set up the tier up BB implementation
            //
            Function* tierUpFn = InterpreterFunctionInterface::CreateFunction(m_module.get(), "__deegen_interpreter_osr_entry_into_baseline_jit");
            tierUpFn->addFnAttr(Attribute::NoUnwind);
            Instruction* dummyInst = new UnreachableInst(ctx, tierUpBB);
            InterpreterFunctionInterface::CreateDispatchToBytecode(
                tierUpFn,
                GetCoroutineCtx(),
                GetStackBase(),
                GetCurBytecode(),
                GetInterpreterCodeBlock(),
                dummyInst /*insertBefore*/);
            dummyInst->eraseFromParent();
        }

        // Check for osr entry
        //
        Value* tierUpCounter = CreateCallToDeegenCommonSnippet(m_module.get(), "GetInterpreterTierUpCounter", { GetInterpreterCodeBlock() }, currentBlock);
        ReleaseAssert(llvm_value_has_type<int64_t>(tierUpCounter));

        Value* shouldTierUp = new ICmpInst(*currentBlock, ICmpInst::ICMP_SLT, tierUpCounter, CreateLLVMConstantInt<int64_t>(ctx, 0));
        Function* expectIntrin = Intrinsic::getDeclaration(m_module.get(), Intrinsic::expect, { Type::getInt1Ty(ctx) });
        shouldTierUp = CallInst::Create(expectIntrin, { shouldTierUp, CreateLLVMConstantInt<bool>(ctx, false) }, "", currentBlock);

        BasicBlock* normalExecutionBB = BasicBlock::Create(ctx, "", m_wrapper);
        BranchInst::Create(tierUpBB, normalExecutionBB, shouldTierUp, currentBlock);
        currentBlock = normalExecutionBB;
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

    ReleaseAssert(m_bytecodeDef->IsBytecodeStructLengthFinalized());
    if (m_bytecodeDef->HasBytecodeMetadata())
    {
        if (m_bytecodeDef->IsBytecodeMetadataInlined())
        {
            ReleaseAssert(!m_bytecodeDef->m_inlinedMetadata->SupportsGetOperandValueFromBytecodeStruct());
            Value* metadataPtr = m_bytecodeDef->m_inlinedMetadata->EmitUsageValueFromBytecodeValue(this, currentBlock, nullptr /*bytecodeValue*/);
            m_valuePreserver.Preserve(x_metadataPtr, metadataPtr);
            metadataPtr->setName(x_metadataPtr);
        }
        else
        {
            Value* metadataPtrOffset32 = m_bytecodeDef->m_metadataPtrOffset->GetOperandValueFromBytecodeStruct(this, currentBlock);
            ReleaseAssert(llvm_value_has_type<uint32_t>(metadataPtrOffset32));
            Value* offset64 = new ZExtInst(metadataPtrOffset32, llvm_type_of<uint64_t>(ctx), "", currentBlock);
            GetElementPtrInst* metadataPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), GetInterpreterCodeBlock(), { offset64 }, "", currentBlock);
            m_valuePreserver.Preserve(x_metadataPtr, metadataPtr);
            metadataPtr->setName(x_metadataPtr);
        }
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

void InterpreterBytecodeImplCreator::LowerGetBytecodeMetadataPtrAPI()
{
    using namespace llvm;
    Function* fn = m_module->getFunction(x_get_bytecode_metadata_ptr_placeholder_fn_name);
    if (fn != nullptr)
    {
        std::unordered_set<CallInst*> instructionToReplace;
        for (Use& u : fn->uses())
        {
            User* usr = u.getUser();
            ReleaseAssert(isa<CallInst>(usr));
            CallInst* ci = cast<CallInst>(usr);
            ReleaseAssert(&u == &ci->getCalledOperandUse());
            ReleaseAssert(llvm_value_has_type<void*>(ci));
            ReleaseAssert(!instructionToReplace.count(ci));
            instructionToReplace.insert(ci);
        }
        if (!instructionToReplace.empty())
        {
            Value* mdPtr = m_valuePreserver.Get(x_metadataPtr);
            ReleaseAssert(llvm_value_has_type<void*>(mdPtr));
            for (CallInst* ci : instructionToReplace)
            {
                ReleaseAssert(ci->getParent() != nullptr);
                ReleaseAssert(ci->getParent()->getParent() == m_wrapper);
                ci->replaceAllUsesWith(mdPtr);
                ci->eraseFromParent();
            }
        }
        ReleaseAssert(fn->use_empty());
    }
}

InterpreterBytecodeImplCreator::InterpreterBytecodeImplCreator(BytecodeIrComponent& bic)
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
    m_mayFallthroughToNextBytecode = false;
}

std::unique_ptr<InterpreterBytecodeImplCreator> WARN_UNUSED InterpreterBytecodeImplCreator::LowerOneComponent(BytecodeIrComponent& bic)
{
    std::unique_ptr<InterpreterBytecodeImplCreator> ifi = std::make_unique<InterpreterBytecodeImplCreator>(bic);
    ifi->DoLowering();
    return ifi;
}

void InterpreterBytecodeImplCreator::DoLowering()
{
    using namespace llvm;

    ReleaseAssert(!m_generated);
    m_generated = true;

    // DoLowering() is alwasy called after the bytecode struct has been finalized.
    // Having decided the final bytecode metadata struct layout, we can lower all the metadata struct element getters.
    // Again, this relies on the fact that the metadata struct layout is solely determined by the Main processor.
    //
    ReleaseAssert(m_bytecodeDef->IsBytecodeStructLengthFinalized());
    if (m_bytecodeDef->HasBytecodeMetadata())
    {
        m_bytecodeDef->m_bytecodeMetadataMaybeNull->LowerAll(m_module.get());
    }

    // Create the wrapper function 'm_wrapper'
    // TODO: currently this happens after we decide whether the bytecode has metadata, because CreateWrapperFunction()
    // also decodes the metadata pointer. However, this is not ideal. In theory, the extra information in the wrapper
    // function (e.g., certain bytecode operands are constant) could eliminate calls in the bytecode, thus making
    // some of the metadata unnecessary, or even eliminate the metadata altogether.
    // This should be fixed by first create the wrapper and run desugaring, then decide the metadata by inspecting the
    // wrapper, and finally emit logic in the wrapper to decode the metadata pointer if it exists. However, we are
    // leaving it as is for now because we don't have such use cases.
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

    m_mayFallthroughToNextBytecode = AstBytecodeReturn::CheckMayFallthroughToNextBytecode(m_wrapper);

    {
        m_mayMakeTailCall = false;
        std::vector<AstMakeCall> allCalls = AstMakeCall::GetAllUseInFunction(m_wrapper);
        for (AstMakeCall& callInfo : allCalls)
        {
            if (callInfo.m_isMustTailCall)
            {
                m_mayMakeTailCall = true;
            }
        }
    }

    // Now we can do the lowerings
    //
    AstBytecodeReturn::LowerForInterpreter(this, m_wrapper);
    AstMakeCall::LowerForInterpreter(this, m_wrapper);
    AstReturnValueAccessor::LowerForInterpreterOrBaselineJIT(this, m_wrapper);
    DeegenAllSimpleApiLoweringPasses::LowerAllForInterpreter(this, m_wrapper);
    LowerGetBytecodeMetadataPtrAPI();
    LowerInterpreterGetBytecodePtrInternalAPI(this, m_wrapper);
    AstSlowPath::LowerAllForInterpreterOrBaselineJIT(this, m_wrapper);

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

    // Run LLVM optimization pass
    //
    RunLLVMOptimizePass(m_module.get());

    // Run our homebrewed simple rewrite passes (targetting some insufficiencies of LLVM's optimizations of FCmp) after the main LLVM optimization pass
    //
    DeegenExtraLLVMOptPass_FuseTwoNaNChecksIntoOne(m_module.get());
    DeegenExtraLLVMOptPass_FuseNaNAndCmpCheckIntoOne(m_module.get());

    // Now, run the vm base pointer optimization pass
    //
    RunVMBasePointerOptimizationPass(m_wrapper);

    // After the optimization pass, change the linkage of everything to 'external' before extraction
    // This is fine: our caller will fix up the linkage for us.
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

    // Extract the functions if m_processKind is not Main
    // For the main component, our caller will do the extraction.
    //
    if (m_processKind != BytecodeIrComponentKind::Main)
    {
        m_module = ExtractFunction(m_module.get(), m_resultFuncName);

        // After the extract, 'm_wrapper' is invalidated since a new module is returned. Refresh its value.
        //
        m_wrapper = m_module->getFunction(m_resultFuncName);
        ReleaseAssert(m_wrapper != nullptr);
    }
}

std::unique_ptr<llvm::Module> WARN_UNUSED InterpreterBytecodeImplCreator::DoLoweringForAll(BytecodeIrInfo& bi)
{
    using namespace llvm;

    BytecodeVariantDefinition* bytecodeDef = bi.m_bytecodeDef;

    size_t finalLength = bytecodeDef->GetTentativeBytecodeStructLength();
    for (BytecodeVariantDefinition* sameLengthConstraintVariant : bytecodeDef->m_sameLengthConstraintList)
    {
        size_t otherLength = sameLengthConstraintVariant->GetTentativeBytecodeStructLength();
        finalLength = std::max(finalLength, otherLength);
    }
    bytecodeDef->FinalizeBytecodeStructLength(finalLength);

    // Figure out the return continuations directly or transitively used by the main function.
    // These return continuations are considered hot, and will be put into the interpreter's hot code section
    // The other return continuations (which are used by calls that can only happen in the slow paths) will
    // be put into the cold code section.
    //
    std::unordered_set<std::string> fnNamesOfAllReturnContinuationsUsedByMainFn;
    ReturnContinuationAndSlowPathFinder rcFinder(bi.m_interpreterMainComponent->m_impl, true /*ignoreSlowPaths*/);
    for (Function* func : rcFinder.m_returnContinuations)
    {
        std::string rcImplName = func->getName().str();
        ReleaseAssert(rcImplName.ends_with("_impl"));
        std::string rcFinalName = rcImplName.substr(0, rcImplName.length() - strlen("_impl"));
        ReleaseAssert(!fnNamesOfAllReturnContinuationsUsedByMainFn.count(rcFinalName));
        fnNamesOfAllReturnContinuationsUsedByMainFn.insert(rcFinalName);
    }

    std::unique_ptr<Module> module;

    // Records some per-component info that we want to aggregate
    //
    struct PerComponentInfo
    {
        bool m_mayFallthroughToNextBytecode;
        bool m_mayMakeTailCall;
    };

    std::unordered_map<std::string /*funcName*/, PerComponentInfo> componentInfoMap;

    // Process the main component
    //
    {
        std::unique_ptr<InterpreterBytecodeImplCreator> mainComponent = InterpreterBytecodeImplCreator::LowerOneComponent(*bi.m_interpreterMainComponent.get());

        ReleaseAssert(!componentInfoMap.count(mainComponent->m_resultFuncName));
        componentInfoMap[mainComponent->m_resultFuncName] = PerComponentInfo {
            .m_mayFallthroughToNextBytecode = mainComponent->m_mayFallthroughToNextBytecode,
            .m_mayMakeTailCall = mainComponent->m_mayMakeTailCall
        };

        // Extract the necessary functions from the main component
        // We need to extract the result function and any IC body functions that didn't get inlined
        //
        std::vector<std::string> extractTargets;
        ReleaseAssert(mainComponent->m_module->getFunction(mainComponent->m_resultFuncName) != nullptr);
        extractTargets.push_back(mainComponent->m_resultFuncName);
        for (auto& icBodyFnName : bi.m_icBodyNames)
        {
            Function* icBodyFn = mainComponent->m_module->getFunction(icBodyFnName);
            if (icBodyFn != nullptr)
            {
                extractTargets.push_back(icBodyFnName);
                // I'm not certain if the IC body shall be put into the hot code section or the cold code section.
                // For now, let's not bother with this, and just put them in the normal code section like the other runtime functions.
                //
                ReleaseAssert(!icBodyFn->hasSection());
            }
            else
            {
                ReleaseAssert(mainComponent->m_module->getNamedValue(icBodyFnName) == nullptr);
            }
        }
        module = ExtractFunctions(mainComponent->m_module.get(), extractTargets);

        // Add section attribute to the main entry function
        //
        Function* mainEntryFn = module->getFunction(mainComponent->m_resultFuncName);
        ReleaseAssert(mainEntryFn != nullptr);

        ReleaseAssert(!mainEntryFn->hasSection());
        mainEntryFn->setSection(x_hot_code_section_name);
    }

    // Link in all the sub-components
    //
    // We also assign hot/cold section for each function.
    // The main function and any return continuations directly/transitively used by the main function are hot,
    // and everything else are cold.
    //
    // Link in the FuseICIntoInterpreterOpcode specializations, if any
    //
    ReleaseAssert(bi.m_affliatedBytecodeFnNames.size() == bi.m_fusedICs.size());
    for (size_t fusedInIcOrd = 0; fusedInIcOrd < bi.m_fusedICs.size(); fusedInIcOrd++)
    {
        std::unique_ptr<InterpreterBytecodeImplCreator> component = InterpreterBytecodeImplCreator::LowerOneComponent(*bi.m_fusedICs[fusedInIcOrd].get());

        std::unique_ptr<Module> spModule = std::move(component->m_module);
        std::string expectedFnName = bi.m_fusedICs[fusedInIcOrd]->m_identFuncName;
        ReleaseAssert(spModule->getFunction(expectedFnName) != nullptr);
        ReleaseAssert(!spModule->getFunction(expectedFnName)->empty());
        ReleaseAssert(expectedFnName == bi.m_affliatedBytecodeFnNames[fusedInIcOrd]);

        ReleaseAssert(!componentInfoMap.count(expectedFnName));
        componentInfoMap[expectedFnName] = PerComponentInfo {
            .m_mayFallthroughToNextBytecode = component->m_mayFallthroughToNextBytecode,
            .m_mayMakeTailCall = component->m_mayMakeTailCall
        };

        Linker linker(*module.get());
        // linkInModule returns true on error
        //
        ReleaseAssert(linker.linkInModule(std::move(spModule)) == false);

        Function* linkedInFn = module->getFunction(expectedFnName);
        ReleaseAssert(linkedInFn != nullptr);
        ReleaseAssert(!linkedInFn->empty());
        ReleaseAssert(!linkedInFn->hasSection());
        linkedInFn->setSection(x_hot_code_section_name);
    }

    // Link in the quickening slow path if needed
    // We link in the quickening slow path before the return continuations so that related code stay closer to each other.
    // Though I guess the benefit of doing this is minimal, it doesn't hurt either, and it makes the assembly dump more readable..
    //
    ReleaseAssertIff(bi.m_bytecodeDef->HasQuickeningSlowPath(), bi.m_quickeningSlowPath.get() != nullptr);
    if (bi.m_bytecodeDef->HasQuickeningSlowPath())
    {
        std::unique_ptr<InterpreterBytecodeImplCreator> component = InterpreterBytecodeImplCreator::LowerOneComponent(*bi.m_quickeningSlowPath.get());

        std::unique_ptr<Module> spModule = std::move(component->m_module);
        std::string expectedSpName = bi.m_quickeningSlowPath->m_identFuncName;
        ReleaseAssert(expectedSpName == BytecodeIrInfo::GetQuickeningSlowPathFuncName(bytecodeDef));
        ReleaseAssert(spModule->getFunction(expectedSpName) != nullptr);
        ReleaseAssert(!spModule->getFunction(expectedSpName)->empty());

        ReleaseAssert(!componentInfoMap.count(expectedSpName));
        componentInfoMap[expectedSpName] = PerComponentInfo {
            .m_mayFallthroughToNextBytecode = component->m_mayFallthroughToNextBytecode,
            .m_mayMakeTailCall = component->m_mayMakeTailCall
        };

        Linker linker(*module.get());
        // linkInModule returns true on error
        //
        ReleaseAssert(linker.linkInModule(std::move(spModule)) == false);

        Function* linkedInFn = module->getFunction(expectedSpName);
        ReleaseAssert(linkedInFn != nullptr);
        ReleaseAssert(!linkedInFn->empty());
        ReleaseAssert(!linkedInFn->hasSection());
        linkedInFn->setSection(x_cold_code_section_name);
    }

    // Note that some of the return continuations could be dead (due to optimizations), however, since return continuations
    // may arbitrarily call each other, we cannot know a return continuation is dead until we have linked in all the return continuations.
    // So first we need to link in all return continuations.
    //
    for (size_t rcOrdinal = 0; rcOrdinal < bi.m_allRetConts.size(); rcOrdinal++)
    {
        std::unique_ptr<InterpreterBytecodeImplCreator> component = InterpreterBytecodeImplCreator::LowerOneComponent(*bi.m_allRetConts[rcOrdinal].get());

        std::unique_ptr<Module> rcModule = std::move(component->m_module);
        BytecodeIrComponent* bic = bi.m_allRetConts[rcOrdinal].get();
        std::string expectedRcName = bic->m_identFuncName;
        ReleaseAssert(expectedRcName == BytecodeIrInfo::GetRetContFuncName(bytecodeDef, rcOrdinal));
        ReleaseAssert(rcModule->getFunction(expectedRcName) != nullptr);
        ReleaseAssert(!rcModule->getFunction(expectedRcName)->empty());
        // Optimization pass may have stripped the return continuation function if it's not directly used by the main function
        // But if it exists, it should always be a declaration at this point
        //
        if (module->getFunction(expectedRcName) != nullptr)
        {
            ReleaseAssert(module->getFunction(expectedRcName)->empty());
        }

        ReleaseAssert(!componentInfoMap.count(expectedRcName));
        componentInfoMap[expectedRcName] = PerComponentInfo {
            .m_mayFallthroughToNextBytecode = component->m_mayFallthroughToNextBytecode,
            .m_mayMakeTailCall = component->m_mayMakeTailCall
        };

        Linker linker(*module.get());
        // linkInModule returns true on error
        //
        ReleaseAssert(linker.linkInModule(std::move(rcModule)) == false);

        Function* linkedInFn = module->getFunction(expectedRcName);
        ReleaseAssert(linkedInFn != nullptr);
        ReleaseAssert(!linkedInFn->empty());
        ReleaseAssert(!linkedInFn->hasSection());
        ReleaseAssert(!bic->m_hasDeterminedIsSlowPathRetCont);
        bic->m_hasDeterminedIsSlowPathRetCont = true;
        if (fnNamesOfAllReturnContinuationsUsedByMainFn.count(expectedRcName))
        {
            fnNamesOfAllReturnContinuationsUsedByMainFn.erase(expectedRcName);
            linkedInFn->setSection(x_hot_code_section_name);
            bic->m_isSlowPathRetCont = false;
        }
        else
        {
            linkedInFn->setSection(x_cold_code_section_name);
            bi.m_allRetConts[rcOrdinal]->m_isSlowPathRetCont = true;
            bic->m_isSlowPathRetCont = true;
        }
    }
    ReleaseAssert(fnNamesOfAllReturnContinuationsUsedByMainFn.empty());

    // Similarly, some slow paths could be dead. But we need to link in everything first.
    //
    for (size_t slowPathOrd = 0; slowPathOrd < bi.m_slowPaths.size(); slowPathOrd++)
    {
        std::unique_ptr<InterpreterBytecodeImplCreator> component = InterpreterBytecodeImplCreator::LowerOneComponent(*bi.m_slowPaths[slowPathOrd].get());

        std::unique_ptr<Module> spModule = std::move(component->m_module);
        std::string expectedFnName = bi.m_slowPaths[slowPathOrd]->m_identFuncName;
        ReleaseAssert(spModule->getFunction(expectedFnName) != nullptr);
        ReleaseAssert(!spModule->getFunction(expectedFnName)->empty());

        ReleaseAssert(!componentInfoMap.count(expectedFnName));
        componentInfoMap[expectedFnName] = PerComponentInfo {
            .m_mayFallthroughToNextBytecode = component->m_mayFallthroughToNextBytecode,
            .m_mayMakeTailCall = component->m_mayMakeTailCall
        };

        Linker linker(*module.get());
        // linkInModule returns true on error
        //
        ReleaseAssert(linker.linkInModule(std::move(spModule)) == false);

        Function* linkedInFn = module->getFunction(expectedFnName);
        ReleaseAssert(linkedInFn != nullptr);
        ReleaseAssert(!linkedInFn->empty());
        ReleaseAssert(!linkedInFn->hasSection());
        linkedInFn->setSection(x_cold_code_section_name);
    }

    for (const auto& it : componentInfoMap)
    {
        const std::string& fnName = it.first;
        ReleaseAssert(module->getFunction(fnName) != nullptr);
    }

    // Now, having linked in everything, we can strip dead return continuations and slow paths by
    // changing the linkage of those functions to internal and run the DCE pass.
    //
    for (size_t rcOrdinal = 0; rcOrdinal < bi.m_allRetConts.size(); rcOrdinal++)
    {
        std::string rcName = bi.m_allRetConts[rcOrdinal]->m_identFuncName;
        Function* func = module->getFunction(rcName);
        ReleaseAssert(func != nullptr);
        ReleaseAssert(!func->empty());
        ReleaseAssert(func->hasExternalLinkage());
        func->setLinkage(GlobalValue::InternalLinkage);
    }

    for (size_t slowPathOrd = 0; slowPathOrd < bi.m_slowPaths.size(); slowPathOrd++)
    {
        std::string spName = bi.m_slowPaths[slowPathOrd]->m_identFuncName;
        Function* func = module->getFunction(spName);
        ReleaseAssert(func != nullptr);
        ReleaseAssert(!func->empty());
        ReleaseAssert(func->hasExternalLinkage());
        func->setLinkage(GlobalValue::InternalLinkage);
    }

    RunLLVMDeadGlobalElimination(module.get());

    // After DCE, we have reliable information to determine whether this bytecode may transfer control to the next bytecode:
    // if any component that may transfer control to the next bytecode survived DCE, the bytecode can.
    //
    bool bytecodeMayFallthroughToNextBytecode = false;
    bool bytecodeMayMakeTailCall = false;
    for (const auto& it : componentInfoMap)
    {
        const std::string& fnName = it.first;
        PerComponentInfo componentInfo = it.second;
        if (module->getFunction(fnName) != nullptr)
        {
            if (componentInfo.m_mayFallthroughToNextBytecode)
            {
                bytecodeMayFallthroughToNextBytecode = true;
            }
            if (componentInfo.m_mayMakeTailCall)
            {
                bytecodeMayMakeTailCall = true;
            }
        }
        else
        {
            ReleaseAssert(module->getNamedValue(fnName) == nullptr);
        }
    }

    // This is not a comprehensive assert: we do not allow mixing MakeTailCall and other terminal APIs (except Throw) in general,
    // but one assert is better than none..
    //
    if (bytecodeMayFallthroughToNextBytecode && bytecodeMayMakeTailCall)
    {
        fprintf(stderr, "[ERROR] You cannot mix MakeTailCall and other terminal APIs (except Throw) in the same bytecode!\n");
        abort();
    }

    ReleaseAssert(!bytecodeDef->m_bytecodeMayFallthroughToNextBytecodeDetermined);
    bytecodeDef->m_bytecodeMayFallthroughToNextBytecodeDetermined = true;
    bytecodeDef->m_bytecodeMayFallthroughToNextBytecode = bytecodeMayFallthroughToNextBytecode;

    ReleaseAssert(!bytecodeDef->m_bytecodeMayMakeTailCallDetermined);
    bytecodeDef->m_bytecodeMayMakeTailCallDetermined = true;
    bytecodeDef->m_bytecodeMayMakeTailCall = bytecodeMayMakeTailCall;

    // In theory it's fine to leave them with internal linkage now, since they are exclusively
    // used by our bytecode, but some of our callers expects them to have external linkage.
    // So we will change the surviving functions back to external linkage after DCE.
    //
    // Also update 'bi' to remove all the dead return continuations, so that 'bi' doesn't contain
    // unnecessary data and corresponds exactly to the situation in the module.
    //
    std::vector<std::unique_ptr<BytecodeIrComponent>> survivedRetConts;
    for (size_t rcOrdinal = 0; rcOrdinal < bi.m_allRetConts.size(); rcOrdinal++)
    {
        std::string rcName = bi.m_allRetConts[rcOrdinal]->m_identFuncName;
        Function* func = module->getFunction(rcName);
        if (func != nullptr)
        {
            ReleaseAssert(func->hasInternalLinkage());
            func->setLinkage(GlobalValue::ExternalLinkage);
            survivedRetConts.push_back(std::move(bi.m_allRetConts[rcOrdinal]));
        }
        else
        {
            ReleaseAssert(module->getNamedValue(rcName) == nullptr);
        }
    }
    bi.m_allRetConts = std::move(survivedRetConts);

    // Additionally, for slow paths, the function names are not unique across translational units.
    // So at this stage, we will rename them and give each of the survived slow path a unique name
    //
    std::vector<std::pair<Function*, std::unique_ptr<BytecodeIrComponent>>> survivedSlowPathFns;
    for (size_t slowPathOrd = 0; slowPathOrd < bi.m_slowPaths.size(); slowPathOrd++)
    {
        std::string spName = bi.m_slowPaths[slowPathOrd]->m_identFuncName;
        Function* func = module->getFunction(spName);
        if (func != nullptr)
        {
            ReleaseAssert(func->hasInternalLinkage());
            func->setLinkage(GlobalValue::ExternalLinkage);
            survivedSlowPathFns.push_back(std::make_pair(func, std::move(bi.m_slowPaths[slowPathOrd])));
        }
        else
        {
            ReleaseAssert(module->getNamedValue(spName) == nullptr);
        }
    }

    // Create the old-to-new name map and rename all the slow path functions to the new names
    //
    std::map<std::string, std::string> slowPathFnRenameMap;
    for (size_t ord = 0; ord < survivedSlowPathFns.size(); ord++)
    {
        // Note that this 'func' is the function in the main module, not the slow path module!
        //
        Function* func = survivedSlowPathFns[ord].first;
        BytecodeIrComponent* bic = survivedSlowPathFns[ord].second.get();

        std::string oldFnName = func->getName().str();
        ReleaseAssert(oldFnName.ends_with("_interpreter_wrapper"));
        oldFnName = oldFnName.substr(0, oldFnName.length() - strlen("_interpreter_wrapper"));

        std::string newFnName = bi.m_interpreterMainComponent->m_identFuncName + "_slow_path_" + std::to_string(ord);
        std::string newFnNameImpl = newFnName + "_impl";
        ReleaseAssert(!slowPathFnRenameMap.count(oldFnName));
        slowPathFnRenameMap[oldFnName] = newFnNameImpl;

        func->setName(newFnName);
        ReleaseAssert(func->getName() == newFnName);
        bic->m_identFuncName = newFnName;
    }

    {
        // Sanity check all the names are different
        //
        std::set<std::string> checkUnique;
        for (auto& it : slowPathFnRenameMap)
        {
            ReleaseAssert(!checkUnique.count(it.first));
            checkUnique.insert(it.first);
            ReleaseAssert(!checkUnique.count(it.second));
            checkUnique.insert(it.second);
        }
    }

    auto renameSlowPathFnNames = [&](Module* m) {
        for (auto& it : slowPathFnRenameMap)
        {
            std::string oldName = it.first;
            std::string newName = it.second;
            Function* func = m->getFunction(oldName);
            if (func != nullptr)
            {
                func->setName(newName);
                ReleaseAssert(func->getName() == newName);
            }
            else
            {
                ReleaseAssert(m->getNamedValue(oldName) == nullptr);
            }
        }
    };

    // Rename all the slow path function names in each slow path module, and update 'bi' to remove the dead slowpaths
    //
    bi.m_slowPaths.clear();
    for (size_t ord = 0; ord < survivedSlowPathFns.size(); ord++)
    {
        std::unique_ptr<BytecodeIrComponent>& bic = survivedSlowPathFns[ord].second;
        Module* m = bic->m_module.get();
        renameSlowPathFnNames(m);
        bi.m_slowPaths.push_back(std::move(bic));
    }

    // Rename the slow path functions in the other modules as well
    //
    renameSlowPathFnNames(bi.m_jitMainComponent->m_module.get());
    if (bi.m_quickeningSlowPath)
    {
        renameSlowPathFnNames(bi.m_quickeningSlowPath->m_module.get());
    }
    for (auto& it : bi.m_allRetConts)
    {
        renameSlowPathFnNames(it->m_module.get());
    }

    // Rename the '__deegen_bytecode_' generic prefix to '__deegen_interpreter_op' to prevent
    // name collision with the other execution tiers of the engine.
    //
    std::vector<Function*> allFunctionsToRename;
    for (Function& func : *module)
    {
        if (func.getName().startswith("__deegen_bytecode_"))
        {
            allFunctionsToRename.push_back(&func);
        }
    }

    for (Function* func : allFunctionsToRename)
    {
        std::string fnName = func->getName().str();
        fnName = BytecodeIrInfo::ToInterpreterName(fnName);
        ReleaseAssert(module->getNamedValue(fnName) == nullptr);
        func->setName(fnName);
        ReleaseAssert(func->getName() == fnName);
    }

    return module;
}

}   // namespace dast
