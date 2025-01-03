#pragma once

#include "deegen_jit_impl_creator_base.h"
#include "deegen_bytecode_ir_components.h"
#include "drt/dfg_reg_alloc_register_info.h"
#include "deegen_jit_register_patch_analyzer.h"
#include "deegen_dfg_reg_alloc_variants.h"
#include "deegen_dfg_jit_regalloc_rt_call_wrapper.h"

namespace dast {

class DfgJitImplCreator final : public JitImplCreatorBase
{
public:
    virtual DeegenEngineTier WARN_UNUSED GetTier() const override { return DeegenEngineTier::DfgJIT; }

    DfgJitImplCreator(BytecodeIrInfo* bii, BytecodeIrComponent& bic, const DeegenGlobalBytecodeTraitAccessor* gbta)
        : JitImplCreatorBase(bii, bic)
        , m_gbtaInfo(gbta)
        , m_regPurposeCtx(nullptr)
        , m_isFastPathRegAllocAlwaysDisabled(false)
        , m_isFastPathRegAllocAlwaysDisabledInitialized(false)
        , m_isRegAllocDisabled(false)
        , m_regAllocInfoSetupDone(false)
        , m_regAllocDisableReason(RegAllocDisableReason::Unknown)
        , m_numGprPassthru(static_cast<size_t>(-1))
        , m_numFprPassthru(static_cast<size_t>(-1))
        , m_numGprPaththruGroup1(static_cast<size_t>(-1))
        , m_outputReg()
        , m_branchReg()
        , m_outputOperandOrdinalInRA(static_cast<uint8_t>(-1))
        , m_branchOperandOrdinalInRA(static_cast<uint8_t>(-1))
        , m_numInAndOutOperandsInRA(static_cast<size_t>(-1))
        , m_numTotalGenericIcCases(static_cast<size_t>(-1))
        , m_slowPathDataLayout(nullptr)
    { }

    DfgJitImplCreator(SlowPathReturnContinuationTag, BytecodeIrInfo* bii, BytecodeIrComponent& bic, const DeegenGlobalBytecodeTraitAccessor* gbta)
        : JitImplCreatorBase(SlowPathReturnContinuationTag(), bii, bic)
        , m_gbtaInfo(gbta)
        , m_regPurposeCtx(nullptr)
        , m_isFastPathRegAllocAlwaysDisabled(false)
        , m_isFastPathRegAllocAlwaysDisabledInitialized(false)
        , m_isRegAllocDisabled(false)
        , m_regAllocInfoSetupDone(false)
        , m_regAllocDisableReason(RegAllocDisableReason::Unknown)
        , m_numGprPassthru(static_cast<size_t>(-1))
        , m_numFprPassthru(static_cast<size_t>(-1))
        , m_numGprPaththruGroup1(static_cast<size_t>(-1))
        , m_outputReg()
        , m_branchReg()
        , m_outputOperandOrdinalInRA(static_cast<uint8_t>(-1))
        , m_branchOperandOrdinalInRA(static_cast<uint8_t>(-1))
        , m_numInAndOutOperandsInRA(static_cast<size_t>(-1))
        , m_numTotalGenericIcCases(static_cast<size_t>(-1))
        , m_slowPathDataLayout(nullptr)
    { }

    enum class RegAllocKind : uint8_t
    {
        Invalid,
        Spilled,
        GPR,
        FPR
    };

    enum class OutputRegAllocKind : uint8_t
    {
        Invalid,
        Spilled,
        GPR,
        FPR,
        // One of the operand's register is discarded and used to store the output
        //
        Operand
    };

    // GPR needs to be divided into the "old" x86-32 registers and the "new" r8-r15 registers as they need different stencils.
    // (For FPR we currently only use xmm0-7 so FPR doesn't need this distinction right now)
    //
    // To reduce redundant LLVM work however, we do most of the lowering work with only the info that an operand is in GPR,
    // and specialize whether the GPR is in the old group or the new group at last
    //
    enum class RegAllocSubKind : uint8_t
    {
        Invalid,
        // GPR register 0-7
        //
        Group1,
        // GPR register 8-15
        //
        Group2
    };

    // Describes the reg alloc info about one input operand
    //
    struct OperandRegAllocInfo
    {
        RegAllocKind m_kind;
        RegAllocSubKind m_subKind;
    };

