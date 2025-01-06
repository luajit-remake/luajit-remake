#include "typemask_overapprox_automata_generator.h"

namespace dast {

std::vector<TypemaskOverapproxAutomataGenerator::ItemTy> WARN_UNUSED TypemaskOverapproxAutomataGenerator::MakeClosure()
{
    std::unordered_set<TypeMaskTy> existMap;
    std::vector<ItemTy> r = m_items;

    // Sort items so we will generate the same automata regardless of the order of the items given to us
    //
    std::sort(r.begin(), r.end());

    for (ItemTy& item : r)
    {
        ReleaseAssert(!existMap.count(item.first));
        existMap.insert(item.first);
    }

    // Compute the bitwise-and-closure by propagating to fixpoint
    //
    {
        size_t lastN = 0;
        while (true)
        {
            if (r.size() > 500)
            {
                fprintf(stderr, "Too many (>500) elements in the bitwise-and-closure for TypeMaskOverapproxAutomata! Input masks:\n");
                for (ItemTy& item : m_items)
                {
                    fprintf(stderr, "%llu ", static_cast<unsigned long long>(item.first));
                }
                fprintf(stderr, "\n");
                abort();
            }

            std::vector<ItemTy> newItems;
            for (size_t i = 0; i < r.size(); i++)
            {
                // Skip cases where both i and j < lastN: these cases have been added before
                //
                size_t jstart = (i < lastN) ? lastN : 0;
                for (size_t j = jstart; j < r.size(); j++)
                {
                    TypeMaskTy mask = (r[i].first & r[j].first);
                    if (!existMap.count(mask))
                    {
                        existMap.insert(mask);
                        newItems.push_back(std::make_pair(mask, r[i].second));
                    }
                }
            }
            if (newItems.size() == 0)
            {
                break;
            }

            lastN = r.size();
            r.insert(r.end(), newItems.begin(), newItems.end());
        }
    }

    // Sort all the items: this is assumed by GenerateImpl
    //
    std::sort(r.begin(), r.end());

    // Sanity check result is correct
    //
    {
        // No duplicate elements should exist
        //
        std::unordered_set<TypeMaskTy> chk;
        for (ItemTy& item : r)
        {
            ReleaseAssert(!chk.count(item.first));
            chk.insert(item.first);
        }

        // Original elements should exist
        //
        for (ItemTy& item : m_items)
        {
            ReleaseAssert(chk.count(item.first));
        }

        // r should be closed under bitwise-and
        //
        for (ItemTy& v1 : r)
        {
            for (ItemTy& v2 : r)
            {
                ReleaseAssert(chk.count(v1.first & v2.first));
            }
        }
    }

    return r;
}

std::vector<uint8_t> WARN_UNUSED TypemaskOverapproxAutomataGenerator::GenerateImpl(bool forLeafOpted, size_t* automataMaxDepth /*out*/)
{
    std::vector<ItemTy> items = MakeClosure();

    // If the items does not have a tTop term, add tTop with failure value (-1) as a catch-all
    //
    TypeMaskTy topMask = x_typeMaskFor<tTop>;
    ReleaseAssertImp(items.size() > 0, items.back().first <= topMask);
    if (items.size() == 0 || items.back().first < topMask)
    {
        items.push_back(std::make_pair(topMask, static_cast<uint16_t>(-1)));
    }

    const size_t N = x_numUsefulBitsInTypeMask;

    // Sanity check that 'items' are sorted and distinct
    //
    for (ItemTy& item : items)
    {
        ReleaseAssert(item.first < (static_cast<uint64_t>(1) << N));
    }
    for (size_t i = 0; i + 1 < items.size(); i++)
    {
        ReleaseAssert(items[i].first < items[i + 1].first);
    }
    ReleaseAssert(items.size() > 0 && items.back().first == topMask);

    // If forLeafOpted is true, we need to pick out the bottom item from the item list
    //
    ItemTy bottomItemForLeafOpted;
    if (forLeafOpted)
    {
        bottomItemForLeafOpted = items[0];
        items.erase(items.begin());
    }

    // Now we've finalized the item list
    //
    const size_t M = items.size();

    auto findMinOverapprox = [&](TypeMaskTy mask) -> size_t
    {
        for (size_t i = 0; i < M; i++)
        {
            if ((items[i].first & mask) == mask)
            {
                // Note that since items are sorted by mask value, the first encounter is always a
                // best choice (and the unique best choice if 'items' are closed under bitwise-and)
                //
                return i;
            }
        }
        ReleaseAssert(false);
    };

    // Compute the transfer function T: [M] * [N] -> [M] (see drt/dfg_typemask_overapprox_automata.h)
    //
    std::vector<std::vector<size_t>> T;
    T.resize(M);
    for (size_t i = 0; i < M; i++) { T[i].resize(N); }

    for (size_t i = 0; i < M; i++)
    {
        for (size_t j = 0; j < N; j++)
        {
            if ((items[i].first & (static_cast<TypeMaskTy>(1) << j)) == 0)
            {
                T[i][j] = findMinOverapprox(items[i].first | (static_cast<TypeMaskTy>(1) << j));
                ReleaseAssert(T[i][j] < M);
            }
            else
            {
                T[i][j] = static_cast<size_t>(-1);
            }
        }
    }

    struct EdgeInfo
    {
        TypeMask m_edgeMask;
        size_t m_dest;
    };

    std::vector<std::vector<EdgeInfo>> edgeMap;
    edgeMap.resize(M);
    for (size_t itemOrd = 0; itemOrd < M; itemOrd++)
    {
        std::map<size_t /*itemOrd*/, TypeMaskTy> edges;
        for (size_t j = 0; j < N; j++)
        {
            if (T[itemOrd][j] != static_cast<size_t>(-1))
            {
                ReleaseAssert(T[itemOrd][j] < M);
                edges[T[itemOrd][j]] |= static_cast<TypeMaskTy>(1) << j;
            }
        }

        for (auto& it : edges)
        {
            edgeMap[itemOrd].push_back({
                .m_edgeMask = it.second,
                .m_dest = it.first
            });
        }
    }

    auto traverseWithMasks = [&](size_t itemOrd, const std::vector<TypeMask>& masks) WARN_UNUSED -> size_t /*itemOrd*/
    {
        while (true)
        {
            ReleaseAssert(itemOrd < M);
            for (EdgeInfo& edge : edgeMap[itemOrd])
            {
                for (TypeMask mask : masks)
                {
                    ReleaseAssert(edge.m_edgeMask.SupersetOf(mask) || edge.m_edgeMask.DisjointFrom(mask));
                }
            }

            size_t next = static_cast<size_t>(-1);
            for (EdgeInfo& edge : edgeMap[itemOrd])
            {
                bool found = false;
                for (TypeMask mask : masks)
                {
                    if (edge.m_edgeMask.SupersetOf(mask))
                    {
                        found = true;
                        break;
                    }
                }
                if (found)
                {
                    next = edge.m_dest;
                    break;
                }
            }
            if (next == static_cast<size_t>(-1))
            {
                break;
            }
            itemOrd = next;
        }

        for (TypeMask mask : masks)
        {
            ReleaseAssert(mask.SubsetOf(items[itemOrd].first));
        }
        return itemOrd;
    };

    constexpr size_t x_maxTests = dfg::TypeMaskOverapproxAutomataNode::x_maxTestMasks;

    using DfgNodeTy = dfg::TypeMaskOverapproxAutomataNode;

    // Nodes in the state machine
    // Use unique_ptr to avoid iterator invalidation issues
    //
    struct NodeInfo
    {
        NodeInfo(ItemTy item, size_t numTestMasks)
            : m_item(item)
        {
            ReleaseAssert(numTestMasks <= x_maxTests);
            m_testMasks.resize(numTestMasks, x_typeMaskFor<tBottom> /*value*/);
            m_dests.resize(1U << numTestMasks, nullptr /*value*/);
        }

        void SetTestMask(size_t idx, TypeMask value)
        {
            ReleaseAssert(idx < m_testMasks.size());
            m_testMasks[idx] = value;
        }

        void SetDest(size_t idx, NodeInfo* value)
        {
            ReleaseAssert(idx < m_dests.size() && m_dests[idx] == nullptr);
            m_dests[idx] = value;
        }

        bool IsTerminalNode()
        {
            bool res = (m_testMasks.size() == 0 && m_dests[0] == nullptr);
            ReleaseAssertIff(res, m_item.first == x_typeMaskFor<tTop>);
            return res;
        }

        size_t GetByteSize()
        {
            size_t res = sizeof(dfg::TypeMaskOverapproxAutomataNodeCommon);
            if (IsTerminalNode())
            {
                return res;
            }
            res += sizeof(TypeMaskTy) * x_maxTests;
            res += sizeof(uint16_t) * m_dests.size();
            return res;
        }

        void Populate(DfgNodeTy* node /*out*/, const std::function<size_t(NodeInfo*)>& nodeInfoToOffsetFn)
        {
            ReleaseAssert((x_typeMaskFor<tTop> & m_item.first) == m_item.first);
            node->m_clearMask = x_typeMaskFor<tTop> ^ m_item.first;
            node->m_answer = m_item.second;
            if (IsTerminalNode())
            {
                return;
            }
            for (size_t i = 0; i < x_maxTests; i++)
            {
                if (i < m_testMasks.size())
                {
                    node->m_testMasks[i] = m_testMasks[i].m_mask;
                }
                else
                {
                    node->m_testMasks[i] = 0;
                }
            }
            ReleaseAssert(m_dests.size() == (static_cast<uint64_t>(1) << m_testMasks.size()));
            for (size_t idx = 0; idx < m_dests.size(); idx++)
            {
                if (m_dests[idx] == nullptr)
                {
                    node->m_dests[idx] = static_cast<uint16_t>(-1);
                }
                else
                {
                    size_t curNodeOffset = nodeInfoToOffsetFn(this);
                    size_t dstNodeOffset = nodeInfoToOffsetFn(m_dests[idx]);
                    ReleaseAssert(dstNodeOffset > curNodeOffset);
                    size_t offset = dstNodeOffset - curNodeOffset;
                    ReleaseAssert(offset <= 65534 && "DFA too large");
                    node->m_dests[idx] = SafeIntegerCast<uint16_t>(offset);
                }
            }
        }

        ItemTy m_item;
        std::vector<TypeMask> m_testMasks;
        std::vector<NodeInfo*> m_dests;
    };

    std::vector<std::unique_ptr<NodeInfo>> dfaNodes;

    // Map from item ordinal (index in 'items') to node ordinal in the state machine (index in 'dfaNodes')
    //
    std::vector<size_t> itemToNodeOrdMap;

    std::vector<bool> itemReachable;
    itemReachable.resize(M, false /*value*/);

    std::vector<TypeMask> knownZeroBitsForItem;
    knownZeroBitsForItem.resize(M, x_typeMaskFor<tTop> /*value*/);

    if (forLeafOpted)
    {
        for (size_t i = 0; i < N; i++)
        {
            if ((bottomItemForLeafOpted.first & (static_cast<TypeMaskTy>(1) << i)) == 0)
            {
                size_t ord = findMinOverapprox(bottomItemForLeafOpted.first | (static_cast<TypeMaskTy>(1) << i));
                ReleaseAssert(ord < M);
                itemReachable[ord] = true;
                knownZeroBitsForItem[ord] = x_typeMaskFor<tBottom>;
            }
        }
    }
    else
    {
        ReleaseAssert(M > 0);
        itemReachable[0] = true;
        knownZeroBitsForItem[0] = x_typeMaskFor<tBottom>;
    }

    // A pending edge from dfaNodeOrd to itemToNodeOrdMap[itemOrd]
    //
    struct PendingEdge
    {
        size_t m_dfaNodeOrd;
        size_t m_itemOrd;
        size_t m_edgeOrd;
    };

    std::vector<PendingEdge> pendingEdges;
    auto addPendingEdge = [&](size_t dfaNodeOrd, size_t itemOrd, size_t edgeOrd)
    {
        ReleaseAssert(dfaNodeOrd < dfaNodes.size());
        ReleaseAssert(itemOrd < M);
        pendingEdges.push_back({
            .m_dfaNodeOrd = dfaNodeOrd,
            .m_itemOrd = itemOrd,
            .m_edgeOrd = edgeOrd
        });
    };

    auto addEdgeNow = [&](size_t dfaNodeOrd, size_t destDfaNodeOrd, size_t edgeOrd)
    {
        ReleaseAssert(dfaNodeOrd < dfaNodes.size());
        ReleaseAssert(destDfaNodeOrd < dfaNodes.size());
        ReleaseAssert(destDfaNodeOrd > dfaNodeOrd);
        NodeInfo* node = dfaNodes[dfaNodeOrd].get();
        NodeInfo* targetNode = dfaNodes[destDfaNodeOrd].get();
        node->SetDest(edgeOrd, targetNode);
    };

    for (size_t itemOrd = 0; itemOrd < M; itemOrd++)
    {
        if (!itemReachable[itemOrd])
        {
            itemToNodeOrdMap.push_back(static_cast<size_t>(-1));
            continue;
        }

        std::function<size_t(std::span<EdgeInfo>, TypeMask)> handleEdges = [&](std::span<EdgeInfo> edges, TypeMask knownZeroMask) WARN_UNUSED -> size_t /*dfaNodeOrd*/
        {
            size_t numTests = std::min(edges.size(), x_maxTests);

            size_t nodeOrd = dfaNodes.size();
            dfaNodes.push_back(std::make_unique<NodeInfo>(items[itemOrd], numTests));
            NodeInfo* node = dfaNodes.back().get();

            if (numTests == 0)
            {
                // It's possible that we are certain that we reached a terminal node due to knownZeroMask
                // Set the item mask to tTop to make assertions happy and for sanity
                //
                node->m_item.first = x_typeMaskFor<tTop>;
            }

            for (size_t i = 0; i < numTests; i++)
            {
                node->SetTestMask(i, edges[i].m_edgeMask);
            }

            uint64_t count = static_cast<uint64_t>(1) << numTests;
            for (uint64_t mask = 1; mask < count; mask++)
            {
                TypeMask newKnownZeroMask = knownZeroMask;
                std::vector<TypeMask> selectedMasks;
                for (size_t i = 0; i < numTests; i++)
                {
                    if (mask & (static_cast<uint64_t>(1) << i))
                    {
                        selectedMasks.push_back(node->m_testMasks[i]);
                    }
                    else
                    {
                        newKnownZeroMask = newKnownZeroMask.Cup(node->m_testMasks[i]);
                    }
                }

                size_t nextItemOrd = traverseWithMasks(itemOrd, selectedMasks);
                addPendingEdge(nodeOrd, nextItemOrd, mask);

                ReleaseAssert(nextItemOrd < M);
                itemReachable[nextItemOrd] = true;
                knownZeroBitsForItem[nextItemOrd] = knownZeroBitsForItem[nextItemOrd].Cap(newKnownZeroMask);
            }

            if (edges.size() > numTests)
            {
                TypeMask newKnownZeroMask = knownZeroMask;
                for (size_t i = 0; i < numTests; i++)
                {
                    newKnownZeroMask = newKnownZeroMask.Cup(node->m_testMasks[i]);
                }
                size_t nextNodeOrd = handleEdges({ edges.data() + numTests, edges.size() - numTests }, newKnownZeroMask);
                addEdgeNow(nodeOrd, nextNodeOrd, 0 /*edgeOrd*/);
            }

            return nodeOrd;
        };

        TypeMask knownZeroMask = knownZeroBitsForItem[itemOrd];

        std::vector<EdgeInfo> edges;
        for (EdgeInfo& edge : edgeMap[itemOrd])
        {
            if (edge.m_edgeMask.SubsetOf(knownZeroMask))
            {
                continue;
            }
            edges.push_back(edge);
        }

        itemToNodeOrdMap.push_back(handleEdges({ edges.begin(), edges.end() }, knownZeroMask));
    }

    ReleaseAssert(itemToNodeOrdMap.size() == M);

    // Patch all pending edges
    //
    for (PendingEdge& edge : pendingEdges)
    {
        ReleaseAssert(edge.m_itemOrd < M);
        size_t destNodeOrd = itemToNodeOrdMap[edge.m_itemOrd];
        ReleaseAssert(destNodeOrd != static_cast<size_t>(-1));
        addEdgeNow(edge.m_dfaNodeOrd, destNodeOrd, edge.m_edgeOrd);
    }

    // Sanity check all DFA nodes
    //
    for (auto& node : dfaNodes)
    {
        ReleaseAssert(TypeMask(node->m_item.first).SubsetOf(x_typeMaskFor<tTop>));
        ReleaseAssert((static_cast<uint64_t>(1) << node->m_testMasks.size()) == node->m_dests.size());
        for (size_t idx = 1; idx < node->m_dests.size(); idx++)
        {
            ReleaseAssert(node->m_dests[idx] != nullptr);
        }
    }

    // Assign offset for each DFA node
    //
    size_t curOffset = 0;
    if (forLeafOpted)
    {
        curOffset = sizeof(TypeMaskTy) + 2 + 2 * N;
    }
    std::vector<size_t> offsetForDfaNode;
    for (auto& node : dfaNodes)
    {
        offsetForDfaNode.push_back(curOffset);
        curOffset += node->GetByteSize();
    }

    // Generate the final DFA
    //
    std::vector<uint8_t> rs;
    if (forLeafOpted)
    {
        size_t arrOffset = sizeof(TypeMaskTy) + 2;
        rs.resize(arrOffset, 0 /*value*/);

        ReleaseAssert(bottomItemForLeafOpted.first <= topMask);
        UnalignedStore<TypeMaskTy>(rs.data(), topMask ^ bottomItemForLeafOpted.first);
        UnalignedStore<uint16_t>(rs.data() + sizeof(TypeMaskTy), bottomItemForLeafOpted.second);

        if (items.size() == 0)
        {
            ReleaseAssert(bottomItemForLeafOpted.first == topMask);
            return rs;
        }

        size_t hdrSize = arrOffset + N * 2;
        rs.resize(hdrSize, 0 /*value*/);

        for (size_t i = 0; i < N; i++)
        {
            if (bottomItemForLeafOpted.first & (static_cast<TypeMaskTy>(1) << i))
            {
                UnalignedStore<uint16_t>(rs.data() + arrOffset + i * 2, 65535);
            }
            else
            {
                size_t destItemOrd = findMinOverapprox(bottomItemForLeafOpted.first | (static_cast<TypeMaskTy>(1) << i));
                ReleaseAssert(destItemOrd < itemToNodeOrdMap.size());
                size_t destNodeOrd = itemToNodeOrdMap[destItemOrd];
                ReleaseAssert(destNodeOrd != static_cast<size_t>(-1));
                ReleaseAssert(destNodeOrd < dfaNodes.size());
                size_t value = offsetForDfaNode[destNodeOrd];
                ReleaseAssert(value <= 65534 && "DFA too large");
                UnalignedStore<uint16_t>(rs.data() + arrOffset + i * 2, SafeIntegerCast<uint16_t>(value));
            }
        }
    }

    ReleaseAssert(curOffset >= rs.size());
    rs.resize(curOffset);

    std::unordered_map<NodeInfo*, size_t> nodeToOrdMap;
    for (size_t i = 0; i < dfaNodes.size(); i++)
    {
        NodeInfo* nodeInfo = dfaNodes[i].get();
        ReleaseAssert(!nodeToOrdMap.count(nodeInfo));
        nodeToOrdMap[nodeInfo] = i;
    }

    auto nodeInfoToOffsetFn = [&](NodeInfo* nodeInfo) -> size_t
    {
        ReleaseAssert(nodeToOrdMap.count(nodeInfo));
        size_t nodeOrd = nodeToOrdMap[nodeInfo];
        ReleaseAssert(nodeOrd < offsetForDfaNode.size());
        return offsetForDfaNode[nodeOrd];
    };

    for (size_t i = 0; i < dfaNodes.size(); i++)
    {
        NodeInfo* nodeInfo = dfaNodes[i].get();
        ReleaseAssert(offsetForDfaNode[i] + nodeInfo->GetByteSize() <= rs.size());
        DfgNodeTy* node = std::launder(reinterpret_cast<DfgNodeTy*>(rs.data() + offsetForDfaNode[i]));
        nodeInfo->Populate(node, nodeInfoToOffsetFn);
    }

    size_t maxDfaDepth = 0;
    std::vector<size_t> maxDepthAtNode;
    maxDepthAtNode.resize(dfaNodes.size());
    for (size_t idx = dfaNodes.size(); idx--;)
    {
        maxDepthAtNode[idx] = 0;
        NodeInfo* node = dfaNodes[idx].get();
        if (!node->IsTerminalNode())
        {
            for (NodeInfo* next : node->m_dests)
            {
                if (next != nullptr)
                {
                    ReleaseAssert(nodeToOrdMap.count(next));
                    maxDepthAtNode[idx] = std::max(maxDepthAtNode[idx], maxDepthAtNode[nodeToOrdMap[next]] + 1);
                }
            }
        }
        maxDfaDepth = std::max(maxDfaDepth, maxDepthAtNode[idx]);
    }

    if (automataMaxDepth != nullptr)
    {
        *automataMaxDepth = maxDfaDepth;
    }

    return rs;
}

}   // namespace dast
