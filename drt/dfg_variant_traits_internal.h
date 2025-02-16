#pragma once

#include "common_utils.h"
#include "bytecode_builder.h"
#include "dfg_node.h"
#include "dfg_reg_alloc_register_info.h"
#include "dfg_codegen_protocol.h"
#include "dfg_builtin_nodes.h"
#include "dfg_typemask_overapprox_automata.h"

// DEVNOTE:
// This header file is only supposed to be directly included by the generated headers.
//
// !!! You should include "dfg_variant_traits.h" instead of this file !!!
//

namespace dfg {

using BCKind = DeegenBytecodeBuilder::BCKind;

// Specialized by generated header files, which should contain the following fields:
//   size_t value
//     The total number of DfgVariants for this bytecode
//
template<BCKind bcKind>
struct NumDfgVariantsForBCKind;

// Specialized by generated header files, which should contain the following fields:
//   size_t numCodegenFuncs
//     The total number of codegen functions for this DfgVariant
//   const DfgVariantTraits* trait
//     The trait object of this DfgVariant
//
template<BCKind bcKind, size_t dvOrd>
struct DfgVariantTraitFor;

// For assertion purpose only, return false if it detects any incompatibility
// TODO FIXME
//
using DfgVariantValidityCheckerFn = bool(*)(void*, void*);

// Specialized by generated header files, which should contain the following fields:
//   DfgVariantValidityCheckerFn checkValidFn;
//   CodegenImplFn codegenFn;
//   size_t numGenericIcCases;
//   std::array<uint8_t, numGenericIcCases> genericIcStubAllocSteppings;
//   uint16_t fastPathLen;
//   uint16_t slowPathLen;
//   uint16_t dataSecLen;
//   uint16_t dataSecAlignment;
//
template<BCKind bcKind, size_t dvOrd, size_t cgFnOrd>
struct DfgCodegenFuncInfoFor;

// Whether an operand is allowed to use GPR or FPR
//
struct DfgNodeOperandRegBankPref
{
    constexpr DfgNodeOperandRegBankPref() : m_taggedVal(0) { }

    constexpr DfgNodeOperandRegBankPref(bool gprAllowed, bool fprAllowed, bool gprPreferred)
    {
        TestAssertImp(gprPreferred, gprAllowed && fprAllowed);
        m_taggedVal = 8;
        if (gprAllowed) { m_taggedVal |= 1; }
        if (fprAllowed) { m_taggedVal |= 2; }
        if (gprPreferred) { m_taggedVal |= 4; }
    }

    constexpr bool GprAllowed() const { TestAssert(Valid()); return m_taggedVal & 1; }
    constexpr bool FprAllowed() const { TestAssert(Valid()); return m_taggedVal & 2; }
    constexpr bool GprPreferred() const { TestAssert(Valid()); return m_taggedVal & 4; }

    constexpr bool Valid() const { return m_taggedVal & 8; }

    // If HasChoices() is true, this operand contributes to the RegBankSubVariant selection
    // (since it may be either GPR or FPR).
    // When computing the RegBankSubVariant ordinal, selecting GPR means 0, FPR means 1
    //
    constexpr bool HasChoices() const { return GprAllowed() && FprAllowed(); }

private:
    uint8_t m_taggedVal;
};
static_assert(sizeof(DfgNodeOperandRegBankPref) == 1);

struct DfgRegBankSubVariantTraits;

struct DfgVariantTraits
{
    consteval DfgVariantTraits(uint16_t codegenFuncOrd)
        : m_numRaOperands(static_cast<uint8_t>(-1))
        , m_outputOperandRegPref()
        , m_codegenFuncOrd(codegenFuncOrd)
    { }

    template<size_t N>
    consteval DfgVariantTraits(std::array<DfgNodeOperandRegBankPref, N> operandsInfo,
                               DfgNodeOperandRegBankPref outputInfo)
        : m_numRaOperands(static_cast<uint8_t>(N))
        , m_outputOperandRegPref(outputInfo)
        , m_operandsRegPref()
    {
        ReleaseAssert(N <= 6);
        for (size_t i = 0; i < N; i++)
        {
            ReleaseAssert(operandsInfo[i].Valid());
            m_operandsRegPref[i] = operandsInfo[i];
        }
    }