    // Describes the reg alloc info obout the output or the branch decision
    // For branch decision, m_kind cannot be FPR (since the decision is either 0 or 1)
    //
    struct OutputRegAllocInfo
    {
        OutputRegAllocInfo()
            : m_kind(OutputRegAllocKind::Invalid)
            , m_subKind(RegAllocSubKind::Invalid)
            , m_reuseOperandOrdinal(static_cast<size_t>(-1))
        { }

        OutputRegAllocKind m_kind;
        RegAllocSubKind m_subKind;
        size_t m_reuseOperandOrdinal;
    };

    enum class RegAllocDisableReason
    {
        Unknown,
        // Reg alloc is always disabled in AOT slowpath and in JIT fastpath return continuations
        // (since if a call is made, registers need to be spilled anyway)
        //
        NotFastPath,
        // Too many operands to fit them all in registers
        // Currently for simplicity, we do not consider the case where some operands are spilled while others are in reg
        // We only enable reg alloc if all operands and outputs can be fit in reg. Otherwise we totally disable reg alloc.
        //
        TooManyOperands,
        // The fast path contains a call to C code, note that rare runtime calls (those with preserve_most convention,
        // e.g., calls to IC body) are exempt to this rule
        //
        // TODO: ideally we should have a better way of identifying rare calls, since preserve_most might make sense for
        //       common calls as well.
        //
        CCall,
        // The fast path contains a rare runtime call, but the function prototype contains weird edge cases that
        // we currently lockdown for simplicity. Specifically, this currently includes:
        // 1. xmm1 used as return value (we lockdown this only for simplicity)
        // 2. Weird return struct that is not converted to sret and we cannot understand (we must understand which registers
        //    are used as return values, but LLVM has weird cases where "sret demotion" happens at backend, so lack of sret
        //    at LLVM level doesn't mean sret is not used, and System V mapping of structs to arch registers has many corner
        //    cases, so we will only whitelist simple cases that we can understand and lockdown everything else).
        //
        CCallEdgeCase,
        // The fast path contains a guest language call (MakeCall API)
        //
        GLCall,
        // This is a guest language return bytecode, cannot use reg alloc due to currently return values are passed in
        // regs that conflicts with reg alloc. This can be fixed but most likely not worth fixing.
        //
        GLReturn,
        // The bytecode explicitly requires the output to be on the stack
        //
        ExplicitStackOutput,
        // The bytecode fast path may call ThrowError.
        // Now the ThrowError convention conflicts with reg alloc. This can be fixed, but for now we just go simple
        // TODO: fix and allow this case
        //
        FastPathThrowError
    };

    static const char* RegAllocDisableReasonToString(RegAllocDisableReason reason)
    {
        switch (reason)
        {
        case RegAllocDisableReason::Unknown: return "Unknown";
        case RegAllocDisableReason::NotFastPath: return "NotFastPath";
        case RegAllocDisableReason::TooManyOperands: return "TooManyOperands";
        case RegAllocDisableReason::CCall: return "CCall";
        case RegAllocDisableReason::CCallEdgeCase: return "CCallEdgeCase";
        case RegAllocDisableReason::GLCall: return "GLCall";
        case RegAllocDisableReason::GLReturn: return "GLReturn";
        case RegAllocDisableReason::ExplicitStackOutput: return "ExplicitStackOutput";
        case RegAllocDisableReason::FastPathThrowError: return "FastPathThrowError";
        }   /*switch*/
        ReleaseAssert(false);
    }

    // A branch decision output is needed if the bytecode can both fallthrough to the next bytecode and branch to another bytecode
    // Note that it is not needed if the bytecode always only fallthrough or always only branch
    //
    bool HasBranchDecisionOutput()
    {
        return GetBytecodeDef()->BytecodeMayFallthroughToNextBytecode() && GetBytecodeDef()->m_hasConditionalBranchTarget;
    }

    bool IsRegAllocDisabled()
    {
        // Make sure we catch cases where this function is called before m_isRegAllocDisabled is properly initialized
        //
        ReleaseAssertImp(m_isRegAllocDisabled, m_regAllocDisableReason != RegAllocDisableReason::Unknown);
        ReleaseAssertImp(!m_isRegAllocDisabled, m_regAllocInfoSetupDone && m_regAllocDisableReason == RegAllocDisableReason::Unknown);
        return m_isRegAllocDisabled;
    }

