#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static TValue DeegenSnippet_GetImmutableUpvalueValueFromFunctionObject(HeapPtr<FunctionObject> funcObj, size_t upvalueOrd)
{
    Assert(upvalueOrd < funcObj->m_numUpvalues);
    return FunctionObject::GetImmutableUpvalueValue(funcObj, upvalueOrd);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetImmutableUpvalueValueFromFunctionObject", DeegenSnippet_GetImmutableUpvalueValueFromFunctionObject)

