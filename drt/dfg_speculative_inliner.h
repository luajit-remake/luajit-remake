#pragma once

#include "dfg_bytecode_speculative_inlining_trait.h"
#include "bytecode_builder.h"
#include "dfg_code_origin.h"
#include "dfg_node.h"

namespace dfg {

struct DfgBuildBasicBlockContext;

struct SpeculativeInliner
{
    MAKE_NONCOPYABLE(SpeculativeInliner);
    MAKE_NONMOVABLE(SpeculativeInliner);

    SpeculativeInliner(TempArenaAllocator& alloc,
                       DeegenBytecodeBuilder::BytecodeDecoder* decoder,
                       DfgBuildBasicBlockContext* bbContext,
                       InlinedCallFrame* inlinedCallFrame);

    struct InliningResultInfo
    {
        // -1 if doesn't exist for whatever reason
        //
        size_t m_nodeIndexOfReturnContinuation;
    };

    bool WARN_UNUSED TrySpeculativeInlining(Node* callNode, size_t bcOffset, size_t bcIndex, InliningResultInfo& inlineResultInfo /*out*/)
    {
        TestAssert(m_baselineCodeBlock->GetBytecodeOffsetFromBytecodeIndex(bcIndex) == bcOffset);
        size_t opcode = m_decoder->GetCanonicalizedOpcodeAtPosition(bcOffset);
        TestAssert(opcode < m_decoder->GetTotalBytecodeKinds());
        TestAssert(m_bcTraitArray[opcode].m_isInitialized);
        if (likely(m_bcTraitArray[opcode].m_info == nullptr))
        {
            return false;
        }
        return TrySpeculativeInliningSlowPath(callNode, bcOffset, bcIndex, opcode, inlineResultInfo /*out*/);
    }

    // For testing only
    //
    static const BytecodeSpeculativeInliningInfo* GetBytecodeSpeculativeInliningInfoArray();

    // Only a safety limiter to avoid overflow in the OsrExitLocation encoding.
    // The static_assert check logic is in dfg_speculative_inliner.cpp
    //
    static constexpr size_t x_maxTotalInliningFramesAllowed = 200;

private:
    bool WARN_UNUSED TrySpeculativeInliningSlowPath(Node* prologue, size_t bcOffset, size_t bcIndex, size_t opcode, InliningResultInfo& inlineResultInfo /*out*/);

    const BytecodeSpeculativeInliningInfo* m_bcTraitArray;
    DeegenBytecodeBuilder::BytecodeDecoder* m_decoder;
    DfgBuildBasicBlockContext* m_bbContext;
    Graph* m_graph;
    InlinedCallFrame* m_inlinedCallFrame;
    BaselineCodeBlock* m_baselineCodeBlock;
    TempBitVector m_tempBv;
    TempVector<uint32_t> m_tempInputEdgeRes;
    size_t m_remainingInlineBudget;
};

}   // namespace dfg
