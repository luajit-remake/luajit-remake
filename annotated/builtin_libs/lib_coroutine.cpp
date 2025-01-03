#include "deegen_api.h"
#include "runtime_utils.h"

// This is just memcpy, but we are mostly only moving a few arguments, so don't unroll the loop.
// And it also doesn't matter to move one more slot, so we don't need the tail check for vectorization
//
static void ALWAYS_INLINE MoveArgumentsForCoroutine(TValue* dst, const TValue* src, size_t num)
{
    size_t i = 0;
#pragma clang loop unroll(disable)
#pragma clang loop vectorize(disable)
    while (i < num)
    {
        __builtin_memcpy_inline(dst + i, src + i, sizeof(TValue) * 2);
        i += 2;
    }
}

// Internal function, invoked when the coroutine execution finished successfully without errors
// This should render the current coroutine dead, transfer control to the parent coroutine, and pass around the return values.
//
DEEGEN_DEFINE_LIB_FUNC_CONTINUATION(coro_finish)
{
    TValue* retStart = GetReturnValuesBegin();
    size_t numRets = GetNumReturnValues();
    CoroutineRuntimeContext* currentCoro = GetCurrentCoroutine();
    Assert(!currentCoro->m_coroutineStatus.IsDead() && !currentCoro->m_coroutineStatus.IsResumable());
    CoroutineRuntimeContext* targetCoro = currentCoro->m_parent;
    Assert(targetCoro != nullptr);
    Assert(!targetCoro->m_coroutineStatus.IsDead() && !targetCoro->m_coroutineStatus.IsResumable());

    // Update coroutine status: the current coroutine becomes dead
    //
    currentCoro->m_coroutineStatus.SetDead(true);
    Assert(currentCoro->m_coroutineStatus.IsDead() && !currentCoro->m_coroutineStatus.IsResumable());

    // Set up the arguments returned to the parent coroutine
    //
    TValue* dstStackBase = targetCoro->m_suspendPointStackBase;
    StackFrameHeader* dstHdr = StackFrameHeader::Get(dstStackBase);
    // DEVNOTE: 'm_numVariadicArguments' is repurposed by us here to distinguish whether
    // it is a coroutine.wrap or a coroutine.resume... 0 means coroutine.resume and 1 mean coroutine.wrap
    //
    if (dstHdr->m_numVariadicArguments == 0)
    {
        // For coroutine.resume, we should store 'true' plus all return values
        // Note that we also need to pad nils to x_minNilFillReturnValues, as required by our internal call scheme.
        // However, since we know that the incoming return values also follows this scheme, it's sufficient to memcpy at least that many elements.
        //
        // TODO: we need to check for stack overflow here once we implement resizable stack
        //
        dstStackBase[0] = TValue::Create<tBool>(true);
        MoveArgumentsForCoroutine(dstStackBase + 1, retStart, std::max(numRets, static_cast<size_t>(x_minNilFillReturnValues) - 1));

        CoroSwitch(targetCoro, dstStackBase, numRets + 1);
    }
    else
    {
        Assert(dstHdr->m_numVariadicArguments == 1);
        // For coroutine.wrap, we should simply store all the return values
        //
        // TODO: we need to check for stack overflow here once we implement resizable stack
        //
        MoveArgumentsForCoroutine(dstStackBase, retStart, std::max(numRets, static_cast<size_t>(x_minNilFillReturnValues)));

        CoroSwitch(targetCoro, dstStackBase, numRets);
    }
}

// Internal function, invoked when a coroutine first starts execution.
// This should transfer control to the entry function of the coroutine, and pass around all arguments.
//
DEEGEN_DEFINE_LIB_FUNC_CONTINUATION(coro_init)
{
    TValue* start = GetReturnValuesBegin();
    size_t numArgs = GetNumReturnValues();
    // By how coroutine.create set up the stack frame, the function to call is already at stackBase[0],
    // and all the arguments must show up at GetStackBase() + x_numSlotsForStackFrameHeader due to how we design the scheme.
    // So we can simply execute a InPlaceCall here.
    //
    Assert(start == GetStackBase() + x_numSlotsForStackFrameHeader);
    Assert(GetStackBase()[0].Is<tFunction>());
    MakeInPlaceCall(start, numArgs, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(coro_finish));
}

