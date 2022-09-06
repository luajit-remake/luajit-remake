#include "gtest/gtest.h"

#include "deegen_api.h"
#include "annotated/unit_test/unit_test_ir_accessor.h"

#include "lambda_parser.h"
#include "switch_case.h"
#include "parse_bytecode_definition.h"
#include "tvalue_typecheck_optimization.h"

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

#if 0
TEST(AnnotationParser, BytecodeDefinitionSanity)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::vector<BytecodeVariantDefinition*> defs = DeegenBytecodeDefinitionParser::ParseList([&]() {
        std::unique_ptr<Module> module = GetDeegenUnitTestLLVMIR(ctx, "bytecode_definition_api");
        return module.release();
    });

    defs[5]->CreateInterpreterFunctionForMaxOperandWidthBytes(4);

    DesugarAndSimplifyLLVMModule(defs[5]->m_currentModule, DesugarUpToExcluding(DesugaringLevel::TypeSpecialization));
    // defs[0]->m_ifunc.RunPostLLVMSimplificationTransforms();

    ValidateLLVMModule(defs[5]->m_currentModule);

    TValueTypecheckOptimizationPass pass;
    pass.SetTargetFunction(defs[5]->m_ifunc.GetImplFunction());
    for (BcOperand* operand : defs[5]->m_ifunc.Operands())
    {
        if (operand->GetKind() == BcOperandKind::Constant)
        {
            BcOpConstant* bcc = assert_cast<BcOpConstant*>(operand);
            if (bcc->m_typeMask != x_typeSpeculationMaskFor<tTop>)
            {
                pass.AddOperandTypeInfo(static_cast<uint32_t>(bcc->OperandOrdinal()), bcc->m_typeMask);
            }
        }
    }

    pass.DoAnalysis();
    pass.DoOptimization();

    defs[5]->m_currentModule->dump();

    defs[0]->m_ifunc.GetImplFunction()->addFnAttr(Attribute::AttrKind::AlwaysInline);
    RunLLVMOptimizePass(defs[0]->m_currentModule);

    ValidateLLVMModule(defs[0]->m_currentModule);

    ExtractFunction(defs[0]->m_currentModule, "deegen_bytecode_impl");
    ValidateLLVMModule(defs[0]->m_currentModule);

    defs[0]->m_currentModule->dump();

    for (BytecodeVariantDefinition* def : defs)
    {
        std::stringstream ss;
        def->dump(ss);
        printf("%s\n", ss.str().c_str());
    }
}
#endif
