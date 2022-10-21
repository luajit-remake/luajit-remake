#include "gtest/gtest.h"

#include "test_util_helper.h"
#include "annotated/unit_test/unit_test_ir_accessor.h"
#include "misc_llvm_helper.h"

#include "deegen_ast_inline_cache.h"
#include "deegen_analyze_lambda_capture_pass.h"

using namespace llvm;
using namespace dast;

namespace {

struct TestHelper
{
    TestHelper(std::string fileName)
    {
        llvmCtxHolder = std::make_unique<LLVMContext>();
        LLVMContext& ctx = *llvmCtxHolder.get();

        moduleHolder = GetDeegenUnitTestLLVMIR(ctx, fileName);
    }

    void CheckIsExpected(llvm::Function* func, std::string suffix = "")
    {
        std::unique_ptr<Module> module = ExtractFunction(moduleHolder.get(), func->getName().str(), true /*ignoreLinkageIssues*/);
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
    DesugarAndSimplifyLLVMModule(module, DesugaringLevel::PerFunctionSimplifyOnlyAggresive);
    AstInlineCache::PreprocessModule(module);
    DeegenAnalyzeLambdaCapturePass::RemoveAnnotations(module);
    DesugarAndSimplifyLLVMModule(module, DesugaringLevel::PerFunctionSimplifyOnlyAggresive);

    Function* targetFunction = module->getFunction(functionName);
    ReleaseAssert(targetFunction != nullptr);
    std::vector<AstInlineCache> list = AstInlineCache::GetAllUseInFunction(targetFunction);
    ReleaseAssert(list.size() == 1);
    AstInlineCache& ic = list[0];
    ReleaseAssert(ic.m_effects.size() == 2);
    ReleaseAssert(ic.m_setUncacheableApiCalls.size() == 0);
    ReleaseAssert(ic.m_icKeyImpossibleValueMaybeNull != nullptr);
    ReleaseAssert(llvm_value_has_type<uint32_t>(ic.m_icKeyImpossibleValueMaybeNull));
    ReleaseAssert(GetValueOfLLVMConstantInt<uint32_t>(ic.m_icKeyImpossibleValueMaybeNull) == 123);

    ic.m_bodyFn->setLinkage(GlobalValue::ExternalLinkage);
    ic.m_effects[0].m_effectFnMain->setLinkage(GlobalValue::ExternalLinkage);
    ic.m_effects[1].m_effectFnMain->setLinkage(GlobalValue::ExternalLinkage);

    helper.CheckIsExpected(targetFunction, "mainFn");
    helper.CheckIsExpected(ic.m_bodyFn, "icBody");
    helper.CheckIsExpected(ic.m_effects[0].m_effectFnMain, "icEffectWrapper0");
    helper.CheckIsExpected(ic.m_effects[1].m_effectFnMain, "icEffectWrapper1");
}
