#include "gtest/gtest.h"

#include "ptr_utils.h"
#include "test_util_helper.h"
#include "annotated/unit_test/unit_test_ir_accessor.h"
#include "misc_llvm_helper.h"
#include "test_util_llvm_jit.h"

#include "deegen_ast_inline_cache.h"
#include "deegen_analyze_lambda_capture_pass.h"

using namespace llvm;
using namespace dast;

TEST(DeegenAst, InlineCacheAPILowering_1)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder = std::make_unique<LLVMContext>();
    LLVMContext& ctx = *llvmCtxHolder.get();
    std::unique_ptr<Module> moduleHolder = GetDeegenUnitTestLLVMIR(ctx, "ic_api_sanity");
    std::string functionName = "testfn1";

    Module* module = moduleHolder.get();

    DeegenAnalyzeLambdaCapturePass::AddAnnotations(module);
    DesugarAndSimplifyLLVMModule(module, DesugaringLevel::PerFunctionSimplifyOnlyAggresive);
    AstInlineCache::PreprocessModule(module);
    DeegenAnalyzeLambdaCapturePass::RemoveAnnotations(module);
    DesugarAndSimplifyLLVMModule(module, DesugaringLevel::PerFunctionSimplifyOnlyAggresive);

    Function* targetFunction = module->getFunction(functionName);
    ReleaseAssert(targetFunction != nullptr);
    std::vector<AstInlineCache> list = AstInlineCache::GetAllUseInFunction(targetFunction);
    ReleaseAssert(list.size() == 1);
    list[0].DoLoweringForInterpreter();
    DesugarAndSimplifyLLVMModule(module, DesugaringLevel::PerFunctionSimplifyOnlyAggresive);

    BytecodeMetadataStructBase::StructInfo icStateInfo = list[0].m_icStruct->FinalizeStructAndAssignOffsets();
    ReleaseAssert(icStateInfo.alignment == 1);
    ReleaseAssert(icStateInfo.allocSize == 9);

    list[0].m_icStruct->LowerAll(module);

    uint8_t ptrBuffer[100];
    memset(ptrBuffer, 0xcd, 100);

    uint8_t* ptr = ptrBuffer + 50;
    UnalignedStore<uint32_t>(ptr, 123);

    auto assertBufferNotClobbered = [&]()
    {
        for (size_t i = 0; i < 100; i++)
        {
            if (50 <= i && i < 59)
            {
                continue;
            }
            ReleaseAssert(ptrBuffer[i] == 0xcd);
        }
    };

    Function* ptrGetter = list[0].m_icPtrOrigin->getCalledFunction();
    ReleaseAssert(ptrGetter->empty());
    BasicBlock* bb = BasicBlock::Create(ctx, "", ptrGetter);
    IntToPtrInst* itpi = new IntToPtrInst(CreateLLVMConstantInt<uint64_t>(ctx, reinterpret_cast<uint64_t>(ptr)), llvm_type_of<void*>(ctx), "", bb);
    ReturnInst::Create(ctx, itpi, bb);

    ValidateLLVMModule(module);

    ptrGetter->addFnAttr(Attribute::AttrKind::AlwaysInline);
    list[0].m_bodyFn->addFnAttr(Attribute::AttrKind::AlwaysInline);
    RunLLVMOptimizePass(module);

    std::unique_ptr<Module> extractedModule = ExtractFunction(module, functionName);

    SimpleJIT jit(extractedModule.get());
    void* testFnAddr = jit.GetFunction(functionName);

    using FnProto = uint32_t(*)(uint32_t, uint32_t);
    FnProto testFn = reinterpret_cast<FnProto>(testFnAddr);

    auto doTest = [&](uint32_t p1, uint32_t p2, uint32_t expectedRes)
    {
        uint32_t res = testFn(p1, p2);
        ReleaseAssert(res == expectedRes);
        assertBufferNotClobbered();
    };

    doTest(12, 34, 234 + 12 + 34);
    doTest(12, 45, 234 + 12 + 45);
    doTest(12, 67, 234 + 12 + 67);
    doTest(34, 56, 256 + 34 + 56);
    doTest(34, 67, 256 + 34 + 67);
    doTest(12, 56, 256 + 12 + 56);
    doTest(12, 67, 256 + 12 + 67);
    doTest(34, 78, 278 + 34 + 78);
    doTest(100, 56, 100);
    doTest(100, 67, 100);
    doTest(100, 78, 100);
    doTest(120, 78, 120);
    doTest(140, 78, 140);
    doTest(160, 89, 160);
    doTest(180, 333, 180);
    doTest(180, 33, 180);
    doTest(12, 34, 234 + 12 + 34);
    doTest(12, 45, 234 + 12 + 45);
    doTest(12, 67, 234 + 12 + 67);
    doTest(34, 56, 256 + 34 + 56);
    doTest(34, 67, 256 + 34 + 67);
    doTest(12, 56, 256 + 12 + 56);
    doTest(12, 67, 256 + 12 + 67);
    doTest(34, 78, 278 + 34 + 78);
    doTest(100, 56, 100);
    doTest(100, 67, 100);
    doTest(100, 78, 100);
    doTest(120, 78, 120);
    doTest(140, 78, 140);
    doTest(160, 89, 160);
    doTest(180, 333, 180);
    doTest(180, 33, 180);
}