static CoroutineRuntimeContext* WARN_UNUSED ALWAYS_INLINE CreateNewCoroutine(UserHeapPointer<TableObject> globalObject,
                                                                             HeapPtr<FunctionObject> entryFn)
{
    // We set up the initial stack for the new coroutine like the following:
    //     [ dummyHdr ] [ nextHdr ]
    // where nextHdr's return continuation is 'coro_init' and function object is the function to call
    //
    // The suspendPointStackBase of the coroutine will be at nextHdr.
    // When the coroutine is resumed, the return continuation in the StackFrameHeader (which is coro_init) will be called.
    // Then coro_init will initiate the call to the entry function with return continuation 'coro_finish',
    // which starts the execution of the coroutine.
    //
    VM* vm = VM::GetActiveVMForCurrentThread();
    CoroutineRuntimeContext* coro = CoroutineRuntimeContext::Create(vm, globalObject);
    StackFrameHeader* hdr = reinterpret_cast<StackFrameHeader*>(coro->m_stackBegin);
    hdr[0].m_func = nullptr;
    hdr[0].m_caller = nullptr;
    hdr[0].m_retAddr = nullptr;
    hdr[0].m_callerBytecodePtr.m_value = 0;
    hdr[0].m_numVariadicArguments = 0;
    hdr[1].m_func = entryFn;
    hdr[1].m_caller = hdr + 1;
    hdr[1].m_retAddr = DEEGEN_LIB_FUNC_RETURN_CONTINUATION(coro_init);
    hdr[1].m_callerBytecodePtr.m_value = 0;
    hdr[1].m_numVariadicArguments = 0;
    coro->m_suspendPointStackBase = reinterpret_cast<TValue*>(hdr + 2);
    return coro;
}

// coroutine.create -- https://www.lua.org/manual/5.1/manual.html#pdf-coroutine.create
//
// coroutine.create (f)
// Creates a new coroutine, with body f. f must be a Lua function. Returns this new coroutine, an object with type "thread".
//
DEEGEN_DEFINE_LIB_FUNC(coroutine_create)
{
    if (unlikely(GetNumArgs() == 0))
    {
        ThrowError("bad argument #1 to 'create' (Lua function expected)");
    }
    TValue arg = GetArg(0);
    if (unlikely(!arg.Is<tFunction>()))
    {
        ThrowError("bad argument #1 to 'create' (Lua function expected)");
    }
    CoroutineRuntimeContext* currentCoro = GetCurrentCoroutine();
    CoroutineRuntimeContext* newCoro = CreateNewCoroutine(currentCoro->m_globalObject, arg.As<tFunction>() /*entryFn*/);
    Return(TValue::Create<tThread>(TranslateToHeapPtr(newCoro)));
}

