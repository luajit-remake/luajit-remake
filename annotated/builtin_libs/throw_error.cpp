#include "deegen_api.h"
#include "runtime_utils.h"

// Design for pcall/xpcall:
//     There are two return function: 'onSuccessReturn' and 'onErrorReturn'.
//     pcall/xpcall always calls the callee using 'onSuccessReturn' as the return function.
//     So if the Lua code did not encounter an error, 'onSuccessReturn' will take control, which simply generates the pcall/xpcall return values for the success case and return to parent.
//     That is, the stack looks like this:
//     [ ... Lua ... ] [ pcall ] [ Lua function being called ] [ .. more call frames .. ]
//                                 ^
//                       return = onSuccessReturn

//     If the Lua code encounters an error, we will walk the stack to find the first stack frame with 'return = onSuccessReturn'.
//     This allows us to locate the pcall/xpcall call frame.
//
//     pcall/xpcall always reserve local variable slot 0. For pcall, the slot stores 'false', and for xpcall the slot stores 'true'.
//     This allows us to know if the frame is a pcall frame or a xpcall frame.
//
//     Now, for pcall, we can simply return 'false' plus the error object.
//     For xpcall, we will locate the error handler from the xpcall call frame, then call the error handler with return = onErrorReturn.
//     That is, the stack looks like this:
//     [ ... Lua ... ] [ xpcall ] [ Lua function being called ] [ .. more call frames .. ] [ function throwing error ] [ error handler ] .. [ error handler (due to error in error handler) ]
//                                  ^                                                                                    ^                    ^
//                       return = onSuccessReturn                                                               return = onErrorReturn   return = onErrorReturn
//
//     Then 'onErrorReturn' will eventually take control.
//     The 'onErrorReturn' function will actually unwind the stack until the first xpcall call frame it encounters (identified by 'onSuccessReturn').
//     Then it generates the xpcall return values for the error case, and return to the parent of xpcall
//
DEEGEN_DEFINE_LIB_FUNC_CONTINUATION(OnProtectedCallSuccessReturn)
{
    // This function is the normal return continuation of pcall/xpcall, so the stack base is the one for the pcall/xpcall
    //
    TValue* retStart = GetReturnValuesBegin();
    size_t numRets = GetNumReturnValues();

    // Return value should be 'true' plus everything returned by callee
    // Note that we reserved local variable slot 0 as a distinguisher between pcall/xpcall, so 'retStart' must be at least at slot 1,
    // so we can overwrite 'retStart[-1]' without worrying about clobbering anything
    //
    assert(retStart > GetStackBase());
    retStart[-1] = TValue::CreateBoolean(true);
    ReturnValueRange(retStart - 1, numRets + 1);
}

DEEGEN_DEFINE_LIB_FUNC_CONTINUATION(OnProtectedCallErrorReturn)
{
    // This return continuation is used by the error handler of 'xpcall', which is 'called' by the function throwing out the error.
    // So 'stackbase' here is the stack frame for the (most recent, if there are multiple outstanding exceptions) function throwing out the error
    //
    TValue* stackbase = GetStackBase();

    // Construct the return values now. Lua 5.1 doesn't have to-be-closed variables, so we can simply overwrite at 'stackbase'
    //
    // Note that Lua discards all but the first return value from error handler, and if error handler returns no value, a nil is added
    //
    if (GetNumReturnValues() == 0)
    {
        stackbase[0] = TValue::CreateFalse();
        stackbase[1] = TValue::Nil();
    }
    else
    {
        TValue val = GetReturnValuesBegin()[0];
        stackbase[0] = TValue::CreateFalse();
        stackbase[1] = val;
    }

    // Now we need to walk the stack and identify the xpcall call frame
    // This must be successful, since we won't be called unless the pcall/xpcall call frame has been identified by the error path
    //
    StackFrameHeader* hdr = GetStackFrameHeader();
    while (true)
    {
        if (hdr->m_retAddr == DEEGEN_LIB_FUNC_RETURN_CONTINUATION(OnProtectedCallSuccessReturn))
        {
            break;
        }
        hdr = reinterpret_cast<StackFrameHeader*>(hdr->m_caller) - 1;
        assert(hdr != nullptr);
    }

    // Now 'hdr' is the callee of the pcall/xpcall
    // We need to return to the caller of pcall/xpcall, so we need to walk up one more frame and return from there
    //
    hdr = reinterpret_cast<StackFrameHeader*>(hdr->m_caller) - 1;
    assert(hdr != nullptr);

    LongJump(hdr, stackbase /*retStart*/, 2 /*numRets*/);
}

