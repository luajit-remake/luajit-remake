#pragma once

#include "common_utils.h"
#include "dfg_reg_alloc_register_info.h"

namespace dast {

struct DfgNodeRegAllocSubVariant;
struct DfgNodeRegAllocVariant;

// This file contains helper classes that records information about the different register allocation variants of a DFG node
//
// DfgNodeRegAllocRootInfo:
//     Records the GPR/FPR allowance/preference of each reg-alloc-eligible operands in a DFG node.
//     This class corresponds 1:1 with each DFG node kind that has reg alloc enabled.
//
struct DfgNodeRegAllocRootInfo
{
    DfgNodeRegAllocRootInfo()
        : m_hasOutput(false)
        , m_hasBrDecision(false)
    { }

    struct OpInfo
    {
        OpInfo() : m_opOrd(static_cast<size_t>(-1)), m_allowGPR(false), m_allowFPR(false), m_preferGPR(false) { }

        bool IsBothGprAndFprAllowed()
        {
            return m_allowGPR && m_allowFPR;
        }

        bool IsOnlyAllowedValueGPR()
        {
            ReleaseAssert(m_allowGPR != m_allowFPR);
            return m_allowGPR;
        }

        // Not every operand is eligible for reg alloc, so this m_opOrd is the original operand ordinal in the full operand list.
        // As such, the exact meaning of m_opOrd depends on caller's use case (of what the "full operand list" is).
        // For guest language DFG nodes, this is the bytecode operand ordinal in the original bytecode.
        //
        size_t m_opOrd;
        bool m_allowGPR;
        bool m_allowFPR;
        bool m_preferGPR;
    };

    // Populate m_variants after having set up the other fields.
    //
    // Caller must complete the set-up of each variant to generate the subvariants: see DfgNodeRegAllocVariant.
    //
    void GenerateVariants();

    void PrintCppDefinitions(FILE* hdrFile,
                             FILE* cppFile,
                             const std::string& cppNameIdent,
                             const std::vector<std::string>& variantCppIdents,
                             const std::string& expectedBaseCgFnOrdExpr,
                             size_t expectedTotalNumCgFns);

    // Reg preference about each operand that participate in reg alloc, in increasing order
    //
    std::vector<OpInfo> m_operandInfo;
    bool m_hasOutput;
    bool m_hasBrDecision;
    OpInfo m_outputInfo;

    std::vector<std::unique_ptr<DfgNodeRegAllocVariant>> m_variants;
};

// DfgNodeRegAllocVariant:
//     Records a reg alloc variant for a specific register bank configuration (i.e., whether each reg alloc operand is in GPR or FPR).
//
// DfgNodeRegAllocRootInfo generates a list of DfgNodeRegAllocVariants.
// Then, for each DfgNodeRegAllocVariant, the following additional information must be determined and set up by caller logic:
//
//    m_maxGprPassthrus, m_maxFprPassthrus:
//        The maximum number of GPR/FPR passthrus this variant can take.
//
//    m_outputWorthReuseRegister, m_brDecisionWorthReuseRegister:
//        Whether letting output/brDecision to take the position of an input register can
//        make space for one additional passthrough register.
//
// After the above information is set up, a list of DfgNodeRegAllocSubVariants can be generated, which corresponds 1:1 to each codegen function.
//
struct DfgNodeRegAllocVariant
{
    DfgNodeRegAllocVariant()
        : m_owner(nullptr)
        , m_isOutputGPRInitialized(false)
        , m_isOutputGPR(false)
        , m_maxGprPassthrus(static_cast<size_t>(-1))
        , m_maxFprPassthrus(static_cast<size_t>(-1))
        , m_outputWorthReuseRegisterInitialized(false)
        , m_outputWorthReuseRegister(false)
        , m_brDecisionWorthReuseRegisterInitialized(false)
        , m_brDecisionWorthReuseRegister(false)
    { }

    std::string GetVariantRegConfigDescForAudit();

    bool IsOutputGPR() { ReleaseAssert(m_owner->m_hasOutput && m_isOutputGPRInitialized); return m_isOutputGPR; }

    size_t NumRaOperands() { return m_isOperandGPR.size(); }

    bool IsInputOperandGPR(size_t raIdx)
    {
        ReleaseAssert(m_isOperandGPR.size() == m_owner->m_operandInfo.size());
        ReleaseAssert(raIdx < m_isOperandGPR.size());
        return m_isOperandGPR[raIdx];
    }

