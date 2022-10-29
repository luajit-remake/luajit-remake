#pragma once

#include "common_utils.h"

namespace detail {

template<typename Lambda>
inline constexpr auto lambda_functor_member_pointer_v = &Lambda::operator();

template<typename Lambda>
constexpr const void* lambda_functor_member_pointer_pointer_v = &lambda_functor_member_pointer_v<Lambda>;

template<typename T>
struct is_const_member_function_impl;

template<typename R, typename C, typename... Args>
struct is_const_member_function_impl<R(C::*)(Args...) const> : std::true_type { };

template<typename R, typename C, typename... Args>
struct is_const_member_function_impl<R(C::*)(Args...)> : std::false_type { };

}   // namespace detail

template<typename Lambda>
constexpr bool is_lambda_mutable_v = !detail::is_const_member_function_impl<decltype(&Lambda::operator())>::value;

// Return the address of the closure state
//
template<typename Lambda>
const void* ALWAYS_INLINE WARN_UNUSED DeegenGetLambdaClosureAddr(const Lambda& lambda)
{
    static_assert(std::is_trivially_copy_constructible_v<Lambda>, "The lambda may only capture trivially-copyable classes!");
    static_assert(std::is_trivially_destructible_v<Lambda>, "The lambda may not capture classes with non-trivial destructor!");
    return static_cast<const void*>(&lambda);
}

// Return the closure function address's address, in a way parsable at LLVM IR level
//
template<typename Lambda>
const void* ALWAYS_INLINE WARN_UNUSED DeegenGetLambdaFunctorPP(const Lambda& /*lambda*/)
{
    static_assert(std::is_trivially_copy_constructible_v<Lambda>, "The lambda may only capture trivially-copyable classes!");
    static_assert(std::is_trivially_destructible_v<Lambda>, "The lambda may not capture classes with non-trivial destructor!");
    return detail::lambda_functor_member_pointer_pointer_v<Lambda>;
}