DEEGEN_DEFINE_LIB_FUNC_CONTINUATION(coro_propagate_error_trampoline)
{
    assert(GetNumReturnValues() == 1);
    TValue errorObject = GetReturnValuesBegin()[0];
    ThrowError(errorObject);
}

// This is a fake library function that is used internally in deegen to implement the ThrowError API
// This is never directly called from outside, and the name is hardcoded
//
DEEGEN_DEFINE_LIB_FUNC(DeegenInternal_ThrowTValueErrorImpl)
{
    // We repurpose 'numArgs' to be the TValue storing the error object
    //
    TValue errorObject; errorObject.m_value = GetNumArgs();

    size_t nestedErrorCount = 0;
    StackFrameHeader* hdr = GetStackFrameHeader();
    while (true)
    {
        if (hdr->m_retAddr == DEEGEN_LIB_FUNC_RETURN_CONTINUATION(OnProtectedCallSuccessReturn))
        {
            break;
        }
        if (hdr->m_retAddr == DEEGEN_LIB_FUNC_RETURN_CONTINUATION(OnProtectedCallErrorReturn))
        {
            nestedErrorCount++;
        }
        hdr = reinterpret_cast<StackFrameHeader*>(hdr->m_caller);
        if (hdr == nullptr)
        {
            break;
        }
        hdr = hdr - 1;
    }

    if (hdr == nullptr)
    {
        // There is no pcall/xpcall on the stack
        // This means this coroutine encountered an uncaught error and should transition to "dead" state.
        // The error should be propagated to the parent coroutine, either as an error (if the parent
        // coroutine resumed this coroutine via coroutine.wrap), or as return value (if the parent coroutine
        // resumed this coroutine via coroutine.resume).
        //
        CoroutineRuntimeContext* currentCoro = GetCurrentCoroutine();
        assert(!currentCoro->m_coroutineStatus.IsDead() && !currentCoro->m_coroutineStatus.IsResumable());

        // Close all upvalues on the coroutine stack
        //
        currentCoro->CloseUpvalues(currentCoro->m_stackBegin);

        CoroutineRuntimeContext* parentCoro = currentCoro->m_parent;
        if (unlikely(parentCoro == nullptr))
        {
            // The current coroutine is already the root coroutine. This means the user did not
            // use xpcall/pcall to launch the root coroutine, and the root coroutine errored out.
            // In this case we should terminate execution.
            //
            // TODO: make the output message and behavior consistent with Lua in this case
            //
            FILE* fp = VM::GetActiveVMForCurrentThread()->GetStderr();
            fprintf(fp, "Uncaught error: ");
            PrintTValue(fp, errorObject);
            fprintf(fp, "\n");
            fclose(fp);
            // Also output to stderr if the VM's stderr is redirected to somewhere else
            //
            if (fp != stderr)
            {
                fprintf(stderr, "Uncaught error: ");
                PrintTValue(stderr, errorObject);
                fprintf(stderr, "\n");
                fflush(stderr);
            }
            exit(1);
        }

        assert(!parentCoro->m_coroutineStatus.IsDead() && !parentCoro->m_coroutineStatus.IsResumable());

        // Set the current coroutine dead
        //
        currentCoro->m_coroutineStatus.SetDead(true);

        // Check if the parent coroutine resumed the current coroutine via coroutine.wrap or coroutine.resume
        // DEVNOTE: this is currently accomplished by a hack that repurposes 'm_numVariadicArguments' of the
        // suspension call frame as a matter of distinguishment. 0 means coroutine.resume and 1 mean coroutine.wrap
        //
        TValue* dstStackBase = parentCoro->m_suspendPointStackBase;
        StackFrameHeader* dstHdr = StackFrameHeader::Get(dstStackBase);
        if (dstHdr->m_numVariadicArguments == 0)
        {
            // For coroutine.resume, the error should not be propagated further.
            // We should simply make coroutine.resume return 'false' plus the error object.
            // Note that we also need to pad nils to x_minNilFillReturnValues, as required by our internal call scheme.
            //
            // TODO: we need to check for stack overflow here once we implement resizable stack
            //
            dstStackBase[0] = TValue::Create<tBool>(false);
            dstStackBase[1] = errorObject;
            for (size_t i = 2; i < x_minNilFillReturnValues; i++)
            {
                dstStackBase[i] = TValue::Create<tNil>();
            }

            CoroSwitch(parentCoro, dstStackBase, 2 /*numArgs*/);
        }
        else
        {
            assert(dstHdr->m_numVariadicArguments == 1);
            // For coroutine.wrap, the error should be propagated as an error of coroutine.wrap
            // We achieve this by set up a trampoline frame above coroutine.wrap, switch coroutine
            // and return to the trampoline, and let the trampoline throw out the error again.
            //
            // This is not the most efficient implementation (clearly), but since this is the
            // error path, it should be fine.
            //
            StackFrameHeader* newHdr = dstHdr + 1;
            newHdr->m_func = nullptr;
            newHdr->m_caller = newHdr;  // stackBase of the old frame is just dstHdr + 1
            newHdr->m_retAddr = DEEGEN_LIB_FUNC_RETURN_CONTINUATION(coro_propagate_error_trampoline);
            newHdr->m_callerBytecodePtr.m_value = 0;
            newHdr->m_numVariadicArguments = 0;

            // Pass the error object as the first argument to the trampoline
            //
            TValue* newStackBase = reinterpret_cast<TValue*>(newHdr + 1);
            newStackBase[0] = errorObject;

            CoroSwitch(parentCoro, newStackBase, 1 /*numArgs*/);
        }
    }

    StackFrameHeader* protectedCallFrame = reinterpret_cast<StackFrameHeader*>(hdr->m_caller) - 1;
    assert(protectedCallFrame != nullptr);

    // Every upvalue >= the frame base of the pcall/xpcall needs to be closed
    //
    CoroutineRuntimeContext* currentCoro = GetCurrentCoroutine();
    currentCoro->CloseUpvalues(reinterpret_cast<TValue*>(protectedCallFrame + 1));

    bool isXpcall;
    {
        TValue* locals = reinterpret_cast<TValue*>(protectedCallFrame + 1);
        assert(locals[0].IsMIV() && locals[0].AsMIV().IsBoolean());
        isXpcall = locals[0].AsMIV().GetBooleanValue();
    }

    if (nestedErrorCount > x_lua_max_nested_error_count)
    {
        // Don't call error handler any more, treat this as if it's a pcall
        //
        errorObject = MakeErrorMessageForTooManyNestedErrors();
        goto handle_pcall;
    }

    if (isXpcall)
    {
        // We need to call error handler, the error handler is stored in local 1 of the xpcall
        //
        TValue* xpcallFramelocals = reinterpret_cast<TValue*>(protectedCallFrame + 1);
        TValue errHandler = xpcallFramelocals[1];

        // Lua 5.4 requires 'errHandler' to be a function.
        // Lua 5.1 doesn't require 'errHandler' to be a function, but ignores its metatable any way.
        // This function is not performance sensitive, so we will make it compatible for both (the xpcall API can check 'errHandler' to implement Lua 5.4 behavior)
        //
        if (!errHandler.Is<tFunction>())
        {
            // The error handler is not callable, so attempting to call it will result in infinite error recursion
            //
            errorObject = MakeErrorMessageForTooManyNestedErrors();
            goto handle_pcall;
        }

        // Set up the call frame
        //
        UserHeapPointer<FunctionObject> handler = errHandler.AsPointer<FunctionObject>();
        ExecutableCode* throwingFuncEc = TranslateToRawPointer(TCGet(hdr->m_func->m_executable).As());
        uint32_t stackFrameSize;
        if (throwingFuncEc->IsBytecodeFunction())
        {
            stackFrameSize = static_cast<CodeBlock*>(throwingFuncEc)->m_stackFrameNumSlots;
        }
        else
        {
            // ThrowError() is called from a C function.
            // Destroying all its locals should be fine, as the function should never be returned to any way
            //
            stackFrameSize = 0;
        }

        // TODO: is this really correct? It seems like hdr->m_func is the function called by xpcall, but it's
        // not necessarily the function that throws out an error, since the error could be thrown out in a callee.
        // I don't think this is a correctness issue since there's no "cleanup" step in Lua whatsoever, so leave
        // it as is fow now.
        //
        TValue* callFrameBegin = GetStackBase() + stackFrameSize;
        callFrameBegin[0] = TValue::CreatePointer(handler);
        callFrameBegin[x_numSlotsForStackFrameHeader] = errorObject;
        MakeInPlaceCall(callFrameBegin + x_numSlotsForStackFrameHeader, 1 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(OnProtectedCallErrorReturn));
    }
    else
    {
handle_pcall:
        // We should just return 'false' plus the error object
        //
        TValue* stackbase = GetStackBase();
        stackbase[0] = TValue::CreateFalse();
        stackbase[1] = errorObject;

        // We need to return to the caller of 'pcall'
        //
        LongJump(protectedCallFrame, stackbase /*retStart*/, 2 /*numReturnValues*/);
    }
}