    size_t GetAbsoluteOperandIdx(size_t raIdx)
    {
        ReleaseAssert(raIdx < m_owner->m_operandInfo.size());
        return m_owner->m_operandInfo[raIdx].m_opOrd;
    }

    size_t GetNumGprOperands();
    size_t GetNumFprOperands();

    size_t GetKthGprOperandOrdinal(size_t k);
    size_t GetKthFprOperandOrdinal(size_t k);

    // Return 1 if the variant does not have output/brDecision
    //
    size_t GetOutputNumChoices();
    size_t GetBrDecisionNumChoices();

    bool MaxGprPassthrusInitialized() { return m_maxGprPassthrus != static_cast<size_t>(-1); }

    size_t GetMaxGprPassthrus()
    {
        ReleaseAssert(MaxGprPassthrusInitialized());
        return m_maxGprPassthrus;
    }

    void SetMaxGprPassthrus(size_t value)
    {
        ReleaseAssert(!MaxGprPassthrusInitialized() && value != static_cast<size_t>(-1));
        m_maxGprPassthrus = value;
    }

    bool MaxFprPassthrusInitialized() { return m_maxFprPassthrus != static_cast<size_t>(-1); }

    size_t GetMaxFprPassthrus()
    {
        ReleaseAssert(MaxFprPassthrusInitialized());
        return m_maxFprPassthrus;
    }

    void SetMaxFprPassthrus(size_t value)
    {
        ReleaseAssert(!MaxFprPassthrusInitialized() && value != static_cast<size_t>(-1));
        m_maxFprPassthrus = value;
    }

    size_t GetNumGprScratchRegistersNeeded()
    {
        size_t numGprUsedByInputsAndOutputs = 0;
        for (size_t opIdx = 0; opIdx < NumRaOperands(); opIdx++)
        {
            if (IsInputOperandGPR(opIdx))
            {
                numGprUsedByInputsAndOutputs++;
            }
        }
        if (m_owner->m_hasOutput)
        {
            if (IsOutputGPR())
            {
                numGprUsedByInputsAndOutputs++;
            }
        }
        if (m_owner->m_hasBrDecision)
        {
            numGprUsedByInputsAndOutputs++;
        }
        // Note that making output reuse an input reg always increases #passthrough by one, making the sum unchanged
        //
        ReleaseAssert(numGprUsedByInputsAndOutputs + GetMaxGprPassthrus() <= x_dfg_reg_alloc_num_gprs);
        return x_dfg_reg_alloc_num_gprs - (numGprUsedByInputsAndOutputs + GetMaxGprPassthrus());
    }

    size_t GetNumFprScratchRegistersNeeded()
    {
        size_t numFprUsedByInputsAndOutputs = 0;
        for (size_t opIdx = 0; opIdx < NumRaOperands(); opIdx++)
        {
            if (!IsInputOperandGPR(opIdx))
            {
                numFprUsedByInputsAndOutputs++;
            }
        }
        if (m_owner->m_hasOutput)
        {
            if (!IsOutputGPR())
            {
                numFprUsedByInputsAndOutputs++;
            }
        }
        ReleaseAssert(numFprUsedByInputsAndOutputs + GetMaxFprPassthrus() <= x_dfg_reg_alloc_num_fprs);
        return x_dfg_reg_alloc_num_fprs - (numFprUsedByInputsAndOutputs + GetMaxFprPassthrus());
    }

    void SetWhetherOutputWorthReuseRegister(bool value)
    {
        ReleaseAssert(m_owner->m_hasOutput);
        ReleaseAssert(!m_outputWorthReuseRegisterInitialized);
        m_outputWorthReuseRegisterInitialized = true;
        m_outputWorthReuseRegister = value;
    }

    bool OutputWorthReuseRegister()
    {
        ReleaseAssert(m_owner->m_hasOutput);
        ReleaseAssert(m_outputWorthReuseRegisterInitialized);
        return m_outputWorthReuseRegister;
    }

    void SetWhetherBrDecisionWorthReuseRegister(bool value)
    {
        ReleaseAssert(m_owner->m_hasBrDecision);
        ReleaseAssert(!m_brDecisionWorthReuseRegisterInitialized);
        m_brDecisionWorthReuseRegisterInitialized = true;
        m_brDecisionWorthReuseRegister = value;
    }

    bool BrDecisionWorthReuseRegister()
    {
        ReleaseAssert(m_owner->m_hasBrDecision);
        ReleaseAssert(m_brDecisionWorthReuseRegisterInitialized);
        return m_brDecisionWorthReuseRegister;
    }