    RegAllocDisableReason GetRegAllocDisableReason()
    {
        ReleaseAssert(IsRegAllocDisabled());
        return m_regAllocDisableReason;
    }

    // Should be called after having set up all the configs
    //
    void SetEnableRegAlloc()
    {
        ReleaseAssert(!m_regAllocInfoSetupDone);
        // Cannot enable reg alloc if we already have a reason that reg alloc must not be enabled
        //
        ReleaseAssert(m_regAllocDisableReason == RegAllocDisableReason::Unknown);
        m_isRegAllocDisabled = false;
        m_regAllocInfoSetupDone = true;
        AssertRegAllocConfigValid();
    }

    void SetIsFastPathRegAllocAlwaysDisabled(bool value)
    {
        ReleaseAssert(!m_isFastPathRegAllocAlwaysDisabledInitialized);
        m_isFastPathRegAllocAlwaysDisabledInitialized = true;
        m_isFastPathRegAllocAlwaysDisabled = value;
    }

    bool IsFastPathRegAllocAlwaysDisabled()
    {
        ReleaseAssert(m_isFastPathRegAllocAlwaysDisabledInitialized);
        ReleaseAssertImp(!m_isRegAllocDisabled, !m_isFastPathRegAllocAlwaysDisabled);
        return m_isFastPathRegAllocAlwaysDisabled;
    }

    void DisableRegAlloc(RegAllocDisableReason reason)
    {
        ReleaseAssert(reason != RegAllocDisableReason::Unknown);
        if (m_isRegAllocDisabled)
        {
            ReleaseAssert(m_regAllocDisableReason != RegAllocDisableReason::Unknown);
            return;
        }

        m_isRegAllocDisabled = true;
        m_regAllocDisableReason = reason;

        for (auto& op : GetBytecodeDef()->m_list)
        {
            if (OperandEligibleForRegAlloc(op.get()))
            {
                m_operandReg[op.get()] = {
                    .m_kind = RegAllocKind::Spilled,
                    .m_subKind = RegAllocSubKind::Invalid
                };
            }
        }

        if (GetBytecodeDef()->m_hasOutputValue)
        {
            m_outputReg.m_kind = OutputRegAllocKind::Spilled;
        }
        if (HasBranchDecisionOutput())
        {
            m_branchReg.m_kind = OutputRegAllocKind::Spilled;
        }

        m_numGprPassthru = static_cast<size_t>(-1);
        m_numFprPassthru = static_cast<size_t>(-1);
        m_numGprPaththruGroup1 = static_cast<size_t>(-1);

        AssertRegAllocConfigValid();
    }

