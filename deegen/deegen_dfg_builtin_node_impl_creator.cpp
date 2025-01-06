#include "deegen_dfg_builtin_node_impl_creator.h"
#include "deegen_bytecode_operand.h"
#include "deegen_jit_codegen_logic_creator.h"
#include "deegen_osr_exit_placeholder.h"
#include "deegen_stencil_lowering_pass.h"
#include "dfg_reg_alloc_register_info.h"
#include "invoke_clang_helper.h"
#include "llvm_fcmp_extra_optimizations.h"
#include "tag_register_optimization.h"
#include "deegen_dfg_jit_regalloc_rt_call_wrapper.h"

namespace dast {

void DfgBuiltinNodeImplCreator::SetupWithRegAllocDisabled(size_t numOperands, bool hasOutput)
{
    ReleaseAssert(!IsSetupComplete());
    ReleaseAssert(numOperands != static_cast<size_t>(-1));
    m_numOperands = numOperands;
    m_isRegAllocEnabled = false;
    m_hasOutput = hasOutput;
}

void DfgBuiltinNodeImplCreator::SetupForRegAllocSubVariant(size_t numOperands, DfgNodeRegAllocSubVariant* sv)
{
    ReleaseAssert(!IsSetupComplete());
    ReleaseAssert(numOperands != static_cast<size_t>(-1));

    DfgNodeRegAllocVariant* owner = sv->GetOwner();
    DfgNodeRegAllocRootInfo* root = owner->m_owner;

    ReleaseAssert(!root->m_hasBrDecision);

    m_numOperands = numOperands;
    m_isRegAllocEnabled = true;
    m_hasOutput = root->m_hasOutput;

    StencilRegisterFileContextSetupHelper regCtx;
    for (size_t raIdx = 0; raIdx < root->m_operandInfo.size(); raIdx++)
    {
        PhyRegPurpose purpose = PhyRegPurpose::Operand(SafeIntegerCast<uint8_t>(raIdx));
        size_t opOrd = root->m_operandInfo[raIdx].m_opOrd;
        if (owner->IsInputOperandGPR(raIdx))
        {
            if (sv->IsGprOperandGroup1(raIdx))
            {
                SetOperandReg(opOrd, regCtx.ConsumeGprGroup1(purpose));
            }
            else
            {
                SetOperandReg(opOrd, regCtx.ConsumeGprGroup2(purpose));
            }
        }
        else
        {
            SetOperandReg(opOrd, regCtx.ConsumeFpr(purpose));
        }
    }

    if (m_hasOutput)
    {
        if (!sv->IsOutputReuseReg())
        {
            PhyRegPurpose purpose = PhyRegPurpose::Operand(SafeIntegerCast<uint8_t>(root->m_operandInfo.size()));
            if (owner->IsOutputGPR())
            {
                if (sv->IsOutputGroup1Reg())
                {
                    m_outputReg = regCtx.ConsumeGprGroup1(purpose);
                }
                else
                {
                    m_outputReg = regCtx.ConsumeGprGroup2(purpose);
                }
            }
            else
            {
                m_outputReg = regCtx.ConsumeFpr(purpose);
            }
        }
        else
        {
            size_t raIdx = sv->GetOutputReuseInputOrd();
            ReleaseAssert(raIdx < root->m_operandInfo.size());
            size_t opOrd = root->m_operandInfo[raIdx].m_opOrd;
            m_outputReg = GetOperandReg(opOrd);
            ReleaseAssertIff(owner->IsOutputGPR(), m_outputReg.IsGPR());
        }
    }

    size_t numGprPt = sv->GetTrueMaxGprPassthrus();
    size_t numFprPt = sv->GetTrueMaxFprPassthrus();
    size_t numGprPtGroup1 = sv->GetNumGroup1Passthrus();

    ReleaseAssert(numGprPtGroup1 <= numGprPt);

    for (size_t i = 0; i < numGprPtGroup1; i++)
    {
        regCtx.ConsumeGprGroup1(PhyRegPurpose::PassThru());
    }
    for (size_t i = 0; i < numGprPt - numGprPtGroup1; i++)
    {
        regCtx.ConsumeGprGroup2(PhyRegPurpose::PassThru());
    }
    for (size_t i = 0; i < numFprPt; i++)
    {
        regCtx.ConsumeFpr(PhyRegPurpose::PassThru());
    }

    ReleaseAssert(m_regPurposeCtx.get() == nullptr);
    m_regPurposeCtx = regCtx.FinalizeAndGet();
}

void DfgBuiltinNodeImplCreator::SetupForRegAllocVariant(size_t numOperands,
                                                        DfgNodeRegAllocVariant* variant,
                                                        size_t numGprPassthrus,
                                                        size_t numFprPassthrus,
                                                        bool outputReusesInputReg)
{
    ReleaseAssert(!IsSetupComplete());
    ReleaseAssert(numOperands != static_cast<size_t>(-1));

    DfgNodeRegAllocRootInfo* root = variant->m_owner;
    ReleaseAssert(!root->m_hasBrDecision);

    m_numOperands = numOperands;
    m_isRegAllocEnabled = true;
    m_hasOutput = root->m_hasOutput;

    StencilRegisterFileContextSetupHelper regCtx;

    for (size_t raIdx = 0; raIdx < root->m_operandInfo.size(); raIdx++)
    {
        PhyRegPurpose purpose = PhyRegPurpose::Operand(SafeIntegerCast<uint8_t>(raIdx));
        size_t opOrd = root->m_operandInfo[raIdx].m_opOrd;
        if (variant->IsInputOperandGPR(raIdx))
        {
            if (regCtx.HasAvailableGprGroup2())
            {
                SetOperandReg(opOrd, regCtx.ConsumeGprGroup2(purpose));
            }
            else
            {
                SetOperandReg(opOrd, regCtx.ConsumeGprGroup1(purpose));
            }
        }
        else
        {
            SetOperandReg(opOrd, regCtx.ConsumeFpr(purpose));
        }
    }

    ReleaseAssertImp(outputReusesInputReg, m_hasOutput);
    if (m_hasOutput)
    {
        if (!outputReusesInputReg)
        {
            PhyRegPurpose purpose = PhyRegPurpose::Operand(SafeIntegerCast<uint8_t>(root->m_operandInfo.size()));
            if (variant->IsOutputGPR())
            {
                if (regCtx.HasAvailableGprGroup2())
                {
                    m_outputReg = regCtx.ConsumeGprGroup2(purpose);
                }
                else
                {
                    m_outputReg = regCtx.ConsumeGprGroup1(purpose);
                }
            }
            else
            {
                m_outputReg = regCtx.ConsumeFpr(purpose);
            }
        }
        else
        {
            bool found = false;
            for (size_t raIdx = 0; raIdx < root->m_operandInfo.size(); raIdx++)
            {
                if (variant->IsInputOperandGPR(raIdx) == variant->IsOutputGPR())
                {
                    m_outputReg = GetOperandReg(root->m_operandInfo[raIdx].m_opOrd);
                    found = true;
                    break;
                }
            }
            ReleaseAssert(found);
            ReleaseAssertIff(variant->IsOutputGPR(), m_outputReg.IsGPR());
        }
    }

    for (size_t i = 0; i < numGprPassthrus; i++)
    {
        if (regCtx.HasAvailableGprGroup2())
        {
            regCtx.ConsumeGprGroup2(PhyRegPurpose::PassThru());
        }
        else
        {
            regCtx.ConsumeGprGroup1(PhyRegPurpose::PassThru());
        }
    }
    for (size_t i = 0; i < numFprPassthrus; i++)
    {
        regCtx.ConsumeFpr(PhyRegPurpose::PassThru());
    }

    ReleaseAssert(m_regPurposeCtx.get() == nullptr);
    m_regPurposeCtx = regCtx.FinalizeAndGet();
}

ExecutorFunctionContext* WARN_UNUSED DfgBuiltinNodeImplCreator::CreateFunction(llvm::LLVMContext& ctx)
{
    using namespace llvm;
    // If caller has provided a base module, just use it. Otherwise, create an empty module
    //
    if (m_module.get() == nullptr)
    {
        m_module = RegisterPinningScheme::CreateModule("generated_builtin_node_logic", ctx);
    }
    else
    {
        ReleaseAssert(&m_module->getContext() == &ctx);
    }

    ReleaseAssert(m_funcCtx.get() == nullptr);
    m_funcCtx = ExecutorFunctionContext::Create(DeegenEngineTier::DfgJIT, true /*isJitCode*/, false /*isReturnContinuation*/);

    m_funcCtx->CreateFunction(m_module.get(), x_implFuncName);
    return m_funcCtx.get();
}

llvm::CallInst* DfgBuiltinNodeImplCreator::CreateOrGetRuntimeConstant(llvm::Type* ty, size_t ord, int64_t lb, int64_t ub, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    if (DeegenPlaceholderUtils::IsConstantPlaceholderAlreadyDefined(GetModule(), ord))
    {
        m_stencilRcInserter.WidenRange(ord, lb, ub);
        return DeegenPlaceholderUtils::GetConstantPlaceholderForOperand(GetModule(), ord, ty, insertBefore);
    }
    else
    {
        m_stencilRcInserter.AddRawRuntimeConstant(ord, lb, ub);
        return DeegenPlaceholderUtils::CreateConstantPlaceholderForOperand(GetModule(), ord, ty, insertBefore);
    }
}

llvm::Value* DfgBuiltinNodeImplCreator::EmitGetStackSlotAddrFromRuntimeConstantOrd(size_t placeholderOrd, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = GetModule()->getContext();

    Value* slotOrd = CreateOrGetRuntimeConstant(llvm_type_of<uint64_t>(ctx),
                                                placeholderOrd,
                                                0 /*lowerBound*/,
                                                BcOpSlot::x_localOrdinalUpperBound /*upperBound*/,
                                                insertBefore);

    ReleaseAssert(llvm_value_has_type<uint64_t>(slotOrd));
    Value* addr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx),
                                                    GetFuncCtx()->GetValueAtEntry<RPV_StackBase>(),
                                                    { slotOrd },
                                                    "", insertBefore);

    ReleaseAssert(llvm_value_has_type<void*>(addr));
    return addr;
}

