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
                for (ItemTy& item : r)
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

std::vector<uint8_t> WARN_UNUSED TypemaskOverapproxAutomataGenerator::GenerateImpl(bool forLeafOpted)
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

    // Nodes in the state machine
    // Use unique_ptr to avoid iterator invalidation issues
    //
    using NodeTy = dfg::TypeMaskOverapproxAutomataNode;
    std::vector<std::unique_ptr<NodeTy>> dfaNodes;

    // Map from item ordinal (index in 'items') to node ordinal in the state machine (index in 'dfaNodes')
    //
    std::vector<size_t> itemToNodeOrdMap;

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
        ReleaseAssert(edgeOrd < 2);
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
        size_t offset = (destDfaNodeOrd - dfaNodeOrd) * sizeof(NodeTy);
        ReleaseAssert(offset <= 65534 && "DFA contains too many nodes!");
        NodeTy* node = dfaNodes[dfaNodeOrd].get();
        if (edgeOrd == 0)
        {
            ReleaseAssert(node->m_dest0Offset == static_cast<uint16_t>(-1));
            node->m_dest0Offset = SafeIntegerCast<uint16_t>(offset);
        }
        else
        {
            ReleaseAssert(edgeOrd == 1);
            ReleaseAssert(node->m_dest1Offset == static_cast<uint16_t>(-1));
            node->m_dest1Offset = SafeIntegerCast<uint16_t>(offset);
        }
    };

    auto initNode = [&](ItemTy item) WARN_UNUSED -> std::unique_ptr<NodeTy>
    {
        TypeMaskTy mask = item.first;
        ReleaseAssert((mask & topMask) == mask);
        std::unique_ptr<NodeTy> res(new NodeTy());
        *res = {
            .m_clearMask = topMask ^ mask,
            .m_testMask = 0,    // not initialized
            .m_answer = item.second,
            .m_dest0Offset = static_cast<uint16_t>(-1),  // invalid
            .m_dest1Offset = static_cast<uint16_t>(-1),  // invalid
        };
        return res;
    };

    for (size_t itemOrd = 0; itemOrd < M; itemOrd++)
    {
        struct EdgeTy
        {
            TypeMaskTy m_edgeMask;
            size_t m_itemOrd;
        };

        std::function<size_t(std::span<EdgeTy>)> handleEdges = [&](std::span<EdgeTy> edges) WARN_UNUSED -> size_t /*dfaNodeOrd*/
        {
            size_t nodeOrd = dfaNodes.size();
            dfaNodes.push_back(initNode(items[itemOrd]));
            NodeTy* node = dfaNodes.back().get();

            if (edges.size() == 0)
            {
                ReleaseAssert(node->m_clearMask == 0);
                node->m_testMask = 0;
                node->m_dest0Offset = 0;
                node->m_dest1Offset = 0;
            }
            else if (edges.size() == 1)
            {
                node->m_testMask = 0;
                addPendingEdge(nodeOrd, edges[0].m_itemOrd, 0 /*edgeOrd*/);
                addPendingEdge(nodeOrd, edges[0].m_itemOrd, 1 /*edgeOrd*/);
            }
            else if (edges.size() == 2)
            {
                node->m_testMask = edges[0].m_edgeMask;
                addPendingEdge(nodeOrd, edges[0].m_itemOrd, 0 /*edgeOrd*/);
                addPendingEdge(nodeOrd, edges[1].m_itemOrd, 1 /*edgeOrd*/);
            }
            else if (edges.size() == 3)
            {
                node->m_testMask = edges[0].m_edgeMask;
                addPendingEdge(nodeOrd, edges[0].m_itemOrd, 0 /*edgeOrd*/);
                size_t edge1NodeOrd = handleEdges({ edges.data() + 1 /*start*/, 2 /*num*/ });
                addEdgeNow(nodeOrd, edge1NodeOrd, 1 /*edgeOrd*/);
            }
            else
            {
                ReleaseAssert(edges.size() >= 4);
                size_t nleft = edges.size() / 2;
                TypeMaskTy leftMask = 0;
                for (size_t i = 0; i < nleft; i++)
                {
                    leftMask |= edges[i].m_edgeMask;
                }
                node->m_testMask = leftMask;
                size_t edge0NodeOrd = handleEdges({ edges.data(), nleft });
                addEdgeNow(nodeOrd, edge0NodeOrd, 0 /*edgeOrd*/);
                size_t edge1NodeOrd = handleEdges({ edges.data() + nleft, edges.size() - nleft });
                addEdgeNow(nodeOrd, edge1NodeOrd, 1 /*edgeOrd*/);
            }

            return nodeOrd;
        };

        std::map<size_t /*itemOrd*/, TypeMaskTy> edgeMap;
        for (size_t j = 0; j < N; j++)
        {
            if (T[itemOrd][j] != static_cast<size_t>(-1))
            {
                ReleaseAssert(T[itemOrd][j] < M);
                edgeMap[T[itemOrd][j]] |= static_cast<TypeMaskTy>(1) << j;
            }
        }

        std::vector<EdgeTy> edges;
        for (auto& it : edgeMap)
        {
            edges.push_back({
                .m_edgeMask = it.second,
                .m_itemOrd = it.first
            });
        }

        itemToNodeOrdMap.push_back(handleEdges({ edges.begin(), edges.end() }));
    }

    ReleaseAssert(itemToNodeOrdMap.size() == M);

    // Patch all pending edges
    //
    for (PendingEdge& edge : pendingEdges)
    {
        ReleaseAssert(edge.m_itemOrd < M);
        size_t destNodeOrd = itemToNodeOrdMap[edge.m_itemOrd];
        addEdgeNow(edge.m_dfaNodeOrd, destNodeOrd, edge.m_edgeOrd);
    }

    // Check that all edges have been populated
    //
    for (auto& node : dfaNodes)
    {
        ReleaseAssert(node->m_dest0Offset != static_cast<uint16_t>(-1));
        ReleaseAssert(node->m_dest1Offset != static_cast<uint16_t>(-1));
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
                ReleaseAssert(destNodeOrd < dfaNodes.size());
                size_t value = hdrSize + sizeof(NodeTy) * destNodeOrd;
                ReleaseAssert(value <= 65534 && "DFA too large");
                UnalignedStore<uint16_t>(rs.data() + arrOffset + i * 2, SafeIntegerCast<uint16_t>(value));
            }
        }
    }

    size_t hdrSize = rs.size();
    rs.resize(hdrSize + sizeof(NodeTy) * dfaNodes.size(), 0 /*value*/);

    for (size_t i = 0; i < dfaNodes.size(); i++)
    {
        NodeTy* node = std::launder(reinterpret_cast<NodeTy*>(rs.data() + hdrSize + sizeof(NodeTy) * i));
        *node = *dfaNodes[i].get();

        auto checkOffsetValid = [&](size_t offset)
        {
            ReleaseAssert(offset % sizeof(NodeTy) == 0);
            ReleaseAssert(SafeIntegerCast<size_t>(reinterpret_cast<uint8_t*>(node) - rs.data()) + offset + sizeof(NodeTy) <= rs.size());
        };
        checkOffsetValid(node->m_dest0Offset);
        checkOffsetValid(node->m_dest1Offset);
    }
    return rs;
}

}   // namespace dast