// This is a fake library function that is used internally in deegen to implement the ThrowError API (C-string case)
// It is a simple wrapper of DeegenInternal_ThrowTValueErrorImpl
// This is never directly called from outside, and the name is hardcoded
//
DEEGEN_DEFINE_LIB_FUNC(DeegenInternal_ThrowCStringErrorImpl)
{
    // We repurpose 'numArgs' to be the TValue storing the C string
    //
    const char* errorMsg = reinterpret_cast<const char*>(GetNumArgs());
    ThrowError(MakeErrorMessage(errorMsg));
}

// base.xpcall -- https://www.lua.org/manual/5.1/manual.html#pdf-xpcall
//
// xpcall (f, err)
// This function is similar to pcall, except that you can set a new error handler.
//
// xpcall calls function f in protected mode, using err as the error handler. Any error inside f is not propagated; instead,
// xpcall catches the error, calls the err function with the original error object, and returns a status code. Its first result
// is the status code (a boolean), which is true if the call succeeds without errors. In this case, xpcall also returns all
// results from the call, after this first result. In case of any error, xpcall returns false plus the result from err.
//
DEEGEN_DEFINE_LIB_FUNC(base_xpcall)
{
    if (GetNumArgs() < 2)
    {
        // Lua always complains about argument #2 even if numArgs == 0, and this error is NOT protected by this xpcall itself
        //
        ThrowError("bad argument #2 to 'xpcall' (value expected)");
    }

    // Any error below should be protected by the xpcall
    //

    TValue* stackbase = GetStackBase();
    TValue calleeInput = GetArg(0);
    TValue errHandler = GetArg(1);

    // Write the identification boolean at local 0. This will be read by the stack walker
    // Note that the error handler happens to already be at local 1, so we don't need to do anything
    //
    stackbase[0] = TValue::CreateBoolean(true /*isXpcall*/);

    // 'callStart' should be at +2 because local 0 and 1 are all needed to be kept alive
    //
    TValue* callStart = stackbase + 2;
    if (likely(calleeInput.Is<tFunction>()))
    {
        callStart[0] = calleeInput;
        MakeInPlaceCall(callStart + x_numSlotsForStackFrameHeader /*argsBegin*/, 0 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(OnProtectedCallSuccessReturn));
    }

    HeapPtr<FunctionObject> callTarget = GetCallTargetViaMetatable(calleeInput);
    if (likely(callTarget != nullptr))
    {
        callStart[0] = TValue::Create<tFunction>(callTarget);
        callStart[x_numSlotsForStackFrameHeader] = calleeInput;
        MakeInPlaceCall(callStart + x_numSlotsForStackFrameHeader /*argsBegin*/, 1 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(OnProtectedCallSuccessReturn));
    }

    // The function is not callable, so we should call the error handler.
    // Now, check if the error handler is a FunctionObject
    // Note that Lua won't consider the metatable of the error handler: it must be a function in order to be called
    //
    if (errHandler.Is<tFunction>())
    {
        // The error handler is a function, so it shall be invoked.
        //
        // However, we cannot throw the error by ourselves, or call the error handler by ourselves: if we do that, since the error
        // is not thrown from the called function, but from xpcall itself, it will not be protected since there is no stack frame
        // with ret = 'onSuccessReturn', so neither 'onErrorReturn' nor the 'ThrowError' could see the xpcall.
        //
        // To workaround this, we will let ourselves call 'base.error' with our error object as argument.
        // Then 'base.error' will throw out that error for us, which will be protected and invoke our error handler, as desired.
        //
        TValue baseDotError = VM_GetLibFunctionObject<VM::LibFn::BaseError>();
        assert(baseDotError.Is<tFunction>());

        callStart[0] = baseDotError;
        callStart[x_numSlotsForStackFrameHeader] = MakeErrorMessageForUnableToCall(calleeInput);

        MakeInPlaceCall(callStart + x_numSlotsForStackFrameHeader /*argsBegin*/, 1 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(OnProtectedCallSuccessReturn));
    }
    else
    {
        // The error handler is not a function (note that Lua ignores the metatable here)
        // So calling the error handler will result in infinite error recursion
        // So we should return false + "error in error handler"
        //
        Return(TValue::CreateBoolean(false), MakeErrorMessageForTooManyNestedErrors());
    }
}