    constexpr bool IsRegAllocEnabled() const
    {
        return m_numRaOperands != static_cast<uint8_t>(-1);
    }

    constexpr DfgCodegenFuncOrd GetCodegenFuncOrdNoRegAlloc() const
    {
        TestAssert(!IsRegAllocEnabled());
        return static_cast<DfgCodegenFuncOrd>(m_codegenFuncOrd);
    }

    constexpr size_t NumOperandsForRA() const
    {
        TestAssert(IsRegAllocEnabled());
        return m_numRaOperands;
    }

    constexpr DfgNodeOperandRegBankPref Operand(size_t opIdx) const
    {
        TestAssert(IsRegAllocEnabled() && opIdx < m_numRaOperands && opIdx < 6);
        return m_operandsRegPref[opIdx];
    }

    constexpr bool HasOutput() const
    {
        TestAssert(IsRegAllocEnabled());
        return m_outputOperandRegPref.Valid();
    }

    constexpr DfgNodeOperandRegBankPref Output() const
    {
        TestAssert(IsRegAllocEnabled());
        TestAssert(HasOutput());
        return m_outputOperandRegPref;
    }

    // For assertion purpose to check that the load is in-bound
    // The final selected codegen variant will have further check that all its inputs are valid
    //
    constexpr size_t GetTotalNumRegBankSubVariants() const
    {
        TestAssert(IsRegAllocEnabled());
        size_t cnt = 1;
        for (size_t i = 0; i < m_numRaOperands; i++)
        {
            if (Operand(i).HasChoices())
            {
                cnt *= 2;
            }
        }
        if (HasOutput() && Output().HasChoices())
        {
            cnt *= 2;
        }
        return cnt;
    }

    static constexpr size_t GetTrailingArrayOffset()
    {
        // The trailing array is an array of pointers
        //
        return RoundUpToMultipleOf<alignof(void*)>(sizeof(DfgVariantTraits));
    }

    // The table for all the RegBankSubVariant
    // The ordinal is computed from the reg bank selection:
    //   For each operand that can be chosen between GPR and FPR, GPR means 0, FPR means 1.
    //   The weight is given from high to low in this order: all the operands, the output.
    //
    const DfgRegBankSubVariantTraits* GetRegBankSubVariant(size_t subVariantOrd) const
    {
        TestAssert(IsRegAllocEnabled());
        TestAssert(subVariantOrd < GetTotalNumRegBankSubVariants());

        static constexpr size_t x_trailingArrayOffset = GetTrailingArrayOffset();
        static_assert(x_trailingArrayOffset % alignof(void*) == 0);

        uintptr_t trailingArrayPtr = reinterpret_cast<uintptr_t>(this) + x_trailingArrayOffset;
        TestAssert(trailingArrayPtr % alignof(void*) == 0);

        const DfgRegBankSubVariantTraits* const* trailingArray = reinterpret_cast<const DfgRegBankSubVariantTraits* const*>(trailingArrayPtr);
        return trailingArray[subVariantOrd];
    }

private:
    // -1 means reg alloc is disabled
    //
    uint8_t m_numRaOperands;
    // The reg bank preference for the output operand, invalid if it doesn't exist
    //
    DfgNodeOperandRegBankPref m_outputOperandRegPref;
    union {
        // The reg bank preference for each input operand
        //
        DfgNodeOperandRegBankPref m_operandsRegPref[6];
        // The codegen function ordinal, if reg alloc is disabled
        //
        uint16_t m_codegenFuncOrd;
    };
};

template<size_t N>
struct DfgVariantTraitsHolder final : public DfgVariantTraits
{
    template<size_t M>
    consteval DfgVariantTraitsHolder(std::array<DfgNodeOperandRegBankPref, M> operandsInfo,
                                     DfgNodeOperandRegBankPref outputInfo,
                                     const std::array<const DfgRegBankSubVariantTraits*, N>& subvariants)
        : DfgVariantTraits(operandsInfo, outputInfo)
        , m_regBankSubVariants(subvariants)
    {
        ReleaseAssert(GetTotalNumRegBankSubVariants() == N);
        ReleaseAssert(offsetof_member_v<&DfgVariantTraitsHolder<N>::m_regBankSubVariants> == GetTrailingArrayOffset());
        for (size_t i = 0; i < N; i++)
        {
            ReleaseAssert(m_regBankSubVariants[i] != nullptr);
        }
    }

