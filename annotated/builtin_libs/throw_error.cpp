#include "deegen_api.h"
#include "bytecode.h"

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
        abort();
    }

    StackFrameHeader* protectedCallFrame = reinterpret_cast<StackFrameHeader*>(hdr->m_caller) - 1;
    assert(protectedCallFrame != nullptr);

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

// Lua standard library base.xpcall
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
    GetCallTargetConsideringMetatableResult res = GetCallTargetConsideringMetatable(calleeInput);
    if (res.m_target.m_value == 0)
    {
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
            TValue baseDotError = VM::GetActiveVMForCurrentThread()->GetLibBaseDotErrorFunctionObject();
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
    else
    {
        // Now we know the function is callable, call it
        //
        size_t numParams = 0;
        if (!res.m_invokedThroughMetatable)
        {
            callStart[0] = calleeInput;
        }
        else
        {
            callStart[0] = TValue::CreatePointer(res.m_target);
            callStart[x_numSlotsForStackFrameHeader] = calleeInput;
            numParams++;
        }
        MakeInPlaceCall(callStart + x_numSlotsForStackFrameHeader /*argsBegin*/, numParams, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(OnProtectedCallSuccessReturn));
    }
}

// Lua standard library base.pcall
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

    GetCallTargetConsideringMetatableResult res = GetCallTargetConsideringMetatable(calleeInput);
    if (res.m_target.m_value == 0)
    {
        // The function is not callable, we should return false + 'unable to call' error msg
        //
        Return(TValue::CreateBoolean(false), MakeErrorMessageForUnableToCall(calleeInput));
    }

    // Now we know the function is callable, set up the call frame, which can start at local 1
    // (because local 0 is reserved by us as the identification boolean for the stack walker to distinguish pcall and xpcall)
    //
    TValue* callFrameBegin = stackbase + 1;
    if (likely(!res.m_invokedThroughMetatable))
    {
        memmove(callFrameBegin + x_numSlotsForStackFrameHeader, stackbase + 1 /*inputArgsBegin*/, sizeof(TValue) * numCalleeArgs);
        callFrameBegin[0] = calleeInput;
    }
    else
    {
        memmove(callFrameBegin + x_numSlotsForStackFrameHeader + 1, stackbase + 1 /*inputArgsBegin*/, sizeof(TValue) * numCalleeArgs);
        callFrameBegin[x_numSlotsForStackFrameHeader] = calleeInput;
        callFrameBegin[0] = TValue::CreatePointer(res.m_target);
        numCalleeArgs++;
    }

    // Write the identification boolean at local 0. This will be read by the stack walker
    //
    stackbase[0] = TValue::CreateBoolean(false /*isXpcall*/);

    MakeInPlaceCall(callFrameBegin + x_numSlotsForStackFrameHeader /*argsBegin*/, numCalleeArgs, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(OnProtectedCallSuccessReturn));
}

DEEGEN_END_LIB_FUNC_DEFINITIONS
