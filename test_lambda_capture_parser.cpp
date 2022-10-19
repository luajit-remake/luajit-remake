#include "gtest/gtest.h"

#include "test_util_helper.h"
#include "annotated/unit_test/unit_test_ir_accessor.h"
#include "misc_llvm_helper.h"

#include "deegen_analyze_lambda_capture_pass.h"

using namespace llvm;
using namespace dast;

using CaptureKind = DeegenAnalyzeLambdaCapturePass::CaptureKind;

namespace {

struct CaptureDesc
{
    CaptureDesc() : m_ordInCaptureStruct(static_cast<size_t>(-1)), m_captureKind(CaptureKind::Invalid), m_value(nullptr), m_ordInParent(static_cast<size_t>(-1)) { }

    size_t m_ordInCaptureStruct;
    CaptureKind m_captureKind;
    llvm::Value* m_value;
    size_t m_ordInParent;
};

struct TestHelper
{
    TestHelper(std::string fileName)
    {
        llvmCtxHolder = std::make_unique<LLVMContext>();
        LLVMContext& ctx = *llvmCtxHolder.get();

        moduleHolder = GetDeegenUnitTestLLVMIR(ctx, fileName);
    }

    static std::vector<CaptureDesc> WARN_UNUSED ParseCaptureDesc(llvm::Function* func)
    {
        CallInst* target = nullptr;
        for (BasicBlock& bb : *func)
        {
            for (Instruction& inst : bb)
            {
                if (isa<CallInst>(&inst))
                {
                    CallInst* ci = cast<CallInst>(&inst);
                    Function* callee = ci->getCalledFunction();
                    if (callee != nullptr)
                    {
                        if (DeegenAnalyzeLambdaCapturePass::IsAnnotationFunction(callee->getName().str()))
                        {
                            ReleaseAssert(target == nullptr);
                            target = ci;
                        }
                    }
                }
            }
        }
        ReleaseAssert(target != nullptr);

        std::vector<CaptureDesc> finalRes;

        Function* callee = target->getCalledFunction();
        ReleaseAssert(callee != nullptr);
        ReleaseAssert(callee->arg_size() % 3 == 1);
        for (uint32_t i = 1; i < callee->arg_size(); i += 3)
        {
            Value* arg1 = target->getArgOperand(i);
            Value* arg2 = target->getArgOperand(i + 1);
            Value* arg3 = target->getArgOperand(i + 2);
            ReleaseAssert(llvm_value_has_type<uint64_t>(arg1));
            ReleaseAssert(llvm_value_has_type<uint64_t>(arg2));
            ReleaseAssert(isa<ConstantInt>(arg1));
            size_t ordInStruct = GetValueOfLLVMConstantInt<uint64_t>(arg1);
            ReleaseAssert(isa<ConstantInt>(arg2));
            CaptureKind captureKind = GetValueOfLLVMConstantInt<CaptureKind>(arg2);
            ReleaseAssert(static_cast<uint64_t>(captureKind) < static_cast<uint64_t>(CaptureKind::Invalid));

            CaptureDesc desc;
            desc.m_ordInCaptureStruct = ordInStruct;
            desc.m_captureKind = captureKind;

            if (captureKind == CaptureKind::ByRefCaptureOfLocalVar)
            {
                ReleaseAssert(llvm_value_has_type<void*>(arg3));
                ReleaseAssert(isa<AllocaInst>(arg3));
                desc.m_value = cast<AllocaInst>(arg3);
            }
            else if (captureKind == CaptureKind::ByValueCaptureOfLocalVar)
            {
                desc.m_value = arg3;
            }
            else
            {
                ReleaseAssert(llvm_value_has_type<uint64_t>(arg3));
                desc.m_ordInParent = GetValueOfLLVMConstantInt<uint64_t>(arg3);
            }

            finalRes.push_back(desc);
        }
        return finalRes;
    }

    llvm::Function* GetTestFnOuter(std::string testFnName)
    {
        Function* fn = moduleHolder->getFunction(testFnName);
        ReleaseAssert(fn != nullptr);
        return fn;
    }