    std::array<const DfgRegBankSubVariantTraits*, N> m_regBankSubVariants;
};

struct DfgRegBankSubVariantTraits
{
    consteval DfgRegBankSubVariantTraits(bool hasOutput,
                                         bool outputWorthReuseInputReg,
                                         bool isOutputGpr,
                                         bool hasBrDecision,
                                         bool brDecisionWorthReuseInputReg,
                                         bool hasRangedOperand,
                                         uint8_t numRAOperands,
                                         uint8_t operandIsGprMask,
                                         uint8_t numGprScratchNeeded,
                                         uint8_t numFprScratchNeeded)
        : m_hasOutput(hasOutput)
        , m_outputWorthReuseInputReg(outputWorthReuseInputReg)
        , m_outputPrefersReuseInputReg(outputWorthReuseInputReg)        // TODO FIXME
        , m_isOutputGpr(isOutputGpr)
        , m_hasBrDecision(hasBrDecision)
        , m_brDecisionWorthReuseInputReg(brDecisionWorthReuseInputReg)
        , m_brDecisionPrefersReuseInputReg(brDecisionWorthReuseInputReg)    // TODO FIXME
        , m_hasRangedOperand(hasRangedOperand)
        , m_shouldRelocateAllGroup1Gprs(false)      // TODO FIXME
        , m_numRAOperands(numRAOperands)
        , m_numBrDecisionChoices(0)
        , m_numOutputChoices(0)
        , m_numGprScratchNeeded(numGprScratchNeeded)
        , m_numFprScratchNeeded(numFprScratchNeeded)
        , m_operandIsGprMask(operandIsGprMask)
        , m_tableLength(0)
    {
        ReleaseAssert(numRAOperands <= 8);
        ReleaseAssert(m_operandIsGprMask < (1U << m_numRAOperands));

        if (m_hasOutput)
        {
            if (m_isOutputGpr)
            {
                m_numOutputChoices = 2;
                if (m_outputWorthReuseInputReg)
                {
                    for (size_t i = 0; i < m_numRAOperands; i++)
                    {
                        if (m_operandIsGprMask & static_cast<uint8_t>(1 << i)) { m_numOutputChoices++; }
                    }
                }
            }
            else
            {
                m_numOutputChoices = 1;
                if (m_outputWorthReuseInputReg)
                {
                    for (size_t i = 0; i < m_numRAOperands; i++)
                    {
                        if ((m_operandIsGprMask & static_cast<uint8_t>(1 << i)) == 0) { m_numOutputChoices++; }
                    }
                }
            }
        }
        else
        {
            m_numOutputChoices = 1;
            ReleaseAssert(!m_outputWorthReuseInputReg && !m_isOutputGpr);
        }

        if (m_hasBrDecision)
        {
            m_numBrDecisionChoices = 2;
            if (m_brDecisionWorthReuseInputReg)
            {
                for (size_t i = 0; i < m_numRAOperands; i++)
                {
                    if (m_operandIsGprMask & static_cast<uint8_t>(1 << i)) { m_numBrDecisionChoices++; }
                }
            }
        }
        else
        {
            m_numBrDecisionChoices = 1;
            ReleaseAssert(!m_brDecisionWorthReuseInputReg);
        }

        size_t len = (x_dfg_reg_alloc_num_group1_gprs + 1);
        len *= m_numOutputChoices;
        len *= m_numBrDecisionChoices;
        for (size_t i = 0; i < m_numRAOperands; i++)
        {
            if (m_operandIsGprMask & static_cast<uint8_t>(1 << i)) { len *= 2; }
        }

        ReleaseAssert(len < 65536);
        m_tableLength = SafeIntegerCast<uint16_t>(len);
    }

