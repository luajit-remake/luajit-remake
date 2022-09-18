#include "gtest/gtest.h"

#include "test_util_helper.h"
#include "deegen_api.h"
#include "annotated/unit_test/unit_test_ir_accessor.h"

#include "deegen_ast_make_call.h"

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

        DesugarAndSimplifyLLVMModule(module, DesugaringLevel::PerFunctionSimplifyOnly);
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

TEST(DeegenAst, MakeCallAPIParser_1)
{
    TestHelper helper("make_call_api_parser" /*fileName*/);
    std::string functionName = "testfn1";

    Module* module = helper.moduleHolder.get();

    AstMakeCall::PreprocessModule(module);

    Function* targetFunction = module->getFunction(functionName);
    ReleaseAssert(targetFunction != nullptr);

    std::vector<AstMakeCall> res = AstMakeCall::GetAllUseInFunction(targetFunction);
    ReleaseAssert(res.size() == 3);

    bool found1 = false, found2 = false, found3 = false;
    for (AstMakeCall& amc : res)
    {
        if (!amc.m_isInPlaceCall && !amc.m_passVariadicRes && !amc.m_isMustTailCall)
        {
            ReleaseAssert(!found1);
            found1 = true;

            ReleaseAssert(amc.m_callOption == MakeCallOption::NoOption);
            ReleaseAssert(amc.m_args.size() == 2);
            ReleaseAssert(!amc.m_args[0].IsArgRange());
            ReleaseAssert(amc.m_args[0].GetArg() == targetFunction->getArg(0));
            ReleaseAssert(!amc.m_args[1].IsArgRange());
            ReleaseAssert(amc.m_args[1].GetArg() == targetFunction->getArg(1));

        }
        else if (!amc.m_isInPlaceCall && !amc.m_passVariadicRes && amc.m_isMustTailCall)
        {
            ReleaseAssert(!found2);
            found2 = true;

            ReleaseAssert(amc.m_callOption == MakeCallOption::NoOption);
            ReleaseAssert(amc.m_args.size() == 2);
            ReleaseAssert(!amc.m_args[0].IsArgRange());
            ReleaseAssert(amc.m_args[0].GetArg() == targetFunction->getArg(1));
            ReleaseAssert(!amc.m_args[1].IsArgRange());
            ReleaseAssert(amc.m_args[1].GetArg() == targetFunction->getArg(0));
        }
        else if (!amc.m_isInPlaceCall && amc.m_passVariadicRes && !amc.m_isMustTailCall)
        {
            ReleaseAssert(!found3);
            found3 = true;

            ReleaseAssert(amc.m_callOption == MakeCallOption::DontProfileInInterpreter);
            ReleaseAssert(amc.m_args.size() == 4);
            ReleaseAssert(!amc.m_args[0].IsArgRange());
            ReleaseAssert(amc.m_args[0].GetArg() == targetFunction->getArg(0));
            ReleaseAssert(!amc.m_args[1].IsArgRange());
            ReleaseAssert(amc.m_args[1].GetArg() == targetFunction->getArg(1));
            ReleaseAssert(!amc.m_args[2].IsArgRange());
            ReleaseAssert(amc.m_args[2].GetArg() == targetFunction->getArg(0));
            ReleaseAssert(!amc.m_args[3].IsArgRange());
            ReleaseAssert(amc.m_args[3].GetArg() == targetFunction->getArg(1));
        }
        else
        {
            ReleaseAssert(false);
        }
    }
    ReleaseAssert(found1 && found2 && found3);

    helper.CheckIsExpected(functionName);
}

TEST(DeegenAst, MakeCallAPIParser_2)
{
    TestHelper helper("make_call_api_parser" /*fileName*/);
    std::string functionName = "testfn2";

    Module* module = helper.moduleHolder.get();

    AstMakeCall::PreprocessModule(module);

    Function* targetFunction = module->getFunction(functionName);
    ReleaseAssert(targetFunction != nullptr);

    std::vector<AstMakeCall> res = AstMakeCall::GetAllUseInFunction(targetFunction);
    ReleaseAssert(res.size() == 2);

    bool found1 = false, found2 = false;
    for (AstMakeCall& amc : res)
    {
        if (amc.m_isInPlaceCall && !amc.m_passVariadicRes && !amc.m_isMustTailCall)
        {
            ReleaseAssert(!found1);
            found1 = true;

            ReleaseAssert(amc.m_callOption == MakeCallOption::NoOption);
            ReleaseAssert(amc.m_args.size() == 1);
            ReleaseAssert(amc.m_args[0].IsArgRange());
            ReleaseAssert(amc.m_args[0].GetArgNum() == targetFunction->getArg(1));

        }
        else if (!amc.m_isInPlaceCall && amc.m_passVariadicRes && amc.m_isMustTailCall)
        {
            ReleaseAssert(!found2);
            found2 = true;

            ReleaseAssert(amc.m_callOption == MakeCallOption::NoOption);
            ReleaseAssert(amc.m_args.size() == 3);
            ReleaseAssert(!amc.m_args[0].IsArgRange());
            ReleaseAssert(amc.m_args[1].IsArgRange());
            ReleaseAssert(amc.m_args[1].GetArgStart() == targetFunction->getArg(0));
            ReleaseAssert(amc.m_args[1].GetArgNum() == targetFunction->getArg(1));
            ReleaseAssert(!amc.m_args[2].IsArgRange());
        }
        else
        {
            ReleaseAssert(false);
        }
    }
    ReleaseAssert(found1 && found2);

    helper.CheckIsExpected(functionName);
}