    llvm::Function* GetTestFnInner(std::string testFnName)
    {
        std::string expectedPrefix = testFnName + "::$_";
        std::string expectedSuffix = "::operator()() const";
        for (Function& fn : *moduleHolder.get())
        {
            if (IsCXXSymbol(fn.getName().str()))
            {
                std::string symName = DemangleCXXSymbol(fn.getName().str());
                if (symName.starts_with(expectedPrefix) && symName.ends_with(expectedSuffix))
                {
                    std::string midPart = symName.substr(expectedPrefix.length(), symName.length() - expectedPrefix.length() - expectedSuffix.length());
                    bool ok = true;
                    for (size_t i = 0; i < midPart.length(); i++)
                    {
                        if (midPart[i] < '0' || midPart[i] > '9')
                        {
                            ok = false;
                            break;
                        }
                    }
                    if (ok)
                    {
                        return &fn;
                    }
                }
            }
        }
        ReleaseAssert(false);
    }

    std::unique_ptr<LLVMContext> llvmCtxHolder;
    std::unique_ptr<Module> moduleHolder;
};

void SanityTestCheck(llvm::Function* fn, CaptureKind expectCaptureKind)
{
    std::vector<CaptureDesc> desc = TestHelper::ParseCaptureDesc(fn);
    ReleaseAssert(desc.size() == 1);
    ReleaseAssert(desc[0].m_ordInCaptureStruct == 0);
    ReleaseAssert(desc[0].m_captureKind == expectCaptureKind);
    if (expectCaptureKind != CaptureKind::ByRefCaptureOfLocalVar && expectCaptureKind != CaptureKind::ByValueCaptureOfLocalVar)
    {
        ReleaseAssert(desc[0].m_ordInParent == 0);
    }
}

}   // anonymous namespace

TEST(DeegenAst, LambdaCaptureParser_1)
{
    TestHelper helper("lambda_capture_parser_sanity" /*fileName*/);
    std::string testFnName = "testfn01";
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(helper.moduleHolder.get());
    {
        Function* fn = helper.GetTestFnOuter(testFnName);
        std::vector<CaptureDesc> desc = TestHelper::ParseCaptureDesc(fn);
        ReleaseAssert(desc.size() == 5);
        ReleaseAssert(desc[0].m_ordInCaptureStruct == 0);
        ReleaseAssert(desc[0].m_captureKind == CaptureKind::ByValueCaptureOfLocalVar);
        ReleaseAssert(desc[1].m_ordInCaptureStruct == 1);
        ReleaseAssert(desc[1].m_captureKind == CaptureKind::ByRefCaptureOfLocalVar);
        ReleaseAssert(desc[2].m_ordInCaptureStruct == 2);
        ReleaseAssert(desc[2].m_captureKind == CaptureKind::ByValueCaptureOfLocalVar);
        ReleaseAssert(desc[3].m_ordInCaptureStruct == 3);
        ReleaseAssert(desc[3].m_captureKind == CaptureKind::ByRefCaptureOfLocalVar);
        ReleaseAssert(desc[4].m_ordInCaptureStruct == 4);
        ReleaseAssert(desc[4].m_captureKind == CaptureKind::ByValueCaptureOfLocalVar);
    }
    {
        Function* fn = helper.GetTestFnInner(testFnName);
        std::vector<CaptureDesc> desc = TestHelper::ParseCaptureDesc(fn);
        ReleaseAssert(desc.size() == 6);
        ReleaseAssert(desc[0].m_ordInCaptureStruct == 0);
        ReleaseAssert(desc[0].m_captureKind == CaptureKind::ByRefCaptureOfByValue);
        ReleaseAssert(desc[0].m_ordInParent == 0);
        ReleaseAssert(desc[1].m_ordInCaptureStruct == 1);
        ReleaseAssert(desc[1].m_captureKind == CaptureKind::ByRefCaptureOfLocalVar);
        ReleaseAssert(desc[2].m_ordInCaptureStruct == 2);
        ReleaseAssert(desc[2].m_captureKind == CaptureKind::ByValueCaptureOfByRef);
        ReleaseAssert(desc[2].m_ordInParent == 1);
        ReleaseAssert(desc[3].m_ordInCaptureStruct == 3);
        ReleaseAssert(desc[3].m_captureKind == CaptureKind::SameCaptureKindAsEnclosingLambda);
        ReleaseAssert(desc[3].m_ordInParent == 3);
        ReleaseAssert(desc[4].m_ordInCaptureStruct == 4);
        ReleaseAssert(desc[4].m_captureKind == CaptureKind::SameCaptureKindAsEnclosingLambda);
        ReleaseAssert(desc[4].m_ordInParent == 2);
        ReleaseAssert(desc[5].m_ordInCaptureStruct == 5);
        ReleaseAssert(desc[5].m_captureKind == CaptureKind::ByValueCaptureOfLocalVar);
    }
}

