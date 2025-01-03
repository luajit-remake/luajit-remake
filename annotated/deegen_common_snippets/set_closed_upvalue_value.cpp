#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_SetClosedUpvalueValue(Upvalue* uv, TValue value)
{
    Assert(uv->m_isClosed && !uv->m_isImmutable);
    uv->m_tv = value;
}

DEFINE_DEEGEN_COMMON_SNIPPET("SetClosedUpvalueValue", DeegenSnippet_SetClosedUpvalueValue)