    void AssertRegAllocConfigValid()
    {
        if (m_isRegAllocDisabled)
        {
            ReleaseAssert(m_regAllocDisableReason != RegAllocDisableReason::Unknown);
            for (auto& op : GetBytecodeDef()->m_list)
            {
                if (OperandEligibleForRegAlloc(op.get()))
                {
                    ReleaseAssert(m_operandReg.count(op.get()) && m_operandReg[op.get()].m_kind == RegAllocKind::Spilled);
                }
                else
                {
                    ReleaseAssert(!m_operandReg.count(op.get()));
                }
            }
            if (GetBytecodeDef()->m_hasOutputValue)
            {
                ReleaseAssert(m_outputReg.m_kind == OutputRegAllocKind::Spilled);
            }
            else
            {
                ReleaseAssert(m_outputReg.m_kind == OutputRegAllocKind::Invalid);
            }
            if (HasBranchDecisionOutput())
            {
                ReleaseAssert(m_branchReg.m_kind == OutputRegAllocKind::Spilled);
            }
            else
            {
                ReleaseAssert(m_branchReg.m_kind == OutputRegAllocKind::Invalid);
            }
        }
        else
        {
            ReleaseAssert(IsMainComponent());
            for (auto& op : GetBytecodeDef()->m_list)
            {
                if (OperandEligibleForRegAlloc(op.get()))
                {
                    ReleaseAssert(m_operandReg.count(op.get()));
                    RegAllocKind kind = m_operandReg[op.get()].m_kind;
                    ReleaseAssert(kind != RegAllocKind::Invalid);
                    // For now we require everything to be in-register, for simplicity
                    //
                    ReleaseAssert(kind != RegAllocKind::Spilled);
                }
                else
                {
                    ReleaseAssert(!m_operandReg.count(op.get()));
                }
            }
            if (GetBytecodeDef()->m_hasOutputValue)
            {
                ReleaseAssert(m_outputReg.m_kind != OutputRegAllocKind::Invalid);
                // For now we require everything to be in-register, for simplicity
                //
                ReleaseAssert(m_outputReg.m_kind != OutputRegAllocKind::Spilled);
                if (m_outputReg.m_kind == OutputRegAllocKind::Operand)
                {
                    ReleaseAssert(m_outputReg.m_reuseOperandOrdinal < GetBytecodeDef()->m_list.size());
                    BcOperand* op = GetBytecodeDef()->m_list[m_outputReg.m_reuseOperandOrdinal].get();
                    ReleaseAssert(OperandEligibleForRegAlloc(op));
                    ReleaseAssert(m_operandReg.count(op));
                    ReleaseAssert(m_operandReg[op].m_kind == RegAllocKind::GPR || m_operandReg[op].m_kind == RegAllocKind::FPR);
                }
            }
            else
            {
                ReleaseAssert(m_outputReg.m_kind == OutputRegAllocKind::Invalid);
            }
            if (HasBranchDecisionOutput())
            {
                ReleaseAssert(m_branchReg.m_kind != OutputRegAllocKind::Invalid);
                // For now we require everything to be in-register, for simplicity
                //
                ReleaseAssert(m_branchReg.m_kind != OutputRegAllocKind::Spilled);
                // Branch decision cannot be stored in FPR
                //
                ReleaseAssert(m_branchReg.m_kind != OutputRegAllocKind::FPR);
                if (m_branchReg.m_kind == OutputRegAllocKind::Operand)
                {
                    ReleaseAssert(m_branchReg.m_reuseOperandOrdinal < GetBytecodeDef()->m_list.size());
                    BcOperand* op = GetBytecodeDef()->m_list[m_branchReg.m_reuseOperandOrdinal].get();
                    ReleaseAssert(OperandEligibleForRegAlloc(op));
                    ReleaseAssert(m_operandReg.count(op));
                    ReleaseAssert(m_operandReg[op].m_kind == RegAllocKind::GPR);
                    if (m_outputReg.m_kind == OutputRegAllocKind::Operand)
                    {
                        ReleaseAssert(m_branchReg.m_reuseOperandOrdinal != m_outputReg.m_reuseOperandOrdinal);
                    }
                }
            }
            else
            {
                ReleaseAssert(m_branchReg.m_kind == OutputRegAllocKind::Invalid);
            }
            ReleaseAssert(m_numGprPassthru != static_cast<size_t>(-1));
            ReleaseAssert(m_numFprPassthru != static_cast<size_t>(-1));
            ReleaseAssert(m_numGprPaththruGroup1 != static_cast<size_t>(-1));
            std::pair<size_t, size_t> validGroup1PtRange = GetGroup1GprPassthruValidRange();
            ReleaseAssert(validGroup1PtRange.first <= m_numGprPaththruGroup1 && m_numGprPaththruGroup1 <= validGroup1PtRange.second);
        }
    }

    bool OperandEligibleForRegAlloc(BcOperand* op)
    {
        return op->GetKind() == BcOperandKind::Slot || op->GetKind() == BcOperandKind::Constant;
    }

    // Specify that how operand 'op' will be register-allocated
    // If 'op' is a range operand, it means that it should be discretized and register-allocated
    //
    void SetOperandRegAllocKind(BcOperand* op, RegAllocKind kind)
    {
        ReleaseAssert(OperandEligibleForRegAlloc(op));
        ReleaseAssert(kind != RegAllocKind::Invalid);
        size_t ord = op->OperandOrdinal();
        ReleaseAssert(ord < GetBytecodeDef()->m_list.size() && GetBytecodeDef()->m_list[ord].get() == op);
        m_operandReg[op] = { .m_kind = kind, .m_subKind = RegAllocSubKind::Invalid };
    }

