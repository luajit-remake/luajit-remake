#pragma once

#include "common_utils.h"
#include "deegen_parse_asm_text.h"

namespace dast {

// Recover control flow graph from assembly text.
// Direct branch targets are analyzed directly. Indirect branch targets are analyzed by
// using debug info to remap it back to LLVM IR.
//
struct DeegenAsmCfgAnalyzer
{
    // If 'addHumanDebugHint' is true, a comment is added after each branch instruction showing its destination
    // We assume that the function entry is the real function entry so it must map to the LLVM entry block (which is obviously
    // true unless the caller has already done some weird transformation)
    //
    static DeegenAsmCfgAnalyzer WARN_UNUSED DoAnalysis(X64AsmFile* file, llvm::Function* func, bool addHumanDebugComment = true);

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

    std::map<std::string, std::set<std::string>> m_cfg;
};

}   // namespace dast
