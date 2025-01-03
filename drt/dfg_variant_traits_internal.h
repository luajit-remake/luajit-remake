#pragma once

#include "common_utils.h"
#include "bytecode_builder.h"
#include "dfg_node.h"
#include "dfg_reg_alloc_register_info.h"
#include "dfg_codegen_protocol.h"
#include "dfg_builtin_nodes.h"

// DEVNOTE:
// This header file is only supposed to be directly included by the generated headers.
//
// !!! You should include "dfg_variant_traits.h" instead of this file !!!
//

namespace dfg {

using BCKind = DeegenBytecodeBuilder::BCKind;

enum class DfgCodegenFuncOrd : uint16_t;

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

struct NodeRegAllocInfo;
struct DfgRegAllocState;

// For assertion purpose only, return false if it detects any incompatibility
//
using DfgVariantValidityCheckerFn = bool(*)(NodeRegAllocInfo*, DfgRegAllocState*);

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

    const DfgRegBankSubVariantTraits* GetRegBankSubVariant(size_t subVariantOrd)
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

    // The table for all the RegBankSubVariant
    // The ordinal is computed from the reg bank selection:
    //   For each operand that can be chosen between GPR and FPR, GPR means 0, FPR means 1.
    //   The weight is given from high to low in this order: all the operands, the output.
    //
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
                                         uint8_t operandIsGprMask)
        : m_hasOutput(hasOutput)
        , m_outputWorthReuseInputReg(outputWorthReuseInputReg)
        , m_isOutputGpr(isOutputGpr)
        , m_hasBrDecision(hasBrDecision)
        , m_brDecisionWorthReuseInputReg(brDecisionWorthReuseInputReg)
        , m_hasRangedOperand(hasRangedOperand)
        , m_numOutputChoices(0)
        , m_numBrDecisionChoices(0)
        , m_numRAOperands(numRAOperands)
        , m_operandIsGprMask(operandIsGprMask)
        , m_operandIsFprMask(0)
        , m_tableLength(0)
    {
        ReleaseAssert(m_numRAOperands <= 8);
        ReleaseAssert(m_operandIsGprMask < (1U << m_numRAOperands));
        m_operandIsFprMask = static_cast<uint8_t>((1U << m_numRAOperands) - 1) ^ m_operandIsGprMask;

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
                        if (m_operandIsFprMask & static_cast<uint8_t>(1 << i)) { m_numOutputChoices++; }
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

    uint16_t GetTrailingArrayElement(size_t idx) const
    {
        TestAssert(idx < m_tableLength);

        static constexpr size_t x_trailingArrayOffset = GetTrailingArrayOffset();
        static_assert(x_trailingArrayOffset % alignof(uint16_t) == 0);

        uintptr_t trailingArrayPtr = reinterpret_cast<uintptr_t>(this) + x_trailingArrayOffset;
        TestAssert(trailingArrayPtr % alignof(uint16_t) == 0);

        const uint16_t* trailingArray = reinterpret_cast<const uint16_t*>(trailingArrayPtr);
        return trailingArray[idx];
    }

    // Returns the final codegen function ordinal based on the register allocation decision
    //
    DfgCodegenFuncOrd WARN_UNUSED GetCodegenFuncOrd(NodeRegAllocInfo* raInfo, DfgRegAllocState* raState) const;

protected:
    bool m_hasOutput : 1;
    bool m_outputWorthReuseInputReg : 1;
    bool m_isOutputGpr : 1;
    bool m_hasBrDecision : 1;
    bool m_brDecisionWorthReuseInputReg : 1;
    bool m_hasRangedOperand : 1;
    uint8_t m_numOutputChoices;
    uint8_t m_numBrDecisionChoices;
    uint8_t m_numRAOperands;
    uint8_t m_operandIsGprMask;
    uint8_t m_operandIsFprMask;
    uint16_t m_tableLength;
};

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
