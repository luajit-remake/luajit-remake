#include "define_deegen_common_snippet.h"
#include "tvalue.h"

static uint32_t DeegenSnippet_TranslateRawPtrToSysHeapPtr(uint8_t* input)
{
    SystemHeapPointer<uint8_t> ptr = input;
    return ptr.m_value;
}

DEFINE_DEEGEN_COMMON_SNIPPET("TranslateRawPtrToSysHeapPtr", DeegenSnippet_TranslateRawPtrToSysHeapPtr)


