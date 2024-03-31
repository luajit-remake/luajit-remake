#include "gtest/gtest.h"

#include "deegen_api.h"
#include "annotated/unit_test/unit_test_ir_accessor.h"

#include "misc_llvm_helper.h"
#include "runtime_utils.h"
#include "tvalue_typecheck_optimization.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_bytecode_operand.h"
#include "deegen_ast_make_call.h"

#include "test_util_llvm_jit.h"

using namespace dast;

namespace {

std::unique_ptr<llvm::Module> WARN_UNUSED GetTestCase(llvm::LLVMContext& ctx, size_t testcaseNum)
{
    using namespace llvm;
    std::unique_ptr<Module> module = GetDeegenUnitTestLLVMIR(ctx, "make_call_api_lowering");

    DesugarAndSimplifyLLVMModule(module.get(), DesugaringLevel::PerFunctionSimplifyOnly);
    AstMakeCall::PreprocessModule(module.get());

    std::vector<std::vector<std::unique_ptr<BytecodeVariantDefinition>>> defs = BytecodeVariantDefinition::ParseAllFromModule(module.get());

    ReleaseAssert(defs.size() >= testcaseNum);
    ReleaseAssert(defs[testcaseNum - 1].size() > 0);
    auto& target = defs[testcaseNum - 1][0];
    target->m_isInterpreterCallIcExplicitlyDisabled = true;
    target->SetMaxOperandWidthBytes(4);

    Function* implFunc = module->getFunction(target->m_implFunctionName);
    BytecodeIrInfo bii = BytecodeIrInfo::Create(target.get(), implFunc);
    return InterpreterBytecodeImplCreator::DoLoweringForAll(bii);
}

struct ExpectedResult
{
    CoroutineRuntimeContext* m_expectedCoroCtx;
    uint64_t* m_stackStart;
    uint64_t* m_expectedCallFrameBase;
    uint64_t m_expectedNumArgs;
    uint64_t m_expectedCb;
    uint64_t m_expectedIsMustTail;
    std::vector<uint64_t> m_expectedStackContent;
    bool m_checkerFnCalled;
};

ExpectedResult g_expectedResult;

void ResultChecker(CoroutineRuntimeContext* coroCtx, uint64_t* stackBase, uint64_t numArgs, uint64_t cb, uint64_t tagRegister1, uint64_t /*unused1*/, uint64_t isMustTail, uint64_t /*unused2*/, uint64_t /*unused3*/, uint64_t tagRegister2)
{
    ReleaseAssert(tagRegister1 == TValue::x_int32Tag);
    ReleaseAssert(tagRegister2 == TValue::x_mivTag);
    ReleaseAssert(g_expectedResult.m_expectedCoroCtx == coroCtx);
    ReleaseAssert(g_expectedResult.m_expectedCallFrameBase == stackBase);
    ReleaseAssert(g_expectedResult.m_expectedNumArgs == numArgs);
    ReleaseAssert(g_expectedResult.m_expectedCb == cb);
    ReleaseAssert(g_expectedResult.m_expectedIsMustTail == isMustTail);
    ReleaseAssert(!g_expectedResult.m_checkerFnCalled);
    g_expectedResult.m_checkerFnCalled = true;

    for (size_t i = 0; i < g_expectedResult.m_expectedStackContent.size(); i++)
    {
        ReleaseAssert(g_expectedResult.m_expectedStackContent[i] == g_expectedResult.m_stackStart[i]);
    }
}

void TestModuleOneCase(llvm::Module* moduleIn,
                       size_t testcaseNum,
                       size_t numVarArgs, // # varargs in caller's vararg region
                       size_t numLocals, // # locals in caller's stack frame
                       size_t argRangeBegin, // the local ordinal of arg range begin
                       size_t argRangeLen, // the length of the arg range
                       ssize_t varResStartOffset, // the ordinal (relative to caller frame begin) for the varres
                       size_t numVarRes, // the length of variadic res
                       bool isMustTail,
                       bool passVarRes,
                       bool isInPlace,
                       // 0 = arg range, otherwise it is the value ordinal
                       const std::vector<int>& expectedArgComposition)
{
    using namespace llvm;
    VM* vm = VM::Create();
    Auto(vm->Destroy());

    std::string expectedFnName = "__deegen_interpreter_op_test" + std::to_string(testcaseNum) + "_0";
    ReleaseAssert(moduleIn->getFunction(expectedFnName) != nullptr);

    std::unique_ptr<Module> module = ExtractFunction(moduleIn, expectedFnName);
    LLVMContext& ctx = module->getContext();
    Function* func = module->getFunction(expectedFnName);
    ReleaseAssert(func != nullptr);
    ReleaseAssert(func->arg_size() == 16);
    ReleaseAssert(func->getCallingConv() == CallingConv::GHC);
    func->setCallingConv(CallingConv::C);

    std::string expectedRcName = expectedFnName + "_retcont_0";
    if (!isMustTail)
    {
        ReleaseAssert(module->getFunction(expectedRcName) != nullptr);
    }

    CodeBlock* calleeCb = TranslateToRawPointer(vm, vm->AllocFromSystemHeap(sizeof(CodeBlock) + 128).AsNoAssert<CodeBlock>());
    SystemHeapGcObjectHeader::Populate<ExecutableCode*>(calleeCb);

    calleeCb->m_bestEntryPoint = reinterpret_cast<void*>(ResultChecker);

    calleeCb->m_numUpvalues = 0;
    calleeCb->m_stackFrameNumSlots = 0;
    HeapPtr<FunctionObject> calleefo = FunctionObject::Create(vm, calleeCb).As();

    CodeBlock* callerCb = TranslateToRawPointer(vm, vm->AllocFromSystemHeap(sizeof(CodeBlock) + 128).AsNoAssert<CodeBlock>());
    SystemHeapGcObjectHeader::Populate<ExecutableCode*>(callerCb);
    callerCb->m_bestEntryPoint = nullptr;
    callerCb->m_numUpvalues = 0;
    callerCb->m_stackFrameNumSlots = static_cast<uint32_t>(numLocals);
    uint8_t* curBytecode = callerCb->GetBytecodeStream() + 50;
    HeapPtr<FunctionObject> callerfo = FunctionObject::Create(vm, callerCb).As();

    std::unordered_map<Instruction*, Value*> replaceInstByValueMap;
    std::unordered_map<Instruction*, Instruction*> replaceInstByInstMap;
    for (BasicBlock& bb : *func)
    {
        for (Instruction& inst : bb)
        {
            CallInst* callInst = dyn_cast<CallInst>(&inst);
            if (callInst != nullptr)
            {
                Function* callee = callInst->getCalledFunction();
                if (callee != nullptr)
                {
                    std::string calleeName = callee->getName().str();
                    if (IsCXXSymbol(calleeName))
                    {
                        std::string demangledName = DemangleCXXSymbol(calleeName);
                        if (demangledName.starts_with("a") && demangledName.ends_with("()") && demangledName.length() == 4)
                        {
                            char c = demangledName[1];
                            if ('1' <= c && c <= '9')
                            {
                                int32_t unboxedI32ValToReplace = static_cast<int>(c - '0') + 1000;
                                uint64_t boxedValue = TValue::CreateInt32(unboxedI32ValToReplace).m_value;
                                ReleaseAssert(!replaceInstByValueMap.count(&inst));
                                ReleaseAssert(!replaceInstByInstMap.count(&inst));
                                replaceInstByValueMap[&inst] = CreateLLVMConstantInt<uint64_t>(ctx, boxedValue);
                            }
                        }
                        else if (demangledName == "callee()")
                        {
                            Constant* ptrVal = CreateLLVMConstantInt<uint64_t>(ctx, reinterpret_cast<uint64_t>(calleefo));
                            Instruction* replaceInst = new IntToPtrInst(ptrVal, llvm_type_of<HeapPtr<void>>(ctx));
                            ReleaseAssert(inst.getType() == replaceInst->getType());
                            ReleaseAssert(!replaceInstByValueMap.count(&inst));
                            ReleaseAssert(!replaceInstByInstMap.count(&inst));
                            replaceInstByInstMap[&inst] = replaceInst;
                        }
                        else if (demangledName == "r1()")
                        {
                            Argument* sb = func->getArg(1);
                            ReleaseAssert(sb->getName().str() == "stackBase");
                            Instruction* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx) /*pointeeType*/, sb, { CreateLLVMConstantInt<uint64_t>(ctx, reinterpret_cast<uint64_t>(argRangeBegin)) });
                            ReleaseAssert(inst.getType() == gep->getType());
                            ReleaseAssert(!replaceInstByValueMap.count(&inst));
                            ReleaseAssert(!replaceInstByInstMap.count(&inst));
                            replaceInstByInstMap[&inst] = gep;
                        }
                        else if (demangledName == "s1()")
                        {
                            Constant* val = CreateLLVMConstantInt<uint64_t>(ctx, argRangeLen);
                            ReleaseAssert(!replaceInstByValueMap.count(&inst));
                            ReleaseAssert(!replaceInstByInstMap.count(&inst));
                            replaceInstByValueMap[&inst] = val;
                        }
                    }
                }
                if (callInst->getCallingConv() == CallingConv::GHC)
                {
                    ReleaseAssert(callee == nullptr);
                    ReleaseAssert(callInst->isMustTailCall());
                    ReleaseAssert(callInst->arg_size() == 16);
                    callInst->setCallingConv(CallingConv::C);
                }
            }
        }
    }

    for (auto& it : replaceInstByInstMap)
    {
        Instruction* inst = it.first;
        Instruction* other = it.second;
        ReplaceInstWithInst(inst, other);
    }

    for (auto& it : replaceInstByValueMap)
    {
        Instruction* inst = it.first;
        Value* other = it.second;
        inst->replaceAllUsesWith(other);
        inst->eraseFromParent();
    }

    if (!isMustTail)
    {
        Function* rcFunc = module->getFunction(expectedRcName);
        ReleaseAssert(rcFunc != nullptr);
        ReleaseAssert(rcFunc->empty());
        BasicBlock* bb = BasicBlock::Create(ctx, "", rcFunc);
        Function* trapIntrin = Intrinsic::getDeclaration(module.get(), Intrinsic::trap, { });
        std::ignore = CallInst::Create(trapIntrin, "", bb);
        std::ignore = new UnreachableInst(ctx, bb);
    }

    ValidateLLVMModule(module.get());

    SimpleJIT jit(module.get());
    jit.AllowAccessWhitelistedHostSymbolsOnly();
    jit.AddAllowedHostSymbol("memcpy");
    jit.AddAllowedHostSymbol("memmove");
    void* testFnAddr = jit.GetFunction(expectedFnName);
    void* rcFnAddr = nullptr;
    if (!isMustTail)
    {
        rcFnAddr = jit.GetFunction(expectedRcName);
    }

    // Now, set up the environment, the stack and the expected results
    //
    CoroutineRuntimeContext* coroCtx = CoroutineRuntimeContext::Create(vm, UserHeapPointer<TableObject> {} /*globalObject*/);
    coroCtx->m_numVariadicRets = static_cast<uint32_t>(numVarRes);
    coroCtx->m_variadicRetSlotBegin = SafeIntegerCast<int32_t>(varResStartOffset);

    uint64_t* stack = reinterpret_cast<uint64_t*>(coroCtx->m_stackBegin);

    g_expectedResult.m_expectedCoroCtx = coroCtx;
    g_expectedResult.m_stackStart = stack;
    g_expectedResult.m_expectedCb = reinterpret_cast<uint64_t>(calleeCb);
    g_expectedResult.m_expectedIsMustTail = isMustTail;
    g_expectedResult.m_checkerFnCalled = false;

    StackFrameHeader* rootSfh = reinterpret_cast<StackFrameHeader*>(stack);
    rootSfh->m_caller = reinterpret_cast<void*>(1000000123);
    rootSfh->m_retAddr = reinterpret_cast<void*>(1000000234);
    rootSfh->m_func = reinterpret_cast<HeapPtr<FunctionObject>>(1000000345);
    rootSfh->m_callerBytecodePtr = 0;
    rootSfh->m_numVariadicArguments = 0;
    uint64_t* callerStackBegin = reinterpret_cast<uint64_t*>(rootSfh + 1);

    for (size_t i = 0; i < numVarArgs; i++)
    {
        callerStackBegin[i] = TValue::CreateInt32(static_cast<int32_t>(i + 10000)).m_value;
    }

    StackFrameHeader* callerSfh = reinterpret_cast<StackFrameHeader*>(callerStackBegin + numVarArgs);
    callerSfh->m_caller = rootSfh + 1;
    callerSfh->m_retAddr = reinterpret_cast<void*>(1000000456);
    callerSfh->m_func = callerfo;
    callerSfh->m_callerBytecodePtr = 0;
    callerSfh->m_numVariadicArguments = static_cast<uint32_t>(numVarArgs);

    uint64_t* callerLocalsBegin = reinterpret_cast<uint64_t*>(callerSfh + 1);

    for (size_t i = 0; i < numLocals; i++)
    {
        callerLocalsBegin[i] = TValue::CreateInt32(static_cast<int32_t>(i + 100000)).m_value;
    }

    if (isInPlace)
    {
        ReleaseAssert(argRangeBegin >= x_numSlotsForStackFrameHeader);
        ReleaseAssert(offsetof_member_v<&StackFrameHeader::m_func> == 0);
        callerLocalsBegin[argRangeBegin - x_numSlotsForStackFrameHeader] = reinterpret_cast<uint64_t>(calleefo);
    }

    if (numVarRes != static_cast<size_t>(-1))
    {
        if (varResStartOffset < 0)
        {
            ReleaseAssert(x_numSlotsForStackFrameHeader <= static_cast<size_t>(-varResStartOffset));
            ReleaseAssert(static_cast<size_t>(-varResStartOffset) <= numVarArgs + x_numSlotsForStackFrameHeader);
            ReleaseAssert(static_cast<ssize_t>(numVarRes) + varResStartOffset <= -static_cast<ssize_t>(x_numSlotsForStackFrameHeader));
        }
        else
        {
            ReleaseAssert(static_cast<size_t>(varResStartOffset) >= numLocals);
            size_t vrStart = static_cast<size_t>(varResStartOffset);
            for (size_t i = 0; i < numVarRes; i++)
            {
                callerLocalsBegin[i + vrStart] = TValue::CreateInt32(static_cast<int32_t>(i + 1000000)).m_value;
            }
        }
    }

    std::vector<uint64_t> expectedStack;
    expectedStack.resize(x_numSlotsForStackFrameHeader);
    if (!isMustTail)
    {
        StackFrameHeader* calleeSfh = reinterpret_cast<StackFrameHeader*>(expectedStack.data());
        calleeSfh->m_caller = callerSfh + 1;
        calleeSfh->m_retAddr = rcFnAddr;
        calleeSfh->m_func = calleefo;
        calleeSfh->m_callerBytecodePtr = curBytecode;
        calleeSfh->m_numVariadicArguments = 0;
    }
    else
    {
        StackFrameHeader* calleeSfh = reinterpret_cast<StackFrameHeader*>(expectedStack.data());
        *calleeSfh = *callerSfh;
        calleeSfh->m_func = calleefo;
        calleeSfh->m_numVariadicArguments = 0;
    }

    size_t totalArgs = 0;
    for (int kind : expectedArgComposition)
    {
        if (kind == 0)
        {
            ReleaseAssert(argRangeBegin <= numLocals);
            ReleaseAssert(argRangeBegin + argRangeLen <= numLocals);
            for (size_t i = 0; i < argRangeLen; i++)
            {
                expectedStack.push_back(callerLocalsBegin[i + argRangeBegin]);
                totalArgs++;
            }
        }
        else
        {
            ReleaseAssert(1 <= kind && kind <= 9);
            int32_t unboxedI32ValToReplace = kind + 1000;
            uint64_t boxedValue = TValue::CreateInt32(unboxedI32ValToReplace).m_value;
            expectedStack.push_back(boxedValue);
            totalArgs++;
        }
    }

    if (passVarRes)
    {
        ReleaseAssert(numVarRes != static_cast<size_t>(-1));
        for (size_t i = 0; i < numVarRes; i++)
        {
            expectedStack.push_back(callerLocalsBegin[static_cast<ssize_t>(i) + varResStartOffset]);
            totalArgs++;
        }
    }

    if (isMustTail)
    {
        g_expectedResult.m_expectedCallFrameBase = callerStackBegin + x_numSlotsForStackFrameHeader;
    }
    else if (isInPlace)
    {
        g_expectedResult.m_expectedCallFrameBase = callerLocalsBegin + argRangeBegin;
    }
    else
    {
        g_expectedResult.m_expectedCallFrameBase = callerLocalsBegin + numLocals + x_numSlotsForStackFrameHeader;
    }
    g_expectedResult.m_expectedNumArgs = totalArgs;

    g_expectedResult.m_expectedStackContent.clear();
    {
        uint64_t* tmp = stack;
        while (tmp < g_expectedResult.m_expectedCallFrameBase - x_numSlotsForStackFrameHeader)
        {
            g_expectedResult.m_expectedStackContent.push_back(*tmp);
            tmp++;
        }
    }

    for (uint64_t tmp : expectedStack)
    {
        g_expectedResult.m_expectedStackContent.push_back(tmp);
    }

    ReleaseAssert(!g_expectedResult.m_checkerFnCalled);

    using EntryFnType = void(*)(
        CoroutineRuntimeContext* /*coroCtx*/,
        uint64_t* /*sb*/,
        uint8_t* /*curbytecode*/,
        CodeBlock* /*cb*/,
        uint64_t /*tagRegister1*/,
        uint64_t /*unused*/,
        uint64_t /*unused*/,
        uint64_t /*unused*/,
        uint64_t /*unused*/,
        uint64_t /*tagRegister2*/);
    reinterpret_cast<EntryFnType>(testFnAddr)(coroCtx, callerLocalsBegin, curBytecode, callerCb, TValue::x_int32Tag, 0, 0, 0, 0, TValue::x_mivTag);

    ReleaseAssert(g_expectedResult.m_checkerFnCalled);
}