llvm::Value* WARN_UNUSED DfgBuiltinNodeImplCreator::EmitGetOperand(llvm::Type* desiredTy, size_t operandOrd, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    ReleaseAssert(operandOrd < GetNumOperands());
    ReleaseAssert(IsReasonableStorageType(desiredTy));

    if (IsOperandInReg(operandOrd))
    {
        X64Reg reg = GetOperandReg(operandOrd);
        uint32_t argOrd = RegisterPinningScheme::GetArgumentOrdinalForRegister(reg);
        Value* i64 = RegisterPinningScheme::GetArgumentAsInt64Value(GetFunction(), argOrd, insertBefore);
        ReleaseAssert(llvm_value_has_type<uint64_t>(i64));
        if (llvm_type_has_type<uint64_t>(desiredTy))
        {
            return i64;
        }
        if (llvm_type_has_type<void*>(desiredTy) || llvm_type_has_type<HeapPtr<void>>(desiredTy))
        {
            return new IntToPtrInst(i64, desiredTy, "", insertBefore);
        }
        ReleaseAssert(llvm_type_has_type<double>(desiredTy));
        return new BitCastInst(i64, desiredTy, "", insertBefore);
    }
    else
    {
        Value* addr = EmitGetStackSlotAddrFromRuntimeConstantOrd(operandOrd, insertBefore);
        LoadInst* bv = new LoadInst(desiredTy, addr, "", insertBefore);
        bv->setAlignment(Align(8));
        ReleaseAssert(bv->getType() == desiredTy);
        return bv;
    }
}

llvm::Value* WARN_UNUSED DfgBuiltinNodeImplCreator::EmitGetOperand(llvm::Type* desiredTy, size_t operandOrd, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    LLVMContext& ctx = GetModule()->getContext();
    UnreachableInst* dummy = new UnreachableInst(ctx, insertAtEnd);
    Value* res = EmitGetOperand(desiredTy, operandOrd, dummy);
    dummy->eraseFromParent();
    return res;
}

void DfgBuiltinNodeImplCreator::UnsafeEmitStoreHiddenOutputToStack(llvm::Value* outputVal, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    // Should only use this function to workaround the limitation that we require output to be reg-allocated
    //
    ReleaseAssert(m_isRegAllocEnabled && !m_hasOutput);
    ReleaseAssert(outputVal != nullptr && IsReasonableStorageType(outputVal->getType()));

    Value* addr = EmitGetStackSlotAddrFromRuntimeConstantOrd(100 /*placeholderOrd*/, insertBefore);
    new StoreInst(outputVal, addr, false /*isVolatile*/, Align(8), insertBefore);

    m_hasHiddenOutput = true;
}

void DfgBuiltinNodeImplCreator::UnsafeEmitStoreHiddenOutputToStack(llvm::Value* outputVal, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    LLVMContext& ctx = GetModule()->getContext();
    UnreachableInst* dummy = new UnreachableInst(ctx, insertAtEnd);
    UnsafeEmitStoreHiddenOutputToStack(outputVal, dummy);
    dummy->eraseFromParent();
}

