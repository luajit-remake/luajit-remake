#include "gtest/gtest.h"

#include "deegen_api.h"

#include "deegen_function_entry_logic_creator.h"
#include "deegen_ast_return.h"
#include "runtime_utils.h"

#include "test_util_helper.h"
#include "test_util_llvm_jit.h"

using namespace llvm;
using namespace dast;

namespace {

struct ExpectedResult
{
    CoroutineRuntimeContext* m_expectedCoroCtx;
    uint64_t* m_stackStart;
    uint64_t* m_previousCallFrameEnd;
    uint64_t m_expectedNumVarArgs;
    CodeBlock* m_expectedCodeBlock;
    void* m_callerStackFrameBase;
    SystemHeapPointer<uint8_t> m_callerBytecodePtr;
    std::vector<uint64_t> m_expectedStackContentBeforeThisFrame;
    std::vector<uint64_t> m_arguments;
    uint64_t m_numFixedArgsAcceptedByCallee;
    bool m_calleeAcceptsVarArgs;
    bool m_isTailCall;
    bool m_checkerFnCalled;
};

ExpectedResult g_expectedResult;

void ResultChecker(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                   uint64_t arg4, uint64_t arg5, uint64_t arg6, uint64_t arg7,
                   uint64_t arg8, uint64_t arg9, uint64_t arg10, uint64_t arg11,
                   uint64_t arg12,
                   double arg13, double arg14, double arg15, double arg16, double arg17, double arg18)
{
    std::vector<uint64_t> args;
    args.push_back(arg0); args.push_back(arg1); args.push_back(arg2); args.push_back(arg3);
    args.push_back(arg4); args.push_back(arg5); args.push_back(arg6); args.push_back(arg7);
    args.push_back(arg8); args.push_back(arg9); args.push_back(arg10); args.push_back(arg11);
    args.push_back(arg12);
    args.push_back(cxx2a_bit_cast<uint64_t>(arg13)); args.push_back(cxx2a_bit_cast<uint64_t>(arg14));
    args.push_back(cxx2a_bit_cast<uint64_t>(arg15)); args.push_back(cxx2a_bit_cast<uint64_t>(arg16));
    args.push_back(cxx2a_bit_cast<uint64_t>(arg17)); args.push_back(cxx2a_bit_cast<uint64_t>(arg18));

    ReleaseAssert(args.size() == RegisterPinningScheme::GetFunctionTypeNumArguments());

    auto getValAsI64 = [&](X64Reg reg)
    {
        size_t argOrd = RegisterPinningScheme::GetArgumentOrdinalForRegister(reg);
        ReleaseAssert(argOrd < args.size());
        return args[argOrd];
    };

    auto checkValEqual = [&]<typename ValTy>(X64Reg reg, ValTy value)
    {
        ReleaseAssert(sizeof(ValTy) == 8);
        uint8_t valBytes[8];
        memcpy(valBytes, &value, 8);
        uint64_t valI64 = UnalignedLoad<uint64_t>(valBytes);

        ReleaseAssert(getValAsI64(reg) == valI64);
    };

    checkValEqual(RPV_TagRegister1::Reg(), TValue::x_int32Tag);
    checkValEqual(RPV_TagRegister2::Reg(), TValue::x_mivTag);
    checkValEqual(RPV_CoroContext::Reg(), g_expectedResult.m_expectedCoroCtx);
    checkValEqual(RPV_CodeBlock::Reg(), g_expectedResult.m_expectedCodeBlock);
    checkValEqual(RPV_CurBytecode::Reg(), g_expectedResult.m_expectedCodeBlock->m_bytecodeStream);

    uint64_t* stackBase = reinterpret_cast<uint64_t*>(getValAsI64(RPV_StackBase::Reg()));

    ReleaseAssert(!g_expectedResult.m_checkerFnCalled);
    g_expectedResult.m_checkerFnCalled = true;

    StackFrameHeader* hdr = reinterpret_cast<StackFrameHeader*>(stackBase) - 1;
    ReleaseAssert(hdr->m_numVariadicArguments == g_expectedResult.m_expectedNumVarArgs);
    if (!g_expectedResult.m_calleeAcceptsVarArgs)
    {
        ReleaseAssert(hdr->m_numVariadicArguments == 0);
    }

    if (g_expectedResult.m_isTailCall)
    {
        ReleaseAssert(g_expectedResult.m_previousCallFrameEnd == stackBase - x_numSlotsForStackFrameHeader - hdr->m_numVariadicArguments);
    }

    ReleaseAssert(reinterpret_cast<uint64_t>(hdr->m_func) == 12345678987654321ULL);
    ReleaseAssert(hdr->m_caller == g_expectedResult.m_callerStackFrameBase);
    ReleaseAssert(reinterpret_cast<uint64_t>(hdr->m_retAddr) == 22345678987654322ULL);
    ReleaseAssert(hdr->m_callerBytecodePtr.m_value == g_expectedResult.m_callerBytecodePtr.m_value);

    for (size_t i = 0; i < g_expectedResult.m_expectedStackContentBeforeThisFrame.size(); i++)
    {
        ReleaseAssert(g_expectedResult.m_stackStart[i] == g_expectedResult.m_expectedStackContentBeforeThisFrame[i]);
    }

    for (size_t i = 0; i < g_expectedResult.m_numFixedArgsAcceptedByCallee; i++)
    {
        if (i < g_expectedResult.m_arguments.size())
        {
            ReleaseAssert(stackBase[i] == g_expectedResult.m_arguments[i]);
        }
        else
        {
            ReleaseAssert(stackBase[i] == TValue::Nil().m_value);
        }
    }

    if (g_expectedResult.m_expectedNumVarArgs > 0)
    {
        ReleaseAssert(g_expectedResult.m_arguments.size() == g_expectedResult.m_numFixedArgsAcceptedByCallee + g_expectedResult.m_expectedNumVarArgs);
        uint64_t* vaStart = stackBase - x_numSlotsForStackFrameHeader - g_expectedResult.m_expectedNumVarArgs;
        for (size_t i = 0; i < g_expectedResult.m_expectedNumVarArgs; i++)
        {
            ReleaseAssert(vaStart[i] == g_expectedResult.m_arguments[g_expectedResult.m_numFixedArgsAcceptedByCallee + i]);
        }
    }
}

void TestOneCase(bool calleeAcceptsVarArgs, uint64_t numFixedArgs, bool isTailCall, uint64_t numProvidedArgs, void* testFnAddr)
{
    using namespace llvm;
    VM* vm = VM::Create();
    Auto(vm->Destroy());

    CodeBlock* calleeCb = TranslateToRawPointer(vm, vm->AllocFromSystemHeap(sizeof(CodeBlock) + 128).AsNoAssert<CodeBlock>());
    SystemHeapGcObjectHeader::Populate<ExecutableCode*>(calleeCb);

    calleeCb->m_numUpvalues = 0;
    calleeCb->m_stackFrameNumSlots = 0;
    calleeCb->m_numFixedArguments = static_cast<uint32_t>(numFixedArgs);
    calleeCb->m_interpreterTierUpCounter = 1LL << 62;
    memset(calleeCb->m_bytecodeStream, 0, 128);

    CoroutineRuntimeContext* coroCtx = CoroutineRuntimeContext::Create(vm, UserHeapPointer<TableObject> {} /*globalObject*/);

    uint64_t* stack = reinterpret_cast<uint64_t*>(coroCtx->m_stackBegin);

    g_expectedResult.m_expectedCoroCtx = coroCtx;
    g_expectedResult.m_stackStart = stack;
    g_expectedResult.m_expectedCodeBlock = calleeCb;
    if (numProvidedArgs > numFixedArgs && calleeAcceptsVarArgs)
    {
        g_expectedResult.m_expectedNumVarArgs = numProvidedArgs - numFixedArgs;
    }
    else
    {
        g_expectedResult.m_expectedNumVarArgs = 0;
    }
    g_expectedResult.m_checkerFnCalled = false;

    StackFrameHeader* rootSfh = reinterpret_cast<StackFrameHeader*>(stack);
    rootSfh->m_caller = reinterpret_cast<void*>(1000000123);
    rootSfh->m_retAddr = reinterpret_cast<void*>(1000000234);
    rootSfh->m_func = reinterpret_cast<HeapPtr<FunctionObject>>(1000000345);
    rootSfh->m_callerBytecodePtr = 0;
    rootSfh->m_numVariadicArguments = 0;
    uint64_t* previousFrameEnd = reinterpret_cast<uint64_t*>(rootSfh + 1);

    StackFrameHeader* hdr = reinterpret_cast<StackFrameHeader*>(previousFrameEnd);
    hdr->m_caller = rootSfh + 1;
    hdr->m_retAddr = reinterpret_cast<void*>(22345678987654322ULL);
    hdr->m_func = reinterpret_cast<HeapPtr<FunctionObject>>(12345678987654321ULL);
    hdr->m_callerBytecodePtr.m_value = 50;
    hdr->m_numVariadicArguments = 0;

    g_expectedResult.m_previousCallFrameEnd = previousFrameEnd;
    g_expectedResult.m_callerStackFrameBase = rootSfh + 1;
    g_expectedResult.m_callerBytecodePtr.m_value = 50;
    g_expectedResult.m_numFixedArgsAcceptedByCallee = numFixedArgs;
    g_expectedResult.m_calleeAcceptsVarArgs = calleeAcceptsVarArgs;
    g_expectedResult.m_isTailCall = isTailCall;

    uint64_t* callerLocalsBegin = reinterpret_cast<uint64_t*>(hdr + 1);

    g_expectedResult.m_arguments.clear();
    for (size_t i = 0; i < numProvidedArgs; i++)
    {
        uint64_t v = TValue::CreateInt32(static_cast<int32_t>(i + 100000)).m_value;
        callerLocalsBegin[i] = v;
        g_expectedResult.m_arguments.push_back(v);
    }

    g_expectedResult.m_expectedStackContentBeforeThisFrame.clear();
    {
        uint64_t* tmp = stack;
        while (tmp < previousFrameEnd)
        {
            g_expectedResult.m_expectedStackContentBeforeThisFrame.push_back(*tmp);
            tmp++;
        }
    }

    ReleaseAssert(!g_expectedResult.m_checkerFnCalled);

    std::vector<uint64_t> params;
    params.resize(RegisterPinningScheme::GetFunctionTypeNumArguments(), 0 /*value*/);
    auto setParam = [&]<typename ValTy>(X64Reg reg, ValTy value)
    {
        ReleaseAssert(sizeof(ValTy) == 8);
        uint8_t valBytes[8];
        memcpy(valBytes, &value, 8);
        uint64_t valI64 = UnalignedLoad<uint64_t>(valBytes);

        size_t argOrd = RegisterPinningScheme::GetArgumentOrdinalForRegister(reg);
        ReleaseAssert(argOrd < params.size());
        params[argOrd] = valI64;
    };

    HeapPtr<CodeBlock> calleeCbHeapPtr = TranslateToHeapPtr(calleeCb);

    setParam(RPV_TagRegister1::Reg(), TValue::x_int32Tag);
    setParam(RPV_TagRegister2::Reg(), TValue::x_mivTag);
    setParam(RPV_CodeBlock::Reg(), calleeCbHeapPtr);
    setParam(RPV_StackBase::Reg(), callerLocalsBegin);
    setParam(RPV_NumArgsAsPtr::Reg(), numProvidedArgs);
    setParam(RPV_IsMustTailCall::Reg(), static_cast<uint64_t>(isTailCall));
    setParam(RPV_CoroContext::Reg(), coroCtx);

    using EntryFnType = void(*)(
        uint64_t, uint64_t, uint64_t, uint64_t,
        uint64_t, uint64_t, uint64_t, uint64_t,
        uint64_t, uint64_t, uint64_t, uint64_t,
        uint64_t,
        double, double, double, double, double, double);

    ReleaseAssert(params.size() == 19);
    reinterpret_cast<EntryFnType>(testFnAddr)(
        params[0], params[1], params[2], params[3],
        params[4], params[5], params[6], params[7],
        params[8], params[9], params[10], params[11],
        params[12],
        cxx2a_bit_cast<double>(params[13]), cxx2a_bit_cast<double>(params[14]),
        cxx2a_bit_cast<double>(params[15]), cxx2a_bit_cast<double>(params[16]),
        cxx2a_bit_cast<double>(params[17]), cxx2a_bit_cast<double>(params[18]));

    ReleaseAssert(g_expectedResult.m_checkerFnCalled);
}

void TestModule(bool calleeAcceptsVarArgs, size_t specializedNumFixedParams)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();
    DeegenFunctionEntryLogicCreator ifi(ctx, DeegenEngineTier::Interpreter, calleeAcceptsVarArgs, specializedNumFixedParams);
    std::unique_ptr<Module> module = ifi.GetInterpreterModule();