void TestModule(size_t testcaseNum,
                bool isMustTail,
                bool passVarRes,
                bool isInPlace,
                // 0 = arg range, otherwise it is the value ordinal
                const std::vector<int>& expectedArgComposition,
                size_t numCycles = 1)
{
    using namespace llvm;
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::unique_ptr<Module> moduleHolder = GetTestCase(ctx, testcaseNum);

    enum class ArgRangeKind
    {
        None,
        ForInPlaceCall,
        Normal
    };

    ArgRangeKind argRangeKind;
    if (isInPlace)
    {
        argRangeKind = ArgRangeKind::ForInPlaceCall;
        ReleaseAssert(expectedArgComposition.size() == 1 && expectedArgComposition[0] == 0);
    }
    else
    {
        argRangeKind = ArgRangeKind::None;
        for (int kind : expectedArgComposition)
        {
            if (kind == 0)
            {
                ReleaseAssert(argRangeKind == ArgRangeKind::None);
                argRangeKind = ArgRangeKind::Normal;
            }
        }
    }

    for (size_t currentCycle = 0; currentCycle < numCycles; currentCycle++)
    {
        std::vector<size_t> numLocalChoices { 0, 5, 10, 20 };
        std::vector<size_t> numVarArgChoices { 0, 5, 10 };

        if (currentCycle > 0)
        {
            for (size_t i = 0; i < numLocalChoices.size(); i++)
            {
                numLocalChoices[i] += static_cast<size_t>(rand()) % 4;
            }
            for (size_t i = 0; i < numVarArgChoices.size(); i++)
            {
                numVarArgChoices[i] += static_cast<size_t>(rand()) % 4;
            }
        }

        for (size_t numVarArgs : numVarArgChoices)
        {
            for (size_t numLocals : numLocalChoices)
            {
                if (argRangeKind == ArgRangeKind::ForInPlaceCall && numLocals < x_numSlotsForStackFrameHeader)
                {
                    continue;
                }
                std::vector<std::pair<size_t, size_t>> argRangeChoices;
                if (argRangeKind == ArgRangeKind::None)
                {
                    argRangeChoices.push_back({0, 0});
                }
                else if (argRangeKind == ArgRangeKind::ForInPlaceCall)
                {
                    for (size_t i = 0; i < 3; i++)
                    {
                        size_t start = static_cast<size_t>(rand()) % (numLocals - x_numSlotsForStackFrameHeader + 1) + x_numSlotsForStackFrameHeader;
                        size_t len = static_cast<size_t>(rand()) % (numLocals - start + 1);
                        argRangeChoices.push_back({ start, len });
                    }
                }
                else
                {
                    for (size_t i = 0; i < 3; i++)
                    {
                        size_t start = static_cast<size_t>(rand()) % (numLocals + 1);
                        size_t len = static_cast<size_t>(rand()) % (numLocals - start + 1);
                        argRangeChoices.push_back({ start, len });
                    }
                }
                for (auto& argRangeChoiceIt : argRangeChoices)
                {
                    size_t argRangeStart = argRangeChoiceIt.first;
                    size_t argRangeLen = argRangeChoiceIt.second;

                    std::vector<std::pair<ssize_t, size_t>> varResChoices;
                    if (!passVarRes)
                    {
                        varResChoices.push_back({ 0, static_cast<size_t>(-1) });
                    }
                    else
                    {
                        {
                            size_t varArgRegionOffset = static_cast<size_t>(rand()) % (numVarArgs + 1);
                            size_t len = static_cast<size_t>(rand()) % (numVarArgs - varArgRegionOffset + 1);
                            ssize_t varResOffset = static_cast<ssize_t>(varArgRegionOffset - numVarArgs - x_numSlotsForStackFrameHeader);
                            varResChoices.push_back({ varResOffset, len });
                        }

                        {
                            size_t lengthChoiceLimit = 10;
                            if (currentCycle > 0)
                            {
                                int dice = rand() % 3;
                                if (dice == 0) { lengthChoiceLimit += 5; }
                                if (dice == 1) { lengthChoiceLimit -= 5; }
                            }
                            size_t offsetChoice = 0;
                            if (currentCycle > 0)
                            {
                                offsetChoice = static_cast<size_t>(rand()) % 3;
                            }
                            size_t lengthChoice = static_cast<size_t>(rand()) % lengthChoiceLimit;
                            varResChoices.push_back({ static_cast<ssize_t>(numLocals + offsetChoice), lengthChoice });
                        }

                        {
                            size_t offsetChoice = 10;
                            if (currentCycle > 0)
                            {
                                offsetChoice = offsetChoice + static_cast<size_t>(rand()) % 7 - 3;
                            }
                            size_t lengthChoiceLimit = 10;
                            if (currentCycle > 0)
                            {
                                int dice = rand() % 3;
                                if (dice == 0) { lengthChoiceLimit += 5; }
                                if (dice == 1) { lengthChoiceLimit -= 5; }
                            }
                            size_t lengthChoice = static_cast<size_t>(rand()) % lengthChoiceLimit;
                            varResChoices.push_back({ static_cast<ssize_t>(numLocals + offsetChoice), lengthChoice });
                        }
                    }

                    for (auto& varResChoicesIt : varResChoices)
                    {
                        ssize_t varResOffset = varResChoicesIt.first;
                        size_t varResLen = varResChoicesIt.second;

                        TestModuleOneCase(moduleHolder.get(),
                                          testcaseNum,
                                          numVarArgs,
                                          numLocals,
                                          argRangeStart,
                                          argRangeLen,
                                          varResOffset,
                                          varResLen,
                                          isMustTail,
                                          passVarRes,
                                          isInPlace,
                                          expectedArgComposition);
                    }
                }
            }
        }
    }
}

}   // anonymous namespace