    void SetOperandRegAllocSubKind(BcOperand* op, RegAllocSubKind subKind)
    {
        ReleaseAssert(OperandEligibleForRegAlloc(op));
        ReleaseAssert(subKind != RegAllocSubKind::Invalid);
        ReleaseAssert(GetOperandRegAllocInfo(op).m_kind == RegAllocKind::GPR);
        ReleaseAssert(m_operandReg.count(op));
        m_operandReg[op].m_subKind = subKind;
    }

    OperandRegAllocInfo GetOperandRegAllocInfo(BcOperand* op)
    {
        ReleaseAssert(OperandEligibleForRegAlloc(op));
        ReleaseAssert(m_operandReg.count(op));
        return m_operandReg[op];
    }

    RegAllocKind GetOperandRegAllocKind(BcOperand* op)
    {
        return GetOperandRegAllocInfo(op).m_kind;
    }

    bool IsOperandRegAllocated(BcOperand* op)
    {
        RegAllocKind rak = GetOperandRegAllocKind(op);
        ReleaseAssert(rak != RegAllocKind::Invalid);
        return rak != RegAllocKind::Spilled;
    }

    X64Reg GetRegisterForOperand(BcOperand* op)
    {
        ReleaseAssert(IsOperandRegAllocated(op));
        ReleaseAssert(m_regPurposeCtx.get() != nullptr && m_regPurposeCtx->IsFinalized());
        ReleaseAssert(m_operandToOrdinalInRA.count(op));
        return m_regPurposeCtx->GetOperandReg(m_operandToOrdinalInRA[op]);
    }

    bool IsOutputRegAllocated()
    {
        ReleaseAssert(GetBytecodeDef()->m_hasOutputValue);
        ReleaseAssert(m_outputReg.m_kind != OutputRegAllocKind::Invalid);
        return m_outputReg.m_kind != OutputRegAllocKind::Spilled;
    }

    OutputRegAllocInfo& GetOutputRegAllocInfo()
    {
        ReleaseAssert(GetBytecodeDef()->m_hasOutputValue);
        return m_outputReg;
    }

    X64Reg GetRegisterForOutput()
    {
        ReleaseAssert(IsOutputRegAllocated());
        ReleaseAssert(m_regPurposeCtx.get() != nullptr && m_regPurposeCtx->IsFinalized());
        if (m_outputReg.m_kind == OutputRegAllocKind::Operand)
        {
            ReleaseAssert(m_outputReg.m_reuseOperandOrdinal != static_cast<size_t>(-1));
            ReleaseAssert(m_outputReg.m_reuseOperandOrdinal < GetBytecodeDef()->m_list.size());
            BcOperand* operand = GetBytecodeDef()->m_list[m_outputReg.m_reuseOperandOrdinal].get();
            ReleaseAssert(m_operandToOrdinalInRA.count(operand));
            return m_regPurposeCtx->GetOperandReg(m_operandToOrdinalInRA[operand]);
        }
        else
        {
            ReleaseAssert(m_outputReg.m_kind == OutputRegAllocKind::GPR || m_outputReg.m_kind == OutputRegAllocKind::FPR);
            ReleaseAssert(m_outputOperandOrdinalInRA != static_cast<uint8_t>(-1));
            return m_regPurposeCtx->GetOperandReg(m_outputOperandOrdinalInRA);
        }
    }

    bool IsBranchDecisionRegAllocated()
    {
        ReleaseAssert(HasBranchDecisionOutput());
        ReleaseAssert(m_branchReg.m_kind != OutputRegAllocKind::Invalid);
        return m_branchReg.m_kind != OutputRegAllocKind::Spilled;
    }

    OutputRegAllocInfo& GetBranchDecisionRegAllocInfo()
    {
        ReleaseAssert(HasBranchDecisionOutput());
        return m_branchReg;
    }

    X64Reg GetRegisterForBranchDecision()
    {
        ReleaseAssert(IsBranchDecisionRegAllocated());
        ReleaseAssert(m_regPurposeCtx.get() != nullptr && m_regPurposeCtx->IsFinalized());
        if (m_branchReg.m_kind == OutputRegAllocKind::Operand)
        {
            ReleaseAssert(m_branchReg.m_reuseOperandOrdinal != static_cast<size_t>(-1));
            ReleaseAssert(m_branchReg.m_reuseOperandOrdinal < GetBytecodeDef()->m_list.size());
            BcOperand* operand = GetBytecodeDef()->m_list[m_branchReg.m_reuseOperandOrdinal].get();
            ReleaseAssert(m_operandToOrdinalInRA.count(operand));
            return m_regPurposeCtx->GetOperandReg(m_operandToOrdinalInRA[operand]);
        }
        else
        {
            ReleaseAssert(m_branchReg.m_kind == OutputRegAllocKind::GPR);
            ReleaseAssert(m_branchOperandOrdinalInRA != static_cast<uint8_t>(-1));
            return m_regPurposeCtx->GetOperandReg(m_branchOperandOrdinalInRA);
        }
    }

