#include "disable_assertions.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static uint64_t DeegenSnippet_BoxFunctionObjectRawPointerToTValue(FunctionObject* funcRawPtr)
{
    HeapPtr<FunctionObject> func = TranslateToHeapPtr(funcRawPtr);
    return reinterpret_cast<uint64_t>(func);
}

DEFINE_DEEGEN_COMMON_SNIPPET("BoxFunctionObjectRawPointerToTValue", DeegenSnippet_BoxFunctionObjectRawPointerToTValue)
