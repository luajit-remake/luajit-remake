#pragma once

#include "tvalue.h"
#include "api_define_lib_function.h"
#include "deegen_preserve_lambda_helper.h"

class CodeBlock;

enum MakeCallOption
{
    NoOption,
    DontProfileInInterpreter
};

// These names are hardcoded for our LLVM IR processor to locate
//
extern "C" void NO_RETURN DeegenImpl_ReturnValue(TValue value);
extern "C" void NO_RETURN DeegenImpl_ReturnNone();
// extern "C" void* WARN_UNUSED DeegenImpl_GetVMBasePointer();
extern "C" void NO_RETURN DeegenImpl_ReturnValueAndBranch(TValue value);
extern "C" void NO_RETURN DeegenImpl_ReturnNoneAndBranch();
extern "C" void NO_RETURN DeegenImpl_MakeCall_ReportContinuationAfterCall(void* handler, void* func);
extern "C" void* WARN_UNUSED DeegenImpl_MakeCall_ReportParam(void* handler, TValue arg);
extern "C" void* WARN_UNUSED DeegenImpl_MakeCall_ReportParamList(void* handler, const TValue* argBegin, size_t numArgs);
extern "C" void* WARN_UNUSED DeegenImpl_MakeCall_ReportTarget(void* handler, uint64_t target);
extern "C" void* WARN_UNUSED DeegenImpl_MakeCall_ReportOption(void* handler, size_t option);
extern "C" void* WARN_UNUSED DeegenImpl_StartMakeCallInfo();
extern "C" void* WARN_UNUSED DeegenImpl_StartMakeCallPassingVariadicResInfo();
extern "C" void* WARN_UNUSED DeegenImpl_StartMakeInPlaceCallInfo();
extern "C" void* WARN_UNUSED DeegenImpl_StartMakeInPlaceCallPassingVariadicResInfo();
extern "C" void* WARN_UNUSED DeegenImpl_StartMakeTailCallInfo();
extern "C" void* WARN_UNUSED DeegenImpl_StartMakeTailCallPassingVariadicResInfo();
extern "C" void* WARN_UNUSED DeegenImpl_StartMakeInPlaceTailCallInfo();
extern "C" void* WARN_UNUSED DeegenImpl_StartMakeInPlaceTailCallPassingVariadicResInfo();
extern "C" void NO_RETURN DeegenImpl_ThrowErrorTValue(TValue value);
extern "C" void NO_RETURN DeegenImpl_ThrowErrorCString(const char* value);
extern "C" TableObject* WARN_UNUSED DeegenImpl_GetFEnvGlobalObject();
extern "C" void NO_RETURN DeegenImpl_GuestLanguageFunctionReturn_NoValue();
extern "C" void NO_RETURN DeegenImpl_GuestLanguageFunctionReturn(const TValue* retStart, size_t numRets);
extern "C" void NO_RETURN DeegenImpl_GuestLanguageFunctionReturnAppendingVariadicResults(const TValue* retStart, size_t numRets);
extern "C" FunctionObject* WARN_UNUSED DeegenImpl_CreateNewClosure(CodeBlock* cb, size_t selfBytecodeSlotOrdinal);
extern "C" size_t WARN_UNUSED ALWAYS_INLINE DeegenImpl_GetOutputBytecodeSlotOrdinal();
TValue WARN_UNUSED DeegenImpl_UpvalueAccessor_GetMutable(size_t ord);
TValue WARN_UNUSED DeegenImpl_UpvalueAccessor_GetImmutable(size_t ord);
void DeegenImpl_UpvalueAccessor_Put(size_t ord, TValue valueToPut);
void DeegenImpl_UpvalueAccessor_Close(const TValue* limit);
extern "C" TValue* WARN_UNUSED DeegenImpl_GetVarArgsStart();
extern "C" size_t WARN_UNUSED DeegenImpl_GetNumVarArgs();
extern "C" void DeegenImpl_StoreVarArgsAsVariadicResults();
extern "C" TValue* WARN_UNUSED DeegenImpl_GetVariadicResultsStart();
extern "C" size_t WARN_UNUSED DeegenImpl_GetNumVariadicResults();
template<typename... Args> void NO_RETURN __attribute__((__nomerge__)) DeegenImpl_MarkEnterSlowPath(Args... args);

