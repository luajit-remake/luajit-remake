#include "gtest/gtest.h"

#include "deegen_api.h"
#include "annotated/unit_test/unit_test_ir_accessor.h"

#include "deegen_process_lib_func.h"

#include "test_util_helper.h"

using namespace llvm;
using namespace dast;

namespace {

std::unique_ptr<llvm::Module> WARN_UNUSED GetTestCase(llvm::LLVMContext& ctx, size_t testcaseNum)
{
    std::unique_ptr<Module> module = GetDeegenUnitTestLLVMIR(ctx, "user_lib_func_api");

    std::vector<DeegenLibFuncInstanceInfo> allInfo = DeegenLibFuncProcessor::ParseInfo(module.get());
    ReleaseAssert(testcaseNum > 0 && allInfo.size() >= testcaseNum);
    DeegenLibFuncInstanceInfo info = allInfo[testcaseNum - 1];

    DeegenLibFuncProcessor::DoLowering(module.get());
    RunLLVMOptimizePass(module.get());

    ReleaseAssert(module->getFunction(info.m_wrapperName) != nullptr);
    std::unique_ptr<Module> m = ExtractFunction(module.get(), info.m_wrapperName);
    TestOnly_StripLLVMIdentMetadata(m.get());
    return m;
}

}   // anonymous namespace

TEST(DeegenAst, UserLibFuncLowering_1)
{
    std::unique_ptr<LLVMContext> ctxHolder = std::make_unique<LLVMContext>();
    std::unique_ptr<Module> module = GetTestCase(*ctxHolder.get(), 1);
    AssertIsExpectedOutput(DumpLLVMModuleAsString(module.get()));
}

TEST(DeegenAst, UserLibFuncLowering_2)
{
    std::unique_ptr<LLVMContext> ctxHolder = std::make_unique<LLVMContext>();
    std::unique_ptr<Module> module = GetTestCase(*ctxHolder.get(), 2);
    AssertIsExpectedOutput(DumpLLVMModuleAsString(module.get()));
}

TEST(DeegenAst, UserLibFuncLowering_3)
{
    std::unique_ptr<LLVMContext> ctxHolder = std::make_unique<LLVMContext>();
    std::unique_ptr<Module> module = GetTestCase(*ctxHolder.get(), 3);
    AssertIsExpectedOutput(DumpLLVMModuleAsString(module.get()));
}

TEST(DeegenAst, UserLibFuncLowering_4)
{
    std::unique_ptr<LLVMContext> ctxHolder = std::make_unique<LLVMContext>();
    std::unique_ptr<Module> module = GetTestCase(*ctxHolder.get(), 4);
    AssertIsExpectedOutput(DumpLLVMModuleAsString(module.get()));
}

TEST(DeegenAst, UserLibFuncLowering_5)
{
    std::unique_ptr<LLVMContext> ctxHolder = std::make_unique<LLVMContext>();
    std::unique_ptr<Module> module = GetTestCase(*ctxHolder.get(), 5);
    AssertIsExpectedOutput(DumpLLVMModuleAsString(module.get()));
}

TEST(DeegenAst, UserLibFuncLowering_6)
{
    std::unique_ptr<LLVMContext> ctxHolder = std::make_unique<LLVMContext>();
    std::unique_ptr<Module> module = GetTestCase(*ctxHolder.get(), 6);
    AssertIsExpectedOutput(DumpLLVMModuleAsString(module.get()));
}

TEST(DeegenAst, UserLibFuncLowering_7)
{
    std::unique_ptr<LLVMContext> ctxHolder = std::make_unique<LLVMContext>();
    std::unique_ptr<Module> module = GetTestCase(*ctxHolder.get(), 7);
    AssertIsExpectedOutput(DumpLLVMModuleAsString(module.get()), PP_STRINGIFY(BUILD_FLAVOR));
}

TEST(DeegenAst, UserLibFuncLowering_8)
{
    std::unique_ptr<LLVMContext> ctxHolder = std::make_unique<LLVMContext>();
    std::unique_ptr<Module> module = GetTestCase(*ctxHolder.get(), 8);
    AssertIsExpectedOutput(DumpLLVMModuleAsString(module.get()));
}

