#include "dfg_node.h"
#include "bytecode_builder.h"

namespace dfg {

constexpr BCKind x_bcKindEndOfEnum = BCKind::X_END_OF_ENUM;

static_assert(static_cast<size_t>(BCKind::X_END_OF_ENUM) + static_cast<size_t>(NodeKind_FirstAvailableGuestLanguageNodeKind) <= std::numeric_limits<std::underlying_type_t<NodeKind>>::max());

void NodeRangeOperandInfoDecoder::Query(Node* node)
{
    if (node->IsBuiltinNodeKind())
    {
        m_nodeHasRangeOperand = false;
        m_numInputs = 0;
        m_numOutputs = 0;
        m_requiredRangeSize = 0;
        return;
    }

    using BytecodeDecoder = DeegenBytecodeBuilder::BytecodeDecoder;

    BCKind bcKind = node->GetGuestLanguageBCKind();
    if (!BytecodeDecoder::BytecodeHasRangeOperand(bcKind))
    {
        m_nodeHasRangeOperand = false;
        m_numInputs = 0;
        m_numOutputs = 0;
        m_requiredRangeSize = 0;
        return;
    }

    m_nodeHasRangeOperand = true;

    {
        size_t requiredBufferSize = node->GetNumInputs();
        if (unlikely(requiredBufferSize > m_inputResultBuffer.size()))
        {
            m_inputResultBuffer.resize(requiredBufferSize);
            m_inputOffsets = m_inputResultBuffer.data();
        }
        m_numInputs = m_inputResultBuffer.size();
    }

    {
        size_t requiredBufferSize = node->GetNumExtraOutputs();
        if (unlikely(requiredBufferSize > m_outputResultBuffer.size()))
        {
            m_outputResultBuffer.resize(requiredBufferSize);
            m_outputOffsets = m_outputResultBuffer.data();
        }
        m_numOutputs = m_outputResultBuffer.size();
    }

    BytecodeDecoder::GetDfgRangeOperandRWInfo(bcKind,
                                              node->GetNodeSpecificData(),
                                              m_inputOffsets /*out*/,
                                              m_outputOffsets /*out*/,
                                              m_numInputs /*inout*/,
                                              m_numOutputs /*inout*/,
                                              m_requiredRangeSize /*out*/);
}

// TODO FIXME
//
extern "C" void NO_RETURN __deegen_dfg_jit_osr_exit_handler()
{
    abort();
}

}   // namespace dfg