llvm::CallInst* DfgBuiltinNodeImplCreator::CreateDispatchToFallthrough(llvm::Value* outputVal, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = GetModule()->getContext();

    ReleaseAssertIff(m_hasOutput, outputVal != nullptr);
    ReleaseAssertImp(outputVal != nullptr, IsReasonableStorageType(outputVal->getType()));

    Value* fallthroughTarget = CreateOrGetRuntimeConstant(llvm_type_of<void*>(ctx),
                                                          101 /*operandOrd*/,
                                                          1 /*valueLowerBound*/,
                                                          m_stencilRcInserter.GetLowAddrRangeUB(),
                                                          insertBefore);

    std::unordered_map<X64Reg, Value*> regValue;

    if (m_isRegAllocEnabled)
    {
        // Figure out all registers that need to be passed to the continuation
        //
        ForEachDfgRegAllocRegister([&](X64Reg reg) {
            PhyRegUseKind purpose = GetRegContext()->GetPhyRegPurpose(reg).Kind();
            if (purpose == PhyRegUseKind::ScratchUse)
            {
                return;
            }
            Value* regVal = RegisterPinningScheme::GetRegisterValueAtEntry(GetFunction(), reg);
            ReleaseAssert(!regValue.count(reg));
            regValue[reg] = regVal;
        });
    }

    // Store output if needed
    //
    if (m_hasOutput)
    {
        if (!m_isRegAllocEnabled)
        {
            Value* addr = EmitGetStackSlotAddrFromRuntimeConstantOrd(100 /*placeholderOrd*/, insertBefore);
            new StoreInst(outputVal, addr, false /*isVolatile*/, Align(8), insertBefore);
        }
        else
        {
            X64Reg reg = GetOutputReg();
            uint32_t argOrd = RegisterPinningScheme::GetArgumentOrdinalForRegister(reg);
            Value* outputAsI64 = EmitCastStorageTypeToI64(outputVal, insertBefore);
            ReleaseAssert(llvm_value_has_type<uint64_t>(outputAsI64));
            Value* castedVal = RegisterPinningScheme::EmitCastI64ToArgumentType(outputAsI64, argOrd, insertBefore);
            ReleaseAssert(regValue.count(reg));
            regValue[reg] = castedVal;
        }
    }

    CallInst* ci = GetFuncCtx()->PrepareDispatch<JitGeneratedCodeInterface>()
                       .Set<RPV_StackBase>(GetFuncCtx()->GetValueAtEntry<RPV_StackBase>())
                       .Set<RPV_CodeBlock>(GetFuncCtx()->GetValueAtEntry<RPV_CodeBlock>())
                       .Dispatch(fallthroughTarget, insertBefore);

    if (m_isRegAllocEnabled)
    {
        for (auto& it : regValue)
        {
            X64Reg reg = it.first;
            Value* regVal = it.second;
            RegisterPinningScheme::SetExtraDispatchArgument(ci, reg, regVal);
        }
    }

    return ci;
}

llvm::CallInst* DfgBuiltinNodeImplCreator::CreateDispatchToFallthrough(llvm::Value* outputVal, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    LLVMContext& ctx = GetModule()->getContext();
    UnreachableInst* dummy = new UnreachableInst(ctx, insertAtEnd);
    CallInst* result = CreateDispatchToFallthrough(outputVal, dummy);
    dummy->eraseFromParent();
    return result;
}

llvm::CallInst* DfgBuiltinNodeImplCreator::CreateDispatchForGuestLanguageFunctionReturn(llvm::Value* retStart, llvm::Value* numRets, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    ReleaseAssert(llvm_value_has_type<void*>(retStart));
    ReleaseAssert(llvm_value_has_type<uint64_t>(numRets));

    // Currently GuestLanguageFunctionReturn must not have reg alloc enabled, since the regs we store return info conflict with DFG reg alloc regs
    //
    ReleaseAssert(!IsRegAllocEnabled());

    Value* stackBase = GetFuncCtx()->GetValueAtEntry<RPV_StackBase>();

    Value* retAddr = CreateCallToDeegenCommonSnippet(GetModule(), "GetRetAddrFromStackBase", { stackBase }, insertBefore);
    return GetFuncCtx()->PrepareDispatch<ReturnContinuationInterface>()
        .Set<RPV_StackBase>(stackBase)
        .Set<RPV_RetValsPtr>(retStart)
        .Set<RPV_NumRetVals>(numRets)
        .Dispatch(retAddr, insertBefore);
}

llvm::CallInst* DfgBuiltinNodeImplCreator::CreateDispatchForGuestLanguageFunctionReturn(llvm::Value* retStart, llvm::Value* numRets, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    LLVMContext& ctx = GetModule()->getContext();
    UnreachableInst* dummy = new UnreachableInst(ctx, insertAtEnd);
    CallInst* result = CreateDispatchForGuestLanguageFunctionReturn(retStart, numRets, dummy);
    dummy->eraseFromParent();
    return result;
}

void DfgBuiltinNodeImplCreator::CreateDispatchToOsrExit(llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = GetModule()->getContext();

    Function* destFn = CreateOrGetOsrExitHandlerFunction(GetModule());

    // TODO: fix
    //
    Value* osrExitPoint = UndefValue::get(llvm_type_of<void*>(ctx));

    CallInst* ci = GetFuncCtx()->PrepareDispatch<JitAOTSlowPathSaveRegStubInterface>()
                       .Set<RPV_StackBase>(GetFuncCtx()->GetValueAtEntry<RPV_StackBase>())
                       .Set<RPV_CodeBlock>(GetFuncCtx()->GetValueAtEntry<RPV_CodeBlock>())
                       .Set<RPV_JitSlowPathDataForSaveRegStub>(osrExitPoint)
                       .Dispatch(destFn, insertBefore);

    if (m_isRegAllocEnabled)
    {
        ForEachDfgRegAllocRegister([&](X64Reg reg) {
            PhyRegUseKind purpose = GetRegContext()->GetPhyRegPurpose(reg).Kind();
            if (purpose == PhyRegUseKind::ScratchUse)
            {
                return;
            }
            Value* regVal = RegisterPinningScheme::GetRegisterValueAtEntry(GetFunction(), reg);
            RegisterPinningScheme::SetExtraDispatchArgument(ci, reg, regVal);
        });
    }
}

void DfgBuiltinNodeImplCreator::CreateDispatchToOsrExit(llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    LLVMContext& ctx = GetModule()->getContext();
    UnreachableInst* dummy = new UnreachableInst(ctx, insertAtEnd);
    CreateDispatchToOsrExit(dummy);
    dummy->eraseFromParent();
}

void DfgBuiltinNodeImplCreator::SetBaseModule(llvm::Module* module)
{
    using namespace llvm;
    ReleaseAssert(m_module.get() == nullptr);
    m_module = CloneModule(*module);
}