TEST(DeegenAst, MakeCallApiLowering_1)
{
    TestModule(1 /*testCaseNum*/,
               false /*isMustTail*/,
               false /*passVarRes*/,
               false /*isInPlace*/,
               { 1, 2, 3 } /*expectedArgComposition*/,
               3 /*numCycles*/);
}

TEST(DeegenAst, MakeCallApiLowering_2)
{
    TestModule(2 /*testCaseNum*/,
               false /*isMustTail*/,
               false /*passVarRes*/,
               false /*isInPlace*/,
               { } /*expectedArgComposition*/,
               3 /*numCycles*/);
}

TEST(DeegenAst, MakeCallApiLowering_3)
{
    TestModule(3 /*testCaseNum*/,
               false /*isMustTail*/,
               false /*passVarRes*/,
               false /*isInPlace*/,
               { 1, 2, 3, 4, 0, 5, 6, 7, 8 } /*expectedArgComposition*/,
               3 /*numCycles*/);
}

TEST(DeegenAst, MakeCallApiLowering_4)
{
    TestModule(4 /*testCaseNum*/,
               false /*isMustTail*/,
               false /*passVarRes*/,
               true /*isInPlace*/,
               { 0 } /*expectedArgComposition*/,
               3 /*numCycles*/);
}

TEST(DeegenAst, MakeCallApiLowering_5)
{
    TestModule(5 /*testCaseNum*/,
               false /*isMustTail*/,
               true /*passVarRes*/,
               true /*isInPlace*/,
               { 0 } /*expectedArgComposition*/,
               2 /*numCycles*/);
}

