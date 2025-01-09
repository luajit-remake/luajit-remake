#pragma once

#include "common_utils.h"
#include "heap_ptr_utils.h"
#include "temp_arena_allocator.h"

namespace dfg {

// Utility to find the strongly connected components of a graph
//
// It uses Pearce's non-recursive SCC algorithm, adapted from the Java implementation described in
// https://github.com/DavePearce/StronglyConnectedComponents/blob/master/src/PeaFindScc2.java
//
// EdgeIteratorImpl should implement the following APIs:
//
//     EdgeIteratorImpl(GraphContext& graph, uint32_t nodeOrd)
//         Construct an edge iterator to iterate the edges from nodeOrd.
//
//     uint32_t GetSourceNode(GraphContext& graph) const
//         Return the node that is used to construct this edge iterator.
//
//     bool IsValid(GraphContext& graph) const
//         Return true if the current edge is valid (i.e., we have not iterated past the last edge).
//
//     uint32_t GetDestNode(GraphContext& graph)
//         Return the destination node of the current edge. Will only be called if IsValid() is true.
//         This method may be called multiple times for the same edge, so the iterator implementation
//         should make this method O(1) (by caching the result, for example).
//
//     bool Advance(GraphContext& graph)
//         Advance to the next edge. Will only be called if IsValid() is true.
//         Return false if the iterator becomes invalid.
//
template<typename GraphContext, typename EdgeIteratorImpl>
struct StronglyConnectedComponentsFinder
{
    // 'result' should be an array of length n.
    //
    // Returns the number of SCCs.
    // Each result[i] will be filled with a value in [0, #SCCs), denoting the SCC that node i belongs to.
    //
    static uint32_t WARN_UNUSED ComputeForGenericGraph(GraphContext* graph, uint32_t numNodes, uint32_t* result /*out*/)
    {
        return StronglyConnectedComponentsFinder::RunImpl(graph, numNodes, 0 /*entryNode*/, false /*entryNodeOnly*/, result /*out*/);
    }

    // Similar to FindForGenericGraph, but with the promise that all nodes are reachable from entryNode
    //
    static uint32_t WARN_UNUSED ComputeForCFG(GraphContext* graph, uint32_t numNodes, uint32_t entryNode, uint32_t* result /*out*/)
    {
        return StronglyConnectedComponentsFinder::RunImpl(graph, numNodes, entryNode, true /*entryNodeOnly*/, result /*out*/);
    }

private:
    uint32_t ALWAYS_INLINE GetSourceNode(const EdgeIteratorImpl& iter)
    {
        return iter.GetSourceNode(*m_graph);
    }

    bool ALWAYS_INLINE IsValid(const EdgeIteratorImpl& iter)
    {
        return iter.IsValid(*m_graph);
    }

    uint32_t ALWAYS_INLINE GetDestNode(const EdgeIteratorImpl& iter)
    {
        return iter.GetDestNode(*m_graph);
    }

    bool ALWAYS_INLINE Advance(EdgeIteratorImpl& iter)
    {
        return iter.Advance(*m_graph);
    }

    static uint32_t NO_INLINE RunImpl(RestrictPtr<GraphContext> graph, uint32_t numNodes, uint32_t entryNode, bool entryNodeOnly, RestrictPtr<uint32_t> result /*out*/)
    {
        memset(result, 0, sizeof(uint32_t) * numNodes);

        StronglyConnectedComponentsFinder impl(graph, numNodes, result);
        TestAssert(entryNode < numNodes);
        while (true)
        {
            impl.FindSccFromNode(entryNode);
            if (entryNodeOnly)
            {
                break;
            }
            while (true)
            {
                entryNode++;
                if (entryNode == numNodes || impl.RIndexComposite(entryNode) == 0)
                {
                    break;
                }
            }
            if (entryNode == numNodes)
            {
                break;
            }
        }

        return impl.FinishComputation();
    }

