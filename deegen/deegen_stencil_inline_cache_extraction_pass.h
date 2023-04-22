#pragma once

#include "common.h"
#include "deegen_parse_asm_text.h"

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
// The extracted IC logic is removed from the input file and returned as a vector, in the same order as 'icLabels'.
//
std::vector<DeegenStencilExtractedICAsm> WARN_UNUSED RunStencilInlineCacheLogicExtractionPass(X64AsmFile* file /*inout*/,
                                                                                              llvm::Function* func,
                                                                                              std::vector<std::string> icLabels);

}   // namespace dast