TEST(DeegenAst, MakeCallApiLowering_6)
{
    TestModule(6 /*testCaseNum*/,
               false /*isMustTail*/,
               true /*passVarRes*/,
               false /*isInPlace*/,
               { 1, 2, 3 } /*expectedArgComposition*/,
               3 /*numCycles*/);
}

TEST(DeegenAst, MakeCallApiLowering_7)
{
    TestModule(7 /*testCaseNum*/,
               false /*isMustTail*/,
               true /*passVarRes*/,
               false /*isInPlace*/,
               { } /*expectedArgComposition*/,
               3 /*numCycles*/);
}

TEST(DeegenAst, MakeCallApiLowering_8)
{
    TestModule(8 /*testCaseNum*/,
               false /*isMustTail*/,
               true /*passVarRes*/,
               false /*isInPlace*/,
               { 1, 2, 3, 4, 0, 5, 6, 7, 8 } /*expectedArgComposition*/,
               2 /*numCycles*/);
}

TEST(DeegenAst, MakeCallApiLowering_9)
{
    TestModule(9 /*testCaseNum*/,
               true /*isMustTail*/,
               false /*passVarRes*/,
               false /*isInPlace*/,
               { 1, 2, 3 } /*expectedArgComposition*/,
               3 /*numCycles*/);
}