// Return zero or one value as the result of the operation
//
inline void ALWAYS_INLINE NO_RETURN Return(TValue value)
{
    DeegenImpl_ReturnValue(value);
}

inline void ALWAYS_INLINE NO_RETURN Return()
{
    DeegenImpl_ReturnNone();
}

// Return the result of the current bytecode, and additionally informs that control flow should not fallthrough to
// the next bytecode, but redirected to the conditional branch target of this bytecode
//
inline void ALWAYS_INLINE NO_RETURN ReturnAndBranch(TValue value)
{
    DeegenImpl_ReturnValueAndBranch(value);
}

inline void ALWAYS_INLINE NO_RETURN ReturnAndBranch()
{
    DeegenImpl_ReturnNoneAndBranch();
}

inline void ALWAYS_INLINE NO_RETURN ThrowError(TValue value)
{
    DeegenImpl_ThrowErrorTValue(value);
}

inline void ALWAYS_INLINE NO_RETURN ThrowError(const char* value)
{
    DeegenImpl_ThrowErrorCString(value);
}

// Get the global object captured by the current function
//
inline TableObject* WARN_UNUSED ALWAYS_INLINE GetFEnvGlobalObject()
{
    return DeegenImpl_GetFEnvGlobalObject();
}

inline void ALWAYS_INLINE NO_RETURN GuestLanguageFunctionReturn()
{
    DeegenImpl_GuestLanguageFunctionReturn_NoValue();
}

inline void ALWAYS_INLINE NO_RETURN GuestLanguageFunctionReturn(const TValue* retStart, size_t numRets)
{
    DeegenImpl_GuestLanguageFunctionReturn(retStart, numRets);
}

inline void ALWAYS_INLINE NO_RETURN GuestLanguageFunctionReturnAppendingVariadicResults(const TValue* retStart, size_t numRets)
{
    DeegenImpl_GuestLanguageFunctionReturnAppendingVariadicResults(retStart, numRets);
}

// 'selfBytecodeSlotOrdinal' is the bytecode slot ordinal where this closure is going to be stored to.
// This is to solve a chicken-and-egg problem: the upvalues of the newly-created function are allowed to
// reference the newly-created function itself, and the value may be read from the stack frame.
// But at the moment the function is being created, its value hasn't been stored to the stack frame yet.
//
// Therefore, we must manually check if the upvalue is a self-reference and handle this case specially.
//
inline FunctionObject* WARN_UNUSED ALWAYS_INLINE CreateNewClosure(CodeBlock* cb, size_t selfBytecodeSlotOrdinal)
{
    return DeegenImpl_CreateNewClosure(cb, selfBytecodeSlotOrdinal);
}

inline size_t WARN_UNUSED ALWAYS_INLINE GetOutputBytecodeSlotOrdinal()
{
    return DeegenImpl_GetOutputBytecodeSlotOrdinal();
}

struct UpvalueAccessor
{
    static TValue WARN_UNUSED ALWAYS_INLINE GetMutable(size_t ord)
    {
        return DeegenImpl_UpvalueAccessor_GetMutable(ord);
    }

    static TValue WARN_UNUSED ALWAYS_INLINE GetImmutable(size_t ord)
    {
        return DeegenImpl_UpvalueAccessor_GetImmutable(ord);
    }

    static void ALWAYS_INLINE Put(size_t ord, TValue valueToPut)
    {
        DeegenImpl_UpvalueAccessor_Put(ord, valueToPut);
    }

    // Close all upvalues >= limit
    //
    static void ALWAYS_INLINE Close(const TValue* limit)
    {
        DeegenImpl_UpvalueAccessor_Close(limit);
    }
};

struct VarArgsAccessor
{
    static TValue* WARN_UNUSED ALWAYS_INLINE GetPtr()
    {
        return DeegenImpl_GetVarArgsStart();
    }

    static size_t WARN_UNUSED ALWAYS_INLINE GetNum()
    {
        return DeegenImpl_GetNumVarArgs();
    }

