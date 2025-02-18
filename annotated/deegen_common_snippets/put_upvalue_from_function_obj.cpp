#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_PutUpvalueFromFunctionObject(HeapPtr<FunctionObject> func, size_t upvalueOrd, TValue valueToPut)
{
    Assert(upvalueOrd < func->m_numUpvalues);
    Upvalue* uv = FunctionObject::GetMutableUpvaluePtr(func, upvalueOrd);
    TValue* ptr = uv->m_ptr;
    *ptr = valueToPut;
}

DEFINE_DEEGEN_COMMON_SNIPPET("PutUpvalueFromFunctionObject", DeegenSnippet_PutUpvalueFromFunctionObject)

