#include "define_deegen_common_snippet.h"
#include "drt/baseline_jit_codegen_helper.h"

static BaselineCodeBlockAndEntryPoint DeegenSnippet_OsrEntryIntoBaselineJit(CodeBlock* cb, void* curBytecode)
{
    return deegen_prepare_osr_entry_into_baseline_jit(cb, curBytecode);
}

DEFINE_DEEGEN_COMMON_SNIPPET("OsrEntryIntoBaselineJit", DeegenSnippet_OsrEntryIntoBaselineJit)