void DfgBuiltinNodeImplCreator::DetermineRegDemandForRegAllocVariant(dfg::NodeKind associatedNodeKind,
                                                                     std::string nodeName,
                                                                     size_t numOperands,
                                                                     DfgNodeRegAllocVariant* variant /*inout*/,
                                                                     const std::function<void(DfgBuiltinNodeImplCreator*)>& implGen,
                                                                     std::string* auditLogResult /*out*/)
{
    AnonymousFile auditFile;
    FILE* auditFp = auditFile.GetFStream("w");
    Auto(
        fclose(auditFp);
        if (auditLogResult != nullptr) { *auditLogResult = auditFile.GetFileContents(); }
    );

    auto getTestImpl = [&](size_t numGprs, size_t numFprs, bool outputReusesInputReg) -> std::unique_ptr<DfgBuiltinNodeImplCreator>
    {
        std::unique_ptr<DfgBuiltinNodeImplCreator> r(new DfgBuiltinNodeImplCreator(associatedNodeKind));
        r->SetVariantOrd(0);
        r->SetupForRegAllocVariant(numOperands, variant, numGprs, numFprs, outputReusesInputReg);
        implGen(r.get());
        r->DoLowering(true /*forRegisterDemandTest*/);
        return r;
    };

    auto getMaxGprAndFprPassthruInfo = [&](bool outputReusesInputReg) -> std::pair<size_t /*gpr*/, size_t /*fpr*/>
    {
        size_t numFprUsed = variant->GetNumFprOperands();
        size_t numGprUsed = variant->GetNumGprOperands();
        if (variant->m_owner->m_hasOutput)
        {
            if (!outputReusesInputReg)
            {
                if (variant->IsOutputGPR())
                {
                    numGprUsed++;
                }
                else
                {
                    numFprUsed++;
                }
            }
        }
        else
        {
            ReleaseAssert(!outputReusesInputReg);
        }

        // Similar to DfgJitImplCreator, it should be fine to assume there's plenety of scratch FPRs available without causing spilling.
        //
        ReleaseAssert(numFprUsed <= x_dfg_reg_alloc_num_fprs);
        size_t numFprPt = x_dfg_reg_alloc_num_fprs - numFprUsed;

        ReleaseAssert(numGprUsed <= x_dfg_reg_alloc_num_gprs);
        size_t maxGprPtPossible = x_dfg_reg_alloc_num_gprs - numGprUsed;

        std::unique_ptr<DfgBuiltinNodeImplCreator> baseImpl = getTestImpl(0 /*numGprs*/, numFprPt /*numFprs*/, outputReusesInputReg);
        RegisterDemandEstimationMetric baseMetric = baseImpl->m_stencilCodeMetrics;

        fprintf(auditFp, "%s\n", baseMetric.GetHeader().c_str());
        fprintf(auditFp, "#PT.GPR=%d, #PT.FPR=%d, Metric=%s\n", 0, static_cast<int>(numFprPt), baseMetric.GetAuditInfo().c_str());

        double bestResult = 0;
        size_t bestNumGprPt = 0;
        for (size_t numGprPt = 1; numGprPt <= maxGprPtPossible; numGprPt++)
        {
            std::unique_ptr<DfgBuiltinNodeImplCreator> impl = getTestImpl(numGprPt, numFprPt, outputReusesInputReg);
            RegisterDemandEstimationMetric metric = impl->m_stencilCodeMetrics;

            double rawCost = baseMetric.CompareWith(metric);
            double adjustedCost = rawCost - 1.891 * static_cast<double>(numGprPt);

            fprintf(auditFp, "#PT.GPR=%d, #PT.FPR=%d, Metric=%s", static_cast<int>(numGprPt), static_cast<int>(numFprPt), metric.GetAuditInfo().c_str());
            fprintf(auditFp, ", cost=%.2lf, score=%.2lf\n", rawCost, adjustedCost);

            if (adjustedCost < bestResult)
            {
                bestResult = adjustedCost;
                bestNumGprPt = numGprPt;
            }
        }

        fprintf(auditFp, "Determined maximum passthrus: GPR=%d, FPR=%d\n", static_cast<int>(bestNumGprPt), static_cast<int>(numFprPt));
        return std::make_pair(bestNumGprPt, numFprPt);
    };

    DfgNodeRegAllocRootInfo* rootInfo = variant->m_owner;
    ReleaseAssert(!rootInfo->m_hasBrDecision);

    {
        fprintf(auditFp, "#### Determining register alloc info for %s (operands: \n", nodeName.c_str());
        if (numOperands > 0)
        {
            std::vector<char> descs;
            descs.resize(numOperands, 's' /*value*/);
            ReleaseAssert(variant->m_isOperandGPR.size() == variant->m_owner->m_operandInfo.size());
            for (size_t idx = 0; idx < variant->m_isOperandGPR.size(); idx++)
            {
                size_t opOrd = variant->m_owner->m_operandInfo[idx].m_opOrd;
                ReleaseAssert(opOrd < numOperands && descs[opOrd] == 's');
                descs[opOrd] = (variant->m_isOperandGPR[opOrd] ? 'g' : 'f');
            }
            for (char ch : descs) { fprintf(auditFp, "%c", ch); }
        }
        else
        {
            fprintf(auditFp, "none");
        }
        fprintf(auditFp, ", output=");
        if (variant->m_owner->m_hasOutput)
        {
            char ch = (variant->IsOutputGPR() ? 'g' : 'f');
            fprintf(auditFp, "%c", ch);
        }
        else
        {
            fprintf(auditFp, "none");
        }
        fprintf(auditFp, ")\n\n");
    }

    std::pair<size_t, size_t> gprAndFprInfo = getMaxGprAndFprPassthruInfo(false /*outputReusesInputReg*/);
    variant->SetMaxGprPassthrus(gprAndFprInfo.first);
    variant->SetMaxFprPassthrus(gprAndFprInfo.second);

    if (rootInfo->m_hasOutput)
    {
        fprintf(auditFp, "Checking if it is beneficial to allow output reuse an input register:\n");

        bool canReuse;
        if (variant->IsOutputGPR())
        {
            canReuse = (variant->GetNumGprOperands() > 0);
        }
        else
        {
            canReuse = (variant->GetNumFprOperands() > 0);
        }
        if (!canReuse)
        {
            fprintf(auditFp, "Output cannot reuse input reg since no input has matching reg type.\n");
            variant->SetWhetherOutputWorthReuseRegister(false);
        }
        else
        {
            std::pair<size_t, size_t> info = getMaxGprAndFprPassthruInfo(true /*outputReusesInputReg*/);
            bool worth = false;
            if (info.first < variant->GetMaxGprPassthrus() || info.second < variant->GetMaxFprPassthrus())
            {
                fprintf(auditFp, "[WARNING]: reusing output reduced #passthroughs available, which is unexpected! (Builtin-node name = %s)\n",
                        nodeName.c_str());
                worth = false;
            }
            else
            {
                if (variant->IsOutputGPR())
                {
                    if (info.first > variant->GetMaxGprPassthrus() + 1 || info.second > variant->GetMaxFprPassthrus())
                    {
                        fprintf(auditFp, "[WARNING]: reusing output increased #passthroughs by more than 1, which is unexpected! (Builtin-node name = %s)\n",
                                nodeName.c_str());
                    }
                    worth = (info.first >= variant->GetMaxGprPassthrus() + 1);
                }
                else
                {
                    if (info.first > variant->GetMaxGprPassthrus() || info.second > variant->GetMaxFprPassthrus() + 1)
                    {
                        fprintf(auditFp, "[WARNING]: reusing output increased #passthroughs by more than 1, which is unexpected! (Builtin-node name = %s)\n",
                                nodeName.c_str());
                    }
                    worth = (info.second >= variant->GetMaxFprPassthrus() + 1);
                }
            }
            variant->SetWhetherOutputWorthReuseRegister(worth);
            fprintf(auditFp, "Determined outputWorthReuseInputReg = %s.\n", (worth ? "true" : "false"));
        }
    }
}

std::string DfgBuiltinNodeImplCreator::GetCCallWrapperPrefix()
{
    ReleaseAssert(m_nodeName != "");
    return "dfg_builtin_node_" + m_nodeName + "_" + std::to_string(GetVariantOrd());
}