    consteval DfgRegBankSubVariantTraits(const DfgRegBankSubVariantTraits& other) = default;
    consteval DfgRegBankSubVariantTraits(DfgRegBankSubVariantTraits&& other) = default;

    static constexpr size_t GetTrailingArrayOffset()
    {
        // The trailing array is an array of uint16_t
        //
        return RoundUpToMultipleOf<alignof(uint16_t)>(sizeof(DfgRegBankSubVariantTraits));
    }

    bool WARN_UNUSED HasOutput() const { return m_hasOutput; }
    bool WARN_UNUSED IsOutputGPR() const { TestAssert(HasOutput()); return m_isOutputGpr; }
    bool WARN_UNUSED OutputMayReuseInputReg() const { TestAssert(HasOutput()); return m_outputWorthReuseInputReg; }
    bool WARN_UNUSED OutputPrefersReuseInputReg() const
    {
        TestAssertImp(!OutputMayReuseInputReg(), !m_outputPrefersReuseInputReg);
        return m_outputPrefersReuseInputReg;
    }

    bool WARN_UNUSED HasBrDecision() const { return m_hasBrDecision; }
    bool WARN_UNUSED BrDecisionMayReuseInputReg() const { TestAssert(HasBrDecision()); return m_brDecisionWorthReuseInputReg; }
    bool WARN_UNUSED BrDecisionPrefersReuseInputReg() const
    {
        TestAssertImp(!BrDecisionMayReuseInputReg(), !m_brDecisionPrefersReuseInputReg);
        return m_brDecisionPrefersReuseInputReg;
    }

    bool WARN_UNUSED HasRangedOperand() const { return m_hasRangedOperand; }

    bool WARN_UNUSED ShouldRelocateAllGroup1Gprs() const { return m_shouldRelocateAllGroup1Gprs; }

    uint8_t WARN_UNUSED NumOutputChoices() const { return m_numOutputChoices; }
    uint8_t WARN_UNUSED NumBrDecisionChoices() const { return m_numBrDecisionChoices; }

    uint8_t WARN_UNUSED NumRAOperands() const { return m_numRAOperands; }

    uint8_t WARN_UNUSED NumGprScratchNeeded() const { return m_numGprScratchNeeded; }
    uint8_t WARN_UNUSED NumFprScratchNeeded() const { return m_numFprScratchNeeded; }

    bool WARN_UNUSED OperandIsGPR(size_t raOperandOrd) const
    {
        TestAssert(raOperandOrd < m_numRAOperands);
        return m_operandIsGprMask & (1U << raOperandOrd);
    }

    uint16_t WARN_UNUSED GetTableLength() const { return m_tableLength; }