// coroutine.resume -- https://www.lua.org/manual/5.1/manual.html#pdf-coroutine.resume
//
// coroutine.resume (co [, val1, ···])
// Starts or continues the execution of coroutine co. The first time you resume a coroutine, it starts running its body.
// The values val1, ··· are passed as the arguments to the body function. If the coroutine has yielded, resume restarts it;
// the values val1, ··· are passed as the results from the yield.
//
// If the coroutine runs without any errors, resume returns true plus any values passed to yield (if the coroutine yields) or
// any values returned by the body function (if the coroutine terminates). If there is any error, resume returns false plus
// the error message.
//
DEEGEN_DEFINE_LIB_FUNC(coroutine_resume)
{
    TValue arg;
    if (unlikely(GetNumArgs() == 0))
    {
        goto not_coroutine_object;
    }
    arg = GetArg(0);
    if (unlikely(!arg.Is<tHeapEntity>()))
    {
        goto not_coroutine_object;
    }
    if (unlikely(!CoroutineStatus::IsCoroutineObjectAndResumable(arg.As<tHeapEntity>()->m_arrayType)))
    {
        goto not_coroutine_object_or_not_resumable;
    }

    // Now we know the first arg is indeed a valid coroutine object for 'resume'
    //
    {
        Assert(arg.Is<tThread>());
        VM* vm = VM::GetActiveVMForCurrentThread();
        CoroutineRuntimeContext* targetCoro = TranslateToRawPointer(vm, arg.As<tThread>());

        // Update coroutine status: the target coroutine becomes no longer resumable and has the current coroutine as parent
        //
        Assert(!targetCoro->m_coroutineStatus.IsDead() && targetCoro->m_coroutineStatus.IsResumable());
        targetCoro->m_coroutineStatus.SetResumable(false);
        CoroutineRuntimeContext* currentCoro = GetCurrentCoroutine();
        targetCoro->m_parent = currentCoro;
        currentCoro->m_suspendPointStackBase = GetStackBase();

        // Set up the arguments passed to the resumed coroutine
        // TODO: we need to check for stack overflow here once we implement resizable stack
        //
        TValue* dstStackBase = targetCoro->m_suspendPointStackBase;
        size_t numArgsToPass = GetNumArgs() - 1;
        MoveArgumentsForCoroutine(dstStackBase, GetStackBase() + 1, numArgsToPass);
        for (size_t i = 0; i < x_minNilFillReturnValues; i++) { dstStackBase[numArgsToPass + i] = TValue::Create<tNil>(); }

        // DEVNOTE: 'm_numVariadicArguments' is repurposed by us here to distinguish whether
        // it is a coroutine.wrap or a coroutine.resume... 0 means coroutine.resume and 1 mean coroutine.wrap
        //
        GetStackFrameHeader()->m_numVariadicArguments = 0;

        // Transfer control to the target coroutine, this function call never returns
        //
        CoroSwitch(targetCoro, dstStackBase, numArgsToPass);
    }

not_coroutine_object_or_not_resumable:
    if (arg.Is<tThread>())
    {
        VM* vm = VM::GetActiveVMForCurrentThread();
        CoroutineStatus status = TCGet(arg.As<tThread>()->m_coroutineStatus);
        Assert(!status.IsResumable());
        if (status.IsDead())
        {
            Return(TValue::Create<tBool>(false), TValue::Create<tString>(vm->CreateStringObjectFromRawCString("cannot resume dead coroutine")));
        }
        else
        {
            Return(TValue::Create<tBool>(false), TValue::Create<tString>(vm->CreateStringObjectFromRawCString("cannot resume non-suspended coroutine")));
        }
    }
not_coroutine_object:
    ThrowError("bad argument #1 to 'resume' (coroutine expected)");
}

// coroutine.running -- https://www.lua.org/manual/5.1/manual.html#pdf-coroutine.running
//
// coroutine.running ()
// Returns the running coroutine, or nil when called by the main thread.
//
DEEGEN_DEFINE_LIB_FUNC(coroutine_running)
{
    CoroutineRuntimeContext* currentCoro = GetCurrentCoroutine();
    CoroutineRuntimeContext* rootCoroutine = VM::VM_GetRootCoroutine();
    if (currentCoro == rootCoroutine)
    {
        Return(TValue::Create<tNil>());
    }
    else
    {
        Return(TValue::Create<tThread>(TranslateToHeapPtr(currentCoro)));
    }
}

// coroutine.status -- https://www.lua.org/manual/5.1/manual.html#pdf-coroutine.status
//
// coroutine.status (co)
// Returns the status of coroutine co, as a string:
//     "running", if the coroutine is running (that is, it called status);
//     "suspended", if the coroutine is suspended in a call to yield, or if it has not started running yet;
//     "normal" if the coroutine is active but not running (that is, it has resumed another coroutine); and
//     "dead" if the coroutine has finished its body function, or if it has stopped with an error.
//
DEEGEN_DEFINE_LIB_FUNC(coroutine_status)
{
    if (unlikely(GetNumArgs() == 0))
    {
        ThrowError("bad argument #1 to 'status' (coroutine expected)");
    }
    TValue arg = GetArg(0);
    if (unlikely(!arg.Is<tThread>()))
    {
        ThrowError("bad argument #1 to 'status' (coroutine expected)");
    }

    HeapPtr<CoroutineRuntimeContext> coro = arg.As<tThread>();
    CoroutineStatus status = TCGet(coro->m_coroutineStatus);
    Assert(status.IsCoroutineObject());

    VM* vm = VM::GetActiveVMForCurrentThread();
    CoroutineRuntimeContext* currentCoro = GetCurrentCoroutine();
    if (TranslateToHeapPtr(currentCoro) == coro)
    {
        Assert(!status.IsDead() && !status.IsResumable());
        Return(TValue::Create<tString>(vm->CreateStringObjectFromRawCString("running")));
    }

    if (status.IsResumable())
    {
        Assert(!status.IsDead());
        Return(TValue::Create<tString>(vm->CreateStringObjectFromRawCString("suspended")));
    }

    if (status.IsDead())
    {
        Assert(!status.IsResumable());
        Return(TValue::Create<tString>(vm->CreateStringObjectFromRawCString("dead")));
    }

    Return(TValue::Create<tString>(vm->CreateStringObjectFromRawCString("normal")));
}