    PhyRegPurpose GetRegisterPurpose(X64Reg reg)
    {
        ReleaseAssert(m_regPurposeCtx.get() != nullptr && m_regPurposeCtx->IsFinalized());
        return m_regPurposeCtx->GetPhyRegPurpose(reg);
    }

    StencilRegisterFileContext* GetRegisterPurposeContext()
    {
        ReleaseAssert(m_regPurposeCtx.get() != nullptr);
        return m_regPurposeCtx.get();
    }

    void SetNumGprPassthru(size_t value) { m_numGprPassthru = value; }
    size_t GetNumGprPassthru() { ReleaseAssert(m_numGprPassthru != static_cast<size_t>(-1)); return m_numGprPassthru; }

    void SetNumFprPassthru(size_t value) { m_numFprPassthru = value; }
    size_t GetNumFprPassthru() { ReleaseAssert(m_numFprPassthru != static_cast<size_t>(-1)); return m_numFprPassthru; }

    void SetNumGprPassthruGroup1(size_t value) { ReleaseAssert(value <= GetNumGprPassthru()); m_numGprPaththruGroup1 = value; }
    size_t GetNumGprPassthruGroup1() { ReleaseAssert(m_numGprPaththruGroup1 != static_cast<size_t>(-1)); return m_numGprPaththruGroup1; }

    // Based on the assignment of GPR registers and the number of GPR pass throughs,
    // compute the valid range of # of group-1 GPRs used as passthroughs
    //
    std::pair<size_t /*min*/, size_t /*max*/> GetGroup1GprPassthruValidRange();

    void PlanAndAssignRegisterPurpose();

    // Get a string signature that identifies the regalloc config
    //
    std::string WARN_UNUSED GetRegAllocSignature();

    // Get the wrapper prefix for C Calls
    //
    std::string WARN_UNUSED GetCCallWrapperPrefix();

    // Return nullptr on failure (which means reg alloc cannot be enabled)
    // If nullptr is returned, reg alloc is also disabled for this class
    //
    std::unique_ptr<DfgNodeRegAllocRootInfo> WARN_UNUSED TryGenerateAllRegAllocVariants();

    // Make a clone of the current DfgJitImplCreator and set the clone up based on the reg alloc config of the RegAllocSubVariant
    //
    std::unique_ptr<DfgJitImplCreator> WARN_UNUSED GetImplCreatorForRegAllocSubVariant(DfgNodeRegAllocSubVariant* sv);

    void DetermineRegisterDemandInfo();

    void DoLowering(bool forRegisterDemandTest);

    llvm::Value* WARN_UNUSED CreatePlaceholderForRegisterAllocatedOperand(size_t operandOrd, llvm::BasicBlock* insertAtEnd);

    bool IsDfgJitSlowPathDataLayoutSetUp()
    {
        return m_slowPathDataLayout != nullptr;
    }

    DfgJitSlowPathDataLayout* WARN_UNUSED GetDfgJitSlowPathDataLayout()
    {
        ReleaseAssert(IsDfgJitSlowPathDataLayoutSetUp());
        return m_slowPathDataLayout;
    }

    // Compute a new DfgJitSlowPathDataLayout, use it as the m_slowPathDataLayout for this class, and return it.
    // Note that this class never holds the memory of the DfgJitSlowPathDataLayout,
    // so caller is responsible for keeping the returned unique_ptr alive
    //
    std::unique_ptr<DfgJitSlowPathDataLayout> WARN_UNUSED ComputeDfgJitSlowPathDataLayout()
    {
        ReleaseAssert(!IsDfgJitSlowPathDataLayoutSetUp());
        std::unique_ptr<DfgJitSlowPathDataLayout> res(new DfgJitSlowPathDataLayout());
        m_slowPathDataLayout = res.get();
        res->ComputeLayout(this);
        return res;
    }

