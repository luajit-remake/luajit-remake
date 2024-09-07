#include "deegen_function_entry_logic_creator.h"
#include "deegen_interpreter_function_interface.h"
#include "deegen_bytecode_operand.h"
#include "deegen_ast_return.h"
#include "deegen_options.h"
#include "invoke_clang_helper.h"
#include "tvalue.h"
#include "tag_register_optimization.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_stencil_lowering_pass.h"
#include "drt/baseline_jit_codegen_helper.h"
#include "vm_base_pointer_optimization.h"

namespace dast {

std::string DeegenFunctionEntryLogicCreator::GetFunctionName()
{
    std::string tierName;
    if (m_tier == DeegenEngineTier::Interpreter)
    {
        tierName = "interpreter";
    }
    else
    {
        ReleaseAssert(m_tier == DeegenEngineTier::BaselineJIT);
        tierName = "baseline_jit";
    }
    std::string name = "deegen_" + tierName + "_enter_guest_language_function_";
    if (IsNumFixedParamSpecialized())
    {
        name += std::to_string(GetSpecializedNumFixedParam()) + "_params_";
    }
    else
    {
        name += "generic_";
    }
    if (m_acceptVarArgs)
    {
        name += "va";
    }
    else
    {
        name += "nova";
    }
    return name;
}

std::unique_ptr<llvm::Module> WARN_UNUSED DeegenFunctionEntryLogicCreator::GenerateInterpreterTierUpOrOsrEntryImplementation(llvm::LLVMContext& ctx, bool isTierUp)
{
    using namespace llvm;
    std::unique_ptr<Module> module = std::make_unique<Module>("generated_function_entry_logic", ctx);

    std::string funcName = (isTierUp ? "__deegen_interpreter_tier_up_into_baseline_jit" : "__deegen_interpreter_osr_entry_into_baseline_jit");
    Function* func = InterpreterFunctionInterface::CreateFunction(module.get(), funcName);

    {
        // Just pull in some C++ function so we can set up the attributes..
        //
        Function* tmp = LinkInDeegenCommonSnippet(module.get(), "SimpleLeftToRightCopyMayOvercopy");
        func->addFnAttr(Attribute::AttrKind::NoUnwind);
        CopyFunctionAttributes(func /*dst*/, tmp /*src*/);
    }

    ReleaseAssert(func->arg_size() == 16);

    if (isTierUp)
    {
        Value* coroutineCtx = func->getArg(0);
        coroutineCtx->setName("coroCtx");
        Value* preFixupStackBase = func->getArg(1);
        preFixupStackBase->setName("preFixupStackBase");
        Value* numArgsAsPtr = func->getArg(2);
        Value* calleeCodeBlock = func->getArg(5);
        Value* isMustTail64 = func->getArg(6);

        BasicBlock* entryBB = BasicBlock::Create(ctx, "", func);

        ReleaseAssert(llvm_value_has_type<void*>(numArgsAsPtr));
        Value* numArgs = new PtrToIntInst(numArgsAsPtr, llvm_type_of<uint64_t>(ctx), "", entryBB);
        numArgs->setName("numProvidedArgs");

        ReleaseAssert(llvm_value_has_type<void*>(calleeCodeBlock));

        // Set up the function implementation, which should call the baseline JIT codegen function and branch to JIT'ed code
        //
        Value* bcbAndCodePointer = CreateCallToDeegenCommonSnippet(module.get(), "TierUpIntoBaselineJit", { calleeCodeBlock }, entryBB);
        ReleaseAssert(bcbAndCodePointer->getType()->isStructTy());
        StructType* sty = dyn_cast<StructType>(bcbAndCodePointer->getType());
        ReleaseAssert(sty->elements().size() == 2);
        Value* bcb = ExtractValueInst::Create(bcbAndCodePointer, { 0 /*idx*/ }, "", entryBB);
        ReleaseAssert(llvm_value_has_type<void*>(bcb));
        Value* codePointer = ExtractValueInst::Create(bcbAndCodePointer, { 1 /*idx*/ }, "", entryBB);
        ReleaseAssert(llvm_value_has_type<void*>(codePointer));

        UnreachableInst* dummyInst = new UnreachableInst(ctx, entryBB);

        InterpreterFunctionInterface::CreateDispatchToCallee(
            codePointer,
            coroutineCtx,
            preFixupStackBase,
            calleeCodeBlock,
            numArgs,
            isMustTail64,
            dummyInst /*insertBefore*/);

        dummyInst->eraseFromParent();
    }
    else
    {
        Value* coroCtx = func->getArg(0);
        coroCtx->setName("coroCtx");

        Value* stackBase = func->getArg(1);
        stackBase->setName("stackBase");

        Value* curBytecode = func->getArg(2);
        curBytecode->setName("curBytecode");

        Value* codeBlock = func->getArg(5);
        codeBlock->setName("codeBlock");

        BasicBlock* entryBB = BasicBlock::Create(ctx, "", func);

        Value* bcbAndCodePointer = CreateCallToDeegenCommonSnippet(module.get(), "OsrEntryIntoBaselineJit", { codeBlock, curBytecode }, entryBB);
        ReleaseAssert(bcbAndCodePointer->getType()->isStructTy());
        StructType* sty = dyn_cast<StructType>(bcbAndCodePointer->getType());
        ReleaseAssert(sty->elements().size() == 2);
        Value* bcb = ExtractValueInst::Create(bcbAndCodePointer, { 0 /*idx*/ }, "", entryBB);
        ReleaseAssert(llvm_value_has_type<void*>(bcb));
        Value* codePointer = ExtractValueInst::Create(bcbAndCodePointer, { 1 /*idx*/ }, "", entryBB);
        ReleaseAssert(llvm_value_has_type<void*>(codePointer));

        UnreachableInst* dummyInst = new UnreachableInst(ctx, entryBB);

        InterpreterFunctionInterface::CreateDispatchToBytecode(
            codePointer,
            coroCtx,
            stackBase,
            UndefValue::get(llvm_type_of<void*>(ctx)) /*bytecodePtr*/,
            bcb,
            dummyInst);

        dummyInst->eraseFromParent();
    }

    RunLLVMOptimizePass(module.get());
    return module;
}

void DeegenFunctionEntryLogicCreator::Run(llvm::LLVMContext& ctx)
{
    ReleaseAssert(!m_generated);
    m_generated = true;

    using namespace llvm;
    std::unique_ptr<Module> module = std::make_unique<Module>("generated_function_entry_logic", ctx);

    std::string funcName = GetFunctionName();
    if (m_tier == DeegenEngineTier::BaselineJIT)
    {
        // For baseline JIT, use a temp function name since it's not our final result
        //
        funcName = "baseline_jit_fn_entry_logic_tmp";
    }
    Function* func = InterpreterFunctionInterface::CreateFunction(module.get(), funcName);

    {
        // Just pull in some C++ function so we can set up the attributes..
        //
        Function* tmp = LinkInDeegenCommonSnippet(module.get(), "SimpleLeftToRightCopyMayOvercopy");
        func->addFnAttr(Attribute::AttrKind::NoUnwind);
        CopyFunctionAttributes(func /*dst*/, tmp /*src*/);
    }

    // TODO: add parameter attributes
    //

    ReleaseAssert(func->arg_size() == 16);
    Value* coroutineCtx = func->getArg(0);
    coroutineCtx->setName("coroCtx");
    Value* preFixupStackBase = func->getArg(1);
    preFixupStackBase->setName("preFixupStackBase");
    Value* numArgsAsPtr = func->getArg(2);
    Value* calleeCodeBlock = func->getArg(5);
    Value* isMustTail64 = func->getArg(6);

    BasicBlock* entryBB = BasicBlock::Create(ctx, "", func);

    ReleaseAssert(llvm_value_has_type<void*>(numArgsAsPtr));
    Value* numArgs = new PtrToIntInst(numArgsAsPtr, llvm_type_of<uint64_t>(ctx), "", entryBB);
    numArgs->setName("numProvidedArgs");

    ReleaseAssert(llvm_value_has_type<void*>(calleeCodeBlock));

    // Check for tier up
    //
    BasicBlock* normalBB = nullptr;     // this is the basic block for normal execution (i.e., no tiering-up)
    if (m_tier == DeegenEngineTier::Interpreter)
    {
        if (x_allow_interpreter_tier_up_to_baseline_jit)
        {
            // Check if we need to tier up
            //
            Value* tierUpCounter = CreateCallToDeegenCommonSnippet(module.get(), "GetInterpreterTierUpCounterFromCb", { calleeCodeBlock }, entryBB);
            ReleaseAssert(llvm_value_has_type<int64_t>(tierUpCounter));

            Value* shouldTierUp = new ICmpInst(*entryBB, ICmpInst::ICMP_SLT, tierUpCounter, CreateLLVMConstantInt<int64_t>(ctx, 0));
            Function* expectIntrin = Intrinsic::getDeclaration(module.get(), Intrinsic::expect, { Type::getInt1Ty(ctx) });
            shouldTierUp = CallInst::Create(expectIntrin, { shouldTierUp, CreateLLVMConstantInt<bool>(ctx, false) }, "", entryBB);

            BasicBlock* tierUpBB = BasicBlock::Create(ctx, "", func);
            normalBB = BasicBlock::Create(ctx, "", func);

            BranchInst::Create(tierUpBB, normalBB, shouldTierUp, entryBB);

            Function* tierUpImpl = InterpreterFunctionInterface::CreateFunction(module.get(), "__deegen_interpreter_tier_up_into_baseline_jit");

            UnreachableInst* dummyInst = new UnreachableInst(ctx, tierUpBB);

            InterpreterFunctionInterface::CreateDispatchToCallee(
                tierUpImpl,
                coroutineCtx,
                preFixupStackBase,
                calleeCodeBlock,
                numArgs,
                isMustTail64,
                dummyInst /*insertBefore*/);

            dummyInst->eraseFromParent();
        }
        else
        {
            // Tier-up logic is hard fused-off.
            //
            normalBB = entryBB;
        }
    }
    else
    {
        ReleaseAssert(m_tier == DeegenEngineTier::BaselineJIT);
        normalBB = entryBB;
    }

    // Set up the normal execution BB, which should do the stack frame adjustments as needed and branch to the real function logic
    //
    calleeCodeBlock->setName("calleeCodeBlock");

    Value* bytecodePtr = nullptr;
    if (m_tier == DeegenEngineTier::Interpreter)
    {
        bytecodePtr = CreateCallToDeegenCommonSnippet(module.get(), "GetBytecodePtrFromCodeBlock", { calleeCodeBlock }, normalBB);
        bytecodePtr->setName("bytecodePtr");
    }

    Value* stackBaseAfterFixUp = nullptr;
    if (!m_acceptVarArgs)
    {
        if (IsNumFixedParamSpecialized())
        {
            size_t numArgsCalleeAccepts = GetSpecializedNumFixedParam();
            if (numArgsCalleeAccepts == 0)
            {
                // Easiest case, no work to do
                //
            }
            else if (numArgsCalleeAccepts <= 2)
            {
                uint64_t nilValue = TValue::Nil().m_value;
                // We can completely get rid of the branches, by simply unconditionally writing 'numArgsCalleeAccepts' nils beginning at stackbase[numArgs]
                //
                GetElementPtrInst* base = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), preFixupStackBase, { numArgs }, "", normalBB);
                for (size_t i = 0; i < numArgsCalleeAccepts; i++)
                {
                    GetElementPtrInst* target = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), base, { CreateLLVMConstantInt<uint64_t>(ctx, i) }, "", normalBB);
                    std::ignore = new StoreInst(CreateLLVMConstantInt<uint64_t>(ctx, nilValue), target, normalBB);
                }
            }
            else
            {
                uint64_t nilValue = TValue::Nil().m_value;
                Constant* numFixedParams = CreateLLVMConstantInt<uint64_t>(ctx, numArgsCalleeAccepts);
                CreateCallToDeegenCommonSnippet(module.get(), "PopulateNilToUnprovidedParams", { preFixupStackBase, numArgs, numFixedParams, CreateLLVMConstantInt<uint64_t>(ctx, nilValue) }, normalBB);
            }
        }
        else
        {
            Value* numFixedParams = CreateCallToDeegenCommonSnippet(module.get(), "GetNumFixedParamsFromCodeBlock", { calleeCodeBlock }, normalBB);
            uint64_t nilValue = TValue::Nil().m_value;
            CreateCallToDeegenCommonSnippet(module.get(), "PopulateNilToUnprovidedParams", { preFixupStackBase, numArgs, numFixedParams, CreateLLVMConstantInt<uint64_t>(ctx, nilValue) }, normalBB);
        }
        stackBaseAfterFixUp = preFixupStackBase;
    }
    else
    {
        // TODO: consider rewrite this and split out the cold path for tail call memmove
        //
        Value* numFixedParams;
        if (IsNumFixedParamSpecialized())
        {
            numFixedParams = CreateLLVMConstantInt<uint64_t>(ctx, GetSpecializedNumFixedParam());
        }
        else
        {
            numFixedParams = CreateCallToDeegenCommonSnippet(module.get(), "GetNumFixedParamsFromCodeBlock", { calleeCodeBlock }, normalBB);
        }
        uint64_t nilValue = TValue::Nil().m_value;
        stackBaseAfterFixUp = CreateCallToDeegenCommonSnippet(module.get(), "FixupStackFrameForVariadicArgFunction", { preFixupStackBase, numArgs, numFixedParams, CreateLLVMConstantInt<uint64_t>(ctx, nilValue), isMustTail64 }, normalBB);
    }

    ReleaseAssert(llvm_value_has_type<void*>(stackBaseAfterFixUp));
    stackBaseAfterFixUp->setName("stackBaseAfterFixUp");

    if (m_tier == DeegenEngineTier::Interpreter)
    {
        ReleaseAssert(bytecodePtr != nullptr);

        UnreachableInst* dummyInst = new UnreachableInst(ctx, normalBB);

        Value* opcode = BytecodeVariantDefinition::DecodeBytecodeOpcode(bytecodePtr, dummyInst /*insertBefore*/);
        ReleaseAssert(llvm_value_has_type<uint64_t>(opcode));

        Value* targetFunction = GetInterpreterFunctionFromInterpreterOpcode(module.get(), opcode, dummyInst /*insertBefore*/);
        ReleaseAssert(llvm_value_has_type<void*>(targetFunction));

        InterpreterFunctionInterface::CreateDispatchToBytecode(
            targetFunction,
            coroutineCtx,
            stackBaseAfterFixUp,
            bytecodePtr,
            calleeCodeBlock,
            dummyInst /*insertBefore*/);

        dummyInst->eraseFromParent();
    }
    else
    {
        ReleaseAssert(m_tier == DeegenEngineTier::BaselineJIT);
        ReleaseAssert(bytecodePtr == nullptr);

        UnreachableInst* dummyInst = new UnreachableInst(ctx, normalBB);

        CallInst* target = DeegenPlaceholderUtils::CreateConstantPlaceholderForOperand(module.get(),
                                                                                       101 /*fallthroughDest*/,
                                                                                       llvm_type_of<void*>(ctx),
                                                                                       dummyInst /*insertBefore*/);
        ReleaseAssert(llvm_value_has_type<void*>(target));

        Value* baselineCodeBlock = CreateCallToDeegenCommonSnippet(module.get(), "GetBaselineJitCodeBlockFromCodeBlock", { calleeCodeBlock }, dummyInst);

        InterpreterFunctionInterface::CreateDispatchToBytecode(
            target,
            coroutineCtx,
            stackBaseAfterFixUp,
            UndefValue::get(llvm_type_of<void*>(ctx)) /*bytecodePtr*/,
            baselineCodeBlock,
            dummyInst /*insertBefore*/);

        dummyInst->eraseFromParent();
    }

    if (m_tier == DeegenEngineTier::Interpreter)
    {
        bool shouldPutIntoHotCodeSection;
        if (m_acceptVarArgs)
        {
            shouldPutIntoHotCodeSection = IsNumFixedParamSpecialized() && GetSpecializedNumFixedParam() <= 2;
        }
        else
        {
            shouldPutIntoHotCodeSection = true;
        }
        if (shouldPutIntoHotCodeSection)
        {
            func->setSection(InterpreterBytecodeImplCreator::x_hot_code_section_name);
        }
    }

    DesugarAndSimplifyLLVMModule(module.get(), DesugaringLevel::Top);
    RunTagRegisterOptimizationPass(func);
    RunVMBasePointerOptimizationPass(func);

    if (m_tier == DeegenEngineTier::Interpreter)
    {
        RunLLVMOptimizePass(module.get());
        m_module = std::move(module);
        return;
    }

    GenerateBaselineJitStencil(std::move(module));
}

