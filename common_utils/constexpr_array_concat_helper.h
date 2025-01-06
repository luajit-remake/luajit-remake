#pragma once

#include "common.h"

// From: https://stackoverflow.com/questions/42749032/concatenating-a-sequence-of-stdarrays/42774523#42774523
//
template <typename T, std::size_t... sizes>
constexpr auto constexpr_std_array_concat(const std::array<T, sizes>&... arrays)
{
    std::array<T, (sizes + ...)> result;
    std::size_t index = 0;
    ((std::copy_n(arrays.begin(), sizes, result.begin() + index), index += sizes), ...);
    return result;
}