    void SetIsOutputGPR(bool value)
    {
        ReleaseAssert(m_owner->m_hasOutput);
        ReleaseAssert(!m_isOutputGPRInitialized);
        m_isOutputGPRInitialized = true;
        m_isOutputGPR = value;
    }

    // The subvariant table is always dimensioned in the order as follows:
    //   2 for each GPR operand (since it may come from Group1 or Group2)
    //   OutputNumChoices if has output
    //   BrDecisionNumChoices if has brDecision
    //   (x_dfg_reg_alloc_num_group1_gprs+1) for numGprPassthrus in group 1
    //
    // Many combinations of the above parameters would end up being invalid,
    // in which case the corresponding entry will be nullptr
    //
    size_t GetTableLength();

    // A vector of length 'GetTableLength()'
    // Invalid variants are nullptr
    //
    // Ordering in each dimension:
    //   GPR operand: Group1 comes first
    //   Output operand: own register (GPR/FPR) comes first, reuses operand register comes next in the same order as operands
    //   brDecision operand: similar to Output operand
    //   numGroup1GprPassthrus: in increasing order
    //
    // Note that the exact ordering of the subvariants above must agree with what the DFG runtime expects,
    // see dfg_variant_traits_internal.h -- DfgRegBankSubVariantTraits
    //
    std::vector<std::unique_ptr<DfgNodeRegAllocSubVariant>> WARN_UNUSED GenerateSubVariants();

    // Print C++ header and CPP file definitions for this variant
    // 'subvariants' should be what is generated by GenerateSubVariants()
    // 'baseOrdExpr' should be a C++ expression that returns the base ord for the first subvariant in the table.
    // The subvariants will be assigned increasing ordinal from 'baseOrdExpr' in the order they appeared in the table.
    //
    void PrintCppDefinitions(std::vector<std::unique_ptr<DfgNodeRegAllocSubVariant>>& subvariants,
                             FILE* hdrFile,
                             FILE* cppFile,
                             const std::string& cppNameIdent,
                             const std::string& baseOrdExpr,
                             bool hasRangedOperand);

    DfgNodeRegAllocRootInfo* m_owner;

    // Whether each operand/output/brDecision uses GPR or FPR
    // Note that this vector is the list of operands that participate in regalloc,
    // not the original operand list in the bytecode
    //
    std::vector<bool> m_isOperandGPR;

private:
    bool m_isOutputGPRInitialized;
    bool m_isOutputGPR;

    // Max number of GPR and FPR passthroughs, assuming that outputs does not reuse input register
    //
    size_t m_maxGprPassthrus;
    size_t m_maxFprPassthrus;

    // If true, it means that by reusing an input register to store output, we can get one more passthru reg
    //
    bool m_outputWorthReuseRegisterInitialized;
    bool m_outputWorthReuseRegister;
    bool m_brDecisionWorthReuseRegisterInitialized;
    bool m_brDecisionWorthReuseRegister;
};

// Corresponds 1:1 to a codegen function used by DFG.
//
struct DfgNodeRegAllocSubVariant
{
    DfgNodeRegAllocSubVariant()
        : m_owner(nullptr)
        , m_isOutputReuseReg(true)
        , m_isOutputGroup1(false)
        , m_outputReuseInputOrdinal(static_cast<size_t>(-1))
        , m_isBrDecisionReuseReg(true)
        , m_isBrDecisionGroup1(false)
        , m_brDecisionReuseInputOrdinal(static_cast<size_t>(-1))
        , m_numGroup1Passthroughs(static_cast<size_t>(-1))
    { }

    bool IsOutputReuseReg()
    {
        ReleaseAssert(m_owner->m_owner->m_hasOutput);
        ReleaseAssertImp(m_isOutputReuseReg, m_owner->OutputWorthReuseRegister() && m_outputReuseInputOrdinal != static_cast<size_t>(-1));
        return m_isOutputReuseReg;
    }

    // May only be called if output exists, is GPR, and does not reuse an input reg
    //
    bool IsOutputGroup1Reg()
    {
        ReleaseAssert(m_owner->m_owner->m_hasOutput);
        ReleaseAssert(m_owner->IsOutputGPR());
        ReleaseAssert(!IsOutputReuseReg());
        return m_isOutputGroup1;
    }

    size_t GetOutputReuseInputOrd()
    {
        ReleaseAssert(IsOutputReuseReg());
        ReleaseAssert(m_outputReuseInputOrdinal < m_isOperandGroup1.size());
        ReleaseAssertIff(m_owner->IsOutputGPR(), m_owner->IsInputOperandGPR(m_outputReuseInputOrdinal));
        return m_outputReuseInputOrdinal;
    }

