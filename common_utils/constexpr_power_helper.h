#pragma once

#include "common.h"

constexpr int math_power(int base, int exp)
{
    int result = 1;
    for (int i = 0; i < exp; i++) result *= base;
    return result;
}

template<typename T>
constexpr bool is_power_of_2(T value)
{
    static_assert(std::is_integral_v<T>, "must be integer");
    return value > 0 && (value & (value - 1)) == 0;
}
