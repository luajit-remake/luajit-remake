#pragma once

#include "common_utils.h"
#include "temp_arena_allocator.h"
#include "bit_vector_utils.h"
#include "strongly_connected_components_util.h"

namespace dfg {

struct BitmapAdjacencyGraph
{
    struct EdgeIter
    {
        ALWAYS_INLINE EdgeIter(BitmapAdjacencyGraph& graph, uint32_t nodeOrd)
        {
            TestAssert(nodeOrd < graph.m_numNodes);
            m_sourceNode = nodeOrd;
            m_curDestNode = 0;
            m_curBvWordPtr = graph.m_bv.m_data.get() + nodeOrd * graph.m_numWordsInEachBV;
            m_curBvWord = *m_curBvWordPtr;
            Advance(graph);
        }

        bool ALWAYS_INLINE IsValid(BitmapAdjacencyGraph& graph) const
        {
            return m_curDestNode < graph.m_numNodes;
        }

        bool ALWAYS_INLINE Advance(BitmapAdjacencyGraph& graph)
        {
            m_curDestNode &= ~static_cast<uint32_t>(63);

            while (unlikely(m_curBvWord == 0))
            {
                m_curDestNode += 64;
                if (m_curDestNode >= graph.m_numNodes)
                {
                    return false;
                }
                m_curBvWordPtr++;
                m_curBvWord = *m_curBvWordPtr;
            }

            uint32_t ctz = CountTrailingZeros(m_curBvWord);
            m_curBvWord ^= static_cast<uint64_t>(1) << ctz;

            m_curDestNode += ctz;
            TestAssert(m_curDestNode < graph.m_numNodes);
            __builtin_assume(m_curDestNode < graph.m_numNodes);
            return true;
        }

        uint32_t ALWAYS_INLINE GetSourceNode(BitmapAdjacencyGraph& /*graph*/) const
        {
            return m_sourceNode;
        }

        uint32_t ALWAYS_INLINE GetDestNode([[maybe_unused]] BitmapAdjacencyGraph& graph) const
        {
            TestAssert(IsValid(graph));
            return m_curDestNode;
        }

        uint32_t m_sourceNode;
        uint32_t m_curDestNode;
        uint64_t m_curBvWord;
        const uint64_t* m_curBvWordPtr;
    };

    void AllocateMemory(TempArenaAllocator& alloc, uint32_t numNodes, bool initializeToZero)
    {
        m_numNodes = numNodes;
        m_numWordsInEachBV = RoundUpToMultipleOf<64>(numNodes) / 64;
        m_bv.Reset(alloc, m_numWordsInEachBV * m_numNodes * 64);
        if (initializeToZero)
        {
            m_bv.Clear();
        }
    }

    BitVectorView GetEdgeBV(uint32_t nodeOrd)
    {
        TestAssert(nodeOrd < m_numNodes);
        return BitVectorView(m_bv.m_data.get() + nodeOrd * m_numWordsInEachBV, m_numNodes);
    }

    // All nodes must be reachable from 'entryNode'
    // Return numSCC, and sccInfo[i] will be populated the SCC ordinal for node i
    //
    uint32_t WARN_UNUSED ComputeSCCForCFG(uint32_t entryNode, uint32_t* sccInfo /*out*/)
    {
        return StronglyConnectedComponentsFinder<BitmapAdjacencyGraph, BitmapAdjacencyGraph::EdgeIter>::ComputeForCFG(
            this, m_numNodes, entryNode, sccInfo /*out*/);
    }

    // Similar to ComputeSCCForCFG, but works for any graph
    //
    uint32_t WARN_UNUSED ComputeSCC(uint32_t* sccInfo /*out*/)
    {
        return StronglyConnectedComponentsFinder<BitmapAdjacencyGraph, BitmapAdjacencyGraph::EdgeIter>::ComputeForGenericGraph(
            this, m_numNodes, sccInfo /*out*/);
    }

    uint32_t m_numNodes;
    uint32_t m_numWordsInEachBV;
    TempBitVector m_bv;
};

}   // namespace dfg
