#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static FunctionObject* DeegenSnippet_CreateNewClosureForDfgAndFillUpvaluesFromParent(UnlinkedCodeBlock* ucbOfNewClosure, HeapPtr<FunctionObject> parentFunc)
{
    return FunctionObject::CreateForDfgAndFillUpvaluesFromParent(ucbOfNewClosure, parentFunc);
}

DEFINE_DEEGEN_COMMON_SNIPPET("CreateNewClosureForDfgAndFillUpvaluesFromParent", DeegenSnippet_CreateNewClosureForDfgAndFillUpvaluesFromParent)