TEST(DeegenAst, MakeCallApiLowering_10)
{
    TestModule(10 /*testCaseNum*/,
               true /*isMustTail*/,
               false /*passVarRes*/,
               false /*isInPlace*/,
               { } /*expectedArgComposition*/,
               3 /*numCycles*/);
}

TEST(DeegenAst, MakeCallApiLowering_11)
{
    TestModule(11 /*testCaseNum*/,
               true /*isMustTail*/,
               false /*passVarRes*/,
               false /*isInPlace*/,
               { 1, 2, 3, 4, 0, 5, 6, 7, 8 } /*expectedArgComposition*/,
               2 /*numCycles*/);
}

TEST(DeegenAst, MakeCallApiLowering_12)
{
    TestModule(12 /*testCaseNum*/,
               true /*isMustTail*/,
               false /*passVarRes*/,
               true /*isInPlace*/,
               { 0 } /*expectedArgComposition*/,
               3 /*numCycles*/);
}

TEST(DeegenAst, MakeCallApiLowering_13)
{
    TestModule(13 /*testCaseNum*/,
               true /*isMustTail*/,
               true /*passVarRes*/,
               true /*isInPlace*/,
               { 0 } /*expectedArgComposition*/,
               2 /*numCycles*/);
}

TEST(DeegenAst, MakeCallApiLowering_14)
{
    TestModule(14 /*testCaseNum*/,
               true /*isMustTail*/,
               true /*passVarRes*/,
               false /*isInPlace*/,
               { 1, 2, 3 } /*expectedArgComposition*/,
               3 /*numCycles*/);
}

TEST(DeegenAst, MakeCallApiLowering_15)
{
    TestModule(15 /*testCaseNum*/,
               true /*isMustTail*/,
               true /*passVarRes*/,
               false /*isInPlace*/,
               { } /*expectedArgComposition*/,
               3 /*numCycles*/);
}

TEST(DeegenAst, MakeCallApiLowering_16)
{
    TestModule(16 /*testCaseNum*/,
               true /*isMustTail*/,
               true /*passVarRes*/,
               false /*isInPlace*/,
               { 1, 2, 3, 4, 0, 5, 6, 7, 8 } /*expectedArgComposition*/,
               2 /*numCycles*/);
}
