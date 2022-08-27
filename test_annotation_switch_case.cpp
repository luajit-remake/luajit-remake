#include "gtest/gtest.h"

#include "deegen_api.h"
#include "annotated/unit_test/unit_test_ir_accessor.h"

#include "dump_llvm_module.h"
#include "lambda_parser.h"
#include "switch_case.h"
#include "parse_bytecode_definition.h"

using namespace DeegenAPI;
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

TEST(AnnotationParser, BytecodeDefinitionSanity)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::unique_ptr<Module> module = GetDeegenUnitTestLLVMIR(ctx, "bytecode_definition_api");

    std::ignore = DeegenBytecodeDefinitionParser::ParseList(module.get());
}
