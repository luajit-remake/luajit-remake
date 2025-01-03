#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static TValue DeegenSnippet_GetClosedUpvalueValue(Upvalue* uv)
{
    Assert(uv->m_isClosed && !uv->m_isImmutable);
    return uv->m_tv;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetClosedUpvalueValue", DeegenSnippet_GetClosedUpvalueValue)
