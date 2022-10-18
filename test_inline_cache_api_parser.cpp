#include "gtest/gtest.h"

#include "test_util_helper.h"
#include "annotated/unit_test/unit_test_ir_accessor.h"
#include "misc_llvm_helper.h"

#include "deegen_ast_inline_cache.h"
#include "deegen_analyze_lambda_capture_pass.h"

using namespace llvm;
using namespace dast;
#if 0
namespace {

struct TestHelper
{
    TestHelper(std::string fileName)
    {
        llvmCtxHolder = std::make_unique<LLVMContext>();
        LLVMContext& ctx = *llvmCtxHolder.get();

        moduleHolder = GetDeegenUnitTestLLVMIR(ctx, fileName);
        // Module* module = moduleHolder.get();

        // DesugarAndSimplifyLLVMModule(module, DesugaringLevel::PerFunctionSimplifyOnly);
    }

    void CheckIsExpected(std::string functionName, std::string suffix = "")
    {
        DesugarAndSimplifyLLVMModule(moduleHolder.get(), DesugarUpToExcluding(DesugaringLevel::PerFunctionSimplifyOnly));

        std::unique_ptr<Module> module = ExtractFunction(moduleHolder.get(), functionName, true /*ignoreLinkageIssues*/);
        std::string dump = DumpLLVMModuleAsString(module.get());
        AssertIsExpectedOutput(dump, suffix);
    }

    std::unique_ptr<LLVMContext> llvmCtxHolder;
    std::unique_ptr<Module> moduleHolder;
};

}   // anonymous namespace

TEST(DeegenAst, InlineCacheAPIParser_1)
{
    TestHelper helper("ic_api_sanity" /*fileName*/);
    std::string functionName = "testfn1";

    Module* module = helper.moduleHolder.get();

    DeegenAnalyzeLambdaCapturePass::AddAnnotations(module);
    module->dump();
    // AstInlineCache::PreprocessModule(module);

    // Function* targetFunction = module->getFunction(functionName);
    // ReleaseAssert(targetFunction != nullptr);

}
#endif
