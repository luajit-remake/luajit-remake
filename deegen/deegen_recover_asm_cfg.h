#pragma once

#include "common_utils.h"
#include "deegen_parse_asm_text.h"

namespace dast {

// Recover control flow graph from assembly text.
// Direct branch targets are analyzed directly. Indirect branch targets are analyzed by
// using debug info to remap it back to LLVM IR.
//
struct DeegenAsmCfg
{
    // If 'addHumanDebugHint' is true, a comment is added after each branch instruction showing its destination
    // We assume that the function entry is the real function entry so it must map to the LLVM entry block (which is obviously
    // true unless the caller has already done some weird transformation)
    //
    // Only needs to be called once
    //
    static void AnalyzeIndirectBranch(X64AsmFile* file, llvm::Function* func, bool addHumanDebugComment = true);

    // Must be called after AnalyzeIndirectBranch
    //
    static DeegenAsmCfg WARN_UNUSED GetCFG(X64AsmFile* file);

    bool WARN_UNUSED HasEdge(const std::string& fromLabel, const std::string& toLabel)
    {
        ReleaseAssert(m_cfg.count(fromLabel) && m_cfg.count(toLabel));
        return m_cfg[fromLabel].count(toLabel);
    }

    const std::set<std::string>& WARN_UNUSED SuccOf(const std::string& fromLabel)
    {
        ReleaseAssert(m_cfg.count(fromLabel));
        return m_cfg[fromLabel];
    }

    // Return the list of blocks that are reachable from 'entryBlock', but must pass through at least one block in 'dominatorBlockList'
    //
    std::vector<std::string> WARN_UNUSED GetAllBlocksDominatedBy(const std::vector<std::string>& dominatorBlockList, const std::string& entryBlock);

    std::map<std::string, std::set<std::string>> m_cfg;
};

}   // namespace dast
