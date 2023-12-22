#include "gtest/gtest.h"

#include "annotated/unit_test/unit_test_ir_accessor.h"

#include "llvm_check_function_effectful.h"

using namespace llvm;
using namespace dast;

TEST(LLVMCheckFunctionEffectful, Sanity_1)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::unique_ptr<Module> module = GetDeegenUnitTestLLVMIR(ctx, "llvm_effectful_function_tests");

    DesugarAndSimplifyLLVMModule(module.get(), DesugaringLevel::PerFunctionSimplifyOnlyAggresive);

    {
        Function* f = module->getFunction("f1");
        ReleaseAssert(f != nullptr);
        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(f) == true);
        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(f, std::vector<bool>{ true }) == false);
    }

    {
        Function* f = module->getFunction("f2");
        ReleaseAssert(f != nullptr);
        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(f) == false);
    }

    {
        Function* f = module->getFunction("f3");
        ReleaseAssert(f != nullptr);
        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(f) == false);
    }

    {
        Function* f = module->getFunction("f4");
        ReleaseAssert(f != nullptr);
        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(f) == false);
    }

    {
        Function* f = module->getFunction("f5");
        ReleaseAssert(f != nullptr);
        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(f) == true);
    }

    {
        Function* f = module->getFunction("f6");
        ReleaseAssert(f != nullptr);
        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(f) == false);
    }

    {
        Function* f = module->getFunction("f7");
        ReleaseAssert(f != nullptr);
        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(f) == true);
        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(f, std::vector<bool> { true, false }) == false);
        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(f, std::vector<bool> { true, true }) == false);
        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(f, std::vector<bool> { false, true }) == true);
    }

    {
        Function* f = module->getFunction("f8");
        ReleaseAssert(f != nullptr);
        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(
                          f,
                          std::vector<bool> {},
                          [&](Function* target, const std::vector<bool>& vec)
                          {
                              ReleaseAssert(target->getName().str() == "e1");
                              ReleaseAssert(vec.size() == 1);
                              ReleaseAssert(vec[0] == true);
                              return false;
                          }) == false);

        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(
                          f,
                          std::vector<bool> {},
                          [&](Function* target, const std::vector<bool>& vec)
                          {
                              ReleaseAssert(target->getName().str() == "e1");
                              ReleaseAssert(vec.size() == 1);
                              ReleaseAssert(vec[0] == true);
                              return true;
                          }) == true);
    }

    {
        Function* f = module->getFunction("f9");
        ReleaseAssert(f != nullptr);
        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(
                          f,
                          std::vector<bool> { false, false },
                          [&](Function* target, const std::vector<bool>& vec)
                          {
                              ReleaseAssert(target->getName().str() == "e1");
                              ReleaseAssert(vec.size() == 1);
                              ReleaseAssert(vec[0] == false);
                              return false;
                          }) == false);

        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(
                          f,
                          std::vector<bool> { false, false },
                          [&](Function* target, const std::vector<bool>& vec)
                          {
                              ReleaseAssert(target->getName().str() == "e1");
                              ReleaseAssert(vec.size() == 1);
                              ReleaseAssert(vec[0] == false);
                              return true;
                          }) == true);

        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(
                          f,
                          std::vector<bool> { true, false },
                          [&](Function* target, const std::vector<bool>& vec)
                          {
                              ReleaseAssert(target->getName().str() == "e1");
                              ReleaseAssert(vec.size() == 1);
                              ReleaseAssert(vec[0] == true);
                              return false;
                          }) == false);

        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(
                          f,
                          std::vector<bool> { true, false },
                          [&](Function* target, const std::vector<bool>& vec)
                          {
                              ReleaseAssert(target->getName().str() == "e1");
                              ReleaseAssert(vec.size() == 1);
                              ReleaseAssert(vec[0] == true);
                              return true;
                          }) == true);

        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(
                          f,
                          std::vector<bool> { false, true },
                          [&](Function* target, const std::vector<bool>& vec)
                          {
                              ReleaseAssert(target->getName().str() == "e1");
                              ReleaseAssert(vec.size() == 1);
                              ReleaseAssert(vec[0] == false);
                              return false;
                          }) == false);

        ReleaseAssert(DetermineIfLLVMFunctionMightBeEffectful(
                          f,
                          std::vector<bool> { false, true },
                          [&](Function* target, const std::vector<bool>& vec)
                          {
                              ReleaseAssert(target->getName().str() == "e1");
                              ReleaseAssert(vec.size() == 1);
                              ReleaseAssert(vec[0] == false);
                              return true;
                          }) == true);
    }
}
