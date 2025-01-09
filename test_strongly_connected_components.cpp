#include "gtest/gtest.h"

#include "drt/strongly_connected_components_util.h"
#include "drt/bitmap_adjacency_graph.h"

namespace {

// Use a plain Tarjan SCC implementation as expected output
//
struct TarjanSCC
{
    void Init(size_t n)
    {
        m_numNodes = n;
        m_graph.clear(); m_graph.resize(n);
    }

    void Dfs(size_t cur)
    {
        ReleaseAssert(cur < m_numNodes);
        m_dfn[cur] = m_index;
        m_low[cur] = m_index;
        m_index++;
        m_stack.push_back(cur);
        m_onStack[cur] = true;
        for (size_t next : m_graph[cur])
        {
            ReleaseAssert(next < m_numNodes);
            if (m_dfn[next] == 0)
            {
                Dfs(next);
                m_low[cur] = std::min(m_low[cur], m_low[next]);
            }
            else if (m_onStack[next])
            {
                m_low[cur] = std::min(m_low[cur], m_dfn[next]);
            }
        }
        if (m_dfn[cur] == m_low[cur])
        {
            while (true)
            {
                ReleaseAssert(!m_stack.empty());
                size_t x = m_stack.back();
                m_stack.pop_back();
                m_onStack[x] = false;
                m_sccInfo[x] = m_numSCCs;
                if (cur == x)
                {
                    break;
                }
            }
            m_numSCCs++;
        }
    }

    void Run()
    {
        m_index = 1;
        m_numSCCs = 0;
        m_dfn.clear(); m_dfn.resize(m_numNodes, 0 /*value*/);
        m_low.clear(); m_low.resize(m_numNodes, 0 /*value*/);
        m_stack.clear();
        m_onStack.clear(); m_onStack.resize(m_numNodes, false /*value*/);
        m_sccInfo.clear(); m_sccInfo.resize(m_numNodes, static_cast<size_t>(-1) /*value*/);

        for (size_t i = 0; i < m_numNodes; i++)
        {
            if (m_dfn[i] == 0)
            {
                Dfs(i);
            }
        }

        ReleaseAssert(m_stack.empty());
        for (size_t i = 0; i < m_numNodes; i++)
        {
            ReleaseAssert(m_sccInfo[i] != static_cast<size_t>(-1));
        }
    }

    size_t m_index;
    size_t m_numNodes;
    size_t m_numSCCs;
    std::vector<size_t> m_dfn;
    std::vector<size_t> m_low;
    std::vector<size_t> m_stack;
    std::vector<bool> m_onStack;
    std::vector<size_t> m_sccInfo;
    std::vector<std::vector<size_t>> m_graph;
};

struct TestChecker
{
    TestChecker(uint32_t n)
    {
        m_numNodes = n;
        m_tarjan.Init(n);
        m_graph.AllocateMemory(m_alloc, n, true /*initializeToZero*/);
    }

    void AddEdge(uint32_t src, uint32_t dst)
    {
        ReleaseAssert(src < m_numNodes && dst < m_numNodes);
        m_tarjan.m_graph[src].push_back(dst);
        m_graph.GetEdgeBV(src).SetBit(dst);
    }

    void RunAndCheck()
    {
        std::vector<uint32_t> sccInfo;
        sccInfo.resize(m_numNodes);
        m_tarjan.Run();
        uint32_t numSCCs = m_graph.ComputeSCC(sccInfo.data());

        ReleaseAssert(numSCCs == m_tarjan.m_numSCCs);

        std::vector<size_t> mapping;
        mapping.resize(numSCCs, static_cast<size_t>(-1));

        for (size_t i = 0; i < m_numNodes; i++)
        {
            size_t src = m_tarjan.m_sccInfo[i];
            size_t dst = sccInfo[i];
            ReleaseAssert(src < numSCCs && dst < numSCCs);

            if (mapping[src] == static_cast<size_t>(-1))
            {
                mapping[src] = dst;
            }
            else
            {
                ReleaseAssert(mapping[src] == dst);
            }
        }

        for (size_t i = 0; i < numSCCs; i++)
        {
            ReleaseAssert(mapping[i] != static_cast<size_t>(-1));
        }
        std::sort(mapping.begin(), mapping.end());
        for (size_t i = 0; i < numSCCs; i++)
        {
            ReleaseAssert(mapping[i] == i);
        }
    }

    TempArenaAllocator m_alloc;
    size_t m_numNodes;
    TarjanSCC m_tarjan;
    dfg::BitmapAdjacencyGraph m_graph;
};

void RunTestForRandomGraph(std::mt19937& rdgen, uint32_t n, uint32_t m)
{
    TestChecker checker(n);
    checker.RunAndCheck();
    for (size_t i = 0; i < m; i++)
    {
        uint32_t src = rdgen() % n;
        uint32_t dst = rdgen() % n;
        checker.AddEdge(src, dst);
        if (i + 1 == m || i % 5 == 0)
        {
            checker.RunAndCheck();
        }
    }
}

}   // anonymous namespace

TEST(StronglyConnectedComponents, Sanity)
{
    std::random_device rd;
    std::mt19937 rdgen(rd());

    for (size_t testCase = 0; testCase < 10; testCase++)
    {
        RunTestForRandomGraph(rdgen, 10, 200);
        RunTestForRandomGraph(rdgen, 35, 1000);
    }

    RunTestForRandomGraph(rdgen, 100, 5000);
    RunTestForRandomGraph(rdgen, 350, 2000);
}