    ALWAYS_INLINE StronglyConnectedComponentsFinder(RestrictPtr<GraphContext> graph, uint32_t numNodes, RestrictPtr<uint32_t> result)
        : m_alloc()
        , m_graph(graph)
        , m_rindexComposite(result)
        , m_index(2)
        , m_componentIndex(4 * numNodes - 1)
        , m_numNodes(numNodes)
    {
        TestAssert(numNodes < (1U << 30));
        TestAssert(m_rindexComposite != nullptr);
        static_assert(sizeof(EdgeIteratorImpl) >= 4, "please pad EdgeIteratorImpl to at least 4 bytes!");
        m_vfBase = reinterpret_cast<EdgeIteratorImpl*>(m_alloc.AllocateWithAlignment(std::max(alignof(EdgeIteratorImpl), alignof(uint32_t)),
                                                                                     sizeof(EdgeIteratorImpl) * (numNodes + 1)));
        m_vfTop = m_vfBase - 1;
        m_vbTop = reinterpret_cast<uint32_t*>(m_vfBase + numNodes);
        m_vbBase = m_vbTop;
    }

    void ALWAYS_INLINE FindSccFromNode(uint32_t nodeOrd)
    {
        TestAssert(IsVfEmpty() && IsVbEmpty());

visit_new_node:
        TestAssert(RIndexComposite(nodeOrd) == 0);
        TestAssert(m_index >= 2 && m_index % 2 == 0);
        PushVfTop(nodeOrd);
        RIndexComposite(nodeOrd) = m_index;
        m_index += 2;

        uint32_t neighborOrd;
        if (IsValid(VfTop()))
        {
            neighborOrd = GetDestNode(VfTop());
            if (RIndexComposite(neighborOrd) == 0)
            {
                nodeOrd = neighborOrd;
                goto visit_new_node;
            }
            else
            {
                goto continue_iterate_edges;
            }
        }
        else
        {
            goto finished_visiting_edges;
        }

continue_iterate_edges:
        TestAssert(nodeOrd == GetSourceNode(VfTop()));
        TestAssert(neighborOrd == GetDestNode(VfTop()));
        while (true)
        {
            TestAssert(RIndexComposite(neighborOrd) != 0);
            uint32_t rindex_w_1 = RIndexComposite(neighborOrd) | 1;
            if (rindex_w_1 < RIndexComposite(nodeOrd))
            {
                RIndexComposite(nodeOrd) = rindex_w_1;
            }
            if (!Advance(VfTop()))
            {
                goto finished_visiting_edges;
            }
            neighborOrd = GetDestNode(VfTop());
            if (RIndexComposite(neighborOrd) == 0)
            {
                nodeOrd = neighborOrd;
                goto visit_new_node;
            }
        }

finished_visiting_edges:
        TestAssert(nodeOrd == GetSourceNode(VfTop()));
        TestAssert(RIndexComposite(nodeOrd) != 0);
        TestAssert(!IsValid(VfTop()));
        PopVfTop();

        if (IsRoot(nodeOrd))
        {
            uint32_t rindex_nodeOrd_0 = RIndexComposite(nodeOrd);
            *m_vbBase = nodeOrd;
            TestAssert(m_componentIndex % 2 == 1);
            TestAssert(m_componentIndex > 2 * m_numNodes);
            while (true)
            {
                uint32_t w = VbTop();
                TestAssertImp(nodeOrd != w, !IsRoot(w));
                TestAssertImp(nodeOrd == w, IsVbEmpty());
                if (rindex_nodeOrd_0 >= RIndexComposite(w))
                {
                    break;
                }
                PopVbTop();
                RIndexComposite(w) = m_componentIndex;
            }
            RIndexComposite(nodeOrd) = m_componentIndex;
            m_componentIndex -= 2;
        }
        else
        {
            PushVb(nodeOrd);
        }

        if (unlikely(IsVfEmpty()))
        {
            TestAssert(IsVbEmpty());
            return;
        }

        TestAssert(IsValid(VfTop()));
        nodeOrd = GetSourceNode(VfTop());
        neighborOrd = GetDestNode(VfTop());
        goto continue_iterate_edges;
    }

