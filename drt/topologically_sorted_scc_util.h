#pragma once

#include "strongly_connected_components_util.h"
#include "bit_vector_utils.h"
#include "bitmap_adjacency_graph.h"

namespace dfg {

struct TopologicallySortedSccInfo
{
    struct SccInfo
    {
        uint32_t m_offset;
        uint32_t m_size;
    };

    // m_sccList lists the SCCs in topologically sorted order (i.e., SCC with no in-bound edges show up first)
    // The nodes in the SCC is given in m_nodeList[m_offset, m_offset + m_size)
    //
    uint32_t m_numSCCs;
    uint32_t m_numNodes;
    SccInfo* m_sccList;
    uint32_t* m_nodeList;
};

template<typename GraphContext, typename EdgeIteratorImpl>
struct TopologicallySortedCondensationGraphBuilder
{
    static TopologicallySortedSccInfo WARN_UNUSED Compute(TempArenaAllocator& alloc,
                                                          GraphContext* graph,
                                                          uint32_t numNodes,
                                                          uint32_t numSCCs,
                                                          uint32_t* sccInfo,
                                                          bool neverUseBitmap = false)
    {
        if (numSCCs <= 256 && !neverUseBitmap)
        {
            return ComputeImpl<true /*useBitMap*/>(alloc, graph, numNodes, numSCCs, sccInfo);
        }
        else
        {
            return ComputeImpl<false /*useBitMap*/>(alloc, graph, numNodes, numSCCs, sccInfo);
        }
    }

private:
    template<bool useBitMap>
    static TopologicallySortedSccInfo WARN_UNUSED ComputeImpl(TempArenaAllocator& alloc,
                                                              GraphContext* graph,
                                                              uint32_t numNodes,
                                                              uint32_t numSCCs,
                                                              uint32_t* sccInfo)
    {
        TopologicallySortedSccInfo r;
        r.m_numNodes = numNodes;
        r.m_numSCCs = numSCCs;
        r.m_sccList = alloc.AllocateArray<TopologicallySortedSccInfo::SccInfo>(numSCCs);
        r.m_nodeList = alloc.AllocateArray<uint32_t>(numNodes);

        if (numSCCs == 0)
        {
            TestAssert(numNodes == 0);
            return r;
        }

        TempArenaAllocator::Mark mark = alloc.TakeMark();
        Auto(alloc.ResetToMark(mark));

        BitmapAdjacencyGraph cdg;
        if (useBitMap)
        {
            cdg.AllocateMemory(alloc, numSCCs, true /*initializeToZero*/);
        }

        TopologicallySortedSccInfo::SccInfo* sccMap = alloc.AllocateArray<TopologicallySortedSccInfo::SccInfo>(numSCCs);
        memset(sccMap, 0, sizeof(TopologicallySortedSccInfo::SccInfo) * numSCCs);

        for (size_t i = 0; i < numNodes; i++)
        {
            TestAssert(sccInfo[i] < numSCCs);
            sccMap[sccInfo[i]].m_size++;
        }

        TestAssert(numSCCs > 0);
        sccMap[0].m_offset = 0;
        for (size_t i = 1; i < numSCCs; i++)
        {
            sccMap[i].m_offset = sccMap[i - 1].m_offset + sccMap[i - 1].m_size;
        }

        uint32_t* sccList = alloc.AllocateArray<uint32_t>(numNodes);
        for (uint32_t i = 0; i < numNodes; i++)
        {
            size_t idx = sccInfo[i];
            TestAssert(sccMap[idx].m_offset < numNodes);
            sccList[sccMap[idx].m_offset] = i;
            sccMap[idx].m_offset++;
        }

#ifdef TESTBUILD
        TestAssert(sccMap[0].m_offset == sccMap[0].m_size);
        for (size_t i = 1; i < numSCCs; i++) { TestAssert(sccMap[i].m_offset == sccMap[i - 1].m_offset + sccMap[i].m_size); }
        TestAssert(sccMap[numSCCs - 1].m_offset == numNodes);
#endif

        for (size_t i = 0; i < numSCCs; i++)
        {
            sccMap[i].m_offset -= sccMap[i].m_size;
        }

        uint32_t* inDeg = alloc.AllocateArray<uint32_t>(numSCCs, 0U /*value*/);

        for (uint32_t nodeOrd = 0; nodeOrd < numNodes; nodeOrd++)
        {
            uint32_t cdgSrcOrd = sccInfo[nodeOrd];
            TestAssert(cdgSrcOrd < numSCCs);

            BitVectorView bv(nullptr, 0);
            if (useBitMap)
            {
                bv = cdg.GetEdgeBV(cdgSrcOrd);
            }

            EdgeIteratorImpl edgeIter(*graph, nodeOrd);
            if (edgeIter.IsValid(*graph))
            {
                while (true)
                {
                    TestAssert(edgeIter.IsValid(*graph));
                    uint32_t dstNodeOrd = edgeIter.GetDestNode(*graph);
                    TestAssert(dstNodeOrd < numNodes);
                    uint32_t cdgDstOrd = sccInfo[dstNodeOrd];
                    TestAssert(cdgDstOrd < numSCCs);

                    if (cdgSrcOrd != cdgDstOrd)
                    {
                        if (useBitMap)
                        {
                            if (!bv.IsSet(cdgDstOrd))
                            {
                                bv.SetBit(cdgDstOrd);
                                inDeg[cdgDstOrd]++;
                            }
                        }
                        else
                        {
                            inDeg[cdgDstOrd]++;
                        }
                    }

                    if (!edgeIter.Advance(*graph))
                    {
                        break;
                    }
                }
            }
        }

        TempVector<uint32_t> workList(alloc);
        workList.reserve(numSCCs);
        for (uint32_t sccOrd = 0; sccOrd < numSCCs; sccOrd++)
        {
            if (inDeg[sccOrd] == 0)
            {
                workList.push_back(sccOrd);
            }
        }

        uint32_t* sccOrder = alloc.AllocateArray<uint32_t>(numSCCs);
        {
            size_t curIdx = 0;
            while (!workList.empty())
            {
                // Invariant: for *every* node in workList, all predecessors of the node have been written to sccOrder
                // So any node in the workList can be written as the next value to sccOrder.
                //
                uint32_t sccOrd = workList.back();
                workList.pop_back();

                TestAssert(inDeg[sccOrd] == 0);
                TestAssert(curIdx < numSCCs);
                sccOrder[curIdx] = sccOrd;
                curIdx++;

                if (useBitMap)
                {
                    BitVectorView bv = cdg.GetEdgeBV(sccOrd);
                    bv.ForEachSetBit(
                        [&](size_t dstSccOrd) ALWAYS_INLINE
                        {
                            TestAssert(dstSccOrd < numSCCs);
                            TestAssert(inDeg[dstSccOrd] > 0);
                            inDeg[dstSccOrd]--;
                            if (inDeg[dstSccOrd] == 0)
                            {
                                workList.push_back(static_cast<uint32_t>(dstSccOrd));
                            }
                        });
                }
                else
                {
                    size_t offsetStart = sccMap[sccOrd].m_offset;
                    size_t offsetEnd = sccMap[sccOrd].m_offset + sccMap[sccOrd].m_size;
                    for (size_t idx = offsetStart; idx < offsetEnd; idx++)
                    {
                        uint32_t nodeOrd = sccList[idx];
                        EdgeIteratorImpl edgeIter(*graph, nodeOrd);
                        if (edgeIter.IsValid(*graph))
                        {
                            while (true)
                            {
                                TestAssert(edgeIter.IsValid(*graph));
                                uint32_t dstNodeOrd = edgeIter.GetDestNode(*graph);
                                TestAssert(dstNodeOrd < numNodes);
                                uint32_t cdgDstOrd = sccInfo[dstNodeOrd];
                                TestAssert(cdgDstOrd < numSCCs);

                                if (sccOrd != cdgDstOrd)
                                {
                                    TestAssert(inDeg[cdgDstOrd] > 0);
                                    inDeg[cdgDstOrd]--;
                                    if (inDeg[cdgDstOrd] == 0)
                                    {
                                        workList.push_back(cdgDstOrd);
                                    }
                                }

                                if (!edgeIter.Advance(*graph))
                                {
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            TestAssert(curIdx == numSCCs);
        }

#ifdef TESTBUILD
        for (size_t i = 0; i < numSCCs; i++) { TestAssert(inDeg[i] == 0); }
#endif

        {
            uint32_t curIdx = 0;
            for (size_t i = 0; i < numSCCs; i++)
            {
                uint32_t sccSize = sccMap[sccOrder[i]].m_size;
                r.m_sccList[i].m_offset = curIdx;
                r.m_sccList[i].m_size = sccSize;
                size_t offset = sccMap[sccOrder[i]].m_offset;
                memcpy(r.m_nodeList + curIdx, sccList + offset, sizeof(uint32_t) * sccSize);
                curIdx += sccSize;
            }
            TestAssert(curIdx == numNodes);
        }

        // Confirm that we computed everything correctly
        //
#ifdef TESTBUILD
        uint32_t* loc = alloc.AllocateArray<uint32_t>(numNodes, static_cast<uint32_t>(-1));
        for (uint32_t i = 0; i < numNodes; i++)
        {
            TestAssert(r.m_nodeList[i] < numNodes);
            TestAssert(loc[r.m_nodeList[i]] == static_cast<uint32_t>(-1));
            loc[r.m_nodeList[i]] = i;
        }

        size_t curIdx = 0;
        for (uint32_t i = 0; i < numSCCs; i++)
        {
            TestAssert(r.m_sccList[i].m_offset == curIdx);
            for (size_t j = curIdx + 1; j < curIdx + r.m_sccList[i].m_size; j++)
            {
                TestAssert(j < numNodes);
                TestAssert(sccInfo[r.m_nodeList[curIdx]] == sccInfo[r.m_nodeList[j]]);
            }
            curIdx += r.m_sccList[i].m_size;
        }
        TestAssert(curIdx == numNodes);

        for (uint32_t nodeOrd = 0; nodeOrd < numNodes; nodeOrd++)
        {
            uint32_t cdgSrcOrd = sccInfo[nodeOrd];
            TestAssert(cdgSrcOrd < numSCCs);

            EdgeIteratorImpl edgeIter(*graph, nodeOrd);
            if (edgeIter.IsValid(*graph))
            {
                while (true)
                {
                    TestAssert(edgeIter.IsValid(*graph));
                    uint32_t dstNodeOrd = edgeIter.GetDestNode(*graph);
                    TestAssert(dstNodeOrd < numNodes);
                    uint32_t cdgDstOrd = sccInfo[dstNodeOrd];
                    TestAssert(cdgDstOrd < numSCCs);

                    if (cdgSrcOrd != cdgDstOrd)
                    {
                        TestAssert(loc[nodeOrd] < loc[dstNodeOrd]);
                    }

                    if (!edgeIter.Advance(*graph))
                    {
                        break;
                    }
                }
            }
        }
#endif

        return r;
    }
};

}   // namespace dfg
