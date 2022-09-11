#include "gtest/gtest.h"

#include "deegen_api.h"
#include "annotated/unit_test/unit_test_ir_accessor.h"

#include "lambda_parser.h"
#include "switch_case.h"
#include "tvalue_typecheck_optimization.h"
#include "deegen_interpreter_interface.h"
#include "deegen_bytecode_operand.h"
#include "deegen_ast_make_call.h"

using namespace llvm;
using namespace dast;

TEST(AnnotationParser, SwitchCaseSanity)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::unique_ptr<Module> module = GetDeegenUnitTestLLVMIR(ctx, "switch_case");
    DAPILambdaMap lm = CXXLambda::ParseDastLambdaMap(module.get());
    std::vector<SwitchCase> switchCaseList = SwitchCase::ParseAll(module.get(), lm);

    ReleaseAssert(switchCaseList.size() == 1);
    ReleaseAssert(GetDemangledName(switchCaseList[0].m_owner) == "testfn(int, int, int)");
    ReleaseAssert(switchCaseList[0].m_cases.size() == 2);
    ReleaseAssert(switchCaseList[0].m_hasDefaultClause);
    ReleaseAssert(lm.size() == 0);
}
#if 0
TEST(AnnotationParser, BytecodeDefinitionSanity)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::unique_ptr<Module> module = GetDeegenUnitTestLLVMIR(ctx, "bytecode_definition_api");

    std::vector<std::vector<std::unique_ptr<BytecodeVariantDefinition>>> defs = BytecodeVariantDefinition::ParseAllFromModule(module.get());

    auto& target = defs[1][0];

    target->SetMaxOperandWidthBytes(4);

    Function* implFunc = module->getFunction(target->m_implFunctionName);
    InterpreterFunctionInterface ifi(implFunc, false);
    ifi.EmitWrapperBody(*target.get());
    ifi.LowerAPIs();

    ifi.GetModule()->dump();

}

#endif

TEST(AnnotationParser, BytecodeInterpreterLoweringSanity_1)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::unique_ptr<Module> module = GetDeegenUnitTestLLVMIR(ctx, "bytecode_interpreter_lowering_sanity_1");

    DesugarAndSimplifyLLVMModule(module.get(), DesugaringLevel::PerFunctionSimplifyOnly);
    AstMakeCall::PreprocessModule(module.get());

    std::vector<std::vector<std::unique_ptr<BytecodeVariantDefinition>>> defs = BytecodeVariantDefinition::ParseAllFromModule(module.get());

    auto& target = defs[0][0];
    target->SetMaxOperandWidthBytes(4);

    Function* implFunc = module->getFunction(target->m_implFunctionName);
    InterpreterFunctionInterface ifi(target.get(), implFunc, false);
    ifi.EmitWrapperBody();
    ifi.LowerAPIs();
    ifi.GetModule()->dump();

}
