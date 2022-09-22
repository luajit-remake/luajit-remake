#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "tvalue.h"

static void* DeegenSnippet_SimpleTranslateToRawPointer(HeapPtr<void> input)
{
    return TranslateToRawPointer(input);
}

DEFINE_DEEGEN_COMMON_SNIPPET("SimpleTranslateToRawPointer", DeegenSnippet_SimpleTranslateToRawPointer)
