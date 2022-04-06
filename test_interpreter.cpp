#include "bytecode.h"
#include "gtest/gtest.h"

using namespace ToyLang;

namespace {

InterpreterCodeBlock* AllocateInterpreterCodeBlockWithBytecodeSize(size_t bytecodeSize)
{
    size_t allocationSize = sizeof(InterpreterCodeBlock) + bytecodeSize;
    allocationSize = RoundUpToMultipleOf<8>(allocationSize);
    SystemHeapPointer ptr = VM::GetActiveVMForCurrentThread()->AllocFromSystemHeap(static_cast<uint32_t>(allocationSize));
    void* retVoid = VM::GetActiveVMForCurrentThread()->GetHeapPtrTranslator().TranslateToRawPtr(ptr.As<void>());
    InterpreterCodeBlock* ret = new (retVoid) InterpreterCodeBlock();
    assert(reinterpret_cast<uintptr_t>(ret) == reinterpret_cast<uintptr_t>(retVoid));
    ret->m_bytecodeLength = SafeIntegerCast<uint32_t>(bytecodeSize);
    return ret;
}

// helper struct for correctness testing
//
struct SanityCallOpcodeInfo
{
    bool m_checkerHasExecuted;
    bool m_calleeAcceptsVariadicArg;
    bool m_passVariadicRet;
    uint32_t m_calleeNumFixedArgs;
    uint32_t m_numCallerVariadicRets;
    uint32_t m_numCallerFixedParams;
    StackFrameHeader* m_expectedSfh;
    uint32_t m_callerArgStart;
    uint32_t m_callerNumStackSlots;
    HeapPtr<FunctionObject> m_calleeFunc;
    HeapPtr<FunctionObject> m_callerFunc;
    CoroutineRuntimeContext* m_expectedRc;
};

// Check that the call frame is set up as we expected
//
void CheckStackLayout(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
{
    SanityCallOpcodeInfo* info = reinterpret_cast<SanityCallOpcodeInfo*>(rc->m_globalObject);
    ReleaseAssert(!info->m_checkerHasExecuted);
    info->m_checkerHasExecuted = true;

    // Check that the new stackframe header is filled correctly
    //
    StackFrameHeader* newHdr = StackFrameHeader::GetStackFrameHeader(stackframe);
    ReleaseAssert(newHdr->m_caller == info->m_expectedSfh + 1);
    ReleaseAssert(newHdr->m_func == info->m_calleeFunc);
    ReleaseAssert(newHdr->m_retAddr == reinterpret_cast<void*>(BcCall::OnReturn));

    uint32_t totalArgumentsProvidedByCaller = info->m_numCallerFixedParams;
    if (info->m_passVariadicRet)
    {
        totalArgumentsProvidedByCaller += info->m_numCallerVariadicRets;
    }

    uint32_t expectedVarArgRegionLength = 0;
    if (info->m_calleeAcceptsVariadicArg)
    {
        if (totalArgumentsProvidedByCaller > info->m_calleeNumFixedArgs)
        {
            expectedVarArgRegionLength = totalArgumentsProvidedByCaller - info->m_calleeNumFixedArgs;
        }
        ReleaseAssert(newHdr->m_numVariadicArguments == expectedVarArgRegionLength);
    }

    // Check that the stack frame is positioned at the expected place
    //
    {
        int64_t diff = reinterpret_cast<TValue*>(newHdr) - reinterpret_cast<TValue*>(info->m_expectedSfh);
        ReleaseAssert(diff == static_cast<int64_t>(info->m_callerNumStackSlots + x_sizeOfStackFrameHeaderInTermsOfTValue + expectedVarArgRegionLength));
    }

    // Check that the caller stack frame is not trashed
    //
    // Check the caller stack header
    //
    ReleaseAssert(info->m_expectedSfh->m_retAddr == nullptr);
    ReleaseAssert(info->m_expectedSfh->m_caller == nullptr);
    // Our BcCall is the first instruction in the caller bytecode
    //
    ReleaseAssert(info->m_expectedSfh->m_callerBytecodeOffset == 0);
    ReleaseAssert(info->m_expectedSfh->m_numVariadicArguments == 67890);
    ReleaseAssert(info->m_expectedSfh->m_func == info->m_callerFunc);

    // Check the caller locals
    //
    TValue* callerLocals = reinterpret_cast<TValue*>(info->m_expectedSfh) + x_sizeOfStackFrameHeaderInTermsOfTValue;
    for (uint32_t i = 0; i < info->m_callerNumStackSlots; i++)
    {
        if (i == info->m_callerArgStart)
        {
            ReleaseAssert(callerLocals[i].IsPointer(TValue::x_mivTag));
            ReleaseAssert(callerLocals[i].AsPointer().As<FunctionObject>() == info->m_calleeFunc);
        }
        else if (info->m_callerArgStart < i && i <= info->m_callerArgStart + info->m_numCallerFixedParams)
        {
            ReleaseAssert(callerLocals[i].IsInt32(TValue::x_int32Tag));
            ReleaseAssert(callerLocals[i].AsInt32() == static_cast<int32_t>(i - info->m_callerArgStart));
        }
        else
        {
            ReleaseAssert(callerLocals[i].IsInt32(TValue::x_int32Tag));
            ReleaseAssert(callerLocals[i].AsInt32() == static_cast<int32_t>(i + 10000));
        }
    }

    // Check that the callee stack frame is set up correctly
    //
    // Check that all the callee parameters are populated correctly
    //
    TValue* calleeLocals = reinterpret_cast<TValue*>(stackframe);

    // First check the fixed arguments part
    //
    for (uint32_t i = 0; i < info->m_calleeNumFixedArgs; i++)
    {
        if (i < totalArgumentsProvidedByCaller)
        {
            ReleaseAssert(calleeLocals[i].IsInt32(TValue::x_int32Tag));
            ReleaseAssert(calleeLocals[i].AsInt32() == static_cast<int32_t>(i + 1));
        }
        else
        {
            ReleaseAssert(calleeLocals[i].IsMIV(TValue::x_mivTag));
            ReleaseAssert(calleeLocals[i].AsMIV(TValue::x_mivTag) == MiscImmediateValue::CreateNil());
        }
    }

    // Then check the variadic arguments part
    //
    for (uint32_t i = 0; i < expectedVarArgRegionLength; i++)
    {
        ReleaseAssert(callerLocals[info->m_callerNumStackSlots + i].IsInt32(TValue::x_int32Tag));
        ReleaseAssert(callerLocals[info->m_callerNumStackSlots + i].AsInt32() == static_cast<int32_t>(i + info->m_calleeNumFixedArgs + 1));
    }

    SystemHeapPointer calleeFuncBytecode = info->m_calleeFunc->m_bytecode;
    ReleaseAssert(bcu == VM::GetActiveVMForCurrentThread()->GetHeapPtrTranslator().TranslateToRawPtr(calleeFuncBytecode.As<uint8_t>()));
    ReleaseAssert(rc == info->m_expectedRc);
}

TEST(Interpreter, SanityCallOpcodeCallPart)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetUpSegmentationRegister();

    HeapPtrTranslator translator = VM::GetActiveVMForCurrentThread()->GetHeapPtrTranslator();

    InterpreterCodeBlock* callerCb = AllocateInterpreterCodeBlockWithBytecodeSize(1000);
    callerCb->m_stackFrameNumSlots = 30;
    callerCb->m_numUpValues = 0;
    callerCb->m_functionEntryPoint = nullptr;

    InterpreterCodeBlock* calleeCb = AllocateInterpreterCodeBlockWithBytecodeSize(1000);
    calleeCb->m_stackFrameNumSlots = 30;
    calleeCb->m_numUpValues = 0;
    calleeCb->m_functionEntryPoint = CheckStackLayout;

    HeapPtr<FunctionObject> callerFunc = vm->AllocFromUserHeap(sizeof(FunctionObject)).As<FunctionObject>();
    callerFunc->m_type = Type::FUNCTION;
    callerFunc->m_bytecode.m_value = translator.TranslateToSystemHeapPtr(callerCb->m_bytecode).m_value;

    HeapPtr<FunctionObject> calleeFunc = vm->AllocFromUserHeap(sizeof(FunctionObject)).As<FunctionObject>();

    calleeFunc->m_type = Type::FUNCTION;
    calleeFunc->m_bytecode.m_value = translator.TranslateToSystemHeapPtr(calleeCb->m_bytecode).m_value;

    SystemHeapPointer byt = callerFunc->m_bytecode;
    BcCall* callOp = translator.TranslateToRawPtr<BcCall>(byt.As<BcCall>());
    callOp->m_opcode = static_cast<uint8_t>(Opcode::BcCall);

    CoroutineRuntimeContext rc;

    TValue* stack = new TValue[1000];

    TValue* locals = stack + x_sizeOfStackFrameHeaderInTermsOfTValue;
    StackFrameHeader* hdr = reinterpret_cast<StackFrameHeader*>(stack);

    for (bool calleeAcceptsVariadicArg : { false, true })
    {
        calleeCb->m_hasVariadicArguments = calleeAcceptsVariadicArg;
        for (bool passVariadicRet : { false, true })
        {
            for (int testcase = 0; testcase < 20000; testcase++)
            {
                SanityCallOpcodeInfo info;
                info.m_checkerHasExecuted = false;
                info.m_calleeAcceptsVariadicArg = calleeAcceptsVariadicArg;
                info.m_passVariadicRet = passVariadicRet;
                info.m_expectedSfh = hdr;
                info.m_calleeFunc = calleeFunc;
                info.m_callerFunc = callerFunc;
                info.m_callerNumStackSlots = callerCb->m_stackFrameNumSlots;
                info.m_expectedRc = &rc;

                memset(stack, 0, sizeof(uint64_t) * 300);

                // populate the caller locals, so we can check they are not stashed
                //
                for (uint32_t i = 0; i < callerCb->m_stackFrameNumSlots; i++)
                {
                    locals[i] = TValue::CreateInt32(static_cast<int32_t>(i + 10000), TValue::x_int32Tag);
                 }

                // roll out how many fixed arguments the callee is accepting
                //
                calleeCb->m_numFixedArguments = static_cast<uint32_t>(rand() % 50);
                info.m_calleeNumFixedArgs = calleeCb->m_numFixedArguments;

                // roll out # of parameters to pass and their position
                //
                uint32_t numFixedParams = static_cast<uint32_t>(rand()) % callerCb->m_stackFrameNumSlots;
                uint32_t funcStart = static_cast<uint32_t>(rand()) % (callerCb->m_stackFrameNumSlots - numFixedParams);
                ReleaseAssert(funcStart + numFixedParams < callerCb->m_stackFrameNumSlots);
                info.m_numCallerFixedParams = numFixedParams;

                int paramV = 1;
                locals[funcStart] = TValue::CreatePointer(calleeFunc);
                for (uint32_t i = 0; i < numFixedParams; i++)
                {
                    locals[funcStart + i + 1] = TValue::CreateInt32(paramV, TValue::x_int32Tag);
                    paramV++;
                }
                info.m_callerArgStart = funcStart;

                // roll out the # of variadic rets and their position
                //
                if (passVariadicRet)
                {
                    uint32_t numVariadicRet = static_cast<uint32_t>(rand()) % 30;
                    rc.m_numVariadicRets = numVariadicRet;

                    uint32_t slotBegin = callerCb->m_stackFrameNumSlots + static_cast<uint32_t>(rand()) % 40;
                    for (uint32_t i = 0; i < numVariadicRet; i++)
                    {
                        locals[slotBegin + i] = TValue::CreateInt32(paramV, TValue::x_int32Tag);
                        paramV++;
                    }
                    rc.m_variadicRetSlotBegin = slotBegin;
                    info.m_numCallerVariadicRets = numVariadicRet;
                }
                else
                {
                    rc.m_numVariadicRets = static_cast<uint32_t>(-1);
                }

                // Fill out hdr information
                //
                hdr->m_caller = nullptr;
                hdr->m_func = callerFunc;
                hdr->m_retAddr = nullptr;
                hdr->m_callerBytecodeOffset = 12345;    // trashed
                hdr->m_numVariadicArguments = 67890;

                // we repurpose this field to pass 'info' to our correctness checker
                //
                rc.m_globalObject = reinterpret_cast<GlobalObject*>(&info);

                callOp->m_funcSlot = BytecodeSlot::Local(static_cast<int>(funcStart));
                callOp->m_numFixedParams = numFixedParams;
                callOp->m_passVariadicRetAsParam = passVariadicRet;

                BcCall::Execute(&rc, locals, callerCb->m_bytecode, 0 /*unused*/);

                ReleaseAssert(info.m_checkerHasExecuted);
            }
        }
    }
}

void TestFibEndpoint(CoroutineRuntimeContext* /*rc*/, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> retValuesU, uint64_t numRetValues)
{
    ReleaseAssert(numRetValues == 1);
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackframe);
    ReleaseAssert(hdr->m_caller == hdr + 1);
    hdr->m_caller = reinterpret_cast<StackFrameHeader*>(const_cast<uint8_t*>(retValuesU));
}