    // Ordering in each dimension:
    //
    //   GPR operand: Group1 comes first
    //   Output operand: own register (GPR/FPR) comes first, reuses operand register comes next in the same order as operands
    //   brDecision operand: similar to Output operand
    //   numGroup1GprPassthrus: in increasing order
    //
    // This must be kept in sync with how Deegen generates the variants, see:
    //   deegen_dfg_reg_alloc_variants.h -- DfgNodeRegAllocVariant::GenerateSubVariants
    //
    DfgCodegenFuncOrd WARN_UNUSED GetTrailingArrayElement(size_t idx) const
    {
        TestAssert(idx < m_tableLength);

        static constexpr size_t x_trailingArrayOffset = GetTrailingArrayOffset();
        static_assert(x_trailingArrayOffset % alignof(uint16_t) == 0);

        uintptr_t trailingArrayPtr = reinterpret_cast<uintptr_t>(this) + x_trailingArrayOffset;
        TestAssert(trailingArrayPtr % alignof(uint16_t) == 0);

        const uint16_t* trailingArray = reinterpret_cast<const uint16_t*>(trailingArrayPtr);
        uint16_t res = trailingArray[idx];
        TestAssert(res != static_cast<uint16_t>(-1));
        return static_cast<DfgCodegenFuncOrd>(res);
    }

protected:
    bool m_hasOutput : 1;
    bool m_outputWorthReuseInputReg : 1;
    bool m_outputPrefersReuseInputReg : 1;
    bool m_isOutputGpr : 1;
    bool m_hasBrDecision : 1;
    bool m_brDecisionWorthReuseInputReg : 1;
    bool m_brDecisionPrefersReuseInputReg : 1;
    bool m_hasRangedOperand : 1;
    bool m_shouldRelocateAllGroup1Gprs : 1;
    uint8_t m_numRAOperands : 3;
    uint8_t m_numBrDecisionChoices: 4;
    uint8_t m_numOutputChoices;
    uint8_t m_numGprScratchNeeded;
    uint8_t m_numFprScratchNeeded;
    uint8_t m_operandIsGprMask;
    uint16_t m_tableLength;
};
static_assert(sizeof(DfgRegBankSubVariantTraits) == 8);

template<size_t N>
struct DfgRegBankSubVariantTraitsHolder final : public DfgRegBankSubVariantTraits
{
    consteval DfgRegBankSubVariantTraitsHolder(DfgRegBankSubVariantTraits base,
                                               const std::array<uint16_t, N>& codegenFuncOrds)
        : DfgRegBankSubVariantTraits(base)
        , m_codegenFuncOrds(codegenFuncOrds)
    {
        ReleaseAssert(m_tableLength == N);
        ReleaseAssert(offsetof_member_v<&DfgRegBankSubVariantTraitsHolder<N>::m_codegenFuncOrds> == GetTrailingArrayOffset());
    }

    // The valid ordinals in the array should form a consecutive non-empty list of values
    // This function check that this is the case, and return the range
    //
    consteval std::pair<size_t /*start*/, size_t /*num*/> WARN_UNUSED CheckArrayValid() const
    {
        size_t startVal = static_cast<size_t>(-1);
        size_t endVal = static_cast<size_t>(-1);
        for (size_t i = 0; i < N; i++)
        {
            if (m_codegenFuncOrds[i] == static_cast<uint16_t>(-1))
            {
                continue;
            }
            if (startVal == static_cast<size_t>(-1))
            {
                startVal = m_codegenFuncOrds[i];
                endVal = m_codegenFuncOrds[i] + 1;
            }
            else
            {
                ReleaseAssert(m_codegenFuncOrds[i] == endVal);
                endVal++;
            }
        }
        ReleaseAssert(startVal != static_cast<size_t>(-1));
        ReleaseAssert(endVal > startVal);
        return std::make_pair(startVal, endVal - startVal);
    }

    std::array<uint16_t, N> m_codegenFuncOrds;
};

struct NodeOperandsAccessor
{
    template<size_t knownNsdSizeBytes = static_cast<size_t>(-1)>
    uint8_t* GetNodeSpecificData()
    {
        if constexpr(knownNsdSizeBytes == static_cast<size_t>(-1))
        {
            return m_node->GetNodeSpecificData();
        }
        else if constexpr(knownNsdSizeBytes <= Node::x_maxNodeSpecificDataSizeToStoreInline)
        {
            return m_node->GetNodeSpecificDataMustInlined();
        }
        else
        {
            return m_node->GetNodeSpecificDataMustOutlined();
        }
    }

    template<size_t knownTotalNumInputs = static_cast<size_t>(-1)>
    Edge& GetInputEdge(uint32_t inputOrd)
    {
        if constexpr(knownTotalNumInputs == static_cast<size_t>(-1))
        {
            return m_node->GetInputEdge(inputOrd);
        }
        else
        {
            return m_node->GetInputEdgeForNodeWithFixedNumInputs<knownTotalNumInputs>(inputOrd);
        }
    }

    void SetDfgVariantId(DfgVariantId /*val*/)
    {
        // TODO FIXME: implement
    }

    Node* m_node;
};

}   // namespace dfg
