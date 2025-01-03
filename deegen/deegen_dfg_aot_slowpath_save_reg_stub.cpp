#include "deegen_dfg_aot_slowpath_save_reg_stub.h"
#include "dfg_reg_alloc_register_info.h"

namespace dast {

std::string WARN_UNUSED DfgAotSlowPathSaveRegStubCreator::GetResultFunctionName(BytecodeIrComponent& bic)
{
    ReleaseAssert(bic.m_processKind == BytecodeIrComponentKind::SlowPath || bic.m_processKind == BytecodeIrComponentKind::QuickeningSlowPath);
    std::string destFuncName = bic.m_identFuncName;
    ReleaseAssert(destFuncName.starts_with("__deegen_bytecode_"));
    destFuncName = "__deegen_dfg_jit_op_" + destFuncName.substr(strlen("__deegen_bytecode_"));

    std::string funcName = destFuncName + "_save_registers";
    return funcName;
}

std::unique_ptr<llvm::Module> WARN_UNUSED DfgAotSlowPathSaveRegStubCreator::Create(BytecodeIrComponent& bic)
{
    using namespace llvm;
    LLVMContext& ctx = bic.m_impl->getContext();

    std::unique_ptr<Module> module = RegisterPinningScheme::CreateModule("generated_save_reg_stub_for_aot_slow_path_logic", ctx);

    std::string funcName = GetResultFunctionName(bic);
    std::unique_ptr<ExecutorFunctionContext> funcCtx = ExecutorFunctionContext::CreateForDfgAOTSaveRegStub();
    Function* func = funcCtx->CreateFunction(module.get(), funcName);
    func->addFnAttr(Attribute::NoInline);

    BasicBlock* entryBB = BasicBlock::Create(ctx, "", func);

    Value* coroCtx = funcCtx->GetValueAtEntry<RPV_CoroContext>();
    Value* stackBase = funcCtx->GetValueAtEntry<RPV_StackBase>();
    Value* dfgCodeBlock = funcCtx->GetValueAtEntry<RPV_CodeBlock>();
    Value* jitSlowPathData = funcCtx->GetValueAtEntry<RPV_JitSlowPathDataForSaveRegStub>();

    coroCtx->setName("coroCtx");
    stackBase->setName("stackBase");
    dfgCodeBlock->setName("dfgCodeBlock");
    jitSlowPathData->setName("jitSlowPathData");

    // Save all registers to the spill area
    //
    Value* offset = CreateCallToDeegenCommonSnippet(module.get(), "GetStackRegSpillRegionOffsetFromDfgCodeBlock", dfgCodeBlock, entryBB);
    ReleaseAssert(llvm_value_has_type<uint64_t>(offset));
    Value* regionStart = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), stackBase, { offset }, "", entryBB);
    ForEachDfgRegAllocRegister(
        [&](X64Reg reg)
        {
            size_t seqOrd = GetDfgRegAllocSequenceOrdForReg(reg);
            Value* dstPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), regionStart, { CreateLLVMConstantInt<uint64_t>(ctx, seqOrd) }, "", entryBB);
            Value* regVal = RegisterPinningScheme::GetRegisterValueAtEntry(func, reg);
            new StoreInst(regVal, dstPtr, false /*isVolatile*/, Align(8), entryBB);
        });

    // Create dispatch to the real AOT slow path function
    //
    ReleaseAssert(funcName.ends_with("_save_registers"));
    std::string destFuncName = funcName.substr(0, funcName.length() - strlen("_save_registers"));
    Function* destFn = RegisterPinningScheme::CreateFunction(module.get(), destFuncName);

    CallInst* ci = funcCtx->PrepareDispatch<JitAOTSlowPathInterface>()
        .Set<RPV_StackBase>(stackBase)
        .Set<RPV_CodeBlock>(dfgCodeBlock)
        .Set<RPV_JitSlowPathData>(jitSlowPathData)
        .Dispatch(destFn, entryBB);

    // If this is a quickening slow path, we are done since there's no extra arguments.
    // If this is a normal AOT slow path, we need to load the extra arguments from the temp buffer and pass them
    //
    if (bic.m_processKind == BytecodeIrComponentKind::SlowPath)
    {
        constexpr size_t baseBufOffset = offsetof_member_v<&CoroutineRuntimeContext::m_dfgTempBuffer>;

        std::vector<uint64_t> extraArgOrds = AstSlowPath::GetDfgCallArgMapInSaveRegStub(bic);
        ReleaseAssert(extraArgOrds.size() <= CoroutineRuntimeContext::x_dfg_temp_buffer_size);

        for (size_t idx = 0; idx < extraArgOrds.size(); idx++)
        {
            // Load the argument as i64
            //
            size_t offsetBytes = baseBufOffset + 8 * idx;
            GetElementPtrInst* addr = GetElementPtrInst::CreateInBounds(
                llvm_type_of<uint8_t>(ctx),
                coroCtx,
                { CreateLLVMConstantInt<uint64_t>(ctx, offsetBytes) },
                "",
                ci /*insertBefore*/);
            Value* argVal = new LoadInst(llvm_type_of<uint64_t>(ctx), addr, "", ci);

            // Cast to actual type expected by the interface and pass it
            //
            uint32_t argOrd = SafeIntegerCast<uint32_t>(extraArgOrds[idx]);
            Value* castedVal = RegisterPinningScheme::EmitCastI64ToArgumentType(argVal, argOrd, ci);
            RegisterPinningScheme::SetExtraDispatchArgument(ci, argOrd, castedVal);
        }
    }
    else
    {
        ReleaseAssert(bic.m_processKind == BytecodeIrComponentKind::QuickeningSlowPath);
    }

    ValidateLLVMModule(module.get());
    RunLLVMOptimizePass(module.get());

    // The resulting module should contain only one non-empty function: the function we just generated
    //
    {
        bool foundResultFunc = false;
        for (Function& fn : *module.get())
        {
            if (!fn.empty())
            {
                ReleaseAssert(fn.getName() == funcName);
                foundResultFunc = true;
            }
        }
        ReleaseAssert(foundResultFunc);
    }

    return module;
}

}   // namespace dast