TEST(DeegenAst, LambdaCaptureParser_2)
{
    TestHelper helper("lambda_capture_parser_sanity" /*fileName*/);
    std::string testFnName = "testfn02";
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(helper.moduleHolder.get());
    SanityTestCheck(helper.GetTestFnOuter(testFnName), CaptureKind::ByRefCaptureOfLocalVar);
    SanityTestCheck(helper.GetTestFnInner(testFnName), CaptureKind::ByValueCaptureOfLocalVar);
}

TEST(DeegenAst, LambdaCaptureParser_3)
{
    TestHelper helper("lambda_capture_parser_sanity" /*fileName*/);
    std::string testFnName = "testfn03";
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(helper.moduleHolder.get());
    SanityTestCheck(helper.GetTestFnOuter(testFnName), CaptureKind::ByRefCaptureOfLocalVar);
    SanityTestCheck(helper.GetTestFnInner(testFnName), CaptureKind::ByRefCaptureOfLocalVar);
}

TEST(DeegenAst, LambdaCaptureParser_4)
{
    TestHelper helper("lambda_capture_parser_sanity" /*fileName*/);
    std::string testFnName = "testfn04";
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(helper.moduleHolder.get());
    SanityTestCheck(helper.GetTestFnOuter(testFnName), CaptureKind::ByValueCaptureOfLocalVar);
    SanityTestCheck(helper.GetTestFnInner(testFnName), CaptureKind::ByValueCaptureOfLocalVar);
}

TEST(DeegenAst, LambdaCaptureParser_5)
{
    TestHelper helper("lambda_capture_parser_sanity" /*fileName*/);
    std::string testFnName = "testfn05";
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(helper.moduleHolder.get());
    SanityTestCheck(helper.GetTestFnOuter(testFnName), CaptureKind::ByValueCaptureOfLocalVar);
    SanityTestCheck(helper.GetTestFnInner(testFnName), CaptureKind::ByRefCaptureOfLocalVar);
}

TEST(DeegenAst, LambdaCaptureParser_6)
{
    TestHelper helper("lambda_capture_parser_sanity" /*fileName*/);
    std::string testFnName = "testfn06";
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(helper.moduleHolder.get());
    SanityTestCheck(helper.GetTestFnOuter(testFnName), CaptureKind::ByValueCaptureOfLocalVar);
    SanityTestCheck(helper.GetTestFnInner(testFnName), CaptureKind::ByRefCaptureOfByValue);
}

TEST(DeegenAst, LambdaCaptureParser_7)
{
    TestHelper helper("lambda_capture_parser_sanity" /*fileName*/);
    std::string testFnName = "testfn07";
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(helper.moduleHolder.get());
    SanityTestCheck(helper.GetTestFnOuter(testFnName), CaptureKind::ByValueCaptureOfLocalVar);
    SanityTestCheck(helper.GetTestFnInner(testFnName), CaptureKind::SameCaptureKindAsEnclosingLambda);
}

TEST(DeegenAst, LambdaCaptureParser_8)
{
    TestHelper helper("lambda_capture_parser_sanity" /*fileName*/);
    std::string testFnName = "testfn08";
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(helper.moduleHolder.get());
    SanityTestCheck(helper.GetTestFnOuter(testFnName), CaptureKind::ByRefCaptureOfLocalVar);
    SanityTestCheck(helper.GetTestFnInner(testFnName), CaptureKind::ByValueCaptureOfByRef);
}

