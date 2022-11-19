#pragma once

#include "common.h"

// From: https://gist.github.com/klemens-morgenstern/b75599292667a4f53007
//
// constexpr_std_array_concat: concatenate two constexpr std::array
// Example:
//    constexpr std::array<int, 3> l = {1, 2, 3};
//    constexpr std::array<int, 3> r = {4, 5, 6};
//    constexpr auto s = constexpr_std_array_concat(l, r);
//
namespace constexpr_std_array_concat_internal
{

template<std::size_t ... Size>
struct num_tuple
{ };

template<std::size_t Prepend, typename T>
struct appender {};

template<std::size_t Prepend, std::size_t ... Sizes>
struct appender<Prepend, num_tuple<Sizes...>>
{
    using type = num_tuple<Prepend, Sizes...>;
};

template<std::size_t Size, std::size_t Counter = 0>
struct counter_tuple
{
    using type = typename appender<Counter, typename counter_tuple<Size, Counter+1>::type>::type;
};

template<std::size_t Size>
struct counter_tuple<Size, Size>
{
    using type = num_tuple<>;
};

template<typename T, std::size_t Len>
consteval std::array<T, Len> array_concat_1(std::array<T, Len> input) { return input; }

template<typename T, std::size_t LL, std::size_t RL, std::size_t ... LLs, std::size_t ... RLs>
consteval std::array<T, LL+RL> array_concat_impl(
        const std::array<T, LL> lhs, const std::array<T, RL> rhs, num_tuple<LLs...>, num_tuple<RLs...>)
{
    return {lhs[LLs]..., rhs[RLs]... };
};

template<typename T, std::size_t LL, std::size_t RL>
consteval std::array<T, LL+RL> array_concat_2(std::array<T, LL> lhs, std::array<T, RL> rhs)
{
    return array_concat_impl(
        lhs,
        rhs,
        typename constexpr_std_array_concat_internal::counter_tuple<LL>::type(),
        typename constexpr_std_array_concat_internal::counter_tuple<RL>::type());
}

template<typename T, std::size_t LL, std::size_t RL, std::size_t... Rest>
consteval std::array<T, ((LL + RL) + ... + Rest)> array_concat_mult(std::array<T, LL> lhs, std::array<T, RL> rhs, std::array<T, Rest>... rest)
{
    static_assert(sizeof...(Rest) > 0);
    if constexpr(sizeof...(Rest) == 1)
    {
        return array_concat_2(array_concat_2(lhs, rhs), rest...);
    }
    else
    {
        return array_concat_mult(array_concat_2(lhs, rhs), rest...);
    }
}

}   // namespace constexpr_std_array_concat_internal


template<typename T, std::size_t... Len>
consteval std::array<T, (0 + ... + Len)> constexpr_std_array_concat(std::array<T, Len>... inputs)
{
    if constexpr(sizeof...(Len) == 0)
    {
        return std::array<T, 0> {};
    }
    else if constexpr(sizeof...(Len) == 1)
    {
        return constexpr_std_array_concat_internal::array_concat_1(inputs...);
    }
    else if constexpr(sizeof...(Len) == 2)
    {
        return constexpr_std_array_concat_internal::array_concat_2(inputs...);
    }
    else
    {
        return constexpr_std_array_concat_internal::array_concat_mult(inputs...);
    }
}
