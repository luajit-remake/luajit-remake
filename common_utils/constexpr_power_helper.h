#pragma once

#include "common.h"

namespace CommonUtils {

constexpr int math_power(int base, int exp)
{
    int result = 1;
    for (int i = 0; i < exp; i++) result *= base;
    return result;
}

constexpr int is_power_of_2(int value)
{
    return value > 0 && (value & -value) == value;
}

}   // namespace CommonUtils
