#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static HeapPtr<TableObject> DeegenSnippet_GetGlobalObjectFromBaselineCodeBlock(BaselineCodeBlock* bcb)
{
    return bcb->m_globalObject.As();
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetGlobalObjectFromBaselineCodeBlock", DeegenSnippet_GetGlobalObjectFromBaselineCodeBlock)

