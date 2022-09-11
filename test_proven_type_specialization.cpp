#include "gtest/gtest.h"

#include "test_util_helper.h"
#include "deegen_api.h"
#include "annotated/unit_test/unit_test_ir_accessor.h"

#include "tvalue_typecheck_optimization.h"

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
        Module* module = moduleHolder.get();

        DesugarAndSimplifyLLVMModule(module, DesugarUpToExcluding(DesugaringLevel::TypeSpecialization));
    }

    void CheckIsExpected(std::string functionName, std::string suffix = "")
    {
        DesugarAndSimplifyLLVMModule(moduleHolder.get(), DesugarUpToExcluding(DesugaringLevel::TypeSpecialization));

        std::unique_ptr<Module> module = ExtractFunction(moduleHolder.get(), functionName);
        std::string dump = DumpLLVMModuleAsString(module.get());
        AssertIsExpectedOutput(dump, suffix);
    }

    std::unique_ptr<LLVMContext> llvmCtxHolder;
    std::unique_ptr<Module> moduleHolder;
};

}   // anonymous namespace

TEST(DeegenOptimizer, ProvenTypeSpecialization_1)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn1";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 1 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(1, x_typeSpeculationMaskFor<tDouble>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_2)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn1";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tDouble>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_3)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn1";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 1 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(1, x_typeSpeculationMaskFor<tDoubleNotNaN>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_4)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn1";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0, 1 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tDouble>));
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(1, x_typeSpeculationMaskFor<tDouble>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_5)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn1";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0, 1 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tMIV>));
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(1, x_typeSpeculationMaskFor<tMIV>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_6)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn1";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0, 1 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tDouble>));
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(1, x_typeSpeculationMaskFor<tDouble>));
        pass.SetConstraint(std::make_unique<TValueTypecheckOptimizationPass::NotConstraint>(std::move(constraint)));

        pass.Run();
    }

    // There is a stray tDouble check in the code (which result is not used by anything).
    // Thich is expected since at this stage we cannot recognize and eliminate side-effect-free functions.
    // Later optimization stages will remove the function call.
    //
    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_7)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn1";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0, 1 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tMIV>));
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(1, x_typeSpeculationMaskFor<tMIV>));
        pass.SetConstraint(std::make_unique<TValueTypecheckOptimizationPass::NotConstraint>(std::move(constraint)));

        pass.Run();
    }

    // There is a stray tMIV check in the code (which result is not used by anything).
    // Thich is expected since at this stage we cannot recognize and eliminate side-effect-free functions.
    // Later optimization stages will remove the function call.
    //
    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_8)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn1";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0, 1 });
        auto c1 = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        c1->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tDouble>));
        c1->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(1, x_typeSpeculationMaskFor<tDouble>));
        auto not1 = std::make_unique<TValueTypecheckOptimizationPass::NotConstraint>(std::move(c1));

        auto c2 = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        c2->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tMIV>));
        c2->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(1, x_typeSpeculationMaskFor<tMIV>));
        auto not2 = std::make_unique<TValueTypecheckOptimizationPass::NotConstraint>(std::move(c2));

        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::move(not1));
        constraint->AddClause(std::move(not2));

        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    // There is a stray tMIV check and a stray tDouble check in the code (which result is not used by anything).
    // Thich is expected since at this stage we cannot recognize and eliminate side-effect-free functions.
    // Later optimization stages will remove the function call.
    //
    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_9)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn2";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tBool>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_10)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn2";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tNil>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_11)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn2";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tMIV>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_12)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn2";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tDouble>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_13)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn3";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tBool>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_14)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn3";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tNil>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_15)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn3";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tMIV>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_16)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn3";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tDouble>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_17)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn4";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tHeapEntity>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}


TEST(DeegenOptimizer, ProvenTypeSpecialization_18)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn4";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tTable>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}


TEST(DeegenOptimizer, ProvenTypeSpecialization_19)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn4";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tFunction>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_20)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn4";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tDoubleNotNaN>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_21)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn4";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tDoubleNotNaN>));
        pass.SetConstraint(std::make_unique<TValueTypecheckOptimizationPass::NotConstraint>(std::move(constraint)));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}

TEST(DeegenOptimizer, ProvenTypeSpecialization_22)
{
    TestHelper helper("proven_type_specialization" /*fileName*/);
    std::string functionName = "testfn4";

    Module* module = helper.moduleHolder.get();
    Function* target = module->getFunction(functionName);
    ReleaseAssert(target != nullptr);

    {
        TValueTypecheckOptimizationPass pass;
        pass.SetTargetFunction(target);
        pass.SetOperandList({ 0 });
        auto constraint = std::make_unique<TValueTypecheckOptimizationPass::AndConstraint>();
        constraint->AddClause(std::make_unique<TValueTypecheckOptimizationPass::LeafConstraint>(0, x_typeSpeculationMaskFor<tThread>));
        pass.SetConstraint(std::move(constraint));

        pass.Run();
    }

    helper.CheckIsExpected(functionName);
}