    static void ALWAYS_INLINE StoreAllVarArgsAsVariadicResults()
    {
        return DeegenImpl_StoreVarArgsAsVariadicResults();
    }
};

struct VariadicResultsAccessor
{
    static TValue* WARN_UNUSED ALWAYS_INLINE GetPtr()
    {
        return DeegenImpl_GetVariadicResultsStart();
    }

    static size_t WARN_UNUSED ALWAYS_INLINE GetNum()
    {
        return DeegenImpl_GetNumVariadicResults();
    }
};

namespace detail {

template<typename... Args>
struct MakeCallArgHandlerImpl;

template<>
struct MakeCallArgHandlerImpl<std::nullptr_t>
{
    static void NO_RETURN ALWAYS_INLINE handle(void* handler, std::nullptr_t /*nullptr*/)
    {
        DeegenImpl_MakeCall_ReportContinuationAfterCall(handler, nullptr);
    }
};

template<typename... ContinuationFnArgs>
struct MakeCallArgHandlerImpl<void(*)(ContinuationFnArgs...)>
{
    static void NO_RETURN ALWAYS_INLINE handle(void* handler, void(*func)(ContinuationFnArgs...))
    {
        DeegenImpl_MakeCall_ReportContinuationAfterCall(handler, reinterpret_cast<void*>(func));
    }
};

template<typename... ContinuationFnArgs>
struct MakeCallArgHandlerImpl<NO_RETURN void(*)(ContinuationFnArgs...)>
{
    static void NO_RETURN ALWAYS_INLINE handle(void* handler, void(*func)(ContinuationFnArgs...))
    {
        DeegenImpl_MakeCall_ReportContinuationAfterCall(handler, reinterpret_cast<void*>(func));
    }
};

template<typename... Args>
struct MakeCallArgHandlerImpl<TValue, Args...>
{
    static void NO_RETURN ALWAYS_INLINE handle(void* handler, TValue arg, Args... remainingArgs)
    {
        MakeCallArgHandlerImpl<Args...>::handle(DeegenImpl_MakeCall_ReportParam(handler, arg), remainingArgs...);
    }
};

template<typename T, typename... Args>
struct MakeCallArgHandlerImpl<TValue*, T, Args...>
{
    static_assert(std::is_integral_v<T> && !std::is_signed_v<T> && !std::is_same_v<T, bool>);

    static void NO_RETURN ALWAYS_INLINE handle(void* handler, TValue* argBegin, T numArgs, Args... remainingArgs)
    {
        MakeCallArgHandlerImpl<Args...>::handle(DeegenImpl_MakeCall_ReportParamList(handler, argBegin, static_cast<size_t>(numArgs)), remainingArgs...);
    }
};

template<typename T, typename... Args>
struct MakeCallArgHandlerImpl<const TValue*, T, Args...>
{
    static_assert(std::is_integral_v<T> && !std::is_signed_v<T> && !std::is_same_v<T, bool>);

    static void NO_RETURN ALWAYS_INLINE handle(void* handler, const TValue* argBegin, T numArgs, Args... remainingArgs)
    {
        MakeCallArgHandlerImpl<Args...>::handle(DeegenImpl_MakeCall_ReportParamList(handler, argBegin, static_cast<size_t>(numArgs)), remainingArgs...);
    }
};

template<typename... Args>
struct MakeCallHandlerImpl;

template<typename... Args>
struct MakeCallHandlerImpl<FunctionObject*, Args...>
{
    static void NO_RETURN ALWAYS_INLINE handle(void* handler, FunctionObject* target, Args... args)
    {
        MakeCallArgHandlerImpl<Args...>::handle(DeegenImpl_MakeCall_ReportTarget(handler, reinterpret_cast<uint64_t>(target)), args...);
    }
};

template<typename... Args>
struct MakeCallHandlerImpl<MakeCallOption, FunctionObject*, Args...>
{
    static void NO_RETURN ALWAYS_INLINE handle(void* handler, MakeCallOption option, FunctionObject* target, Args... args)
    {
        MakeCallHandlerImpl<FunctionObject*, Args...>::handle(DeegenImpl_MakeCall_ReportOption(handler, static_cast<size_t>(option)), target, args...);
    }
};

constexpr size_t x_stackFrameHeaderSlots = 4;

template<typename... ContinuationFnArgs>
void NO_RETURN ALWAYS_INLINE ReportInfoForInPlaceCall(void* handler, FunctionObject* target, TValue* argsBegin, size_t numArgs, void(*continuationFn)(ContinuationFnArgs...))
{
    handler = DeegenImpl_MakeCall_ReportTarget(handler, reinterpret_cast<uint64_t>(target));
    handler = DeegenImpl_MakeCall_ReportParamList(handler, argsBegin, numArgs);
    DeegenImpl_MakeCall_ReportContinuationAfterCall(handler, reinterpret_cast<void*>(continuationFn));
}

inline void NO_RETURN ALWAYS_INLINE ReportInfoForInPlaceTailCall(void* handler, FunctionObject* target, TValue* argsBegin, size_t numArgs)
{
    handler = DeegenImpl_MakeCall_ReportTarget(handler, reinterpret_cast<uint64_t>(target));
    handler = DeegenImpl_MakeCall_ReportParamList(handler, argsBegin, numArgs);
    DeegenImpl_MakeCall_ReportContinuationAfterCall(handler, nullptr);
}

}   // namespace detail

