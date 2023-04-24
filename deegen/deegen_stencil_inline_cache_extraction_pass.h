#pragma once

#include "common.h"
#include "deegen_parse_asm_text.h"
#include "deegen_recover_asm_cfg.h"

namespace dast {

struct DeegenStencilExtractedICAsm
{
    std::string WARN_UNUSED GetICLabel()
    {
        ReleaseAssert(m_blocks.size() > 0);
        return m_blocks[0]->m_normalizedLabelName;
    }

    std::vector<X64AsmBlock*> m_blocks;
    X64AsmFile* m_owner;
};

// Analyze and extract inline caching logic from the input file.
// Each piece of IC logic is identified by its entry point label, as specified in 'icLabels'.
//
// The extracted IC logic is returned as a vector, in the same order as 'icLabels'.
// The input file is modified so that all the blocks that belongs to (any) IC are removed from m_blocks and moved to m_icPath
//
std::vector<DeegenStencilExtractedICAsm> WARN_UNUSED RunStencilInlineCacheLogicExtractionPass(X64AsmFile* file /*inout*/,
                                                                                              DeegenAsmCfg cfg,
                                                                                              std::vector<std::string> icLabels);

}   // namespace dast