// base.pcall -- https://www.lua.org/manual/5.1/manual.html#pdf-pcall
//
// pcall (f, arg1, ···)
// Calls function f with the given arguments in protected mode. This means that any error inside f is not propagated; instead, pcall
// catches the error and returns a status code. Its first result is the status code (a boolean), which is true if the call succeeds
// without errors. In such case, pcall also returns all results from the call, after this first result. In case of any error, pcall
// returns false plus the error message.
//
DEEGEN_DEFINE_LIB_FUNC(base_pcall)
{
    if (GetNumArgs() == 0)
    {
        // This error is NOT protected by this pcall itself
        //
        ThrowError("bad argument #1 to 'pcall' (value expected)");
    }

    // Any error below should be protected by the pcall
    //

    TValue* stackbase = GetStackBase();
    TValue calleeInput = GetArg(0);
    size_t numCalleeArgs = GetNumArgs() - 1;

    // Set up the call frame, which can start at local 1
    // (local 0 is reserved by us as the identification boolean for the stack walker to distinguish pcall and xpcall)
    //
    TValue* callFrameBegin = stackbase + 1;
    if (likely(calleeInput.Is<tFunction>()))
    {
        memmove(callFrameBegin + x_numSlotsForStackFrameHeader, stackbase + 1 /*inputArgsBegin*/, sizeof(TValue) * numCalleeArgs);
        callFrameBegin[0] = calleeInput;
    }
    else
    {
        HeapPtr<FunctionObject> callTarget = GetCallTargetViaMetatable(calleeInput);
        if (unlikely(callTarget == nullptr))
        {
            // The function is not callable, we should return false + 'unable to call' error msg
            //
            Return(TValue::CreateBoolean(false), MakeErrorMessageForUnableToCall(calleeInput));
        }

        memmove(callFrameBegin + x_numSlotsForStackFrameHeader + 1, stackbase + 1 /*inputArgsBegin*/, sizeof(TValue) * numCalleeArgs);
        callFrameBegin[x_numSlotsForStackFrameHeader] = calleeInput;
        callFrameBegin[0] = TValue::Create<tFunction>(callTarget);
        numCalleeArgs++;
    }

    // Write the identification boolean at local 0. This will be read by the stack walker
    //
    stackbase[0] = TValue::CreateBoolean(false /*isXpcall*/);

    MakeInPlaceCall(callFrameBegin + x_numSlotsForStackFrameHeader /*argsBegin*/, numCalleeArgs, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(OnProtectedCallSuccessReturn));
}

// base.error -- https://www.lua.org/manual/5.1/manual.html#pdf-error
// error (message [, level])
//     Terminates the last protected function called and returns message as the error message. Function error never returns.
//     Usually, error adds some information about the error position at the beginning of the message. The level argument specifies
//     how to get the error position. With level 1 (the default), the error position is where the error function was called. Level 2
//     points the error to where the function that called error was called; and so on. Passing a level 0 avoids the addition of error
//     position information to the message.
//
// TODO: the 'level' argument is unsupported yet.
//
DEEGEN_DEFINE_LIB_FUNC(base_error)
{
    TValue errorObject;
    if (GetNumArgs() == 0)
    {
        errorObject = TValue::Nil();
    }
    else
    {
        errorObject = GetArg(0);
    }

    ThrowError(errorObject);
}

DEEGEN_END_LIB_FUNC_DEFINITIONS