    uint32_t ALWAYS_INLINE FinishComputation()
    {
        TestAssert(IsVfEmpty() && IsVbEmpty());
        TestAssert(2 * m_numNodes - 1 <= m_componentIndex && m_componentIndex < 4 * m_numNodes - 1);
        TestAssert(m_componentIndex % 2 == 1);
        uint32_t numNodes = m_numNodes;
        uint32_t numSCC = (4 * numNodes - m_componentIndex) / 2;
        TestAssert(1 <= numSCC && numSCC <= numNodes);
        for (size_t i = 0; i < numNodes; i++)
        {
            TestAssert(2 * numNodes < m_rindexComposite[i] && m_rindexComposite[i] <= 4 * numNodes - 1);
            TestAssert(m_rindexComposite[i] % 2 == 1);
            m_rindexComposite[i] = (4 * numNodes - m_rindexComposite[i]) / 2;
            TestAssert(m_rindexComposite[i] < numSCC);
        }
        return numSCC;
    }

    void ALWAYS_INLINE PushVfTop(uint32_t nodeOrd)
    {
        m_vfTop++;
        TestAssert(m_vfTop + 1 <= reinterpret_cast<EdgeIteratorImpl*>(m_vbTop));
        ConstructInPlace(m_vfTop, *m_graph, nodeOrd);
    }

    EdgeIteratorImpl& WARN_UNUSED ALWAYS_INLINE VfTop()
    {
        TestAssert(!IsVfEmpty());
        return *m_vfTop;
    }

    void ALWAYS_INLINE PopVfTop()
    {
        TestAssert(!IsVfEmpty());
        m_vfTop->~EdgeIteratorImpl();
        m_vfTop--;
    }

    bool ALWAYS_INLINE IsVfEmpty()
    {
        TestAssert(m_vfTop + 1 >= m_vfBase);
        return m_vfTop < m_vfBase;
    }

    void ALWAYS_INLINE PushVb(uint32_t value)
    {
        m_vbTop--;
        TestAssert(m_vfTop + 1 <= reinterpret_cast<EdgeIteratorImpl*>(m_vbTop));
        *m_vbTop = value;
    }

    // Note that we allow peeking top even if it is empty,
    // since the address will always be valid and the caller logic may set a sentry value there
    //
    uint32_t WARN_UNUSED ALWAYS_INLINE VbTop()
    {
        TestAssert(m_vbTop <= m_vbBase);
        return *m_vbTop;
    }

    void ALWAYS_INLINE PopVbTop()
    {
        TestAssert(!IsVbEmpty());
        m_vbTop++;
    }

    bool ALWAYS_INLINE IsVbEmpty()
    {
        TestAssert(m_vbTop <= m_vbBase);
        return m_vbTop == m_vbBase;
    }

    bool WARN_UNUSED ALWAYS_INLINE IsRoot(uint32_t nodeOrd)
    {
        TestAssert(nodeOrd < m_numNodes);
        return (m_rindexComposite[nodeOrd] & 1) == 0;
    }

    uint32_t& WARN_UNUSED ALWAYS_INLINE RIndexComposite(uint32_t nodeOrd)
    {
        TestAssert(nodeOrd < m_numNodes);
        return m_rindexComposite[nodeOrd];
    }

    TempArenaAllocator m_alloc;

    RestrictPtr<GraphContext> m_graph;

    // [m_vfBase, m_vfTop] are valid
    //
    EdgeIteratorImpl* m_vfBase;
    RestrictPtr<EdgeIteratorImpl> m_vfTop;

    // [m_vbTop, m_vbBase) are valid, but [m_vbBase] will be valid memory and can be used to hold a sentry value
    //
    uint32_t* m_vbTop;
    uint32_t* m_vbBase;

    // Bit 0 = 0 means isRoot. This is critical since we directly compare the composite value in many places.
    //
    RestrictPtr<uint32_t> m_rindexComposite;

    uint32_t m_index;
    uint32_t m_componentIndex;
    uint32_t m_numNodes;
};

}   // namespace dfg