TEST(Interpreter, SanityFibonacci)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetUpSegmentationRegister();

    HeapPtrTranslator translator = VM::GetActiveVMForCurrentThread()->GetHeapPtrTranslator();

    InterpreterCodeBlock* fib = AllocateInterpreterCodeBlockWithBytecodeSize(1000);
    fib->m_stackFrameNumSlots = 4;
    fib->m_hasVariadicArguments = false;
    fib->m_numFixedArguments = 1;
    fib->m_numUpValues = 0;
    fib->m_functionEntryPoint = EnterInterpreter;

    HeapPtr<FunctionObject> fibObj = vm->AllocFromUserHeap(sizeof(FunctionObject)).As<FunctionObject>();
    fibObj->m_type = Type::FUNCTION;
    fibObj->m_bytecode.m_value = translator.TranslateToSystemHeapPtr(fib->m_bytecode).m_value;

    uint8_t* p = fib->m_bytecode;

    BcConstant* instr1 = reinterpret_cast<BcConstant*>(p);  // 2
    p += sizeof(BcConstant);
    BcIsLTVV* instr2 = reinterpret_cast<BcIsLTVV*>(p);  // cmp
    p += sizeof(BcIsLTVV);
    BcConstant* instr3 = reinterpret_cast<BcConstant*>(p); // f
    p += sizeof(BcConstant);
    BcConstant* instr4 = reinterpret_cast<BcConstant*>(p); // 1
    p += sizeof(BcConstant);
    BcSubVV* instr5 = reinterpret_cast<BcSubVV*>(p);    // 'n-1'
    p += sizeof(BcSubVV);
    BcCall* instr6 = reinterpret_cast<BcCall*>(p);      // 'call'
    p += sizeof(BcCall);
    BcConstant* instr7 = reinterpret_cast<BcConstant*>(p); // f
    p += sizeof(BcConstant);
    BcConstant* instr8 = reinterpret_cast<BcConstant*>(p); // 2
    p += sizeof(BcConstant);
    BcSubVV* instr9 = reinterpret_cast<BcSubVV*>(p);    // 'n-2'
    p += sizeof(BcSubVV);
    BcCall* instr10 = reinterpret_cast<BcCall*>(p);      // 'call'
    p += sizeof(BcCall);
    BcAddVV* instr11 = reinterpret_cast<BcAddVV*>(p);       // 'add'
    p += sizeof(BcAddVV);
    BcReturn* instr12 = reinterpret_cast<BcReturn*>(p);     // 'ret'
    p += sizeof(BcReturn);
    BcConstant* instr13 = reinterpret_cast<BcConstant*>(p);     // '1'
    p += sizeof(BcConstant);
    BcReturn* instr14 = reinterpret_cast<BcReturn*>(p);     // 'ret'
    p += sizeof(BcReturn);

    instr1->m_opcode = static_cast<uint8_t>(Opcode::BcConstant);
    instr1->m_dst = BytecodeSlot::Local(1);
    instr1->m_value = TValue::CreateDouble(3);

    instr2->m_opcode = static_cast<uint8_t>(Opcode::BcIsLTVV);
    instr2->m_lhs = BytecodeSlot::Local(0);
    instr2->m_rhs = BytecodeSlot::Local(1);
    instr2->m_offset = static_cast<int32_t>(reinterpret_cast<intptr_t>(instr13) - reinterpret_cast<intptr_t>(instr2));

    instr3->m_opcode = static_cast<uint8_t>(Opcode::BcConstant);
    instr3->m_dst = BytecodeSlot::Local(1);
    instr3->m_value = TValue::CreatePointer(fibObj);

    instr4->m_opcode = static_cast<uint8_t>(Opcode::BcConstant);
    instr4->m_dst = BytecodeSlot::Local(2);
    instr4->m_value = TValue::CreateDouble(1);

    instr5->m_opcode = static_cast<uint8_t>(Opcode::BcSubVV);
    instr5->m_result = BytecodeSlot::Local(2);
    instr5->m_lhs = BytecodeSlot::Local(0);
    instr5->m_rhs = BytecodeSlot::Local(2);

    instr6->m_opcode = static_cast<uint8_t>(Opcode::BcCall);
    instr6->m_funcSlot = BytecodeSlot::Local(1);
    instr6->m_numFixedParams = 1;
    instr6->m_numFixedRets = 1;
    instr6->m_passVariadicRetAsParam = false;
    instr6->m_keepVariadicRet = false;

    instr7->m_opcode = static_cast<uint8_t>(Opcode::BcConstant);
    instr7->m_dst = BytecodeSlot::Local(2);
    instr7->m_value = TValue::CreatePointer(fibObj);

    instr8->m_opcode = static_cast<uint8_t>(Opcode::BcConstant);
    instr8->m_dst = BytecodeSlot::Local(3);
    instr8->m_value = TValue::CreateDouble(2);

    instr9->m_opcode = static_cast<uint8_t>(Opcode::BcSubVV);
    instr9->m_result = BytecodeSlot::Local(3);
    instr9->m_lhs = BytecodeSlot::Local(0);
    instr9->m_rhs = BytecodeSlot::Local(3);

    instr10->m_opcode = static_cast<uint8_t>(Opcode::BcCall);
    instr10->m_funcSlot = BytecodeSlot::Local(2);
    instr10->m_numFixedParams = 1;
    instr10->m_numFixedRets = 1;
    instr10->m_passVariadicRetAsParam = false;
    instr10->m_keepVariadicRet = false;

    instr11->m_opcode = static_cast<uint8_t>(Opcode::BcAddVV);
    instr11->m_lhs = BytecodeSlot::Local(1);
    instr11->m_rhs = BytecodeSlot::Local(2);
    instr11->m_result = BytecodeSlot::Local(1);

    instr12->m_opcode = static_cast<uint8_t>(Opcode::BcReturn);
    instr12->m_numReturnValues = 1;
    instr12->m_isVariadicRet = false;
    instr12->m_slotBegin = BytecodeSlot::Local(1);

    instr13->m_opcode = static_cast<uint8_t>(Opcode::BcConstant);
    instr13->m_dst = BytecodeSlot::Local(1);
    instr13->m_value = TValue::CreateDouble(1);

    instr14->m_opcode = static_cast<uint8_t>(Opcode::BcReturn);
    instr14->m_numReturnValues = 1;
    instr14->m_isVariadicRet = false;
    instr14->m_slotBegin = BytecodeSlot::Local(1);

    TValue* stack = new TValue[1000];
    CoroutineRuntimeContext rc;

    auto testFib = [&](int n)
    {
        TValue* locals = stack + x_sizeOfStackFrameHeaderInTermsOfTValue;
        StackFrameHeader* hdr = reinterpret_cast<StackFrameHeader*>(stack);
        hdr->m_caller = hdr + 1;
        hdr->m_func = fibObj;
        hdr->m_retAddr = reinterpret_cast<void*>(TestFibEndpoint);

        locals[0] = TValue::CreateDouble(static_cast<double>(n));

        rc.m_numVariadicRets = static_cast<uint32_t>(-1);
        fib->m_functionEntryPoint(&rc, locals, fib->m_bytecode, 0 /*unused*/);

        ReleaseAssert(hdr->m_caller != hdr + 1);
        TValue* r = reinterpret_cast<TValue*>(hdr->m_caller);
        ReleaseAssert(r->IsDouble(TValue::x_int32Tag));
        return r->AsDouble();
    };

    double a = testFib(15);
    SUPRESS_FLOAT_EQUAL_WARNING(
        ReleaseAssert(a == 610.0);
    )
}

}   // anonymous namespace