    Function* func = module->getFunction(ifi.GetFunctionName());
    ReleaseAssert(func != nullptr);
    ReleaseAssert(func->getCallingConv() == CallingConv::GHC);
    ReleaseAssert(func->arg_size() == RegisterPinningScheme::GetFunctionTypeNumArguments());
    func->setCallingConv(CallingConv::C);

    for (BasicBlock& bb : *func)
    {
        for (Instruction& inst : bb)
        {
            CallInst* callInst = dyn_cast<CallInst>(&inst);
            if (callInst != nullptr)
            {
                if (callInst->getCallingConv() == CallingConv::GHC)
                {
                    ReleaseAssert(callInst->getCalledFunction() == nullptr ||
                                  callInst->getCalledFunction()->getName() == "__deegen_interpreter_tier_up_into_baseline_jit");
                    ReleaseAssert(callInst->isMustTailCall());
                    ReleaseAssert(callInst->arg_size() == RegisterPinningScheme::GetFunctionTypeNumArguments());
                    callInst->setCallingConv(CallingConv::C);
                }
            }
        }
    }

    GlobalVariable* gv = module->getGlobalVariable(x_deegen_interpreter_dispatch_table_symbol_name);

    llvm::ArrayType* dispatchTableTy = llvm::ArrayType::get(llvm_type_of<void*>(ctx), 1 /*numElements*/);
    GlobalVariable* fakeDispatchTable = new GlobalVariable(*module,
                                             dispatchTableTy /*valueType*/,
                                             true /*isConstant*/,
                                             GlobalValue::InternalLinkage,
                                             nullptr /*initializer*/,
                                             "__fake_dispatch_table" /*name*/);
    fakeDispatchTable->setAlignment(MaybeAlign(8));
    gv->replaceAllUsesWith(fakeDispatchTable);