TEST(DeegenAst, LambdaCaptureParser_9)
{
    TestHelper helper("lambda_capture_parser_sanity" /*fileName*/);
    std::string testFnName = "testfn09";
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(helper.moduleHolder.get());
    SanityTestCheck(helper.GetTestFnOuter(testFnName), CaptureKind::ByRefCaptureOfLocalVar);
    SanityTestCheck(helper.GetTestFnInner(testFnName), CaptureKind::SameCaptureKindAsEnclosingLambda);
}

TEST(DeegenAst, LambdaCaptureParser_10)
{
    TestHelper helper("lambda_capture_parser_sanity" /*fileName*/);
    std::string testFnName = "testfn10";
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(helper.moduleHolder.get());
    SanityTestCheck(helper.GetTestFnOuter(testFnName), CaptureKind::ByRefCaptureOfLocalVar);
    SanityTestCheck(helper.GetTestFnInner(testFnName), CaptureKind::ByValueCaptureOfLocalVar);
}

TEST(DeegenAst, LambdaCaptureParser_11)
{
    TestHelper helper("lambda_capture_parser_sanity" /*fileName*/);
    std::string testFnName = "testfn11";
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(helper.moduleHolder.get());
    SanityTestCheck(helper.GetTestFnOuter(testFnName), CaptureKind::ByRefCaptureOfLocalVar);
    SanityTestCheck(helper.GetTestFnInner(testFnName), CaptureKind::ByRefCaptureOfLocalVar);
}

TEST(DeegenAst, LambdaCaptureParser_12)
{
    TestHelper helper("lambda_capture_parser_sanity" /*fileName*/);
    std::string testFnName = "testfn12";
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(helper.moduleHolder.get());
    SanityTestCheck(helper.GetTestFnOuter(testFnName), CaptureKind::ByValueCaptureOfLocalVar);
    SanityTestCheck(helper.GetTestFnInner(testFnName), CaptureKind::ByRefCaptureOfLocalVar);
}

TEST(DeegenAst, LambdaCaptureParser_13)
{
    TestHelper helper("lambda_capture_parser_sanity" /*fileName*/);
    std::string testFnName = "testfn13";
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(helper.moduleHolder.get());
    SanityTestCheck(helper.GetTestFnOuter(testFnName), CaptureKind::ByValueCaptureOfLocalVar);
    SanityTestCheck(helper.GetTestFnInner(testFnName), CaptureKind::ByValueCaptureOfLocalVar);
}

TEST(DeegenAst, LambdaCaptureParser_14)
{
    TestHelper helper("lambda_capture_parser_sanity" /*fileName*/);
    std::string testFnName = "testfn14";
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(helper.moduleHolder.get());
    SanityTestCheck(helper.GetTestFnOuter(testFnName), CaptureKind::ByRefCaptureOfLocalVar);
    SanityTestCheck(helper.GetTestFnInner(testFnName), CaptureKind::ByValueCaptureOfByRef);
}

TEST(DeegenAst, LambdaCaptureParser_15)
{
    TestHelper helper("lambda_capture_parser_sanity" /*fileName*/);
    std::string testFnName = "testfn15";
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(helper.moduleHolder.get());
    SanityTestCheck(helper.GetTestFnOuter(testFnName), CaptureKind::ByValueCaptureOfLocalVar);
    SanityTestCheck(helper.GetTestFnInner(testFnName), CaptureKind::SameCaptureKindAsEnclosingLambda);
}

TEST(DeegenAst, LambdaCaptureParser_16)
{
    TestHelper helper("lambda_capture_parser_sanity" /*fileName*/);
    std::string testFnName = "testfn16";
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(helper.moduleHolder.get());
    SanityTestCheck(helper.GetTestFnOuter(testFnName), CaptureKind::ByValueCaptureOfLocalVar);
    SanityTestCheck(helper.GetTestFnInner(testFnName), CaptureKind::ByRefCaptureOfByValue);
}

TEST(DeegenAst, LambdaCaptureParser_17)
{
    TestHelper helper("lambda_capture_parser_sanity" /*fileName*/);
    std::string testFnName = "testfn17";
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(helper.moduleHolder.get());
    SanityTestCheck(helper.GetTestFnOuter(testFnName), CaptureKind::ByRefCaptureOfLocalVar);
    SanityTestCheck(helper.GetTestFnInner(testFnName), CaptureKind::SameCaptureKindAsEnclosingLambda);
}