    void SetDfgJitSlowPathDataLayout(DfgJitSlowPathDataLayout* layout)
    {
        ReleaseAssert(!IsDfgJitSlowPathDataLayoutSetUp());
        ReleaseAssert(layout != nullptr);
        m_slowPathDataLayout = layout;
    }

    const std::vector<DfgCCallFuncInfo>& GetFunctionNamesWrappedForRegAllocCCall()
    {
        return m_functionNamesWrappedForRegAllocCCall;
    }

    std::unique_ptr<DfgJitImplCreator> WARN_UNUSED Clone();

    RegisterDemandEstimationMetric GetStencilCodeMetrics()
    {
        ReleaseAssert(m_stencilCodeMetrics.m_isValid);
        return m_stencilCodeMetrics;
    }

    std::string GetRegAllocInfoAuditLog() { return m_regAllocInfoAuditLog; }

    size_t GetTotalNumGenericIcCases()
    {
        ReleaseAssert(m_numTotalGenericIcCases != static_cast<size_t>(-1));
        return m_numTotalGenericIcCases;
    }

    const DeegenGlobalBytecodeTraitAccessor& GetGBTA() { ReleaseAssert(m_gbtaInfo != nullptr); return *m_gbtaInfo; }
    void SetGBTA(const DeegenGlobalBytecodeTraitAccessor* gbta) { ReleaseAssert(m_gbtaInfo == nullptr && gbta != nullptr); m_gbtaInfo = gbta; }

    static constexpr const char* x_slowPathSectionName = "deegen_dfg_jit_slow_path_section";
    static constexpr const char* x_codegenFnSectionName = "deegen_dfg_jit_codegen_fn_section";
    static constexpr const char* x_icCodegenFnSectionName = "deegen_dfg_jit_ic_codegen_fn_section";

private:
    // For clone
    //
    DfgJitImplCreator(DfgJitImplCreator* other);

    std::unique_ptr<DfgNodeRegAllocRootInfo> WARN_UNUSED TryGenerateAllRegAllocVariantsImpl();

    void LowerRegisterPlaceholders();

    struct RegPressureTestResult
    {
        bool m_success;     // false if regalloc must be disabled
        RegAllocDisableReason m_failReason;
        RegisterDemandEstimationMetric m_codeMetric;
    };
    RegPressureTestResult WARN_UNUSED TestForGivenRegisterPressure(size_t numGprPassthru,
                                                                   size_t numFprPassthru,
                                                                   std::unique_ptr<DfgJitSlowPathDataLayout>& computedLayout /*out*/);

    const DeegenGlobalBytecodeTraitAccessor* m_gbtaInfo;

    std::unique_ptr<StencilRegisterFileContext> m_regPurposeCtx;
    std::unordered_map<BcOperand*, OperandRegAllocInfo> m_operandReg;
    std::unordered_map<BcOperand*, uint8_t> m_operandToOrdinalInRA;
    std::vector<DfgCCallFuncInfo> m_functionNamesWrappedForRegAllocCCall;
    // Denotes if reg alloc is always disabled for the fast path (in all variants),
    // in that case the slow path will not need to restore the spilled registers
    //
    bool m_isFastPathRegAllocAlwaysDisabled;
    bool m_isFastPathRegAllocAlwaysDisabledInitialized;
    bool m_isRegAllocDisabled;
    bool m_regAllocInfoSetupDone;
    RegAllocDisableReason m_regAllocDisableReason;
    size_t m_numGprPassthru;
    size_t m_numFprPassthru;
    // Among the # of GPR passthrus, how many of them are in Group1 GPR
    //
    size_t m_numGprPaththruGroup1;
    OutputRegAllocInfo m_outputReg;
    OutputRegAllocInfo m_branchReg;
    // Note that these two ordinals are reserved even if the output/branch reuses an input register
    //
    uint8_t m_outputOperandOrdinalInRA;
    uint8_t m_branchOperandOrdinalInRA;
    size_t m_numInAndOutOperandsInRA;
    size_t m_numTotalGenericIcCases;
    DfgJitSlowPathDataLayout* m_slowPathDataLayout;
    RegisterDemandEstimationMetric m_stencilCodeMetrics;
    std::string m_regAllocInfoAuditLog;
};

}   // namespace dast
