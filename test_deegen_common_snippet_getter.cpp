#include "gtest/gtest.h"

#include "test_util_helper.h"
#include "misc_llvm_helper.h"

#include "annotated/deegen_common_snippets/deegen_common_snippet_ir_accessor.h"

using namespace llvm;
using namespace dast;

TEST(DeegenCommonSnippet, Sanity)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder = std::make_unique<LLVMContext>();
    LLVMContext& ctx = *llvmCtxHolder.get();
    std::unique_ptr<Module> module = GetDeegenCommonSnippetLLVMIR(ctx, "GetEndOfCallFrameFromInterpreterCodeBlock", 0 /*expectedKind = snippet*/);
    TestOnly_StripLLVMIdentMetadata(module.get());

    std::string dump = DumpLLVMModuleAsString(module.get());
    AssertIsExpectedOutput(dump);
}
