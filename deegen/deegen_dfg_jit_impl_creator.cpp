#include "deegen_dfg_jit_impl_creator.h"
#include "deegen_ast_make_call.h"
#include "deegen_ast_return.h"
#include "deegen_ast_return_value_accessor.h"
#include "deegen_ast_slow_path.h"
#include "deegen_ast_simple_lowering_utils.h"
#include "deegen_register_pinning_scheme.h"
#include "deegen_stencil_lowering_pass.h"
#include "invoke_clang_helper.h"
#include "tag_register_optimization.h"
#include "llvm_fcmp_extra_optimizations.h"
#include "deegen_dfg_jit_regalloc_rt_call_wrapper.h"

namespace dast {

constexpr const char* x_registerAllocatedOperandPlaceholderPrefix = "__deegen_register_allocated_operand_placeholder_";

llvm::Value* WARN_UNUSED DfgJitImplCreator::CreatePlaceholderForRegisterAllocatedOperand(
    size_t operandOrd, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    Module* module = GetModule();
    LLVMContext& ctx = module->getContext();

    BcOperand* operand = GetBytecodeDef()->m_list[operandOrd].get();
    ReleaseAssert(operand->GetKind() == BcOperandKind::Slot ||
                  operand->GetKind() == BcOperandKind::Constant ||
                  operand->GetKind() == BcOperandKind::BytecodeRangeBase);

    Type* operandTy;
    if (operand->GetKind() == BcOperandKind::BytecodeRangeBase)
    {
        operandTy = llvm_type_of<void*>(ctx);
    }
    else
    {
        ReleaseAssert(operand->GetKind() == BcOperandKind::Slot ||
                      operand->GetKind() == BcOperandKind::Constant);
        operandTy = llvm_type_of<uint64_t>(ctx);
    }

    std::string placeholderName = x_registerAllocatedOperandPlaceholderPrefix + std::to_string(operandOrd);
    if (module->getNamedValue(placeholderName) == nullptr)
    {
        FunctionType* fty = FunctionType::get(operandTy, {}, false /*isVarArg*/);
        Function* func = Function::Create(fty, GlobalValue::ExternalLinkage, placeholderName, module);
        ReleaseAssert(func->getName() == placeholderName);
        func->addFnAttr(Attribute::NoUnwind);
        func->addFnAttr(Attribute::WillReturn);
        func->setDoesNotAccessMemory();
    }

    Function* func = module->getFunction(placeholderName);
    ReleaseAssert(func != nullptr && func->getReturnType() == operandTy);
    return CallInst::Create(func, { }, "", insertAtEnd);
}

DfgJitImplCreator::DfgJitImplCreator(DfgJitImplCreator* other)
    : JitImplCreatorBase(other)
{
    m_operandReg = other->m_operandReg;
    m_isFastPathRegAllocAlwaysDisabled = other->m_isFastPathRegAllocAlwaysDisabled;
    m_isFastPathRegAllocAlwaysDisabledInitialized = other->m_isFastPathRegAllocAlwaysDisabledInitialized;
    m_isRegAllocDisabled = other->m_isRegAllocDisabled;
    m_regAllocDisableReason = other->m_regAllocDisableReason;
    m_regAllocInfoSetupDone = other->m_regAllocInfoSetupDone;
    m_numGprPassthru = other->m_numGprPassthru;
    m_numFprPassthru = other->m_numFprPassthru;
    m_numGprPaththruGroup1 = other->m_numGprPaththruGroup1;
    m_outputReg = other->m_outputReg;
    m_branchReg = other->m_branchReg;
    if (other->m_regPurposeCtx.get() != nullptr)
    {
        m_regPurposeCtx.reset(new StencilRegisterFileContext());
        *m_regPurposeCtx.get() = *other->m_regPurposeCtx.get();
    }
    m_slowPathDataLayout = other->m_slowPathDataLayout;
    m_operandToOrdinalInRA = other->m_operandToOrdinalInRA;
    m_outputOperandOrdinalInRA = other->m_outputOperandOrdinalInRA;
    m_branchOperandOrdinalInRA = other->m_branchOperandOrdinalInRA;
    m_numInAndOutOperandsInRA = other->m_numInAndOutOperandsInRA;
    m_functionNamesWrappedForRegAllocCCall = other->m_functionNamesWrappedForRegAllocCCall;
    m_stencilCodeMetrics = other->m_stencilCodeMetrics;
    m_regAllocInfoAuditLog = other->m_regAllocInfoAuditLog;
    m_gbtaInfo = other->m_gbtaInfo;
    m_numTotalGenericIcCases = other->m_numTotalGenericIcCases;
}

std::pair<size_t /*min*/, size_t /*max*/> DfgJitImplCreator::GetGroup1GprPassthruValidRange()
{
    ReleaseAssert(!IsRegAllocDisabled());
    size_t numGroup1 = 0, numGroup2 = 0;
    auto countItem = [&](RegAllocSubKind subKind)
    {
        if (subKind == RegAllocSubKind::Group1)
        {
            numGroup1++;
        }
        else
        {
            ReleaseAssert(subKind == RegAllocSubKind::Group2);
            numGroup2++;
        }
    };

    for (auto& operand : GetBytecodeDef()->m_list)
    {
        if (OperandEligibleForRegAlloc(operand.get()))
        {
            OperandRegAllocInfo raInfo = GetOperandRegAllocInfo(operand.get());
            ReleaseAssert(raInfo.m_kind != RegAllocKind::Invalid);
            if (raInfo.m_kind == RegAllocKind::GPR)
            {
                countItem(raInfo.m_subKind);
            }
        }
    }
    if (GetBytecodeDef()->m_hasOutputValue)
    {
        OutputRegAllocInfo raInfo = GetOutputRegAllocInfo();
        ReleaseAssert(raInfo.m_kind != OutputRegAllocKind::Invalid);
        if (raInfo.m_kind == OutputRegAllocKind::GPR)
        {
            countItem(raInfo.m_subKind);
        }
    }
    if (HasBranchDecisionOutput())
    {
        OutputRegAllocInfo raInfo = GetBranchDecisionRegAllocInfo();
        ReleaseAssert(raInfo.m_kind != OutputRegAllocKind::Invalid);
        if (raInfo.m_kind == OutputRegAllocKind::GPR)
        {
            countItem(raInfo.m_subKind);
        }
    }

    ReleaseAssert(numGroup1 <= x_dfg_reg_alloc_num_group1_gprs);
    ReleaseAssert(numGroup2 <= x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs);
    ReleaseAssert(m_numGprPassthru != static_cast<size_t>(-1));
    ReleaseAssert(numGroup1 + numGroup2 + m_numGprPassthru <= x_dfg_reg_alloc_num_gprs);

    size_t maxGroup1Passthru = std::min(m_numGprPassthru, x_dfg_reg_alloc_num_group1_gprs - numGroup1);
    size_t maxGroup2Passthru = std::min(m_numGprPassthru, x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs - numGroup2);
    size_t minGroup1Passthru = m_numGprPassthru - maxGroup2Passthru;
    ReleaseAssert(minGroup1Passthru <= maxGroup1Passthru);
    return std::make_pair(minGroup1Passthru, maxGroup1Passthru);
}

void DfgJitImplCreator::PlanAndAssignRegisterPurpose()
{
    AssertRegAllocConfigValid();
    if (IsRegAllocDisabled()) { return; }

    ReleaseAssert(m_regPurposeCtx.get() == nullptr);

    // Assign regalloc operand ordinal for each operand eligible for regalloc
    //
    {
        uint8_t curOrd = 0;
        m_operandToOrdinalInRA.clear();
        for (auto& operand : GetBytecodeDef()->m_list)
        {
            if (OperandEligibleForRegAlloc(operand.get()))
            {
                ReleaseAssert(!m_operandToOrdinalInRA.count(operand.get()));
                m_operandToOrdinalInRA[operand.get()] = curOrd;
                curOrd++;
            }
        }
        if (GetBytecodeDef()->m_hasOutputValue)
        {
            m_outputOperandOrdinalInRA = curOrd;
            curOrd++;
        }
        if (HasBranchDecisionOutput())
        {
            m_branchOperandOrdinalInRA = curOrd;
            curOrd++;
        }
        m_numInAndOutOperandsInRA = curOrd;
        ReleaseAssert(m_numInAndOutOperandsInRA <= 8);
    }

    StencilRegisterFileContextSetupHelper regCtx;

    // Assign register for each reg-allocated operand
    //
    for (auto& operand : GetBytecodeDef()->m_list)
    {
        if (OperandEligibleForRegAlloc(operand.get()))
        {
            ReleaseAssert(m_operandToOrdinalInRA.count(operand.get()));
            uint8_t ordInRA = m_operandToOrdinalInRA[operand.get()];
            PhyRegPurpose operandPurpose = PhyRegPurpose::Operand(ordInRA);
            OperandRegAllocInfo raInfo = GetOperandRegAllocInfo(operand.get());
            ReleaseAssert(raInfo.m_kind != RegAllocKind::Invalid);
            if (raInfo.m_kind == RegAllocKind::GPR)
            {
                if (raInfo.m_subKind == RegAllocSubKind::Group1)
                {
                    regCtx.ConsumeGprGroup1(operandPurpose);
                }
                else
                {
                    ReleaseAssert(raInfo.m_subKind == RegAllocSubKind::Group2);
                    regCtx.ConsumeGprGroup2(operandPurpose);
                }
            }
            else if (raInfo.m_kind == RegAllocKind::FPR)
            {
                regCtx.ConsumeFpr(operandPurpose);
            }
        }
    }

    // Assign register for each reg-allocated output
    //
    auto handleOutputOperand = [&](OutputRegAllocInfo raInfo, PhyRegPurpose purpose)
    {
        ReleaseAssert(raInfo.m_kind != OutputRegAllocKind::Invalid);
        if (raInfo.m_kind == OutputRegAllocKind::GPR)
        {
            if (raInfo.m_subKind == RegAllocSubKind::Group1)
            {
                regCtx.ConsumeGprGroup1(purpose);
            }
            else
            {
                ReleaseAssert(raInfo.m_subKind == RegAllocSubKind::Group2);
                regCtx.ConsumeGprGroup2(purpose);
            }
        }
        else if (raInfo.m_kind == OutputRegAllocKind::FPR)
        {
            regCtx.ConsumeFpr(purpose);
        }
    };

    if (GetBytecodeDef()->m_hasOutputValue)
    {
        ReleaseAssert(m_outputOperandOrdinalInRA != static_cast<uint8_t>(-1));
        PhyRegPurpose purpose = PhyRegPurpose::Operand(m_outputOperandOrdinalInRA);
        OutputRegAllocInfo raInfo = GetOutputRegAllocInfo();
        handleOutputOperand(raInfo, purpose);
    }

    if (HasBranchDecisionOutput())
    {
        ReleaseAssert(m_branchOperandOrdinalInRA != static_cast<uint8_t>(-1));
        PhyRegPurpose purpose = PhyRegPurpose::Operand(m_branchOperandOrdinalInRA);
        OutputRegAllocInfo raInfo = GetBranchDecisionRegAllocInfo();
        handleOutputOperand(raInfo, purpose);
    }

    // Assign register for each passthrough
    //
    ReleaseAssert(m_numGprPassthru != static_cast<size_t>(-1));
    ReleaseAssert(m_numGprPaththruGroup1 != static_cast<size_t>(-1));
    ReleaseAssert(m_numGprPaththruGroup1 <= m_numGprPassthru);
    for (size_t i = 0; i < m_numGprPaththruGroup1; i++)
    {
        regCtx.ConsumeGprGroup1(PhyRegPurpose::PassThru());
    }
    for (size_t i = 0; i < m_numGprPassthru - m_numGprPaththruGroup1; i++)
    {
        regCtx.ConsumeGprGroup2(PhyRegPurpose::PassThru());
    }
    ReleaseAssert(m_numFprPassthru != static_cast<size_t>(-1));
    for (size_t i = 0; i < m_numFprPassthru; i++)
    {
        regCtx.ConsumeFpr(PhyRegPurpose::PassThru());
    }

    // All remaining registers may be used as scratch.
    // Finish set up and get the result
    //
    ReleaseAssert(m_regPurposeCtx.get() == nullptr);
    m_regPurposeCtx = regCtx.FinalizeAndGet();
}

std::string WARN_UNUSED DfgJitImplCreator::GetRegAllocSignature()
{
    ReleaseAssert(!IsRegAllocDisabled());

    std::string res = "";
    for (auto& operand : GetBytecodeDef()->m_list)
    {
        if (OperandEligibleForRegAlloc(operand.get()))
        {
            OperandRegAllocInfo raInfo = GetOperandRegAllocInfo(operand.get());
            switch (raInfo.m_kind)
            {
            case RegAllocKind::GPR:
            {
                if (raInfo.m_subKind == RegAllocSubKind::Group1)
                {
                    res += "g1";
                }
                else
                {
                    ReleaseAssert(raInfo.m_subKind == RegAllocSubKind::Group2);
                    res += "g2";
                }
                break;
            }
            case RegAllocKind::FPR:
            {
                res += "f";
                break;
            }
            case RegAllocKind::Spilled:
            {
                res += "s";
                break;
            }
            default:
            {
                ReleaseAssert(false);
            }
            }   /*switch*/
        }
    }

    res += "_";

    auto handleOutputOperand = [&](OutputRegAllocInfo raInfo)
    {
        switch (raInfo.m_kind)
        {
        case OutputRegAllocKind::GPR:
        {
            if (raInfo.m_subKind == RegAllocSubKind::Group1)
            {
                res += "g1";
            }
            else
            {
                ReleaseAssert(raInfo.m_subKind == RegAllocSubKind::Group2);
                res += "g2";
            }
            break;
        }
        case OutputRegAllocKind::FPR:
        {
            res += "f";
            break;
        }
        case OutputRegAllocKind::Spilled:
        {
            res += "s";
            break;
        }
        case OutputRegAllocKind::Operand:
        {
            size_t opOrd = raInfo.m_reuseOperandOrdinal;
            ReleaseAssert(opOrd != static_cast<size_t>(-1) && opOrd < GetBytecodeDef()->m_list.size());
            BcOperand* operand = GetBytecodeDef()->m_list[opOrd].get();
            ReleaseAssert(m_operandToOrdinalInRA.count(operand));
            res += "o" + std::to_string(m_operandToOrdinalInRA[operand]);
            break;
        }
        default:
        {
            ReleaseAssert(false);
        }
        }   /*switch*/
    };

    if (GetBytecodeDef()->m_hasOutputValue)
    {
        handleOutputOperand(GetOutputRegAllocInfo());
    }

    if (HasBranchDecisionOutput())
    {
        handleOutputOperand(GetBranchDecisionRegAllocInfo());
    }

    res += "_";
    ReleaseAssert(m_numGprPassthru != static_cast<size_t>(-1));
    res += std::to_string(m_numGprPassthru);
    res += "_";
    ReleaseAssert(m_numFprPassthru != static_cast<size_t>(-1));
    res += std::to_string(m_numFprPassthru);
    res += "_";
    ReleaseAssert(m_numGprPaththruGroup1 != static_cast<size_t>(-1));
    res += std::to_string(m_numGprPaththruGroup1);

    return res;
}

std::string WARN_UNUSED DfgJitImplCreator::GetCCallWrapperPrefix()
{
    return GetBytecodeDef()->GetBytecodeIdName() + "_regconf_" + GetRegAllocSignature() + "_";
}

void DfgJitImplCreator::LowerRegisterPlaceholders()
{
    using namespace llvm;
    ReleaseAssert(m_wrapper != nullptr);
    std::vector<CallInst*> allUses;
    for (BasicBlock& bb : *m_wrapper)
    {
        for (Instruction& inst : bb)
        {
            CallInst* callInst = dyn_cast<CallInst>(&inst);
            if (callInst != nullptr)
            {
                Function* callee = callInst->getCalledFunction();
                if (callee != nullptr)
                {
                    if (callee->getName().str().starts_with(x_registerAllocatedOperandPlaceholderPrefix))
                    {
                        allUses.push_back(callInst);
                    }
                }
            }
        }
    }
    for (CallInst* callInst : allUses)
    {
        Function* callee = callInst->getCalledFunction();
        ReleaseAssert(callee != nullptr);
        std::string calleeName = callee->getName().str();
        ReleaseAssert(calleeName.starts_with(x_registerAllocatedOperandPlaceholderPrefix));
        size_t ord = SafeIntegerCast<size_t>(StoiOrFail(calleeName.substr(strlen(x_registerAllocatedOperandPlaceholderPrefix))));
        ReleaseAssert(ord < GetBytecodeDef()->m_list.size());
        BcOperand* op = GetBytecodeDef()->m_list[ord].get();
        X64Reg reg = GetRegisterForOperand(op);
        uint32_t argOrd = RegisterPinningScheme::GetArgumentOrdinalForRegister(reg);
        ReleaseAssert(argOrd < m_wrapper->arg_size());
        ReleaseAssert(llvm_value_has_type<uint64_t>(callInst));
        Value* replacement = RegisterPinningScheme::GetArgumentAsInt64Value(m_wrapper, argOrd, callInst /*insertBefore*/);
        ReplaceInstructionWithValue(callInst, replacement);
    }
}

std::unique_ptr<DfgJitImplCreator> WARN_UNUSED DfgJitImplCreator::Clone()
{
    std::unique_ptr<DfgJitImplCreator> dst(new DfgJitImplCreator(this));
    return dst;
}

static void SetupDfgJitImplCreatorForRegAllocSubVariant(DfgJitImplCreator* impl, DfgNodeRegAllocSubVariant* sv)
{
    using RegAllocKind = DfgJitImplCreator::RegAllocKind;
    using RegAllocSubKind = DfgJitImplCreator::RegAllocSubKind;
    using OutputRegAllocKind = DfgJitImplCreator::OutputRegAllocKind;

    BytecodeVariantDefinition* bvd = impl->GetBytecodeDef();
    DfgNodeRegAllocVariant* owner = sv->GetOwner();
    DfgNodeRegAllocRootInfo* root = owner->m_owner;

    ReleaseAssert(owner->m_isOperandGPR.size() == sv->NumRaOperands());
    ReleaseAssert(sv->NumRaOperands() == root->m_operandInfo.size());
    for (size_t raIdx = 0; raIdx < sv->NumRaOperands(); raIdx++)
    {
        size_t absOperandIdx = sv->GetOwner()->GetAbsoluteOperandIdx(raIdx);
        ReleaseAssert(absOperandIdx < bvd->m_list.size());
        BcOperand* op = bvd->m_list[absOperandIdx].get();
        if (owner->IsInputOperandGPR(raIdx))
        {
            impl->SetOperandRegAllocKind(op, RegAllocKind::GPR);
            RegAllocSubKind sk = (sv->IsGprOperandGroup1(raIdx) ? RegAllocSubKind::Group1 : RegAllocSubKind::Group2);
            impl->SetOperandRegAllocSubKind(op, sk);
        }
        else
        {
            impl->SetOperandRegAllocKind(op, RegAllocKind::FPR);
        }
    }

    if (root->m_hasOutput)
    {
        if (!sv->IsOutputReuseReg())
        {
            if (owner->IsOutputGPR())
            {
                RegAllocSubKind sk = (sv->IsOutputGroup1Reg() ? RegAllocSubKind::Group1 : RegAllocSubKind::Group2);
                impl->GetOutputRegAllocInfo().m_kind = OutputRegAllocKind::GPR;
                impl->GetOutputRegAllocInfo().m_subKind = sk;
            }
            else
            {
                impl->GetOutputRegAllocInfo().m_kind = OutputRegAllocKind::FPR;
            }
        }
        else
        {
            size_t raIdx = sv->GetOutputReuseInputOrd();
            ReleaseAssert(raIdx < sv->NumRaOperands());
            ReleaseAssertIff(owner->IsOutputGPR(), owner->IsInputOperandGPR(raIdx));

            size_t absOperandIdx = owner->GetAbsoluteOperandIdx(raIdx);
            impl->GetOutputRegAllocInfo().m_kind = OutputRegAllocKind::Operand;
            impl->GetOutputRegAllocInfo().m_reuseOperandOrdinal = absOperandIdx;
        }
    }

    if (root->m_hasBrDecision)
    {
        if (!sv->IsBrDecisionReuseReg())
        {
            RegAllocSubKind sk = (sv->IsBrDecisionGroup1Reg() ? RegAllocSubKind::Group1 : RegAllocSubKind::Group2);
            impl->GetBranchDecisionRegAllocInfo().m_kind = OutputRegAllocKind::GPR;
            impl->GetBranchDecisionRegAllocInfo().m_subKind = sk;
        }
        else
        {
            size_t raIdx = sv->GetBrDecisionReuseInputOrd();
            ReleaseAssert(raIdx < sv->NumRaOperands());
            ReleaseAssert(owner->IsInputOperandGPR(raIdx));

            size_t absOperandIdx = owner->GetAbsoluteOperandIdx(raIdx);
            impl->GetBranchDecisionRegAllocInfo().m_kind = OutputRegAllocKind::Operand;
            impl->GetBranchDecisionRegAllocInfo().m_reuseOperandOrdinal = absOperandIdx;
        }
    }

    impl->SetNumGprPassthru(sv->GetTrueMaxGprPassthrus());
    impl->SetNumGprPassthruGroup1(sv->GetNumGroup1Passthrus());
    impl->SetNumFprPassthru(sv->GetTrueMaxFprPassthrus());

    impl->SetEnableRegAlloc();
}

std::unique_ptr<DfgJitImplCreator> WARN_UNUSED DfgJitImplCreator::GetImplCreatorForRegAllocSubVariant(DfgNodeRegAllocSubVariant* sv)
{
    ReleaseAssert(!m_regAllocInfoSetupDone);
    ReleaseAssert(m_regAllocDisableReason == RegAllocDisableReason::Unknown);

    std::unique_ptr<DfgJitImplCreator> impl = Clone();
    SetupDfgJitImplCreatorForRegAllocSubVariant(impl.get(), sv);

    ReleaseAssert(!impl->IsRegAllocDisabled());
    return impl;
}

std::unique_ptr<DfgNodeRegAllocRootInfo> WARN_UNUSED DfgJitImplCreator::TryGenerateAllRegAllocVariants()
{
    ReleaseAssert(!m_isFastPathRegAllocAlwaysDisabledInitialized);
    std::unique_ptr<DfgNodeRegAllocRootInfo> res = TryGenerateAllRegAllocVariantsImpl();
    if (res.get() != nullptr)
    {
        // We haven't complete reg alloc setup, so cannot use IsRegAllocDisabled() yet
        //
        ReleaseAssert(!m_isRegAllocDisabled);
        SetIsFastPathRegAllocAlwaysDisabled(false);
    }
    else
    {
        ReleaseAssert(IsRegAllocDisabled());
        SetIsFastPathRegAllocAlwaysDisabled(true);
    }
    return res;
}

std::unique_ptr<DfgNodeRegAllocRootInfo> WARN_UNUSED DfgJitImplCreator::TryGenerateAllRegAllocVariantsImpl()
{
    ReleaseAssert(!IsDfgJitSlowPathDataLayoutSetUp());

    using OpInfo = DfgNodeRegAllocRootInfo::OpInfo;

    auto getOpInfo = [&](size_t idx, OperandRegPreferenceInfo rpi) -> OpInfo
    {
        OpInfo info;
        info.m_opOrd = idx;
        if (!rpi.m_isInitialized)
        {
            info.m_allowGPR = true;
            info.m_allowFPR = true;
            info.m_preferGPR = true;
        }
        else
        {
            info.m_allowGPR = rpi.m_isGprAllowed;
            info.m_allowFPR = rpi.m_isFprAllowed;
            info.m_preferGPR = rpi.m_isGprPreferred;
        }
        return info;
    };

    std::unique_ptr<DfgNodeRegAllocRootInfo> raInfo(new DfgNodeRegAllocRootInfo());
    for (size_t i = 0; i < GetBytecodeDef()->m_list.size(); i++)
    {
        BcOperand* op = GetBytecodeDef()->m_list[i].get();
        if (OperandEligibleForRegAlloc(op))
        {
            ReleaseAssert(i < GetBytecodeDef()->m_operandRegPrefInfo.size());
            OperandRegPreferenceInfo rpi = GetBytecodeDef()->m_operandRegPrefInfo[i];
            OpInfo info = getOpInfo(i, rpi);
            raInfo->m_operandInfo.push_back(info);

            ReleaseAssert(info.m_allowGPR || info.m_allowFPR);
        }
    }

    raInfo->m_hasOutput = GetBytecodeDef()->m_hasOutputValue;
    if (raInfo->m_hasOutput)
    {
        OperandRegPreferenceInfo rpi = GetBytecodeDef()->m_outputRegPrefInfo;
        OpInfo info = getOpInfo(static_cast<size_t>(-1) /*idx*/, rpi);
        raInfo->m_outputInfo = info;

        ReleaseAssert(info.m_allowGPR || info.m_allowFPR);
    }

    raInfo->m_hasBrDecision = HasBranchDecisionOutput();

    // raInfo is set up at this point
    //
    raInfo->GenerateVariants();

    m_regAllocInfoAuditLog = "";
    std::string raAuditLog = "";
    std::string raAuditLogSummary = "";

    for (std::unique_ptr<DfgNodeRegAllocVariant>& rav : raInfo->m_variants)
    {
        size_t numInputsInGpr = rav->GetNumGprOperands();
        size_t numInputsInFpr = rav->GetNumFprOperands();

        auto getTestImplAndSetupRegs = [&](bool outputReusesInputReg, bool brDecisionReusesInputReg) WARN_UNUSED -> std::unique_ptr<DfgJitImplCreator>
        {
            std::unique_ptr<DfgJitImplCreator> impl = Clone();
            impl->SetIsFastPathRegAllocAlwaysDisabled(false);

            // Note that this is the bytecode operand ordinal, not the raOpIdx
            //
            std::vector<size_t> operandIdxInGpr, operandIdxInFpr;

            for (size_t operandIdx = 0; operandIdx < raInfo->m_operandInfo.size(); operandIdx++)
            {
                size_t absoluteOperandIdx = rav->GetAbsoluteOperandIdx(operandIdx);
                ReleaseAssert(absoluteOperandIdx < GetBytecodeDef()->m_list.size());
                BcOperand* op = GetBytecodeDef()->m_list[absoluteOperandIdx].get();
                if (rav->IsInputOperandGPR(operandIdx))
                {
                    impl->SetOperandRegAllocKind(op, RegAllocKind::GPR);
                    operandIdxInGpr.push_back(absoluteOperandIdx);
                }
                else
                {
                    impl->SetOperandRegAllocKind(op, RegAllocKind::FPR);
                    operandIdxInFpr.push_back(absoluteOperandIdx);
                }
            }

            if (raInfo->m_hasOutput)
            {
                if (rav->IsOutputGPR())
                {
                    if (!outputReusesInputReg)
                    {
                        impl->GetOutputRegAllocInfo().m_kind = OutputRegAllocKind::GPR;
                    }
                    else
                    {
                        ReleaseAssert(operandIdxInGpr.size() > 0);
                        impl->GetOutputRegAllocInfo().m_kind = OutputRegAllocKind::Operand;
                        impl->GetOutputRegAllocInfo().m_reuseOperandOrdinal = operandIdxInGpr.back();
                        operandIdxInGpr.pop_back();
                    }
                }
                else
                {
                    if (!outputReusesInputReg)
                    {
                        impl->GetOutputRegAllocInfo().m_kind = OutputRegAllocKind::FPR;
                    }
                    else
                    {
                        ReleaseAssert(operandIdxInFpr.size() > 0);
                        impl->GetOutputRegAllocInfo().m_kind = OutputRegAllocKind::Operand;
                        impl->GetOutputRegAllocInfo().m_reuseOperandOrdinal = operandIdxInFpr.back();
                        operandIdxInFpr.pop_back();
                    }
                }
            }
            else
            {
                ReleaseAssert(!outputReusesInputReg);
            }

            if (raInfo->m_hasBrDecision)
            {
                if (!brDecisionReusesInputReg)
                {
                    impl->GetBranchDecisionRegAllocInfo().m_kind = OutputRegAllocKind::GPR;
                }
                else
                {
                    ReleaseAssert(operandIdxInGpr.size() > 0);
                    impl->GetBranchDecisionRegAllocInfo().m_kind = OutputRegAllocKind::Operand;
                    impl->GetBranchDecisionRegAllocInfo().m_reuseOperandOrdinal = operandIdxInGpr.back();
                    operandIdxInGpr.pop_back();
                }
            }
            else
            {
                ReleaseAssert(!brDecisionReusesInputReg);
            }

            impl->DetermineRegisterDemandInfo();
            return impl;
        };

        // Figure out max # of GPR and FPR passthroughs
        //
        {
            std::unique_ptr<DfgJitImplCreator> impl = getTestImplAndSetupRegs(false /*outputReusesInputReg*/, false /*brDecisionReusesInputReg*/);

            // Cannot use IsRegAllocDisabled() since impl haven't complete its reg setup
            //
            if (impl->m_isRegAllocDisabled)
            {
                m_regAllocInfoAuditLog = "Failed to do regalloc for: " + rav->GetVariantRegConfigDescForAudit();
                m_regAllocInfoAuditLog += " (reason: " + std::string(RegAllocDisableReasonToString(impl->GetRegAllocDisableReason())) + ")\n";
                DisableRegAlloc(impl->GetRegAllocDisableReason());
                return nullptr;
            }

            raAuditLog += "Doing regalloc for: " + rav->GetVariantRegConfigDescForAudit() + "\n";
            raAuditLog += impl->m_regAllocInfoAuditLog;
            raAuditLog += "\n";

            rav->SetMaxGprPassthrus(impl->m_numGprPassthru);
            rav->SetMaxFprPassthrus(impl->m_numFprPassthru);
        }

        // Figure out if we can get another passthru by letting the output overtake an input operand register
        //
        if (raInfo->m_hasOutput)
        {
            raAuditLog += "Checking if it is beneficial to allow output reuse an input register:\n";

            bool canReuse = false;
            if (rav->IsOutputGPR())
            {
                canReuse = (numInputsInGpr > 0);
            }
            else
            {
                canReuse = (numInputsInFpr > 0);
            }
            if (canReuse)
            {
                std::unique_ptr<DfgJitImplCreator> impl = getTestImplAndSetupRegs(true /*outputReusesInputReg*/, false /*brDecisionReusesInputReg*/);
                ReleaseAssert(!impl->m_isRegAllocDisabled);

                raAuditLog += impl->m_regAllocInfoAuditLog;
                raAuditLog += "\n";

                bool worth = false;
                if (impl->m_numGprPassthru < rav->GetMaxGprPassthrus() || impl->m_numFprPassthru < rav->GetMaxFprPassthrus())
                {
                    raAuditLog += "[WARNING]: reusing output reduced #passthroughs available, which is unexpected!\n";
                    worth = false;
                }
                else
                {
                    if (rav->IsOutputGPR())
                    {
                        if (impl->m_numGprPassthru > rav->GetMaxGprPassthrus() + 1 || impl->m_numFprPassthru > rav->GetMaxFprPassthrus())
                        {
                            raAuditLog += "[WARNING]: reusing output increased #passthroughs by more than 1, which is unexpected!\n";
                        }
                        worth = (impl->m_numGprPassthru >= rav->GetMaxGprPassthrus() + 1);
                    }
                    else
                    {
                        if (impl->m_numGprPassthru > rav->GetMaxGprPassthrus() || impl->m_numFprPassthru > rav->GetMaxFprPassthrus() + 1)
                        {
                            raAuditLog += "[WARNING]: reusing output increased #passthroughs by more than 1, which is unexpected!\n";
                        }
                        worth = (impl->m_numFprPassthru >= rav->GetMaxFprPassthrus() + 1);
                    }
                }
                raAuditLog += std::string("Decision: ") + (worth ? "worth" : "NOT worth") + " allowing output to reuse input reg.\n";
                rav->SetWhetherOutputWorthReuseRegister(worth);
            }
            else
            {
                raAuditLog += "Output cannot reuse input reg since no input has matching reg type.\n";
                rav->SetWhetherOutputWorthReuseRegister(false);
            }
            raAuditLog += "\n";
        }

        // Figure out if we can get (or further get) another passthru by letting brDecision overtake an input operand register
        //
        if (raInfo->m_hasBrDecision)
        {
            raAuditLog += "Checking if it is beneficial to allow brDecision reuse an input register:\n";

            bool hasOutputAndOutputWorthReuseReg = raInfo->m_hasOutput && rav->OutputWorthReuseRegister();

            bool isOutputReusedGPR = false;
            if (hasOutputAndOutputWorthReuseReg && rav->IsOutputGPR())
            {
                isOutputReusedGPR = true;
            }
            bool canReuse = isOutputReusedGPR ? (numInputsInGpr >= 2) : (numInputsInGpr >= 1);
            if (canReuse)
            {
                std::unique_ptr<DfgJitImplCreator> impl = getTestImplAndSetupRegs(hasOutputAndOutputWorthReuseReg, true /*brDecisionReusesInputReg*/);
                ReleaseAssert(!impl->m_isRegAllocDisabled);

                raAuditLog += impl->m_regAllocInfoAuditLog;
                raAuditLog += "\n";

                size_t baseGprPt = rav->GetMaxGprPassthrus();
                size_t baseFprPt = rav->GetMaxFprPassthrus();
                if (hasOutputAndOutputWorthReuseReg)
                {
                    if (rav->IsOutputGPR()) { baseGprPt++; } else { baseFprPt++; }
                }

                bool worth = false;
                if (impl->m_numGprPassthru < baseGprPt || impl->m_numFprPassthru < baseFprPt)
                {
                    raAuditLog += "[WARNING]: reusing brDecision reduced #passthroughs available, which is unexpected!\n";
                    worth = false;
                }
                else
                {
                    if (impl->m_numGprPassthru > baseGprPt + 1 || impl->m_numFprPassthru > baseFprPt)
                    {
                        raAuditLog += "[WARNING]: reusing brDecision increased #passthroughs by more than 1, which is unexpected!\n";
                    }
                    worth = (impl->m_numGprPassthru >= baseGprPt + 1);
                }
                raAuditLog += std::string("Decision: ") + (worth ? "worth" : "NOT worth") + " allowing brDecision to reuse input reg.\n";
                rav->SetWhetherBrDecisionWorthReuseRegister(worth);
            }
            else
            {
                raAuditLog += "BrDecision cannot reuse input reg since no input (or no more input) has matching reg type.\n";
                rav->SetWhetherBrDecisionWorthReuseRegister(false);
            }
            raAuditLog += "\n";
        }

        raAuditLogSummary += "Variant " + rav->GetVariantRegConfigDescForAudit() + ": #PT.GPR=" + std::to_string(rav->GetMaxGprPassthrus()) + ", #PT.FPR=" + std::to_string(rav->GetMaxFprPassthrus());
        if (raInfo->m_hasOutput)
        {
            raAuditLogSummary += std::string(", OutputMayReuse=") + (rav->OutputWorthReuseRegister() ? "true" : "false");
        }
        if (raInfo->m_hasBrDecision)
        {
            raAuditLogSummary += std::string(", BrDecisionMayReuse=") + (rav->BrDecisionWorthReuseRegister() ? "true" : "false");
        }
        raAuditLogSummary += "\n";
    }

    m_regAllocInfoAuditLog = "Summary:\n" + raAuditLogSummary + "\nFull log:\n" + raAuditLog;
    return raInfo;
}

DfgJitImplCreator::RegPressureTestResult WARN_UNUSED DfgJitImplCreator::TestForGivenRegisterPressure(size_t numGprPassthru,
                                                                                                     size_t numFprPassthru,
                                                                                                     std::unique_ptr<DfgJitSlowPathDataLayout>& computedLayout /*out*/)
{
    ReleaseAssert(m_numGprPassthru == static_cast<size_t>(-1) && m_numFprPassthru == static_cast<size_t>(-1));
    SetNumGprPassthru(numGprPassthru);
    SetNumFprPassthru(numFprPassthru);

    size_t numGroup2GprsLeft = x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs;
    for (size_t opIdx = 0; opIdx < GetBytecodeDef()->m_list.size(); opIdx++)
    {
        BcOperand* op = GetBytecodeDef()->m_list[opIdx].get();
        if (OperandEligibleForRegAlloc(op) && IsOperandRegAllocated(op) && GetOperandRegAllocKind(op) == RegAllocKind::GPR)
        {
            if (numGroup2GprsLeft > 0)
            {
                SetOperandRegAllocSubKind(op, RegAllocSubKind::Group2);
                numGroup2GprsLeft--;
            }
            else
            {
                SetOperandRegAllocSubKind(op, RegAllocSubKind::Group1);
            }
        }
    }

    if (GetBytecodeDef()->m_hasOutputValue && GetOutputRegAllocInfo().m_kind == OutputRegAllocKind::GPR)
    {
        if (numGroup2GprsLeft > 0)
        {
            GetOutputRegAllocInfo().m_subKind = RegAllocSubKind::Group2;
            numGroup2GprsLeft--;
        }
        else
        {
            GetOutputRegAllocInfo().m_subKind = RegAllocSubKind::Group1;
        }
    }

    if (HasBranchDecisionOutput() && GetBranchDecisionRegAllocInfo().m_kind == OutputRegAllocKind::GPR)
    {
        if (numGroup2GprsLeft > 0)
        {
            GetBranchDecisionRegAllocInfo().m_subKind = RegAllocSubKind::Group2;
            numGroup2GprsLeft--;
        }
        else
        {
            GetBranchDecisionRegAllocInfo().m_subKind = RegAllocSubKind::Group1;
        }
    }

    if (numGroup2GprsLeft >= numGprPassthru)
    {
        SetNumGprPassthruGroup1(0);
    }
    else
    {
        SetNumGprPassthruGroup1(numGprPassthru - numGroup2GprsLeft);
    }

    SetEnableRegAlloc();

    // The computed layout must be passed out so that it is kept alive after this function
    //
    computedLayout = ComputeDfgJitSlowPathDataLayout();

    DoLowering(true /*forRegisterDemandTest*/);

    if (IsRegAllocDisabled())
    {
        ReleaseAssert(m_regAllocDisableReason != RegAllocDisableReason::Unknown);
        return {
            .m_success = false,
            .m_failReason = m_regAllocDisableReason,
            .m_codeMetric = RegisterDemandEstimationMetric()
        };
    }

    return {
        .m_success = true,
        .m_failReason = RegAllocDisableReason::Unknown,
        .m_codeMetric = GetStencilCodeMetrics()
    };
}

void DfgJitImplCreator::DetermineRegisterDemandInfo()
{
    ReleaseAssert(!m_regAllocInfoSetupDone);
    ReleaseAssert(m_numFprPassthru == static_cast<size_t>(-1));
    ReleaseAssert(m_numGprPassthru == static_cast<size_t>(-1));
    ReleaseAssert(IsMainComponent());

    // This function must be called before computing SlowPathDataLayout
    //
    ReleaseAssert(!IsDfgJitSlowPathDataLayoutSetUp());

    size_t numGprUsed = 0;
    size_t numFprUsed = 0;
    for (size_t opIdx = 0; opIdx < GetBytecodeDef()->m_list.size(); opIdx++)
    {
        BcOperand* op = GetBytecodeDef()->m_list[opIdx].get();
        if (OperandEligibleForRegAlloc(op) && IsOperandRegAllocated(op))
        {
            RegAllocKind kind = GetOperandRegAllocKind(op);
            ReleaseAssert(kind != RegAllocKind::Invalid);
            if (kind == RegAllocKind::GPR)
            {
                numGprUsed++;
            }
            if (kind == RegAllocKind::FPR)
            {
                numFprUsed++;
            }
        }
    }

    if (GetBytecodeDef()->m_hasOutputValue)
    {
        OutputRegAllocKind raKind = GetOutputRegAllocInfo().m_kind;
        if (raKind == OutputRegAllocKind::GPR)
        {
            numGprUsed++;
        }
        else if (raKind == OutputRegAllocKind::FPR)
        {
            numFprUsed++;
        }
    }

    if (HasBranchDecisionOutput())
    {
        OutputRegAllocKind raKind = GetBranchDecisionRegAllocInfo().m_kind;
        ReleaseAssert(raKind != OutputRegAllocKind::FPR);
        if (raKind == OutputRegAllocKind::GPR)
        {
            numGprUsed++;
        }
    }

    if (numGprUsed > x_dfg_reg_alloc_num_gprs || numFprUsed > x_dfg_reg_alloc_num_fprs)
    {
        DisableRegAlloc(RegAllocDisableReason::TooManyOperands);
        return;
    }

    AnonymousFile auditFile;
    FILE* auditFp = auditFile.GetFStream("w");
    Auto(
        fclose(auditFp);
        m_regAllocInfoAuditLog = auditFile.GetFileContents();
    );

    size_t maxGprPassthru = x_dfg_reg_alloc_num_gprs - numGprUsed;
    size_t maxFprPassthru = x_dfg_reg_alloc_num_fprs - numFprUsed;

    size_t curGprPassthru = 0;
    // There are plenty of FPR registers not used by reg alloc,
    // so for now for simplicity, always assume that we have enough of them to use without spilling
    //
    size_t curFprPassthru = maxFprPassthru;

    auto doTest = [&]() -> RegPressureTestResult
    {
        std::unique_ptr<DfgJitImplCreator> tester = Clone();
        std::unique_ptr<DfgJitSlowPathDataLayout> slowPathDataLayout;
        return tester->TestForGivenRegisterPressure(curGprPassthru, curFprPassthru, slowPathDataLayout /*out*/);
    };

    RegisterDemandEstimationMetric baseMetric;
    {
        RegPressureTestResult rpt = doTest();
        if (!rpt.m_success)
        {
            ReleaseAssert(rpt.m_failReason != RegAllocDisableReason::Unknown);
            DisableRegAlloc(rpt.m_failReason);
            return;
        }
        baseMetric = rpt.m_codeMetric;
    }

    fprintf(auditFp, "%s\n", baseMetric.GetHeader().c_str());
    fprintf(auditFp, "#PT.GPR=%d, #PT.FPR=%d, Metric=%s\n", static_cast<int>(curGprPassthru), static_cast<int>(curFprPassthru), baseMetric.GetAuditInfo().c_str());

    double bestResult = 0;
    size_t bestGprPassthru = 0;

    while (curGprPassthru < maxGprPassthru)
    {
        curGprPassthru++;

        RegPressureTestResult rpt = doTest();
        ReleaseAssert(rpt.m_success);
        // Cannot use IsRegAllocDisabled() since we haven't complete set up
        //
        ReleaseAssert(!m_isRegAllocDisabled);

        fprintf(auditFp, "#PT.GPR=%d, #PT.FPR=%d, Metric=%s", static_cast<int>(curGprPassthru), static_cast<int>(curFprPassthru), rpt.m_codeMetric.GetAuditInfo().c_str());

        double rawCost = baseMetric.CompareWith(rpt.m_codeMetric);

        // The benefit for passing through one more reg is 2 instructions (as we don't need to save/restore it)
        // so the benefit must outweigh the cost (but the exact value is just chosen to avoid ties..)
        //
        double adjustedCost = rawCost - 1.891 * static_cast<double>(curGprPassthru);

        fprintf(auditFp, ", cost=%.2lf, score=%.2lf\n", rawCost, adjustedCost);

        if (adjustedCost < bestResult)
        {
            bestResult = adjustedCost;
            bestGprPassthru = curGprPassthru;
        }
    }

    fprintf(auditFp, "Determined maximum passthrus: GPR=%d, FPR=%d\n", static_cast<int>(bestGprPassthru), static_cast<int>(curFprPassthru));
    ReleaseAssert(!m_isRegAllocDisabled);
    m_numGprPassthru = bestGprPassthru;
    m_numFprPassthru = curFprPassthru;
}

void DfgJitImplCreator::DoLowering(bool forRegisterDemandTest)
{
    using namespace llvm;
    LLVMContext& ctx = m_module->getContext();

    ReleaseAssert(!m_generated);
    m_generated = true;

    ReleaseAssert(m_bytecodeDef->IsBytecodeStructLengthFinalized());

    bool statusOfIsRegAllocDisabledAtFuncEntry = IsRegAllocDisabled();
    Auto(if (!forRegisterDemandTest) { ReleaseAssert(IsRegAllocDisabled() == statusOfIsRegAllocDisabledAtFuncEntry); });

    if (!IsMainComponent())
    {
        ReleaseAssert(IsRegAllocDisabled());
    }

    auto declareOrCheckRegAllocIllegal = [&](RegAllocDisableReason reason)
    {
        if (IsRegAllocDisabled())
        {
            return;
        }
        if (!forRegisterDemandTest)
        {
            fprintf(stderr, "DfgJitImplCreator: The component is compiled with reg alloc enabled, "
                            "but encountered a condition incompatible with reg alloc! (reason: %s)\n", RegAllocDisableReasonToString(reason));
            abort();
        }
        DisableRegAlloc(reason);
    };

    ReleaseAssertImp(forRegisterDemandTest, IsMainComponent());

    // Check conditions where reg alloc should be disabled (or re-affirm that they don't exist)
    //
    if (!IsRegAllocDisabled())
    {
        if (AstMakeCall::GetAllUseInFunction(m_impl).size() > 0)
        {
            declareOrCheckRegAllocIllegal(RegAllocDisableReason::GLCall);
            return;
        }

        {
            bool isGlReturn = false;
            bool isExplicitStackOutput = false;
            bool hasThrowError = false;
            ForEachUseOfDeegenSimpleApi(
                m_impl,
                [&](DeegenSimpleApiLoweringPassName passName, DeegenAbstractSimpleApiLoweringPass* /*pass*/, CallInst* /*origin*/)
                {
                    if (passName == DeegenSimpleApiLoweringPassName::LowerGuestLanguageFunctionReturnPass)
                    {
                        isGlReturn = true;
                    }
                    if (passName == DeegenSimpleApiLoweringPassName::LowerGetOutputSlotApiPass)
                    {
                        isExplicitStackOutput = true;
                    }
                    if (passName == DeegenSimpleApiLoweringPassName::LowerThrowErrorApiPass)
                    {
                        hasThrowError = true;
                    }
                });

            if (isGlReturn)
            {
                declareOrCheckRegAllocIllegal(RegAllocDisableReason::GLReturn);
                return;
            }
            if (isExplicitStackOutput)
            {
                declareOrCheckRegAllocIllegal(RegAllocDisableReason::ExplicitStackOutput);
                return;
            }
            if (hasThrowError)
            {
                declareOrCheckRegAllocIllegal(RegAllocDisableReason::FastPathThrowError);
                return;
            }
        }
    }

    PlanAndAssignRegisterPurpose();

    // Create the wrapper function 'm_wrapper'
    //
    CreateWrapperFunction();
    ReleaseAssert(m_wrapper != nullptr);

     // Inline 'm_impl' into 'm_wrapper'
    //
    if (m_impl->hasFnAttribute(Attribute::NoInline))
    {
        m_impl->removeFnAttr(Attribute::NoInline);
    }
    m_impl->addFnAttr(Attribute::AlwaysInline);
    m_impl->setLinkage(GlobalValue::InternalLinkage);

    DesugarAndSimplifyLLVMModule(m_module.get(), DesugaringLevel::PerFunctionSimplifyOnlyAggresive);
    RunLLVMDeadGlobalElimination(m_module.get());
    m_impl = nullptr;

    m_valuePreserver.RefreshAfterTransform();

    LowerRegisterPlaceholders();

    std::vector<AstInlineCache::JitLLVMLoweringResult> icLLRes;

    if (IsMainComponent())
    {
        std::vector<AstInlineCache> allIcUses = AstInlineCache::GetAllUseInFunction(m_wrapper);
        size_t icTraitOrdBase = 0;
        for (size_t i = 0; i < allIcUses.size(); i++)
        {
            icLLRes.push_back(allIcUses[i].DoLoweringForJit(this, i, icTraitOrdBase));
            icTraitOrdBase += allIcUses[i].m_totalEffectKinds;
        }
        m_numTotalGenericIcCases = icTraitOrdBase;
    }

    // Now we can do the lowerings
    //
    AstBytecodeReturn::LowerAllForDfgJIT(this, m_wrapper);
    AstMakeCall::LowerAllForDfgJIT(this, m_wrapper);
    AstReturnValueAccessor::LowerForInterpreterOrBaselineOrDfg(this, m_wrapper);
    DeegenAllSimpleApiLoweringPasses::LowerAllForDfgJIT(this, m_wrapper);
    AstSlowPath::LowerAllForInterpreterOrBaselineOrDfg(this, m_wrapper);

    // Lower the remaining function APIs from the generic IC
    //
    if (!IsJitSlowPath())
    {
        LowerInterpreterGetBytecodePtrInternalAPI(this, m_wrapper);
        AstInlineCache::LowerIcPtrGetterFunctionForJit(this, m_wrapper);
    }

    // All lowerings are complete.
    // Remove the NoReturn attribute since all pseudo no-return API calls have been replaced to dispatching tail calls
    //
    m_wrapper->removeFnAttr(Attribute::NoReturn);

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
    if (!IsJitSlowPath())
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

    if (icLLRes.size() > 0)
    {
        std::string fallthroughPlaceholderName = DeegenPlaceholderUtils::FindFallthroughPlaceholderSymbolName(GetStencilRcDefinitions());
        if (fallthroughPlaceholderName != "")
        {
            AstInlineCache::AttemptIrRewriteToManuallyTailDuplicateSimpleIcCases(m_wrapper, fallthroughPlaceholderName);
        }
    }

    ValidateLLVMModule(m_module.get());

    // After the optimization pass, change the linkage of everything to 'external' before extraction
    // This is fine: for AOT slow path, our caller will fix up the linkage for us.
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

    // If reg alloc is enabled, turn all preserve_most functions called by the main logic to preserve_all, before stencil extraction.
    // This is fine (and needed for performance), since we will later redirect the calls to those preserve_most functions to an AOT stub
    // that deal with the register issues incl. saving FPR state (effectively, calling a preserve_all wrapper for the preserve_most function),
    // so the JIT code doesn't need a pile of instructions only to save all the FPR states.
    //
    // Note that this must be done after having extracted the IC body, since the IC body should still have preserve_most.
    //
    std::unordered_set<std::string> wrappedForCCallFns;
    if (!IsRegAllocDisabled())
    {
        ReleaseAssert(IsMainComponent() && !IsJitSlowPath());
        if (!DfgRegAllocCCallAsmTransformPass::TryRewritePreserveMostToPreserveAll(m_wrapper, wrappedForCCallFns /*out*/))
        {
            // Failed to rewrite due to presense of indirect calls, reg alloc must not be enabled
            //
            ReleaseAssert(forRegisterDemandTest);
            declareOrCheckRegAllocIllegal(RegAllocDisableReason::CCall);
            return;
        }
    }

    ValidateLLVMModule(m_module.get());

    m_module = ExtractFunction(m_module.get(), m_resultFuncName);

    // After the extract, m_execFnContext and m_wrapper are invalidated since a new module is returned. Refresh its value.
    //
    m_wrapper = m_module->getFunction(m_resultFuncName);
    ReleaseAssert(m_wrapper != nullptr);
    m_execFnContext->ResetFunction(m_wrapper);

    if (IsJitSlowPath())
    {
        // For AOT slow path, we are done at this point. For JIT, we need to do further processing.
        //
        return;
    }

    // Run the stencil lowering pass in preparation for stencil generation
    //
    DeegenStencilLoweringPass slPass = DeegenStencilLoweringPass::RunIrRewritePhase(m_wrapper, IsLastJitStencilInBytecode(), GetRcPlaceholderNameForFallthrough());

    {
        std::string asmFile = CompileToAssemblyFileForStencilGeneration();
        slPass.ParseAsmFile(asmFile);
    }

    // If reg alloc is enabled, C calls must be specially handled
    //
    if (!IsRegAllocDisabled())
    {
        DfgRegAllocCCallAsmTransformPass cctPass(slPass.m_workFile.get(), m_module.get());
        std::string regAllocCCallWrapperPrefix = GetCCallWrapperPrefix();
        if (forRegisterDemandTest)
        {
            // If the pass fails, it means that reg alloc cannot be enabled
            //
            if (!cctPass.RunPass(regAllocCCallWrapperPrefix))
            {
                using FailReason = DfgRegAllocCCallAsmTransformPass::FailReason;
                switch (cctPass.m_failReason)
                {
                case FailReason::HasIndirectCall:
                case FailReason::NotPreserveMostCC:
                {
                    declareOrCheckRegAllocIllegal(RegAllocDisableReason::CCall);
                    return;
                }
                case FailReason::ReturnRegConflict:
                case FailReason::FprArgumentRegConflict:
                {
                    declareOrCheckRegAllocIllegal(RegAllocDisableReason::CCallEdgeCase);
                    return;
                }
                default:
                {
                    ReleaseAssert(false);
                }
                }   /*switch*/
            }
        }
        else
        {
            // The pass must success, since we have determined that reg alloc is possible
            //
            ReleaseAssert(cctPass.RunPass(regAllocCCallWrapperPrefix));
        }

        m_functionNamesWrappedForRegAllocCCall = cctPass.m_calledFns;

        // Assert that all the calls are what we anticipated
        //
        for (auto& it : m_functionNamesWrappedForRegAllocCCall)
        {
            if (!wrappedForCCallFns.count(it.m_fnName))
            {
                fprintf(stderr, "%s\n", it.m_fnName.c_str());
                m_wrapper->dump();
                fprintf(stderr, "%s\n", slPass.m_workFile->ToString().c_str());
            }
            ReleaseAssert(wrappedForCCallFns.count(it.m_fnName));
        }
        // There is no reason that a call randomly disappeared after compilation, assert that the two sets are equal.
        // (Note that cctPass.m_calledFns is already dedup'ed so size comparison is sufficient).
        //
        ReleaseAssert(m_functionNamesWrappedForRegAllocCCall.size() == wrappedForCCallFns.size());
    }

    // Run the ASM phase of the stencil lowering pass
    //
    slPass.RunAsmRewritePhase();

    // Compute various code metrics, we use this as a heuristic to determine the maximum number of passthrough regs possible
    //
    {
        m_stencilCodeMetrics.m_isValid = true;
        m_stencilCodeMetrics.m_fastPathInst = slPass.m_numInstructionsInFastPath;
        m_stencilCodeMetrics.m_slowPathInst = slPass.m_numInstructionsInSlowPath;

        size_t numIcInsts = 0, numIcs = 0;
        for (auto& icInfo : slPass.m_callIcLoweringResults)
        {
            numIcInsts += icInfo.m_closureCallLogicAsmLineCount + icInfo.m_directCallLogicAsmLineCount;
            numIcs++;
        }
        for (auto& icInfo : slPass.m_genericIcLoweringResults)
        {
            for (size_t instCnt : icInfo.m_icLogicAsmLineCount)
            {
                numIcInsts += instCnt;
                numIcs++;
            }
        }

        m_stencilCodeMetrics.m_icTotalInst = numIcInsts;
        if (numIcs > 0)
        {
            m_stencilCodeMetrics.m_icAvgInst = static_cast<double>(numIcInsts) / static_cast<double>(numIcs);
        }
        else
        {
            m_stencilCodeMetrics.m_icAvgInst = 0;
        }

        m_stencilCodeMetrics.m_fastPathStackOp = slPass.m_numStackOperationsInFastPath;
        m_stencilCodeMetrics.m_slowPathStackOp = slPass.m_numStackOperationsInSlowPath;
        size_t numIcStackOp = slPass.m_numTotalStackOperationsInIc;

        // If there is a C call, due to stack alignment issues there needs to be a push/pop even if otherwise it needs no stack.
        // Treat them as free.
        //
        if (m_functionNamesWrappedForRegAllocCCall.size() > 0)
        {
            ReleaseAssert(slPass.m_numStackPopBeforeTailCallInFastPath <= m_stencilCodeMetrics.m_fastPathStackOp);
            m_stencilCodeMetrics.m_fastPathStackOp -= slPass.m_numStackPopBeforeTailCallInFastPath;

            ReleaseAssert(slPass.m_numStackPopBeforeTailCallInSlowPath <= m_stencilCodeMetrics.m_slowPathStackOp);
            m_stencilCodeMetrics.m_slowPathStackOp -= slPass.m_numStackPopBeforeTailCallInSlowPath;

            ReleaseAssert(slPass.m_numTotalStackPopBeforeTailCallInIc <= numIcStackOp);
            numIcStackOp -= slPass.m_numTotalStackPopBeforeTailCallInIc;
        }
        if (numIcs > 0)
        {
            m_stencilCodeMetrics.m_icAvgStackOp = static_cast<double>(numIcStackOp) / static_cast<double>(numIcs);
        }
        else
        {
            m_stencilCodeMetrics.m_icAvgStackOp = 0;
        }
    }

    // If we are just trying to determine register demand, we can end here since we've checked all cases where
    // reg alloc should be disabled, and have determined the total instruction count. No need to actually generate everything.
    //
    if (forRegisterDemandTest)
    {
        return;
    }

    m_stencilPreTransformAsmFile = std::move(slPass.m_rawInputFileForAudit);

    m_stencilPostTransformAsmFile = slPass.m_primaryPostTransformAsmFile;

    // Compile the final ASM file to object file
    //
    m_stencilObjectFile = CompileAssemblyFileToObjectFile(slPass.m_primaryPostTransformAsmFile, " -fno-pic -fno-pie ");

    // Parse object file into copy-and-patch stencil
    //
    m_stencil = DeegenStencil::ParseMainLogic(ctx, IsLastJitStencilInBytecode(), m_stencilObjectFile);
    if (!IsRegAllocDisabled())
    {
        m_stencil.ParseRegisterPatches(GetRegisterPurposeContext());

        for (const std::string& calledFnName : m_stencil.m_rtCallFnNamesForRegAllocEnabledStencil)
        {
            if (!calledFnName.starts_with("deegen_dfg_rt_wrapper_"))
            {
                Function* fn = m_module->getFunction(calledFnName);
                ReleaseAssert(fn != nullptr);
                ReleaseAssert(fn->hasFnAttribute(Attribute::NoReturn));
            }
        }
    }

    m_genericIcLoweringResult = AstInlineCache::DoLoweringAfterAsmTransform(
        GetBytecodeIrInfo(),
        this,
        std::move(icBodyModule),
        icLLRes,
        slPass.m_genericIcLoweringResults,
        m_stencil /*mainStencil*/,
        0 /*icEffectTraitBaseOrd*/,
        static_cast<size_t>(-1) /*doNotAssertExpectedNumTotalIcEffect*/);

    if (slPass.m_shouldAssertNoGenericIcWithInlineSlab)
    {
        for (auto& item : m_genericIcLoweringResult.m_inlineSlabInfo)
        {
            ReleaseAssert(!item.m_hasInlineSlab);
        }
    }

    if (m_genericIcLoweringResult.m_icBodyModule.get() != nullptr)
    {
        Module* m = m_genericIcLoweringResult.m_icBodyModule.get();
        for (const std::string& funcName : wrappedForCCallFns)
        {
            Function* func = m->getFunction(funcName);
            if (func != nullptr)
            {
                ReleaseAssert(func->getCallingConv() == CallingConv::PreserveMost);
            }
        }
    }
}

}   // namespace dast
