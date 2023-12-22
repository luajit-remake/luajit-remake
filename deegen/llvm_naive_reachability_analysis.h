#pragma once

#include "common_utils.h"
#include "misc_llvm_helper.h"

namespace dast {

// A naive O(n^2) reachability analysis
// Normally these sort of things can often be solved by dominator tree, but sometimes we want more flexibility...
// And since we don't care about performance here, using a naive implementation is often simpler
//
struct LLVMNaiveReachabilityAnalysis
{
    LLVMNaiveReachabilityAnalysis(llvm::Function* func)
    {
        using namespace llvm;
        m_func = func;

        size_t curBBOrd = 0;
        for (BasicBlock& bb : *func)
        {
            m_allBBs.push_back(&bb);
            ReleaseAssert(!m_labelMap.count(&bb));
            m_labelMap[&bb] = curBBOrd;
            curBBOrd++;
        }

        size_t n = m_labelMap.size();
        ReleaseAssert(n == curBBOrd);
        ReleaseAssert(n == m_allBBs.size());

        m_isReachable.resize(n);
        for (size_t bbOrd = 0; bbOrd < n; bbOrd++)
        {
            bfs(n, bbOrd, m_isReachable[bbOrd]);
        }
    }

    bool WARN_UNUSED IsReachable(llvm::BasicBlock* src, llvm::BasicBlock* dst)
    {
        ReleaseAssert(m_labelMap.count(src));
        size_t srcOrd = m_labelMap[src];
        ReleaseAssert(m_labelMap.count(dst));
        size_t dstOrd = m_labelMap[dst];
        ReleaseAssert(srcOrd < m_isReachable.size());
        ReleaseAssert(dstOrd < m_isReachable[srcOrd].size());
        return m_isReachable[srcOrd][dstOrd];
    }

private:
    void bfs(size_t n, size_t origin, std::vector<bool>& visited /*inout*/)
    {
        using namespace llvm;

        visited.resize(n, false);
        std::queue<size_t> q;
        auto pushQueue = [&](size_t ord) ALWAYS_INLINE
        {
            ReleaseAssert(ord < n);
            if (visited[ord]) { return; }
            visited[ord] = true;
            q.push(ord);
        };

        pushQueue(origin);
        while (!q.empty())
        {
            size_t curNode = q.front();
            q.pop();
            ReleaseAssert(curNode < m_allBBs.size());
            BasicBlock* bb = m_allBBs[curNode];
            for (BasicBlock* succ : successors(bb))
            {
                ReleaseAssert(m_labelMap.count(succ));
                size_t succOrd = m_labelMap[succ];
                ReleaseAssert(m_allBBs[succOrd] == succ);
                pushQueue(succOrd);
            }
        }
    }

    llvm::Function* m_func;
    std::unordered_map<llvm::BasicBlock*, size_t> m_labelMap;
    std::vector<llvm::BasicBlock*> m_allBBs;
    std::vector<std::vector<bool>> m_isReachable;
};

}   // namespace dast