void DfgBuiltinNodeImplCreator::DoLowering(bool forRegisterDemandTest)
{
    using namespace llvm;
    ReleaseAssert(IsSetupComplete());
    ReleaseAssertImp(forRegisterDemandTest, m_isRegAllocEnabled);
    LLVMContext& ctx = GetModule()->getContext();

    // Common optimizations and preprations for stencil generation
    //
    std::unordered_set<std::string> fnNamesNeedsToBeWrapped;
    {
        Module* m = GetModule();
        ValidateLLVMModule(m);

        Function* implFunc = m->getFunction(x_implFuncName);
        ReleaseAssert(implFunc != nullptr);
        ReleaseAssert(implFunc == GetFunction());

        // Run the common optimization passes
        //
        DesugarAndSimplifyLLVMModule(m, DesugaringLevel::Top);

        RunTagRegisterOptimizationPass(implFunc);

        DesugarAndSimplifyLLVMModule(m, DesugaringLevel::PerFunctionSimplifyOnlyAggresive);

        m_stencilRcDefinitions = m_stencilRcInserter.RunOnFunction(implFunc);

        RunLLVMOptimizePass(m);

        DeegenExtraLLVMOptPass_FuseTwoNaNChecksIntoOne(m);
        DeegenExtraLLVMOptPass_FuseNaNAndCmpCheckIntoOne(m);

        ReleaseAssert(m->getFunction(x_implFuncName) == implFunc);
        ValidateLLVMModule(m);

        for (Function& func : *m)
        {
            func.setLinkage(GlobalValue::ExternalLinkage);
            func.setComdat(nullptr);
        }

        if (m_isRegAllocEnabled)
        {
            ReleaseAssert(DfgRegAllocCCallAsmTransformPass::TryRewritePreserveMostToPreserveAll(implFunc, fnNamesNeedsToBeWrapped /*out*/));
        }

        m_module = ExtractFunction(m, x_implFuncName);
        implFunc = m_module->getFunction(x_implFuncName);
        ReleaseAssert(implFunc != nullptr);
        GetFuncCtx()->ResetFunction(implFunc);
    }

    // Run the stencil lowering pass in preparation for stencil generation
    // Note that for builtin nodes, even if the node is implemented by concatenating multiple stencils,
    // the fallthrough destination of each stencil is always its immediate next stencil, not the next DFG node,
    // so the isLastStencilInBytecode below should be true
    //
    DeegenStencilLoweringPass slPass = DeegenStencilLoweringPass::RunIrRewritePhase(
        GetFunction(), true /*isLastStencilInBytecode*/, DeegenPlaceholderUtils::FindFallthroughPlaceholderSymbolName(m_stencilRcDefinitions));

    {
        std::string asmFile = CompileLLVMModuleToAssemblyFileForStencilGeneration(
            GetModule(),
            llvm::Reloc::Static,
            llvm::CodeModel::Small,
            [](TargetOptions& opt) {
                // This is the option that is equivalent to the clang -fdata-sections flag
                // Put each data symbol into a separate data section so our stencil creation pass can produce more efficient result
                //
                opt.DataSections = true;
            });

        slPass.ParseAsmFile(asmFile);
    }

    // If reg alloc is enabled, C calls must be specially handled
    //
    if (m_isRegAllocEnabled)
    {
        DfgRegAllocCCallAsmTransformPass cctPass(slPass.m_workFile.get(), m_module.get());
        std::string regAllocCCallWrapperPrefix = GetCCallWrapperPrefix();
        if (!cctPass.RunPass(regAllocCCallWrapperPrefix))
        {
            m_module->dump();
            // Always print 'typecheck implementation' as the error message, since this error shouldn't hit for DFG builtin nodes
            //
            fprintf(stderr, "[ERROR] Cannot enable register allocation for a typecheck implementation because it "
                            "contains a C call that is incompatible with reg alloc (reason: %s). "
                            "You may want to mark the functions as always_inline (if commonly used) or preserve_most (if rarely used).\n",
                    cctPass.GetFailReasonStringName(cctPass.m_failReason));
            abort();
        }

        m_rtCCallWrappers = cctPass.m_calledFns;

        for (auto& it : m_rtCCallWrappers)
        {
            ReleaseAssert(fnNamesNeedsToBeWrapped.count(it.m_fnName));
        }
        ReleaseAssert(m_rtCCallWrappers.size() == fnNamesNeedsToBeWrapped.size());
    }

    // Run the ASM phase of the stencil lowering pass
    //
    slPass.RunAsmRewritePhase();

    ReleaseAssert(slPass.m_callIcLoweringResults.empty());
    ReleaseAssert(slPass.m_genericIcLoweringResults.empty());

    // Populate StencilCodeMetrics
    //
    m_stencilCodeMetrics.m_isValid = true;
    m_stencilCodeMetrics.m_fastPathInst = slPass.m_numInstructionsInFastPath;
    m_stencilCodeMetrics.m_slowPathInst = slPass.m_numInstructionsInSlowPath;
    m_stencilCodeMetrics.m_icTotalInst = 0;
    m_stencilCodeMetrics.m_icAvgInst = 0;
    m_stencilCodeMetrics.m_fastPathStackOp = slPass.m_numStackOperationsInFastPath;
    m_stencilCodeMetrics.m_slowPathStackOp = slPass.m_numStackOperationsInSlowPath;
    m_stencilCodeMetrics.m_icAvgStackOp = 0;

    // If there is a C call, due to stack alignment issues there needs to be a push/pop even if otherwise it needs no stack.
    // Treat them as free.
    //
    if (m_rtCCallWrappers.size() > 0)
    {
        ReleaseAssert(slPass.m_numStackPopBeforeTailCallInFastPath <= m_stencilCodeMetrics.m_fastPathStackOp);
        m_stencilCodeMetrics.m_fastPathStackOp -= slPass.m_numStackPopBeforeTailCallInFastPath;

        ReleaseAssert(slPass.m_numStackPopBeforeTailCallInSlowPath <= m_stencilCodeMetrics.m_slowPathStackOp);
        m_stencilCodeMetrics.m_slowPathStackOp -= slPass.m_numStackPopBeforeTailCallInSlowPath;
    }

    if (forRegisterDemandTest)
    {
        return;
    }

    // Generate copy-and-patch stencil
    //
    std::string objFile = CompileAssemblyFileToObjectFile(slPass.m_primaryPostTransformAsmFile, " -fno-pic -fno-pie ");
    m_stencil = DeegenStencil::ParseMainLogic(ctx, true /*isLastStencilInBytecode*/, objFile);
    if (m_isRegAllocEnabled)
    {
        m_stencil.ParseRegisterPatches(GetRegContext());

        for (const std::string& calledFnName : m_stencil.m_rtCallFnNamesForRegAllocEnabledStencil)
        {
            if (!calledFnName.starts_with("deegen_dfg_rt_wrapper_"))
            {
                Function* fn = GetModule()->getFunction(calledFnName);
                ReleaseAssert(fn != nullptr && fn->hasFnAttribute(Attribute::NoReturn));
            }
        }
    }

    DeegenStencilCodegenResult cgResult = m_stencil.PrintCodegenFunctions(
        GetNumOperands() + m_extraLiteralOps.size() /*numOperands*/,
        0 /*numGenericIcCaptures*/,
        m_stencilRcDefinitions /*placeholderDefs*/);

    std::unique_ptr<Module> cgMod = cgResult.GenerateCodegenLogicLLVMModule(GetModule());

    ReleaseAssert(m_nodeName != "");
    std::string cgFnName = "__deegen_dfg_codegen_builtin_node_" + m_nodeName + "_" + std::to_string(GetVariantOrd());

    std::unique_ptr<DfgJitCodegenFnProto> cg = DfgJitCodegenFnProto::Create(cgMod.get(), cgFnName);

    Function* fastPathPatchFn = cgMod->getFunction("deegen_do_codegen_fastpath");
    Function* slowPathPatchFn = cgMod->getFunction("deegen_do_codegen_slowpath");
    Function* dataSecPatchFn = cgMod->getFunction("deegen_do_codegen_datasec");
    ReleaseAssert(fastPathPatchFn != nullptr && slowPathPatchFn != nullptr && dataSecPatchFn != nullptr);

    Function* cgFn = cg->GetFunction();
    CopyFunctionAttributes(cgFn /*dst*/, fastPathPatchFn /*src*/);

    BasicBlock* entryBB = BasicBlock::Create(ctx, "", cgFn);

    // Align the data section pointer if necessary
    //
    Value* dataSecAddr = cg->EmitGetAlignedDataSectionPtr(cg->GetJitUnalignedDataSecPtr(), cgResult.m_dataSecAlignment, entryBB);
    ReleaseAssert(llvm_value_has_type<void*>(dataSecAddr));

    Value* fastPathAddr = cg->GetJitFastPathPtr();
    Value* slowPathAddr = cg->GetJitSlowPathPtr();

    // This is a BuiltinNodeOperandsInfoBase pointer if m_shouldUseCustomInterface is true,
    // or a NodeRegAllocInfo pointer if m_shouldUseCustomInterface is false
    //
    Value* ssaOperandInfo = cg->GetNodeRegAllocInfoPtr();

    // The list of placeholder values, in the order expected by the codegen function
    //
    std::vector<Value*> valList;

    // This is the dest address that we need to fill later
    //
    valList.push_back(nullptr);

    Value* fastPathAddrI64 = new PtrToIntInst(fastPathAddr, llvm_type_of<uint64_t>(ctx), "", entryBB);
    valList.push_back(fastPathAddrI64);

    Value* slowPathAddrI64 = new PtrToIntInst(slowPathAddr, llvm_type_of<uint64_t>(ctx), "", entryBB);
    valList.push_back(slowPathAddrI64);

    valList.push_back(nullptr);   // icCodeAddr
    valList.push_back(nullptr);   // icDataSecAddr

    Value* dataSecAddrI64 = new PtrToIntInst(dataSecAddr, llvm_type_of<uint64_t>(ctx), "", entryBB);
    valList.push_back(dataSecAddrI64);

    // Slot 100 (outputSlot)
    //
    if (!m_hasOutput && !m_hasHiddenOutput)
    {
        valList.push_back(nullptr);
    }
    else
    {
        if (m_shouldUseCustomInterface)
        {
            Value* outputSlot = CreateCallToDeegenCommonSnippet(cgMod.get(), "GetDfgBuiltinNodeOutputPhysicalSlot", { ssaOperandInfo }, entryBB);
            ReleaseAssert(llvm_value_has_type<uint64_t>(outputSlot));
            valList.push_back(outputSlot);
        }
        else
        {
            Value* outputSlot = CreateCallToDeegenCommonSnippet(cgMod.get(), "GetDfgNodePhysicalSlotForOutput", { ssaOperandInfo }, entryBB);
            ReleaseAssert(llvm_value_has_type<uint64_t>(outputSlot));
            valList.push_back(outputSlot);
        }
    }

    // Slot 101 (fallthroughAddr)
    //
    {
        GetElementPtrInst* fastPathEnd = GetElementPtrInst::CreateInBounds(
            llvm_type_of<uint8_t>(ctx),
            fastPathAddr,
            { CreateLLVMConstantInt<uint64_t>(ctx, cgResult.m_fastPathPreFixupCode.size()) },
            "", entryBB);
        Value* fastPathEndI64 = new PtrToIntInst(fastPathEnd, llvm_type_of<uint64_t>(ctx), "", entryBB);
        valList.push_back(fastPathEndI64);
    }

    // Slot 103 (slowPathDataOffset)
    //
    valList.push_back(cg->GetSlowPathDataOffset());
    // Slot 104 (BaselineCodeBlock32/DfgCodeBlock32)
    //
    valList.push_back(cg->GetJitCodeBlock32());
    // Slot 105 (conditionBrDecision, never used)
    //
    valList.push_back(nullptr);

    // Slot for each SSA value operand, if not reg-allocated
    //
    for (size_t opOrd = 0; opOrd < GetNumOperands(); opOrd++)
    {
        if (IsOperandInReg(opOrd))
        {
            valList.push_back(nullptr);
        }
        else
        {
            if (m_shouldUseCustomInterface)
            {
                Value* slot = CreateCallToDeegenCommonSnippet(
                    cgMod.get(), "GetDfgBuiltinNodeInputPhysicalSlot", { ssaOperandInfo, CreateLLVMConstantInt<uint64_t>(ctx, opOrd) }, entryBB);
                ReleaseAssert(llvm_value_has_type<uint64_t>(slot));
                valList.push_back(slot);
            }
            else
            {
                Value* slot = CreateCallToDeegenCommonSnippet(
                    cgMod.get(), "GetDfgPhysicalSlotForSSAInput", { ssaOperandInfo, CreateLLVMConstantInt<uint64_t>(ctx, opOrd) }, entryBB);
                ReleaseAssert(llvm_value_has_type<uint64_t>(slot));
                valList.push_back(slot);
            }
        }
    }

    // Value for each literal operand
    //
    Value* nsdPtr = cg->GetNodeSpecificDataPtr();
    for (LiteralInfo& info : m_extraLiteralOps)
    {
        size_t offsetInNsd = info.m_offsetInNsd;
        size_t sizeInNsd = info.m_sizeInNsd;
        ReleaseAssert(is_power_of_2(sizeInNsd) && sizeInNsd <= 8);
        GetElementPtrInst* addr = GetElementPtrInst::CreateInBounds(
            llvm_type_of<uint8_t>(ctx),
            nsdPtr,
            { CreateLLVMConstantInt<uint64_t>(ctx, offsetInNsd) },
            "", entryBB);

        // For simplicity, always conservely use Align(1)
        //
        Value* val = new LoadInst(Type::getIntNTy(ctx, static_cast<uint32_t>(sizeInNsd * 8)), addr, "", false /*isVolatile*/, Align(1), entryBB);
        if (sizeInNsd < 8)
        {
            if (info.m_shouldSignExt)
            {
                val = new SExtInst(val, llvm_type_of<uint64_t>(ctx), "", entryBB);
            }
            else
            {
                val = new ZExtInst(val, llvm_type_of<uint64_t>(ctx), "", entryBB);
            }
        }
        ReleaseAssert(llvm_value_has_type<uint64_t>(val));
        valList.push_back(val);
    }

    // Validate that the codegen function does not use operands we do not provide
    //
    {
        auto validate = [&](Function* patchFn)
        {
            ReleaseAssert(patchFn->arg_size() == valList.size());

            // Skip the first arg, which is the destAddr that we will provide later
            //
            for (uint32_t idx = 1; idx < valList.size(); idx++)
            {
                Argument* arg = patchFn->getArg(idx);
                ReleaseAssert(llvm_value_has_type<uint64_t>(arg));
                if (valList[idx] == nullptr)
                {
                    ReleaseAssert(arg->user_empty());
                }
                else
                {
                    ReleaseAssert(llvm_value_has_type<uint64_t>(valList[idx]));
                }
            }
        };
        validate(fastPathPatchFn);
        validate(slowPathPatchFn);
        validate(dataSecPatchFn);
    }

    for (uint32_t idx = 1; idx < valList.size(); idx++)
    {
        if (valList[idx] == nullptr)
        {
            valList[idx] = UndefValue::get(llvm_type_of<uint64_t>(ctx));
        }
    }

    // Generate the audit log for the generated code
    //
    std::string jitCodeAuditLog;
    {
        if (m_isRegAllocEnabled)
        {
            jitCodeAuditLog += "# JIT code for built-in node " + m_nodeName + " (operands: ";
            for (size_t idx = 0; idx < m_numOperands; idx++)
            {
                if (IsOperandInReg(idx))
                {
                    X64Reg reg = GetOperandReg(idx);
                    if (reg.IsFPR())
                    {
                        jitCodeAuditLog += "f";
                    }
                    else
                    {
                        jitCodeAuditLog += (reg.MachineOrd() < 8) ? "g1" : "g2";
                    }
                }
                else
                {
                    jitCodeAuditLog += "s";
                }
            }
            if (m_numOperands == 0)
            {
                jitCodeAuditLog += "none";
            }
            jitCodeAuditLog += ", output: ";
            if (!m_hasOutput && !m_hasHiddenOutput)
            {
                jitCodeAuditLog += "none";
            }
            else if (m_hasHiddenOutput)
            {
                ReleaseAssert(!m_hasOutput);
                jitCodeAuditLog += "s";
            }
            else
            {
                X64Reg outputReg = GetOutputReg();
                bool found = false;
                for (size_t idx = 0; idx < m_numOperands; idx++)
                {
                    if (IsOperandInReg(idx) && outputReg == GetOperandReg(idx))
                    {
                        ReleaseAssert(!found);
                        jitCodeAuditLog += "reuses input #" + std::to_string(idx);
                        found = true;
                    }
                }
                if (!found)
                {
                    if (outputReg.IsFPR())
                    {
                        jitCodeAuditLog += "f";
                    }
                    else
                    {
                        jitCodeAuditLog += (outputReg.MachineOrd() < 8) ? "g1" : "g2";
                    }
                }
            }
            jitCodeAuditLog += ")\n#\n";
        }
        else
        {
            jitCodeAuditLog += "# JIT code for built-in node " + m_nodeName + " (reg alloc disabled)\n#\n";
        }

        Triple targetTriple = m_stencil.m_triple;

        std::string fastPathAuditLog;
        if (m_isRegAllocEnabled)
        {
            fastPathAuditLog = DumpStencilDisassemblyForAuditPurpose(
                targetTriple,
                false /*isDataSection*/,
                cgResult.m_fastPathPreFixupCode,
                cgResult.m_fastPathRelocMarker,
                cgResult.m_fastPathRegPatches,
                GetRegContext(),
                "# " /*linePrefix*/);
        }
        else
        {
            fastPathAuditLog = DumpStencilDisassemblyForAuditPurpose(
                targetTriple, false /*isDataSection*/, cgResult.m_fastPathPreFixupCode, cgResult.m_fastPathRelocMarker, "# " /*linePrefix*/);
        }

        jitCodeAuditLog += std::string("# Fast Path:\n") + fastPathAuditLog;

        if (cgResult.m_slowPathPreFixupCode.size() > 0)
        {
            std::string slowPathAuditLog;
            if (m_isRegAllocEnabled)
            {
                slowPathAuditLog = DumpStencilDisassemblyForAuditPurpose(
                    targetTriple,
                    false /*isDataSection*/,
                    cgResult.m_slowPathPreFixupCode,
                    cgResult.m_slowPathRelocMarker,
                    cgResult.m_slowPathRegPatches,
                    GetRegContext(),
                    "# " /*linePrefix*/);
            }
            else
            {
                slowPathAuditLog = DumpStencilDisassemblyForAuditPurpose(
                    targetTriple, false /*isDataSection*/, cgResult.m_slowPathPreFixupCode, cgResult.m_slowPathRelocMarker, "# " /*linePrefix*/);
            }

            jitCodeAuditLog += std::string("#\n# Slow Path:\n") + slowPathAuditLog;
        }

        if (cgResult.m_dataSecPreFixupCode.size() > 0)
        {
            std::string dataSecAuditLog = DumpStencilDisassemblyForAuditPurpose(
                targetTriple, true /*isDataSection*/, cgResult.m_dataSecPreFixupCode, cgResult.m_dataSecRelocMarker, "# " /*linePrefix*/);

            jitCodeAuditLog += std::string("#\n# Data Section:\n") + dataSecAuditLog;
        }

        jitCodeAuditLog += std::string("#\n\n");
    }

    // Emit copy-and-patch logic
    //
    EmitCopyLogicForJitCodeGen(cgMod.get(), cgResult.m_fastPathPreFixupCode, fastPathAddr, "deegen_fastpath_prefixup_code", entryBB /*insertAtEnd*/, false /*mustBeExact*/);
    EmitCopyLogicForJitCodeGen(cgMod.get(), cgResult.m_slowPathPreFixupCode, slowPathAddr, "deegen_slowpath_prefixup_code", entryBB /*insertAtEnd*/, false /*mustBeExact*/);
    EmitCopyLogicForJitCodeGen(cgMod.get(), cgResult.m_dataSecPreFixupCode, dataSecAddr, "deegen_datasec_prefixup_code", entryBB /*insertAtEnd*/, false /*mustBeExact*/);

    {
        auto emitPatchLogic = [&](Function* callee, Value* dstAddr)
        {
            ReleaseAssert(llvm_value_has_type<void*>(dstAddr));
            ReleaseAssert(valList.size() > 0 && valList.size() == callee->arg_size());
            valList[0] = dstAddr;

            for (uint32_t i = 0; i < valList.size(); i++)
            {
                ReleaseAssert(valList[i] != nullptr);
                ReleaseAssert(valList[i]->getType() == callee->getArg(i)->getType());
            }

            ReleaseAssert(llvm_type_has_type<void>(callee->getReturnType()));
            CallInst::Create(callee, valList, "", entryBB);

            ReleaseAssert(callee->hasExternalLinkage());
            ReleaseAssert(!callee->empty());
            ReleaseAssert(!callee->hasFnAttribute(Attribute::NoInline));
            callee->setLinkage(GlobalValue::InternalLinkage);
            callee->addFnAttr(Attribute::AlwaysInline);
        };

        emitPatchLogic(fastPathPatchFn, fastPathAddr);
        emitPatchLogic(slowPathPatchFn, slowPathAddr);
        emitPatchLogic(dataSecPatchFn, dataSecAddr);
    }

    Value* advancedJitFastPathAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), fastPathAddr,
                                                                       { CreateLLVMConstantInt<uint64_t>(ctx, cgResult.m_fastPathPreFixupCode.size()) }, "", entryBB);
    Value* advancedJitSlowPathAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathAddr,
                                                                       { CreateLLVMConstantInt<uint64_t>(ctx, cgResult.m_slowPathPreFixupCode.size()) }, "", entryBB);
    Value* advancedJitDataSecAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), dataSecAddr,
                                                                      { CreateLLVMConstantInt<uint64_t>(ctx, cgResult.m_dataSecPreFixupCode.size()) }, "", entryBB);

    // Update the main codegen state
    //
    using PCS = dfg::PrimaryCodegenState;
    Value* pcs = cg->AsDfgJIT()->GetPrimaryCodegenStatePtr();
    WriteCppStructMember<&PCS::m_fastPathAddr>(pcs, advancedJitFastPathAddr, entryBB);
    WriteCppStructMember<&PCS::m_slowPathAddr>(pcs, advancedJitSlowPathAddr, entryBB);
    WriteCppStructMember<&PCS::m_dataSecAddr>(pcs, advancedJitDataSecAddr, entryBB);

    // Emit register patch logic if needed
    //
    if (m_isRegAllocEnabled)
    {
        auto emitRegPatchLogic = [&](EncodedStencilRegPatchStream& data, Value* codePtr)
        {
            ReleaseAssert(llvm_value_has_type<void*>(codePtr));
            ReleaseAssert(data.IsValid());
            if (!data.IsEmpty())
            {
                Constant* patchData = data.EmitDataAsLLVMConstantGlobal(cgMod.get());
                CreateCallToDeegenCommonSnippet(
                    cgMod.get(),
                    "ApplyDfgRuntimeRegPatchData",
                    {
                        codePtr,
                        cg->GetRegAllocStateForCodegenPtr(),
                        patchData
                    },
                    entryBB);
            }
        };

        emitRegPatchLogic(cgResult.m_fastPathRegPatches, fastPathAddr);
        emitRegPatchLogic(cgResult.m_slowPathRegPatches, slowPathAddr);
    }
    else
    {
        ReleaseAssert(!cgResult.NeedRegPatchPhase());
    }

    ReturnInst::Create(ctx, nullptr, entryBB);

    // Create the real codegen function (which calls the impl function we just created
    //
    cg->CreateWrapper(cgMod.get(), cgFnName);

    for (Function& fn : *cgMod.get()) { fn.setDSOLocal(true); }

    ValidateLLVMModule(cgMod.get());
    RunLLVMOptimizePass(cgMod.get());

    cgFn = cgMod->getFunction(cgFnName);
    ReleaseAssert(cgFn != nullptr);

    // The module should only have one function with non-empty body -- the codegen function
    //
    for (Function& func : cgMod->functions())
    {
        if (!func.empty())
        {
            ReleaseAssert(func.getName() == cgFnName);
        }
    }

    JitCodeGenLogicCreator::SetSectionsForCodegenModule(DeegenEngineTier::DfgJIT, cgMod.get(), cgFnName);

    m_codegenResult.m_fastPathLength = cgResult.m_fastPathPreFixupCode.size();
    m_codegenResult.m_slowPathLength = cgResult.m_slowPathPreFixupCode.size();
    m_codegenResult.m_dataSecLength = cgResult.m_dataSecPreFixupCode.size();
    m_codegenResult.m_dataSecAlignment = cgResult.m_dataSecAlignment;
    m_codegenResult.m_cgFnName = cgFnName;
    ReleaseAssert(cgFn->arg_size() == 4);
    m_codegenResult.m_mayProvideNullNsd = cgFn->getArg(2)->use_empty();
    m_codegenResult.m_mayProvideNullPhySlotInfo = cgFn->getArg(1)->use_empty();
    m_codegenResult.m_isCustomCodegenInterface = m_shouldUseCustomInterface;
    m_codegenResult.m_variantOrd = GetVariantOrd();
    m_codegenResult.m_jitCodeAuditLog = jitCodeAuditLog;
    if (m_isRegAllocEnabled)
    {
        m_codegenResult.m_ccwRequests = JitCodeGenLogicCreator::GenerateCCallWrapperRequests(
            m_stencil.m_usedFpuRegs, GetCCallWrapperPrefix(), m_rtCCallWrappers);
    }
    else
    {
        m_codegenResult.m_ccwRequests.clear();
        ReleaseAssert(m_rtCCallWrappers.size() == 0);
    }
    m_codegenResult.m_cgMod = std::move(cgMod);
    m_codegenResult.m_implMod = std::move(m_module);
}