// Internal function that implements the function created by 'coroutine.wrap'
//
DEEGEN_DEFINE_LIB_FUNC(coroutine_wrap_call)
{
    HeapPtr<FunctionObject> func = GetStackFrameHeader()->m_func;
    Assert(func->m_numUpvalues == 1);
    TValue uv = TCGet(func->m_upvalues[0]);
    Assert(uv.Is<tThread>());
    CoroutineRuntimeContext* targetCoro = TranslateToRawPointer(uv.As<tThread>());

    // We are basically duplicating the logic in coroutine.resume here, because we do not want to make an
    // extra indirect call (which also happens to need to copy parameters) for performance reasons...
    // and also some checks can be elided.
    //
    if (unlikely(!targetCoro->m_coroutineStatus.IsResumable()))
    {
        if (targetCoro->m_coroutineStatus.IsDead())
        {
            ThrowError("cannot resume dead coroutine");
        }
        else
        {
            ThrowError("cannot resume non-suspended coroutine");
        }
    }

    // Update coroutine status: the target coroutine becomes no longer resumable and has the current coroutine as parent
    //
    Assert(!targetCoro->m_coroutineStatus.IsDead() && targetCoro->m_coroutineStatus.IsResumable());
    targetCoro->m_coroutineStatus.SetResumable(false);
    CoroutineRuntimeContext* currentCoro = GetCurrentCoroutine();
    targetCoro->m_parent = currentCoro;
    currentCoro->m_suspendPointStackBase = GetStackBase();

    // Set up the arguments passed to the resumed coroutine
    // TODO: we need to check for stack overflow here once we implement resizable stack
    //
    TValue* dstStackBase = targetCoro->m_suspendPointStackBase;
    size_t numArgsToPass = GetNumArgs();
    MoveArgumentsForCoroutine(dstStackBase, GetStackBase(), numArgsToPass);
    for (size_t i = 0; i < x_minNilFillReturnValues; i++) { dstStackBase[numArgsToPass + i] = TValue::Create<tNil>(); }

    // DEVNOTE: 'm_numVariadicArguments' is repurposed by us here to distinguish whether
    // it is a coroutine.wrap or a coroutine.resume... 0 means coroutine.resume and 1 mean coroutine.wrap
    //
    GetStackFrameHeader()->m_numVariadicArguments = 1;

    // Transfer control to the target coroutine, this function call never returns
    //
    CoroSwitch(targetCoro, dstStackBase, numArgsToPass);
}

// coroutine.wrap -- https://www.lua.org/manual/5.1/manual.html#pdf-coroutine.wrap
//
// coroutine.wrap (f)
// Creates a new coroutine, with body f. f must be a Lua function. Returns a function that resumes the coroutine each time
// it is called. Any arguments passed to the function behave as the extra arguments to resume. Returns the same values
// returned by resume, except the first boolean. In case of error, propagates the error.
//
DEEGEN_DEFINE_LIB_FUNC(coroutine_wrap)
{
    if (unlikely(GetNumArgs() == 0))
    {
        ThrowError("bad argument #1 to 'wrap' (Lua function expected)");
    }
    TValue arg = GetArg(0);
    if (unlikely(!arg.Is<tFunction>()))
    {
        ThrowError("bad argument #1 to 'wrap' (Lua function expected)");
    }
    CoroutineRuntimeContext* currentCoro = GetCurrentCoroutine();
    CoroutineRuntimeContext* newCoro = CreateNewCoroutine(currentCoro->m_globalObject, arg.As<tFunction>() /*entryFn*/);
    VM* vm = VM::GetActiveVMForCurrentThread();
    HeapPtr<FunctionObject> wrap = FunctionObject::CreateCFunc(vm, vm->GetLibFnProto<VM::LibFnProto::CoroutineWrapCall>(), 1 /*numUpValues*/).As();
    TValue uv = TValue::Create<tThread>(TranslateToHeapPtr(newCoro));
    TCSet(wrap->m_upvalues[0], uv);
    Return(TValue::Create<tFunction>(wrap));
}