void DeegenFunctionEntryLogicCreator::GenerateBaselineJitStencil(std::unique_ptr<llvm::Module> srcModule)
{
    using namespace llvm;
    LLVMContext& ctx = srcModule->getContext();

    ReleaseAssert(m_tier == DeegenEngineTier::BaselineJIT);

    Function* func = srcModule->getFunction("baseline_jit_fn_entry_logic_tmp");
    ReleaseAssert(func != nullptr);

    // The logic below basically duplicates the logic in baseline JIT lowering, which is a bit unfortunate..
    //
    DesugarAndSimplifyLLVMModule(srcModule.get(), DesugaringLevel::PerFunctionSimplifyOnlyAggresive);

    std::vector<CPRuntimeConstantNodeBase*> rcDef;
    {
        StencilRuntimeConstantInserter rcInserter;
        rcInserter.AddRawRuntimeConstantAsLowAddressFnPointer(101 /*fallthroughDest*/);
        rcDef = rcInserter.RunOnFunction(func);
    }

    RunLLVMOptimizePass(srcModule.get());

    DeegenStencilLoweringPass slPass = DeegenStencilLoweringPass::RunIrRewritePhase(func, DeegenPlaceholderUtils::FindFallthroughPlaceholderSymbolName(rcDef));

    std::string asmFile = CompileLLVMModuleToAssemblyFileForStencilGeneration(srcModule.get(), llvm::Reloc::Static, llvm::CodeModel::Small);

    slPass.RunAsmRewritePhase(asmFile);
    asmFile = slPass.m_primaryPostTransformAsmFile;

    // Save contents for audit
    //
    {
        m_baselineJitSourceAsmForAudit = asmFile;

        // Do some simple heuristic to try to pick out the function asm for better readability
        //
        size_t startPos = asmFile.find("baseline_jit_fn_entry_logic_tmp:");
        if (startPos != std::string::npos)
        {
            size_t endPos = asmFile.find(".Lfunc_end0:");
            if (endPos != std::string::npos && endPos > startPos)
            {
                m_baselineJitSourceAsmForAudit = asmFile.substr(startPos, endPos - startPos);
            }
        }
    }

    std::string objectFile = CompileAssemblyFileToObjectFile(asmFile, " -fno-pic -fno-pie ");
    DeegenStencil stencil = DeegenStencil::ParseMainLogic(ctx, objectFile);

    DeegenStencilCodegenResult cgRes = stencil.PrintCodegenFunctions(true /*mayAttemptToEliminateJmpToFallthrough*/,
                                                                     0 /*numBytecodeOperands*/,
                                                                     0 /*numGenericIcTotalCaptures*/,
                                                                     rcDef);
    // The codegen result must have no late CondBr patches
    //
    ReleaseAssert(cgRes.m_condBrFixupOffsetsInFastPath.size() == 0);
    ReleaseAssert(cgRes.m_condBrFixupOffsetsInSlowPath.size() == 0);
    ReleaseAssert(cgRes.m_condBrFixupOffsetsInDataSec.size() == 0);

    // Note that since the function entry logic is always the first thing to be emitted,
    // the data section pointer is always at offset 0 with assumed max alignment.
    // So we don't need to worry about aligning the data section pointer here, just assert that it doesn't exceed our assumed max alignment
    //
    ReleaseAssert(cgRes.m_dataSecAlignment <= x_baselineJitMaxPossibleDataSectionAlignment);
    ReleaseAssert(is_power_of_2(cgRes.m_dataSecAlignment));

    std::unique_ptr<llvm::Module> cgMod = cgRes.GenerateCodegenLogicLLVMModule(srcModule.get() /*originModule*/);

    Function* fastPathPatchFn = cgMod->getFunction(DeegenStencilCodegenResult::x_fastPathCodegenFuncName);
    ReleaseAssert(fastPathPatchFn != nullptr);

    Function* slowPathPatchFn = cgMod->getFunction(DeegenStencilCodegenResult::x_slowPathCodegenFuncName);
    ReleaseAssert(slowPathPatchFn != nullptr);

    Function* dataSecPatchFn = cgMod->getFunction(DeegenStencilCodegenResult::x_dataSecCodegenFuncName);
    ReleaseAssert(dataSecPatchFn != nullptr);

    fastPathPatchFn->setLinkage(GlobalValue::InternalLinkage);
    fastPathPatchFn->addFnAttr(Attribute::AlwaysInline);
    slowPathPatchFn->setLinkage(GlobalValue::InternalLinkage);
    slowPathPatchFn->addFnAttr(Attribute::AlwaysInline);
    dataSecPatchFn->setLinkage(GlobalValue::InternalLinkage);
    dataSecPatchFn->addFnAttr(Attribute::AlwaysInline);

    constexpr size_t x_numArgsBeforeBytecodeOperands = 6;

    // Currently patch functions takes 4 fixed operands even if they don't exists (placeholder ordinal 100, 101, 103, 104)
    //
    constexpr size_t x_numExpectedArgsInPatchFn = x_numArgsBeforeBytecodeOperands + 4;
    auto validatePatchFnProto = [&](Function* f)
    {
        ReleaseAssert(f->arg_size() == x_numExpectedArgsInPatchFn);
        // Our stencil doesn't have any runtime constants to provide, so no patch values should exist.
        // The only usable inputs are the first 4 arguments (dstAddr, fastPathAddr, slowPathAddr, dataSecAddr)
        //
        for (size_t i = x_numArgsBeforeBytecodeOperands; i < fastPathPatchFn->arg_size(); i++)
        {
            if (i == x_numArgsBeforeBytecodeOperands + 1 /*ordinal 101*/) { continue; }
            ReleaseAssert(fastPathPatchFn->getArg(static_cast<uint32_t>(i))->use_empty());
        }
    };
    validatePatchFnProto(fastPathPatchFn);
    validatePatchFnProto(slowPathPatchFn);
    validatePatchFnProto(dataSecPatchFn);

    FunctionType* fty = FunctionType::get(llvm_type_of<void>(ctx), { llvm_type_of<void*>(ctx), llvm_type_of<void*>(ctx), llvm_type_of<void*>(ctx) }, false /*isVarArg*/);
    Function* patchFn = Function::Create(fty, GlobalValue::ExternalLinkage, GetFunctionName(), cgMod.get());
    ReleaseAssert(patchFn->getName() == GetFunctionName());

    patchFn->addFnAttr(Attribute::NoUnwind);
    CopyFunctionAttributes(patchFn, fastPathPatchFn);
    patchFn->setDSOLocal(true);

    Value* fastPathAddr = patchFn->getArg(0);
    Value* slowPathAddr = patchFn->getArg(1);
    Value* dataSecAddr = patchFn->getArg(2);

    BasicBlock* bb = BasicBlock::Create(ctx, "", patchFn);

    // Note that we don't have to align the data section pointer since it is at offset 0 and must already be aligned
    //
    EmitCopyLogicForBaselineJitCodeGen(cgMod.get(), cgRes.m_fastPathPreFixupCode, fastPathAddr, "deegen_fastpath_prefixup_code", bb /*insertAtEnd*/);
    EmitCopyLogicForBaselineJitCodeGen(cgMod.get(), cgRes.m_slowPathPreFixupCode, slowPathAddr, "deegen_slowpath_prefixup_code", bb /*insertAtEnd*/);
    EmitCopyLogicForBaselineJitCodeGen(cgMod.get(), cgRes.m_dataSecPreFixupCode, dataSecAddr, "deegen_datasec_prefixup_code", bb /*insertAtEnd*/);

    Value* fastPathAddrI64 = new PtrToIntInst(fastPathAddr, llvm_type_of<uint64_t>(ctx), "", bb);
    Value* slowPathAddrI64 = new PtrToIntInst(slowPathAddr, llvm_type_of<uint64_t>(ctx), "", bb);
    Value* dataSecAddrI64 = new PtrToIntInst(dataSecAddr, llvm_type_of<uint64_t>(ctx), "", bb);
    Value* fastPathEnd = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), fastPathAddr,
                                                           { CreateLLVMConstantInt<uint64_t>(ctx, cgRes.m_fastPathPreFixupCode.size()) }, "", bb);
    Value* fastPathEndI64 = new PtrToIntInst(fastPathEnd, llvm_type_of<uint64_t>(ctx), "", bb);

    auto callPatchFn = [&](Function* target, Value* dstAddr)
    {
        std::vector<Value*> args;
        args.push_back(dstAddr);
        args.push_back(fastPathAddrI64);
        args.push_back(slowPathAddrI64);
        args.push_back(UndefValue::get(llvm_type_of<uint64_t>(ctx)));   // icCodeAddr
        args.push_back(UndefValue::get(llvm_type_of<uint64_t>(ctx)));   // icDataAddr
        args.push_back(dataSecAddrI64);
        args.push_back(UndefValue::get(llvm_type_of<uint64_t>(ctx)));   // ordinal 100
        args.push_back(fastPathEndI64);                                 // ordinal 101
        args.push_back(UndefValue::get(llvm_type_of<uint64_t>(ctx)));   // ordinal 103
        args.push_back(UndefValue::get(llvm_type_of<uint64_t>(ctx)));   // ordinal 104
        ReleaseAssert(args.size() == x_numExpectedArgsInPatchFn);
        CallInst::Create(target, args, "", bb);
    };
    callPatchFn(fastPathPatchFn, fastPathAddr);
    callPatchFn(slowPathPatchFn, slowPathAddr);
    callPatchFn(dataSecPatchFn, dataSecAddr);

    ReturnInst::Create(ctx, nullptr, bb);

    ValidateLLVMModule(cgMod.get());

    RunLLVMOptimizePass(cgMod.get());

    ReleaseAssert(cgMod->getFunction(GetFunctionName()) != nullptr);
    m_module = std::move(cgMod);
    m_baselineJitFastPathLen = cgRes.m_fastPathPreFixupCode.size();
    m_baselineJitSlowPathLen = cgRes.m_slowPathPreFixupCode.size();
    m_baselineJitDataSecLen = cgRes.m_dataSecPreFixupCode.size();
}

}   // namespace dast