// Make a call to a guest language function, with pre-layouted frame holding the stack frame header and the arguments.
// Specifically, a StackFrameHeader must already have been reserved right before argsBegin (i.e., in memory region
// argsBegin[-stackFrameHdrSize, 0) ), and argsBegin[0, numArgs) must hold all the arguments.
// Note that this also implies that everything >= argsBegin - stackFrameHdrSize are invalidated after the call
//
template<typename... ContinuationFnArgs>
void NO_RETURN ALWAYS_INLINE MakeInPlaceCall(FunctionObject* target, TValue* argsBegin, size_t numArgs, void(*continuationFn)(ContinuationFnArgs...))
{
    detail::ReportInfoForInPlaceCall(DeegenImpl_StartMakeInPlaceCallInfo(), target, argsBegin, numArgs, continuationFn);
}

// Same as above, except that the variadic results from the immediate preceding opcode are appended to the end of the argument list
//
template<typename... ContinuationFnArgs>
void NO_RETURN ALWAYS_INLINE MakeInPlaceCallPassingVariadicRes(FunctionObject* target, TValue* argsBegin, size_t numArgs, void(*continuationFn)(ContinuationFnArgs...))
{
    detail::ReportInfoForInPlaceCall(DeegenImpl_StartMakeInPlaceCallPassingVariadicResInfo(), target, argsBegin, numArgs, continuationFn);
}

// The tail call versions
//
inline void NO_RETURN ALWAYS_INLINE MakeInPlaceTailCall(FunctionObject* target, TValue* argsBegin, size_t numArgs)
{
    detail::ReportInfoForInPlaceTailCall(DeegenImpl_StartMakeInPlaceTailCallInfo(), target, argsBegin, numArgs);
}

inline void NO_RETURN ALWAYS_INLINE MakeInPlaceTailCallPassingVariadicRes(FunctionObject* target, TValue* argsBegin, size_t numArgs)
{
    detail::ReportInfoForInPlaceTailCall(DeegenImpl_StartMakeInPlaceTailCallPassingVariadicResInfo(), target, argsBegin, numArgs);
}

// Make a call to a guest language function.
// The new call frame is set up at the end of the current function's stack frame.
// This requires copying, so it's less efficient, but also more flexible.
//
// The parameters are (in listed order):
// 1. An optional Option flag
// 2. The target function (a FunctionObject* value)
// 3. The arguments, described as a list of TValue and (TValue*, size_t)
// 4. The continuation function
//
template<typename... Args>
void NO_RETURN ALWAYS_INLINE MakeCall(Args... args)
{
    detail::MakeCallHandlerImpl<Args...>::handle(DeegenImpl_StartMakeCallInfo(), args...);
}

// Same as above, except additionally passing varret
//
template<typename... Args>
void NO_RETURN ALWAYS_INLINE MakeCallPassingVariadicRes(Args... args)
{
    detail::MakeCallHandlerImpl<Args...>::handle(DeegenImpl_StartMakeCallPassingVariadicResInfo(), args...);
}

