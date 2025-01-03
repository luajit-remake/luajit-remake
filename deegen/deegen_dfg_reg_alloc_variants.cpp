#include "deegen_dfg_reg_alloc_variants.h"
#include "dfg_reg_alloc_register_info.h"

namespace dast {

void DfgNodeRegAllocRootInfo::GenerateVariants()
{
    ReleaseAssert(m_variants.empty());

    size_t numConfigs = 1;
    for (OpInfo& info : m_operandInfo)
    {
        ReleaseAssert(info.m_allowGPR || info.m_allowFPR);
        if (info.m_allowGPR && info.m_allowFPR)
        {
            numConfigs *= 2;
        }
    }

    if (m_hasOutput)
    {
        ReleaseAssert(m_outputInfo.m_allowGPR || m_outputInfo.m_allowFPR);
        if (m_outputInfo.m_allowGPR && m_outputInfo.m_allowFPR)
        {
            numConfigs *= 2;
        }
    }

    for (size_t mask = 0; mask < numConfigs; mask++)
    {
        std::unique_ptr<DfgNodeRegAllocVariant> rav(new DfgNodeRegAllocVariant());
        rav->m_owner = this;

        {
            size_t tmp = mask;
            if (m_hasOutput)
            {
                if (m_outputInfo.IsBothGprAndFprAllowed())
                {
                    rav->SetIsOutputGPR(tmp % 2 == 0);
                    tmp /= 2;
                }
                else
                {
                    rav->SetIsOutputGPR(m_outputInfo.IsOnlyAllowedValueGPR());
                }
            }

            rav->m_isOperandGPR.resize(m_operandInfo.size());
            for (size_t operandIdx = m_operandInfo.size(); operandIdx--;)
            {
                if (m_operandInfo[operandIdx].IsBothGprAndFprAllowed())
                {
                    rav->m_isOperandGPR[operandIdx] = (tmp % 2 == 0);
                    tmp /= 2;
                }
                else
                {
                    rav->m_isOperandGPR[operandIdx] = m_operandInfo[operandIdx].IsOnlyAllowedValueGPR();
                }
            }

            ReleaseAssert(tmp == 0);
        }

        m_variants.push_back(std::move(rav));
    }

    ReleaseAssert(m_variants.size() == numConfigs);
}

void DfgNodeRegAllocRootInfo::PrintCppDefinitions(FILE* hdrFile,
                                                  FILE* cppFile,
                                                  const std::string& cppNameIdent,
                                                  const std::vector<std::string>& variantCppIdents,
                                                  const std::string& expectedBaseCgFnOrdExpr,
                                                  size_t expectedTotalNumCgFns)
{
    ReleaseAssert(variantCppIdents.size() == m_variants.size());

    fprintf(hdrFile, "extern const DfgVariantTraitsHolder<%d> x_deegen_dfg_variant_%s;\n",
            static_cast<int>(m_variants.size()), cppNameIdent.c_str());

    fprintf(cppFile, "constexpr DfgVariantTraitsHolder<%d> x_deegen_dfg_variant_%s(std::array<DfgNodeOperandRegBankPref, %d> {\n",
            static_cast<int>(m_variants.size()), cppNameIdent.c_str(), static_cast<int>(m_operandInfo.size()));

    for (size_t idx = 0; idx < m_operandInfo.size(); idx++)
    {
        auto& op = m_operandInfo[idx];
        if (idx > 0) { fprintf(cppFile, ",\n"); }
        fprintf(cppFile, "        DfgNodeOperandRegBankPref(/*gprAllowed*/ %s, /*fprAllowed*/ %s, /*gprPreferred*/ %s)",
                (op.m_allowGPR ? "true" : "false"),
                (op.m_allowFPR ? "true" : "false"),
                (op.m_allowGPR && op.m_allowFPR && op.m_preferGPR ? "true" : "false"));
    }
    fprintf(cppFile, "\n    },\n");
    if (m_hasOutput)
    {
        auto& op = m_outputInfo;
        fprintf(cppFile, "    DfgNodeOperandRegBankPref(/*gprAllowed*/ %s, /*fprAllowed*/ %s, /*gprPreferred*/ %s),\n",
                (op.m_allowGPR ? "true" : "false"),
                (op.m_allowFPR ? "true" : "false"),
                (op.m_allowGPR && op.m_allowFPR && op.m_preferGPR ? "true" : "false"));
    }
    else
    {
        fprintf(cppFile, "    DfgNodeOperandRegBankPref(),\n");
    }
    fprintf(cppFile, "    std::array<const DfgRegBankSubVariantTraits*, %d> {\n", static_cast<int>(m_variants.size()));

    for (size_t idx = 0; idx < variantCppIdents.size(); idx++)
    {
        if (idx > 0) { fprintf(cppFile, ",\n"); }
        fprintf(cppFile, "        &x_deegen_dfg_reg_bank_subvariant_%s", variantCppIdents[idx].c_str());
    }
    fprintf(cppFile, "\n    });\n");

    fprintf(cppFile, "static_assert([]() {\n");
    fprintf(cppFile, "    std::pair<size_t, size_t> val, newVal;\n");
    ReleaseAssert(variantCppIdents.size() > 0);
    for (size_t idx = 0; idx < variantCppIdents.size(); idx++)
    {
        if (idx > 0)
        {
            fprintf(cppFile, "val = newVal;\n");
        }
        else
        {
            fprintf(cppFile, "size_t startOrd = (%s);\n", expectedBaseCgFnOrdExpr.c_str());
            fprintf(cppFile, "val = std::make_pair(startOrd, 0);\n");
        }
        fprintf(cppFile, "    newVal = x_deegen_dfg_reg_bank_subvariant_%s.CheckArrayValid();\n", variantCppIdents[idx].c_str());
        fprintf(cppFile, "    ReleaseAssert(newVal.first == val.first + val.second);\n");
    }
    ReleaseAssert(expectedTotalNumCgFns > 0);
    fprintf(cppFile, "    ReleaseAssert(newVal.first + newVal.second == startOrd + %d);\n", static_cast<int>(expectedTotalNumCgFns));
    fprintf(cppFile, "    return true;\n");
    fprintf(cppFile, "}());\n\n");
}

std::string DfgNodeRegAllocVariant::GetVariantRegConfigDescForAudit()
{
    std::string s = "";
    for (size_t operandIdx = 0; operandIdx < m_isOperandGPR.size(); operandIdx++)
    {
        s += (m_isOperandGPR[operandIdx]) ? "g" : "f";
    }
    if (m_owner->m_hasOutput)
    {
        s += std::string("_") + (IsOutputGPR() ? "g" : "f");
    }
    return s;
}

size_t DfgNodeRegAllocVariant::GetNumGprOperands()
{
    size_t numGprOperands = 0;
    for (size_t i = 0; i < m_isOperandGPR.size(); i++)
    {
        if (m_isOperandGPR[i])
        {
            numGprOperands++;
        }
    }
    return numGprOperands;
}

size_t DfgNodeRegAllocVariant::GetNumFprOperands()
{
    size_t numGprOperands = GetNumGprOperands();
    ReleaseAssert(numGprOperands <= m_isOperandGPR.size());
    return m_isOperandGPR.size() - numGprOperands;
}

size_t DfgNodeRegAllocVariant::GetKthGprOperandOrdinal(size_t k)
{
    size_t numGprOperands = 0;
    for (size_t i = 0; i < m_isOperandGPR.size(); i++)
    {
        if (m_isOperandGPR[i])
        {
            if (numGprOperands == k)
            {
                return i;
            }
            numGprOperands++;
        }
    }
    ReleaseAssert(false);
}

size_t DfgNodeRegAllocVariant::GetKthFprOperandOrdinal(size_t k)
{
    size_t numFprOperands = 0;
    for (size_t i = 0; i < m_isOperandGPR.size(); i++)
    {
        if (!m_isOperandGPR[i])
        {
            if (numFprOperands == k)
            {
                return i;
            }
            numFprOperands++;
        }
    }
    ReleaseAssert(false);
}

size_t DfgNodeRegAllocVariant::GetOutputNumChoices()
{
    ReleaseAssertIff(m_owner->m_hasOutput, m_outputWorthReuseRegisterInitialized);
    if (!m_owner->m_hasOutput)
    {
        return 1;
    }

    size_t choices = (IsOutputGPR() ? 2 : 1);
    if (OutputWorthReuseRegister())
    {
        if (IsOutputGPR())
        {
            size_t numGprOperands = GetNumGprOperands();
            ReleaseAssert(numGprOperands > 0);
            choices += numGprOperands;
        }
        else
        {
            size_t numFprOperands = GetNumFprOperands();
            ReleaseAssert(numFprOperands > 0);
            choices += numFprOperands;
        }
    }
    return choices;
}

size_t DfgNodeRegAllocVariant::GetBrDecisionNumChoices()
{
    ReleaseAssertIff(m_owner->m_hasBrDecision, m_brDecisionWorthReuseRegisterInitialized);
    if (!m_owner->m_hasBrDecision)
    {
        return 1;
    }

    size_t choices = 2;
    if (BrDecisionWorthReuseRegister())
    {
        size_t numGprOperands = GetNumGprOperands();
        ReleaseAssert(numGprOperands > 0);
        choices += numGprOperands;
    }
    return choices;
}

size_t DfgNodeRegAllocVariant::GetTableLength()
{
    size_t len = static_cast<size_t>(1) << GetNumGprOperands();

    if (m_owner->m_hasOutput)
    {
        len *= GetOutputNumChoices();
    }

    if (m_owner->m_hasBrDecision)
    {
        len *= GetBrDecisionNumChoices();
    }

    len *= (x_dfg_reg_alloc_num_group1_gprs + 1);
    return len;
}

std::vector<std::unique_ptr<DfgNodeRegAllocSubVariant>> WARN_UNUSED DfgNodeRegAllocVariant::GenerateSubVariants()
{
    ReleaseAssert(MaxGprPassthrusInitialized() && MaxFprPassthrusInitialized());

    size_t numOutputChoices = GetOutputNumChoices();
    size_t numBrDecisionChoices = GetBrDecisionNumChoices();
    size_t numGprGroup1PtChoices = (x_dfg_reg_alloc_num_group1_gprs + 1);

    size_t tabLength = GetTableLength();
    std::vector<std::unique_ptr<DfgNodeRegAllocSubVariant>> result;
    result.resize(tabLength);

    for (size_t tabIdx  = 0; tabIdx < tabLength; tabIdx++)
    {
        ReleaseAssert(tabIdx < result.size());
        std::unique_ptr<DfgNodeRegAllocSubVariant>& entry = result[tabIdx];
        entry.reset(new DfgNodeRegAllocSubVariant());
        bool success = false;
        Auto(if (!success) { entry.reset(nullptr); });

        entry->m_owner = this;

        // Decode config from tabIdx
        //
        {
            size_t tmp = tabIdx;
            entry->m_numGroup1Passthroughs = tmp % numGprGroup1PtChoices;
            tmp /= numGprGroup1PtChoices;

            if (m_owner->m_hasBrDecision)
            {
                size_t brDecisionChoice = tmp % numBrDecisionChoices;
                tmp /= numBrDecisionChoices;

                if (brDecisionChoice < 2)
                {
                    entry->m_isBrDecisionReuseReg = false;
                    entry->m_isBrDecisionGroup1 = (brDecisionChoice == 0);
                }
                else
                {
                    ReleaseAssert(BrDecisionWorthReuseRegister());
                    entry->m_isBrDecisionReuseReg = true;
                    entry->m_brDecisionReuseInputOrdinal = GetKthGprOperandOrdinal(brDecisionChoice - 2);
                }
            }

            if (m_owner->m_hasOutput)
            {
                size_t outputChoice = tmp % numOutputChoices;
                tmp /= numOutputChoices;

                if (IsOutputGPR())
                {
                    if (outputChoice < 2)
                    {
                        entry->m_isOutputReuseReg = false;
                        entry->m_isOutputGroup1 = (outputChoice == 0);
                    }
                    else
                    {
                        ReleaseAssert(OutputWorthReuseRegister());
                        entry->m_isOutputReuseReg = true;
                        entry->m_outputReuseInputOrdinal = GetKthGprOperandOrdinal(outputChoice - 2);
                    }
                }
                else
                {
                    if (outputChoice < 1)
                    {
                        entry->m_isOutputReuseReg = false;
                    }
                    else
                    {
                        ReleaseAssert(OutputWorthReuseRegister());
                        entry->m_isOutputReuseReg = true;
                        entry->m_outputReuseInputOrdinal = GetKthFprOperandOrdinal(outputChoice - 1);
                    }
                }
            }

            entry->m_isOperandGroup1.resize(m_isOperandGPR.size(), false /*value*/);
            for (size_t idx = m_isOperandGPR.size(); idx--;)
            {
                if (m_isOperandGPR[idx])
                {
                    entry->m_isOperandGroup1[idx] = (tmp % 2 == 0);
                    tmp /= 2;
                }
            }

            ReleaseAssert(tmp == 0);
        }

        size_t numGprPt = entry->GetTrueMaxGprPassthrus();
        size_t numFprPt = entry->GetTrueMaxFprPassthrus();

        // Check config is valid
        //
        {
            size_t numGprGroup1Used = 0;
            size_t numGprGroup2Used = 0;
            size_t numFprUsed = 0;
            ReleaseAssert(m_isOperandGPR.size() == entry->m_isOperandGroup1.size());
            for (size_t idx = 0; idx < m_isOperandGPR.size(); idx++)
            {
                if (m_isOperandGPR[idx])
                {
                    if (entry->IsGprOperandGroup1(idx)) { numGprGroup1Used++; } else { numGprGroup2Used++; }
                }
                else
                {
                    numFprUsed++;
                }
            }

            if (m_owner->m_hasOutput)
            {
                if (!entry->IsOutputReuseReg())
                {
                    if (IsOutputGPR())
                    {
                        if (entry->IsOutputGroup1Reg()) { numGprGroup1Used++; } else { numGprGroup2Used++; }
                    }
                    else
                    {
                        numFprUsed++;
                    }
                }
            }

            if (m_owner->m_hasBrDecision)
            {
                if (!entry->IsBrDecisionReuseReg())
                {
                    if (entry->m_isBrDecisionGroup1) { numGprGroup1Used++; } else { numGprGroup2Used++; }
                }
            }

            ReleaseAssert(numGprGroup1Used + numGprGroup2Used + numGprPt <= x_dfg_reg_alloc_num_gprs);
            ReleaseAssert(numFprUsed + numFprPt <= x_dfg_reg_alloc_num_fprs);

            if (numGprGroup1Used > x_dfg_reg_alloc_num_group1_gprs)
            {
                continue;
            }
            if (numGprGroup2Used > x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs)
            {
                continue;
            }

            // The numGroup1GprPassthru value should be valid
            //
            if (numGprGroup1Used + entry->GetNumGroup1Passthrus() > x_dfg_reg_alloc_num_group1_gprs)
            {
                continue;
            }
            if (entry->GetNumGroup1Passthrus() > numGprPt)
            {
                continue;
            }
            if (numGprGroup2Used + (numGprPt - entry->GetNumGroup1Passthrus()) > x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs)
            {
                continue;
            }

            // If both output and brDecision reused a register,
            // they must not reuse the same register
            //
            if (m_owner->m_hasOutput && m_owner->m_hasBrDecision)
            {
                if (entry->IsOutputReuseReg() && entry->IsBrDecisionReuseReg())
                {
                    if (entry->GetOutputReuseInputOrd() == entry->GetBrDecisionReuseInputOrd())
                    {
                        continue;
                    }
                }
            }
        }
        success = true;
    }

    return result;
}

void DfgNodeRegAllocVariant::PrintCppDefinitions(std::vector<std::unique_ptr<DfgNodeRegAllocSubVariant>>& subvariants,
                                                 FILE* hdrFile,
                                                 FILE* cppFile,
                                                 const std::string& cppNameIdent,
                                                 const std::string& baseOrdExpr,
                                                 bool hasRangedOperand)
{
    ReleaseAssert(subvariants.size() == GetTableLength());

    fprintf(hdrFile, "extern const DfgRegBankSubVariantTraitsHolder<%d> x_deegen_dfg_reg_bank_subvariant_%s;\n\n",
            static_cast<int>(subvariants.size()), cppNameIdent.c_str());

    fprintf(cppFile, "constexpr DfgRegBankSubVariantTraitsHolder<%d> x_deegen_dfg_reg_bank_subvariant_%s {\n\n",
            static_cast<int>(subvariants.size()), cppNameIdent.c_str());

    fprintf(cppFile, "    DfgRegBankSubVariantTraits {\n");
    fprintf(cppFile, "        /*hasOutput*/ %s,\n", m_owner->m_hasOutput ? "true" : "false");
    fprintf(cppFile, "        /*outputWorthReuseRegister*/ %s,\n", (m_owner->m_hasOutput && OutputWorthReuseRegister()) ? "true" : "false");
    fprintf(cppFile, "        /*isOutputGPR*/ %s,\n", (m_owner->m_hasOutput && IsOutputGPR()) ? "true" : "false");
    fprintf(cppFile, "        /*hasBrDecision*/ %s,\n", m_owner->m_hasBrDecision ? "true" : "false");
    fprintf(cppFile, "        /*brDecisionWorthReuseRegister*/ %s,\n", (m_owner->m_hasBrDecision && BrDecisionWorthReuseRegister()) ? "true" : "false");
    fprintf(cppFile, "        /*hasRangedOperand*/ %s,\n", hasRangedOperand ? "true" : "false");
    fprintf(cppFile, "        /*numRAOperands*/ %d,\n", static_cast<int>(NumRaOperands()));

    uint64_t isGprMask = 0;
    for (size_t opIdx = 0; opIdx < NumRaOperands(); opIdx++)
    {
        if (IsInputOperandGPR(opIdx))
        {
            isGprMask |= static_cast<uint64_t>(1) << opIdx;
        }
    }
    ReleaseAssert(isGprMask <= 255);
    fprintf(cppFile, "        /*operandIsGprMask*/ %d\n", static_cast<int>(isGprMask));
    fprintf(cppFile, "    },\n");

    fprintf(cppFile, "    []() {\n");
    fprintf(cppFile, "        constexpr uint16_t baseOrd = (%s);\n", baseOrdExpr.c_str());
    fprintf(cppFile, "        return std::array<uint16_t, %d> {\n", static_cast<int>(subvariants.size()));
    size_t ordOffset = 0;
    for (size_t idx = 0; idx < subvariants.size(); idx++)
    {
        if (idx != 0) { fprintf(cppFile, ", "); }
        if (idx % 4 == 0) { fprintf(cppFile, "\n            "); }
        if (subvariants[idx].get() == nullptr)
        {
            fprintf(cppFile, "65535");
        }
        else
        {
            fprintf(cppFile, "baseOrd + %d", static_cast<int>(ordOffset));
            ordOffset++;
        }
    }
    fprintf(cppFile, "\n        };\n    }()\n};\n");
}

std::string RegisterDemandEstimationMetric::GetAuditInfo()
{
    ReleaseAssert(m_isValid);
    char buf[100];
    snprintf(buf, 100, "%.2lf", m_icAvgInst);
    std::string icAvgInst = buf;
    snprintf(buf, 100, "%.2lf", m_icAvgStackOp);
    std::string icAvgStackOp = buf;
    return  "{ " + std::to_string(m_fastPathInst) + ", " + std::to_string(m_slowPathInst) + ", "
        + icAvgInst + ", " + std::to_string(m_fastPathStackOp) + ", " + std::to_string(m_slowPathStackOp) +
        + ", " + icAvgStackOp + " }";
}

double RegisterDemandEstimationMetric::CompareWith(RegisterDemandEstimationMetric& other)
{
    double fastPathInstDiff = static_cast<double>(other.m_fastPathInst) - static_cast<double>(m_fastPathInst);
    double slowPathInstDiff = static_cast<double>(other.m_slowPathInst) - static_cast<double>(m_slowPathInst);
    double icAvgInstDiff = other.m_icAvgInst - m_icAvgInst;
    double fastPathStackOpDiff = static_cast<double>(other.m_fastPathStackOp) - static_cast<double>(m_fastPathStackOp);
    double slowPathStackOpDiff = static_cast<double>(other.m_slowPathStackOp) - static_cast<double>(m_slowPathStackOp);
    double icAvgStackOpDiff = other.m_icAvgStackOp - m_icAvgStackOp;

    double w = 0;

    // Extra stack operation in the fast path is a strong indication that we are running out of regs
    //
    w += fastPathStackOpDiff * 2;
    // Also for slow path and IC path.. but give them slightly lower weight
    //
    w += slowPathStackOpDiff * 0.5 + icAvgStackOpDiff * 1;

    // But also consider the difference in instructions
    // There are usually two possibilities:
    // 1. Extra register shuffling due to increased register pressure
    // 2. LLVM behaved abnormally and did a different tail merge/duplication decision than before which changed instruction counts
    //
    // Instruction changes in the fast path are likely (1), they are likely always executed, but they are also likely
    // cheap register movs, so give them a slightly low weight
    //
    w += fastPathInstDiff * 0.7;

    // Instruction changes in IC can both be (1) and (2), (2) is effectively noise for our use case, so give it a even lower weight
    //
    w += icAvgInstDiff * 0.5;

    // Give slow path a further lower weight
    //
    w += slowPathInstDiff * 0.1;
    return w;
}

}   // namespace dast