// coroutine.yield -- https://www.lua.org/manual/5.1/manual.html#pdf-coroutine.yield
//
// coroutine.yield (···)
// Suspends the execution of the calling coroutine. The coroutine cannot be running a C function, a metamethod, or an iterator.
// Any arguments to yield are passed as extra results to resume.
//
DEEGEN_DEFINE_LIB_FUNC(coroutine_yield)
{
    TValue* sb = GetStackBase();
    size_t numArgs = GetNumArgs();

    CoroutineRuntimeContext* currentCoro = GetCurrentCoroutine();
    Assert(!currentCoro->m_coroutineStatus.IsDead() && !currentCoro->m_coroutineStatus.IsResumable());
    CoroutineRuntimeContext* targetCoro = currentCoro->m_parent;
    if (unlikely(targetCoro == nullptr))
    {
        // NOTE: official Lua doesn't support this, but I think a better API is to return to C with a special return
        // code denoting that the root coroutine yielded. For now just stay with Lua's behavior.
        //
        ThrowError("Cannot yield the root coroutine");
    }

    Assert(!targetCoro->m_coroutineStatus.IsDead() && !targetCoro->m_coroutineStatus.IsResumable());

    // Update the coroutine status: the current coroutine becomes resumable
    //
    currentCoro->m_coroutineStatus.SetResumable(true);
    currentCoro->m_suspendPointStackBase = sb;

    // Set up the arguments returned to the parent coroutine
    //
    TValue* dstStackBase = targetCoro->m_suspendPointStackBase;
    StackFrameHeader* dstHdr = StackFrameHeader::Get(dstStackBase);
    // DEVNOTE: 'm_numVariadicArguments' is repurposed by us here to distinguish whether
    // it is a coroutine.wrap or a coroutine.resume... 0 means coroutine.resume and 1 mean coroutine.wrap
    //
    if (dstHdr->m_numVariadicArguments == 0)
    {
        // For coroutine.resume, we should store 'true' plus all return values
        // Note that we also need to pad nils to x_minNilFillReturnValues, as required by our internal call scheme.
        //
        // TODO: we need to check for stack overflow here once we implement resizable stack
        //
        dstStackBase[0] = TValue::Create<tBool>(true);
        MoveArgumentsForCoroutine(dstStackBase + 1, sb, numArgs);
        // Pad x_minNilFillReturnValues - 1 nils
        //
        static_assert(x_minNilFillReturnValues == 3);
        dstStackBase[numArgs + 1] = TValue::Create<tNil>();
        dstStackBase[numArgs + 2] = TValue::Create<tNil>();

        CoroSwitch(targetCoro, dstStackBase, numArgs + 1);
    }
    else
    {
        Assert(dstHdr->m_numVariadicArguments == 1);
        // For coroutine.wrap, we should simply store all the return values
        //
        // TODO: we need to check for stack overflow here once we implement resizable stack
        //
        MoveArgumentsForCoroutine(dstStackBase, sb, numArgs);
        // Pad x_minNilFillReturnValues nils
        //
        static_assert(x_minNilFillReturnValues == 3);
        dstStackBase[numArgs] = TValue::Create<tNil>();
        dstStackBase[numArgs + 1] = TValue::Create<tNil>();
        dstStackBase[numArgs + 2] = TValue::Create<tNil>();

        CoroSwitch(targetCoro, dstStackBase, numArgs);
    }
}

DEEGEN_END_LIB_FUNC_DEFINITIONS
