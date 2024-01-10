#pragma once

#include "common_utils.h"
#include "dfg_arena.h"
#include "bit_vector_utils.h"

class CodeBlock;

namespace dfg {

struct DfgControlFlowAndUpvalueAnalysisResult;

struct BytecodeLiveness
{
    MAKE_NONCOPYABLE(BytecodeLiveness);
    MAKE_NONMOVABLE(BytecodeLiveness);

    BytecodeLiveness() = default;

    enum CalculationPoint
    {
        // The liveness state before the bytecode has used its inputs
        //
        BeforeUse,
        // The liveness state after the bytecode has used its inputs, but before it does any effect (writing outputs, clobbering slots)
        //
        AfterUse
    };

    bool IsInitialized() { return m_beforeUse.size() > 0; }

    const BitVectorView WARN_UNUSED GetLiveness(size_t bytecodeIndex, CalculationPoint calculationPoint)
    {
        TestAssert(IsInitialized());
        if (calculationPoint == CalculationPoint::BeforeUse)
        {
            TestAssert(bytecodeIndex < m_beforeUse.size());
            return m_beforeUse[bytecodeIndex];
        }
        else
        {
            TestAssert(bytecodeIndex < m_afterUse.size());
            return m_afterUse[bytecodeIndex];
        }
    }

    bool IsBytecodeLocalLive(size_t bytecodeIndex, CalculationPoint calculationPoint, size_t bytecodeLocalOrd)
    {
        TestAssert(IsInitialized());
        if (calculationPoint == CalculationPoint::BeforeUse)
        {
            TestAssert(bytecodeIndex < m_beforeUse.size());
            TestAssert(bytecodeLocalOrd < m_beforeUse[bytecodeIndex].m_length);
            return m_beforeUse[bytecodeIndex].IsSet(bytecodeLocalOrd);
        }
        else
        {
            TestAssert(bytecodeIndex < m_afterUse.size());
            TestAssert(bytecodeLocalOrd < m_afterUse[bytecodeIndex].m_length);
            return m_afterUse[bytecodeIndex].IsSet(bytecodeLocalOrd);
        }
    }

    static BytecodeLiveness* WARN_UNUSED ComputeBytecodeLiveness(CodeBlock* codeBlock, const DfgControlFlowAndUpvalueAnalysisResult& cfUvInfo);

    // bytecodeIndex => a bitvector representing liveness of each interpreter slot
    //
    DVector<DBitVector> m_beforeUse;
    DVector<DBitVector> m_afterUse;
};

}   // namespace dfg
