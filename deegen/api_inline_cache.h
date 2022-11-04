#pragma once

#include "common_utils.h"
#include "deegen_preserve_lambda_helper.h"

struct ICHandler;
template<typename ICKeyType> struct ICHandlerKeyRef;

template<typename ICKeyType> ICHandlerKeyRef<ICKeyType>* DeegenImpl_MakeIC_AddKey(ICHandler* ic, ICKeyType icKey);
template<typename ICKeyType> void DeegenImpl_MakeIC_SetICKeyImpossibleValue(ICHandlerKeyRef<ICKeyType>* ickey, ICKeyType value);
template<typename ResType> ResType DeegenImpl_MakeIC_SetMainLambda(ICHandler* ic, const void* cp, const void* fpp);
template<typename ResType> ResType DeegenImpl_MakeIC_MarkEffect(ICHandler* ic, const void* cp, const void* fpp);
template<typename ResType> ResType DeegenImpl_MakeIC_MarkEffectValue(ICHandler* ic, const ResType& value);
void DeegenImpl_MakeIC_SetUncacheableForThisExecution(ICHandler* ic);
template<typename ICCaptureType, typename... Rest> void DeegenImpl_MakeIC_SpecializeIcEffect(bool isFullCoverage, const ICCaptureType* capture, Rest... values);
void DeegenImpl_MakeIC_SetShouldFuseICIntoInterpreterOpcode(ICHandler* ic);

template<typename ICKeyType>
struct ICHandlerKeyRef
{
    MAKE_NONCOPYABLE(ICHandlerKeyRef);
    MAKE_NONMOVABLE(ICHandlerKeyRef);

    ICHandlerKeyRef& ALWAYS_INLINE SpecifyImpossibleValue(ICKeyType value)
    {
        DeegenImpl_MakeIC_SetICKeyImpossibleValue(this, value);
        return *this;
    }
};

struct ICHandler
{
    MAKE_NONCOPYABLE(ICHandler);
    MAKE_NONMOVABLE(ICHandler);

    // Specify the key that the IC is caching on.
    // Currently only one key is supported, and it must be an integral type
    //
    template<typename ICKeyType>
    ICHandlerKeyRef<ICKeyType>& ALWAYS_INLINE AddKey(ICKeyType icKey)
    {
        static_assert(std::is_integral_v<ICKeyType> && !std::is_same_v<ICKeyType, bool>);
        ICHandlerKeyRef<ICKeyType>* r = DeegenImpl_MakeIC_AddKey(this, icKey);
        return *r;
    }

    // Fuse the IC cached effect kind into the interpreter opcode.
    // This removes one (often indirect) branch for the IC hit case in the interpreter, and improves
    // code locality for each IC hit case (but increases overall code size as more code is produced overall).
    //
    // The following additional restrictions must be met, otherwise it's undefined behavior:
    // (1) At most one IC in the function can have this attribute.
    // (2) The IC may only be executed at most once in any possible execution path (that is, it must not show up in a loop).
    //
    void ALWAYS_INLINE FuseICIntoInterpreterOpcode()
    {
        DeegenImpl_MakeIC_SetShouldFuseICIntoInterpreterOpcode(this);
    }

    // Specify the main body of the IC
    //
    template<typename LambdaType>
    std::invoke_result_t<LambdaType> ALWAYS_INLINE Body(const LambdaType& lambda)
    {
        using ResType = std::invoke_result_t<LambdaType>;
        static_assert(!is_lambda_mutable_v<LambdaType>, "The lambda must not be declared with 'mutable' keyword!");
        // static_assert(std::is_trivially_copyable_v<ResType>);
        return DeegenImpl_MakeIC_SetMainLambda<ResType>(this, DeegenGetLambdaClosureAddr(lambda), DeegenGetLambdaFunctorPP(lambda));
    }

    // Can only be used in the main lambda
    // Denote that this execution will not create an IC entry even if Effect/EffectValue is called
    // It is illegal to use SetUncacheable() after Effect/EffectValue
    //
    void ALWAYS_INLINE SetUncacheable()
    {
        DeegenImpl_MakeIC_SetUncacheableForThisExecution(this);
    }

    // Can only be used in the main lambda.
    // At most one 'Effect' call may be executed in every possible execution path.
    // Creates an IC entry with the given lambda.
    //
    template<typename LambdaType>
    std::invoke_result_t<LambdaType> ALWAYS_INLINE Effect(const LambdaType& lambda)
    {
        using ResType = std::invoke_result_t<LambdaType>;
        static_assert(!is_lambda_mutable_v<LambdaType>, "The lambda must not be declared with 'mutable' keyword!");
        // static_assert(std::is_trivially_copyable_v<ResType>);
        return DeegenImpl_MakeIC_MarkEffect<ResType>(this, DeegenGetLambdaClosureAddr(lambda), DeegenGetLambdaFunctorPP(lambda));
    }
};

// Create an inline cache.
// All the API calls between MakeInlineCache() and ICHandler::Body() must be unconditionally executed
// (e.g., you cannot do 'if (...) { ic->AddKey(...) }'), otherwise the behavior is undefined.
//
ICHandler* WARN_UNUSED __attribute__((__nomerge__)) MakeInlineCache();

// May only be used inside the effect lambda.
// Generate specialization for 'capture' == each 'value', and the default generic fallback
// for the case that 'capture' is not among the specialized 'value' list.
//
// For example, if some int value is specialized for '0' and '1', then three effect lambdas
// will be generated: one for the == 0 case, one for the == 1 case, and one for the generic case.
//
// If this is called multiple times (on different captured values), all those specializations will
// compose as a Cartesian product.
//
template<typename T, typename... Ts>
void ALWAYS_INLINE IcSpecializeValue(const T& capture, Ts... values)
{
    static_assert(sizeof...(Ts) > 0, "You need to provide at least one specialization value.");
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>, "Currently only specializing integer or boolean types is supported!");
    if constexpr(std::is_integral_v<T>)
    {
        DeegenImpl_MakeIC_SpecializeIcEffect(false /*isFullCoverage*/, &capture, static_cast<T>(values)...);
    }
    else
    {
        using U = std::underlying_type_t<T>;
        DeegenImpl_MakeIC_SpecializeIcEffect(false /*isFullCoverage*/, reinterpret_cast<const U*>(&capture), static_cast<U>(values)...);
    }
}

// Similar to 'IcSpecializeValue', except that the default generic fallback is not generated.
// That is, if at runtime the value of 'capture' is not among the list of values, the behavior is undefined.
//
template<typename T, typename... Ts>
void ALWAYS_INLINE IcSpecializeValueFullCoverage(const T& capture, Ts... values)
{
    static_assert(sizeof...(Ts) > 1, "If you do SpecializeFullCoverage, you should provide at least 2 specialization values (if you only provide one value, then you are effectively saying it's already known as a constant!)");
    static_assert(std::is_integral_v<T>|| std::is_enum_v<T>, "Currently only specializing integer or boolean types is supported!");
    if constexpr(std::is_integral_v<T>)
    {
        DeegenImpl_MakeIC_SpecializeIcEffect(true /*isFullCoverage*/, &capture, static_cast<T>(values)...);
    }
    else
    {
        using U = std::underlying_type_t<T>;
        DeegenImpl_MakeIC_SpecializeIcEffect(true /*isFullCoverage*/, reinterpret_cast<const U*>(&capture), static_cast<U>(values)...);
    }
}