// The tail call versions
// Note that we require TailCall to be never mixed with other types of terminal API (except throw).
// That is, you cannot write a bytecode that performs a tail call in one path but a Return() in another path.
// If a bytecode may perform a TailCall, it must end with a TailCall (or Throw) in every possible control flow path.
//
template<typename... Args>
void NO_RETURN ALWAYS_INLINE MakeTailCall(Args... args)
{
    detail::MakeCallHandlerImpl<Args..., std::nullptr_t>::handle(DeegenImpl_StartMakeTailCallInfo(), args..., nullptr);
}

template<typename... Args>
void NO_RETURN ALWAYS_INLINE MakeTailCallPassingVariadicRes(Args... args)
{
    detail::MakeCallHandlerImpl<Args..., std::nullptr_t>::handle(DeegenImpl_StartMakeTailCallPassingVariadicResInfo(), args..., nullptr);
}

// These names are hardcoded for our LLVM IR processor to locate
//
TValue DeegenImpl_GetReturnValueAtOrd(size_t ord);
size_t DeegenImpl_GetNumReturnValues();
void DeegenImpl_StoreReturnValuesTo(TValue* dst, size_t numToStore);
void DeegenImpl_StoreReturnValuesAsVariadicResults();

// APIs for accessing return values
//
inline TValue WARN_UNUSED ALWAYS_INLINE GetReturnValue(size_t ord)
{
    return DeegenImpl_GetReturnValueAtOrd(ord);
}

inline size_t WARN_UNUSED ALWAYS_INLINE GetNumReturnValues()
{
    return DeegenImpl_GetNumReturnValues();
}

// Store the first 'numToStore' return values to the destination address, padding nil as needed
//
inline void ALWAYS_INLINE StoreReturnValuesTo(TValue* dst, size_t numToStore)
{
    DeegenImpl_StoreReturnValuesTo(dst, numToStore);
}

inline void ALWAYS_INLINE StoreReturnValuesAsVariadicResults()
{
    DeegenImpl_StoreReturnValuesAsVariadicResults();
}

namespace detail {

// Cast each argument to the type expected by the slow path function
//
template<size_t ord, auto func, typename Arg, typename... Args>
void ALWAYS_INLINE NO_RETURN EnterSlowPathApiImpl(Arg arg1, Args... args)
{
    using Func = decltype(func);
    static_assert(std::is_trivially_copyable_v<Arg>);
    if constexpr(ord == sizeof...(Args) + 1)
    {
        DeegenImpl_MarkEnterSlowPath(func, arg1, args...);
    }
    else
    {
        static_assert(ord < sizeof...(Args) + 1);
        using expected_arg_t = arg_nth_t<Func, num_args_in_function<Func> - sizeof...(Args) - 1 + ord>;
        EnterSlowPathApiImpl<ord + 1, func>(args..., static_cast<expected_arg_t>(arg1));
    }
}

}   // namespace detail

// Creates a slow path. The logic in the slow path will be separated out into a dedicated slow path function.
// This helps improve code locality, and can also slightly improve the fast path code by reducing unnecessary
// register shuffling, spilling and stack pointer adjustments.
//
// Note that the slow path will be executed as a tail call, so the lifetime of all the local variables in the
// interpreter function has ended when the slow path lambda is executed. That is, it is illegal for the slow path
// to use any variable defined in the interpreter function by reference.
//
template<auto func, typename... Args>
void ALWAYS_INLINE NO_RETURN EnterSlowPath(Args... args)
{
    using Func = decltype(func);
    static_assert(num_args_in_function<Func> >= sizeof...(Args));

    // This will be problematic if the slow path function has more than 6 arguments and contains non-word-sized
    // structure types, as under Linux ABI those structure won't be decomposed so we'll have a hard time to figure
    // out how to reconstruct them at LLVM IR level. But for now let's stay simple since we don't have such use cases.
    //
    if constexpr(sizeof...(Args) == 0)
    {
        DeegenImpl_MarkEnterSlowPath(func);
    }
    else
    {
        detail::EnterSlowPathApiImpl<0, func>(args...);
    }
}