    bool IsBrDecisionReuseReg()
    {
        ReleaseAssert(m_owner->m_owner->m_hasBrDecision);
        ReleaseAssertImp(m_isBrDecisionReuseReg, m_owner->BrDecisionWorthReuseRegister() && m_brDecisionReuseInputOrdinal != static_cast<size_t>(-1));
        return m_isBrDecisionReuseReg;
    }

    // May only be called if brDecision exists and does not reuse an input reg
    //
    bool IsBrDecisionGroup1Reg()
    {
        ReleaseAssert(m_owner->m_owner->m_hasBrDecision);
        ReleaseAssert(!IsBrDecisionReuseReg());
        return m_isBrDecisionGroup1;
    }

    size_t GetBrDecisionReuseInputOrd()
    {
        ReleaseAssert(IsBrDecisionReuseReg());
        ReleaseAssert(m_brDecisionReuseInputOrdinal < m_isOperandGroup1.size());
        ReleaseAssert(m_owner->IsInputOperandGPR(m_brDecisionReuseInputOrdinal));
        return m_brDecisionReuseInputOrdinal;
    }

    std::pair<size_t /*GPR*/, size_t /*FPR*/> GetTrueMaxPassthrus()
    {
        size_t gpr = m_owner->GetMaxGprPassthrus();
        size_t fpr = m_owner->GetMaxFprPassthrus();
        if (m_owner->m_owner->m_hasOutput)
        {
            if (IsOutputReuseReg())
            {
                if (m_owner->IsOutputGPR()) { gpr++; } else { fpr++; }
            }
        }
        if (m_owner->m_owner->m_hasBrDecision)
        {
            if (IsBrDecisionReuseReg())
            {
                gpr++;
            }
        }
        return std::make_pair(gpr, fpr);
    }

    size_t GetTrueMaxGprPassthrus() { return GetTrueMaxPassthrus().first; }
    size_t GetTrueMaxFprPassthrus() { return GetTrueMaxPassthrus().second; }

    bool IsGprOperandGroup1(size_t raOpIdx)
    {
        ReleaseAssert(raOpIdx < m_isOperandGroup1.size());
        ReleaseAssert(m_isOperandGroup1.size() == m_owner->m_isOperandGPR.size());
        ReleaseAssert(m_owner->IsInputOperandGPR(raOpIdx));
        return m_isOperandGroup1[raOpIdx];
    }

    size_t GetNumGroup1Passthrus()
    {
        ReleaseAssert(m_numGroup1Passthroughs != static_cast<size_t>(-1));
        return m_numGroup1Passthroughs;
    }

    DfgNodeRegAllocVariant* GetOwner() { ReleaseAssert(m_owner != nullptr); return m_owner; }
    size_t NumRaOperands() { return m_isOperandGroup1.size(); }

private:
    friend DfgNodeRegAllocVariant;

    DfgNodeRegAllocVariant* m_owner;

    // If the operand is GPR, whether it is in Group1 or Group2
    //
    std::vector<bool> m_isOperandGroup1;
    bool m_isOutputReuseReg;
    bool m_isOutputGroup1;
    size_t m_outputReuseInputOrdinal;
    bool m_isBrDecisionReuseReg;
    bool m_isBrDecisionGroup1;
    size_t m_brDecisionReuseInputOrdinal;
    size_t m_numGroup1Passthroughs;
};

struct RegisterDemandEstimationMetric
{
    bool m_isValid;
    size_t m_fastPathInst;
    size_t m_slowPathInst;
    size_t m_icTotalInst;
    double m_icAvgInst;
    size_t m_fastPathStackOp;
    size_t m_slowPathStackOp;
    double m_icAvgStackOp;

    RegisterDemandEstimationMetric()
        : m_isValid(false)
        , m_fastPathInst(0)
        , m_slowPathInst(0)
        , m_icTotalInst(0)
        , m_icAvgInst(0)
        , m_fastPathStackOp(0)
        , m_slowPathStackOp(0)
        , m_icAvgStackOp(0)
    { }

    static std::string GetHeader()
    {
        return "#FastPathInst, #SlowPathInst, #IcAvgInst, #FastPathStackOp, #SlowPathStackOp, #IcAvgStackOp";
    }

    std::string GetAuditInfo();

    // A negative result means 'other' is better
    //
    double CompareWith(RegisterDemandEstimationMetric& other);
};

}   // namespace dast
