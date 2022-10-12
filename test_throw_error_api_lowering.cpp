#include "gtest/gtest.h"

#include "deegen_api.h"
#include "annotated/unit_test/unit_test_ir_accessor.h"

#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_bytecode_operand.h"

#include "test_util_helper.h"

using namespace llvm;
using namespace dast;

namespace {

std::unique_ptr<llvm::Module> WARN_UNUSED GetTestCase(llvm::LLVMContext& ctx, size_t testcaseNum)
{
    std::unique_ptr<Module> module = GetDeegenUnitTestLLVMIR(ctx, "throw_error_api_lowering");

    DesugarAndSimplifyLLVMModule(module.get(), DesugaringLevel::PerFunctionSimplifyOnly);

    std::vector<std::vector<std::unique_ptr<BytecodeVariantDefinition>>> defs = BytecodeVariantDefinition::ParseAllFromModule(module.get());

    ReleaseAssert(defs.size() >= testcaseNum);
    ReleaseAssert(defs[testcaseNum - 1].size() > 0);
    auto& target = defs[testcaseNum - 1][0];
    target->SetMaxOperandWidthBytes(4);

    Function* implFunc = module->getFunction(target->m_implFunctionName);
    InterpreterBytecodeImplCreator ifi(target.get(), implFunc, InterpreterBytecodeImplCreator::ProcessKind::Main);
    return ifi.DoLowering();
}

}   // anonymous namespace

TEST(DeegenAst, ThrowErrorAPILowering_1)
{
    std::unique_ptr<LLVMContext> ctxHolder = std::make_unique<LLVMContext>();
    std::unique_ptr<Module> module = GetTestCase(*ctxHolder.get(), 1);
    AssertIsExpectedOutput(DumpLLVMModuleAsString(module.get()));
}

TEST(DeegenAst, ThrowErrorAPILowering_2)
{
    std::unique_ptr<LLVMContext> ctxHolder = std::make_unique<LLVMContext>();
    std::unique_ptr<Module> module = GetTestCase(*ctxHolder.get(), 2);
    AssertIsExpectedOutput(DumpLLVMModuleAsString(module.get()));
}
