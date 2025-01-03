#include "gtest/gtest.h"

#include "deegen_api.h"
#include "annotated/unit_test/unit_test_ir_accessor.h"

#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_bytecode_operand.h"
#include "deegen_ast_make_call.h"

#include "test_util_helper.h"

using namespace llvm;
using namespace dast;

namespace {

std::unique_ptr<llvm::Module> WARN_UNUSED GetTestCase(llvm::LLVMContext& ctx, size_t testcaseNum)
{
    std::unique_ptr<Module> module = GetDeegenUnitTestLLVMIR(ctx, "return_value_accessor_api_lowering");

    DesugarAndSimplifyLLVMModule(module.get(), DesugaringLevel::PerFunctionSimplifyOnly);
    AstMakeCall::PreprocessModule(module.get());

    std::vector<BytecodeVariantCollection> defs = BytecodeVariantDefinition::ParseAllFromModule(module.get());

    ReleaseAssert(defs.size() >= testcaseNum);
    ReleaseAssert(defs[testcaseNum - 1].m_variants.size() > 0);
    auto& target = defs[testcaseNum - 1].m_variants[0];
    target->SetMaxOperandWidthBytes(4);

    Function* implFunc = module->getFunction(target->m_implFunctionName);
    BytecodeIrInfo bii = BytecodeIrInfo::Create(target.get(), implFunc);
    std::unique_ptr<Module> res = InterpreterBytecodeImplCreator::DoLoweringForAll(bii);

    std::string rcName = "__deegen_interpreter_op_test" + std::to_string(testcaseNum) + "_0_retcont_0";
    Function* rcFunc = res->getFunction(rcName);
    ReleaseAssert(rcFunc != nullptr);

    std::unique_ptr<Module> m = ExtractFunction(res.get(), rcName);
    TestOnly_StripLLVMIdentMetadata(m.get());
    return m;
}

}   // anonymous namespace

TEST(DeegenAst, ReturnValueAccessorLowering_1)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::unique_ptr<Module> module = GetTestCase(ctx, 1 /*testCaseNum*/);
    AssertIsExpectedOutput(DumpLLVMModuleAsString(module.get()), PP_STRINGIFY(BUILD_FLAVOR));
}

TEST(DeegenAst, ReturnValueAccessorLowering_2)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::unique_ptr<Module> module = GetTestCase(ctx, 2 /*testCaseNum*/);
    AssertIsExpectedOutput(DumpLLVMModuleAsString(module.get()), PP_STRINGIFY(BUILD_FLAVOR));
}

TEST(DeegenAst, ReturnValueAccessorLowering_3)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::unique_ptr<Module> module = GetTestCase(ctx, 3 /*testCaseNum*/);
    AssertIsExpectedOutput(DumpLLVMModuleAsString(module.get()), PP_STRINGIFY(BUILD_FLAVOR));
}

TEST(DeegenAst, ReturnValueAccessorLowering_4)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::unique_ptr<Module> module = GetTestCase(ctx, 4 /*testCaseNum*/);
    AssertIsExpectedOutput(DumpLLVMModuleAsString(module.get()), PP_STRINGIFY(BUILD_FLAVOR));
}

TEST(DeegenAst, ReturnValueAccessorLowering_5)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::unique_ptr<Module> module = GetTestCase(ctx, 5 /*testCaseNum*/);
    AssertIsExpectedOutput(DumpLLVMModuleAsString(module.get()), PP_STRINGIFY(BUILD_FLAVOR));
}

TEST(DeegenAst, ReturnValueAccessorLowering_6)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::unique_ptr<Module> module = GetTestCase(ctx, 6 /*testCaseNum*/);
    AssertIsExpectedOutput(DumpLLVMModuleAsString(module.get()), PP_STRINGIFY(BUILD_FLAVOR));
}

TEST(DeegenAst, ReturnValueAccessorLowering_7)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::unique_ptr<Module> module = GetTestCase(ctx, 7 /*testCaseNum*/);
    AssertIsExpectedOutput(DumpLLVMModuleAsString(module.get()), PP_STRINGIFY(BUILD_FLAVOR));
}
