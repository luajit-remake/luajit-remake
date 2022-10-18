#pragma once

#include "common_utils.h"

namespace detail {

template<typename Lambda>
inline constexpr auto lambda_functor_member_pointer_v = &Lambda::operator();

template<typename Lambda>
constexpr const void* lambda_functor_member_pointer_pointer_v = &lambda_functor_member_pointer_v<Lambda>;

}   // namespace detail

// Return the address of the closure state
//
template<typename Lambda>
const void* ALWAYS_INLINE WARN_UNUSED DeegenGetLambdaClosureAddr(const Lambda& lambda)
{
    return static_cast<const void*>(&lambda);
}

// Return the closure function address's address, in a way parsable at LLVM IR level
//
template<typename Lambda>
const void* ALWAYS_INLINE WARN_UNUSED DeegenGetLambdaFunctorPP(const Lambda& /*lambda*/)
{
    return detail::lambda_functor_member_pointer_pointer_v<Lambda>;
}