void DfgBuiltinNodeCodegenProcessorBase::ProcessWithRegAllocDisabled(bool hasOutput)
{
    ReleaseAssert(!m_processed);
    m_processed = true;

    DfgBuiltinNodeImplCreator impl(AssociatedNodeKind(), NodeName());
    impl.SetVariantOrd(0);
    impl.SetupWithRegAllocDisabled(NumOperands(), hasOutput);
    GenerateImpl(&impl);
    impl.DoLowering(false /*forRegisterDemandTest*/);
    m_isRegAllocEnabled = false;
    m_jitCodeAuditLog = impl.GetCodegenResult().m_jitCodeAuditLog;
    m_cgInfoForRegDisabled = std::make_unique<DfgBuiltinNodeVariantCodegenInfo>(std::move(impl.GetCodegenResult()));
}

void DfgBuiltinNodeCodegenProcessorBase::ProcessWithRegAllocEnabled(std::unique_ptr<DfgNodeRegAllocRootInfo> rootInfo)
{
    ReleaseAssert(!m_processed);
    m_processed = true;

    m_isRegAllocEnabled = true;
    m_rootInfo = std::move(rootInfo);
    if (m_rootInfo->m_variants.size() == 0)
    {
        m_rootInfo->GenerateVariants();
    }
    ReleaseAssert(m_rootInfo->m_variants.size() > 0);

    m_regAllocAuditLog = "";
    m_jitCodeAuditLog = "";
    size_t cgFnOrd = 0;
    for (auto& variantIt : m_rootInfo->m_variants)
    {
        DfgNodeRegAllocVariant* variant = variantIt.get();

        std::string regAllocAuditLog;
        DfgBuiltinNodeImplCreator::DetermineRegDemandForRegAllocVariant(
            AssociatedNodeKind(),
            NodeName(),
            NumOperands(),
            variant /*inout*/,
            [&](DfgBuiltinNodeImplCreator* impl) { GenerateImpl(impl); },
            &regAllocAuditLog /*out*/);

        m_regAllocAuditLog += regAllocAuditLog + "\n";

        ReleaseAssert(!m_svMap.count(variant));
        m_svMap[variant] = variant->GenerateSubVariants();

        for (auto& subVariantIt : m_svMap[variant])
        {
            DfgNodeRegAllocSubVariant* sv = subVariantIt.get();
            if (sv != nullptr)
            {
                DfgBuiltinNodeImplCreator impl(AssociatedNodeKind(), NodeName());
                impl.SetVariantOrd(cgFnOrd);
                cgFnOrd++;
                impl.SetupForRegAllocSubVariant(NumOperands(), sv);
                GenerateImpl(&impl);
                impl.DoLowering(false /*forRegisterDemandTest*/);

                m_jitCodeAuditLog += impl.GetCodegenResult().m_jitCodeAuditLog;

                ReleaseAssert(!m_cgInfoMap.count(sv));
                m_cgInfoMap[sv] = std::move(impl.GetCodegenResult());
            }
        }
    }
}

}   // namespace dast
