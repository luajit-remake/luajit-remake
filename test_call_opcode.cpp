#include "bytecode.h"
#include "gtest/gtest.h"

using namespace ToyLang;

namespace {

CodeBlock* AllocateInterpreterCodeBlockWithBytecodeSize(size_t bytecodeSize)
{
    SystemHeapPointer<void> ptr = VM::GetActiveVMForCurrentThread()->AllocFromSystemHeap(static_cast<uint32_t>(sizeof(CodeBlock)));
    void* retVoid = TranslateToRawPointer(ptr.As<void>());
    CodeBlock* ret = new (retVoid) CodeBlock();
    SystemHeapGcObjectHeader::Populate<ExecutableCode*>(ret);
    ret->m_bytecodeLength = SafeIntegerCast<uint32_t>(bytecodeSize);
    ret->m_bytecode = new uint8_t[bytecodeSize];
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
    SanityCallOpcodeInfo* info = reinterpret_cast<SanityCallOpcodeInfo*>(rc->m_globalObject.m_value);
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
    else
    {
        ReleaseAssert(newHdr->m_numVariadicArguments == 0);
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
    TValue* callerLocals = reinterpret_cast<TValue*>(info->m_expectedSfh) + x_numSlotsForStackFrameHeader;
    for (uint32_t i = 0; i < info->m_callerNumStackSlots; i++)
    {
        ReleaseAssert(callerLocals[i].IsInt32(TValue::x_int32Tag));
        ReleaseAssert(callerLocals[i].AsInt32() == static_cast<int32_t>(i + 10000));
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
    if (expectedVarArgRegionLength > 0)
    {
        TValue* vaBegin = reinterpret_cast<TValue*>(newHdr) - expectedVarArgRegionLength;
        for (uint32_t i = 0; i < expectedVarArgRegionLength; i++)
        {
            ReleaseAssert(vaBegin[i].IsInt32(TValue::x_int32Tag));
            ReleaseAssert(vaBegin[i].AsInt32() == static_cast<int32_t>(i + info->m_calleeNumFixedArgs + 1));
        }
    }

    uint8_t* calleeFuncBytecode = TCGet(info->m_calleeFunc->m_executable).As()->m_bytecode;
    ReleaseAssert(bcu == calleeFuncBytecode);
    ReleaseAssert(rc == info->m_expectedRc);
}

TEST(CallOpcode, Sanity)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetUpSegmentationRegister();

    CodeBlock* callerCb = AllocateInterpreterCodeBlockWithBytecodeSize(1000);
    callerCb->m_stackFrameNumSlots = 30;
    callerCb->m_numUpvalues = 0;
    callerCb->m_bestEntryPoint = nullptr;

    CodeBlock* calleeCb = AllocateInterpreterCodeBlockWithBytecodeSize(1000);
    calleeCb->m_stackFrameNumSlots = 30;
    calleeCb->m_numUpvalues = 0;
    calleeCb->m_bestEntryPoint = CheckStackLayout;

    HeapPtr<FunctionObject> callerFunc = vm->AllocFromUserHeap(sizeof(FunctionObject)).AsNoAssert<FunctionObject>();
    UserHeapGcObjectHeader::Populate(callerFunc);
    TCSet(callerFunc->m_executable, SystemHeapPointer<ExecutableCode>(static_cast<ExecutableCode*>(callerCb)));

    HeapPtr<FunctionObject> calleeFunc = vm->AllocFromUserHeap(sizeof(FunctionObject)).AsNoAssert<FunctionObject>();
    UserHeapGcObjectHeader::Populate(calleeFunc);
    TCSet(calleeFunc->m_executable, SystemHeapPointer<ExecutableCode>(static_cast<ExecutableCode*>(calleeCb)));

    uint8_t* byt = callerCb->m_bytecode;
    BcCall* callOp = reinterpret_cast<BcCall*>(byt);
    callOp->m_opcode = x_opcodeId<BcCall>;

    CoroutineRuntimeContext rc;

    TValue* stack = new TValue[1000];

    TValue* locals = stack + x_numSlotsForStackFrameHeader;
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
                uint32_t numFixedParams = static_cast<uint32_t>(rand()) % (callerCb->m_stackFrameNumSlots - x_numSlotsForStackFrameHeader);
                uint32_t funcStart = static_cast<uint32_t>(rand()) % (callerCb->m_stackFrameNumSlots - numFixedParams - x_numSlotsForStackFrameHeader);
                ReleaseAssert(funcStart + x_numSlotsForStackFrameHeader - 1 + numFixedParams < callerCb->m_stackFrameNumSlots);
                info.m_numCallerFixedParams = numFixedParams;

                info.m_callerNumStackSlots = funcStart;

                int paramV = 1;
                locals[funcStart] = TValue::CreatePointer(UserHeapPointer<FunctionObject> { calleeFunc });
                for (uint32_t i = 0; i < numFixedParams; i++)
                {
                    locals[funcStart + i + x_numSlotsForStackFrameHeader] = TValue::CreateInt32(paramV, TValue::x_int32Tag);
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
                    rc.m_variadicRetSlotBegin = static_cast<int32_t>(slotBegin);
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
                rc.m_globalObject.m_value = reinterpret_cast<int64_t>(&info);

                callOp->m_funcSlot = BytecodeSlot::Local(static_cast<int>(funcStart));
                callOp->m_numFixedParams = numFixedParams;
                callOp->m_passVariadicRetAsParam = passVariadicRet;

                rc.m_codeBlock = callerCb;

                BcCall::Execute(&rc, locals, callerCb->m_bytecode, 0 /*unused*/);

                ReleaseAssert(info.m_checkerHasExecuted);
            }
        }
    }
}

}   // anonymous namespace
