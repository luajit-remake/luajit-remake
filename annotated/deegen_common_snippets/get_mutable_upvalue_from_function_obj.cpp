#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static TValue DeegenSnippet_GetMutableUpvalueValueFromFunctionObject(HeapPtr<FunctionObject> func, size_t upvalueOrd)
{
    Assert(upvalueOrd < func->m_numUpvalues);
    HeapPtr<Upvalue> uvPtr = FunctionObject::GetMutableUpvaluePtr(func, upvalueOrd);
    return *(uvPtr->m_ptr);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetMutableUpvalueValueFromFunctionObject", DeegenSnippet_GetMutableUpvalueValueFromFunctionObject)