    Constant* ptr = ConstantExpr::getIntToPtr(CreateLLVMConstantInt<uint64_t>(ctx, reinterpret_cast<uint64_t>(ResultChecker)), llvm_type_of<void*>(ctx));
    Constant* arr = ConstantArray::get(dispatchTableTy, { ptr });
    fakeDispatchTable->setInitializer(arr);

    ValidateLLVMModule(module.get());

    SimpleJIT jit(module.get());
    jit.AllowAccessWhitelistedHostSymbolsOnly();
    jit.AddAllowedHostSymbol("memcpy");
    jit.AddAllowedHostSymbol("memmove");
    jit.AddAllowedHostSymbol("__deegen_interpreter_tier_up_into_baseline_jit");
    void* testFnAddr = jit.GetFunction(ifi.GetFunctionName());

    size_t numTests = 0;
    for (bool isTailCall : { false, true })
    {
        std::vector<uint64_t> numCalleeFixedArgsChoices;
        if (ifi.IsNumFixedParamSpecialized())
        {
            numCalleeFixedArgsChoices.push_back(ifi.GetSpecializedNumFixedParam());
        }
        else
        {
            for (uint64_t i = 0; i < 20; i++)
            {
                numCalleeFixedArgsChoices.push_back(i);
            }
            numCalleeFixedArgsChoices.push_back(100);
        }

        for (size_t numCalleeFixedArgs : numCalleeFixedArgsChoices)
        {
            std::vector<uint64_t> numProvidedArgsChoices;
            for (uint64_t i = 0; i < 25; i++)
            {
                numProvidedArgsChoices.push_back(i);
            }
            numProvidedArgsChoices.push_back(50);
            numProvidedArgsChoices.push_back(100);
            numProvidedArgsChoices.push_back(150);

            for (size_t numProvidedArgs : numProvidedArgsChoices)
            {
                TestOneCase(calleeAcceptsVarArgs, numCalleeFixedArgs, isTailCall, numProvidedArgs, testFnAddr);
                numTests++;
            }
        }
    }
    // Just sanity check we didn't screw any of our loop conditions to enumerate test cases..
    //
    ReleaseAssert(numTests > 10);
}

}   // anonymous namespace

TEST(DeegenAst, InterpreterFunctionEntry_NoVarArgs_SpecializedFixedArgs)
{
    for (size_t i = 0; i < 10; i++)
    {
        TestModule(false /*acceptVarArgs*/, i /*numFixedArgs*/);
    }
}

TEST(DeegenAst, InterpreterFunctionEntry_NoVarArgs_GenericFixedArgs)
{
    TestModule(false /*acceptVarArgs*/, static_cast<size_t>(-1) /*numFixedArgs*/);
}

TEST(DeegenAst, InterpreterFunctionEntry_TakesVarArgs_SpecializedFixedArgs)
{
    for (size_t i = 0; i < 10; i++)
    {
        TestModule(true /*acceptVarArgs*/, i /*numFixedArgs*/);
    }
}

TEST(DeegenAst, InterpreterFunctionEntry_TakesVarArgs_GenericFixedArgs)
{
    TestModule(true /*acceptVarArgs*/, static_cast<size_t>(-1) /*numFixedArgs*/);
}
