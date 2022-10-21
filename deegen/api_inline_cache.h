#pragma once

#include "common_utils.h"
#include "api_preserve_lambda_helper.h"

struct ICHandler;
template<typename ICKeyType> struct ICHandlerKeyRef;

template<typename ICKeyType> ICHandlerKeyRef<ICKeyType>* DeegenImpl_MakeIC_AddKey(ICHandler* ic, ICKeyType icKey);
template<typename ICKeyType> void DeegenImpl_MakeIC_SetICKeyImpossibleValue(ICHandlerKeyRef<ICKeyType>* ickey, ICKeyType value);
template<typename ResType> ResType DeegenImpl_MakeIC_SetMainLambda(ICHandler* ic, const void* cp, const void* fpp);
template<typename ResType> ResType DeegenImpl_MakeIC_MarkEffect(ICHandler* ic, const void* cp, const void* fpp);
template<typename ResType> ResType DeegenImpl_MakeIC_MarkEffectValue(ICHandler* ic, const ResType& value);
void DeegenImpl_MakeIC_SetUncacheableForThisExecution(ICHandler* ic);

template<typename ICKeyType>
struct ICHandlerKeyRef
{
    MAKE_NONCOPYABLE(ICHandlerKeyRef);
    MAKE_NONMOVABLE(ICHandlerKeyRef);

    ICHandlerKeyRef& ALWAYS_INLINE SetImpossibleValue(ICKeyType value)
    {
        DeegenImpl_MakeIC_SetICKeyImpossibleValue(this, value);
        return *this;
    }
};

struct ICHandler
{
    MAKE_NONCOPYABLE(ICHandler);
    MAKE_NONMOVABLE(ICHandler);

    template<typename ICKeyType>
    ICHandlerKeyRef<ICKeyType>& ALWAYS_INLINE AddKey(ICKeyType icKey)
    {
        static_assert(std::is_integral_v<ICKeyType> && !std::is_same_v<ICKeyType, bool>);
        ICHandlerKeyRef<ICKeyType>* r = DeegenImpl_MakeIC_AddKey(this, icKey);
        return *r;
    }

    template<typename LambdaType>
    std::invoke_result_t<LambdaType> ALWAYS_INLINE Body(const LambdaType& lambda)
    {
        using ResType = std::invoke_result_t<LambdaType>;
        static_assert(std::is_trivial_v<ResType>);
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
        static_assert(std::is_trivial_v<ResType>);
        return DeegenImpl_MakeIC_MarkEffect<ResType>(this, DeegenGetLambdaClosureAddr(lambda), DeegenGetLambdaFunctorPP(lambda));
    }
};

// Create an inline cache.
// All the API calls between MakeInlineCache() and ICHandler::Setup() must be unconditionally executed
// (e.g., you cannot do 'if (...) { ic->AddKey(...) }'), otherwise the behavior is undefined.
//
ICHandler* WARN_UNUSED __attribute__((__nomerge__)) MakeInlineCache();
