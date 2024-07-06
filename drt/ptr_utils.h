#pragma once

#include "common_utils.h"

template<typename T>
using RestrictPtr = T* __restrict__;

template<typename T>
using ConstRestrictPtr = RestrictPtr<const T>;

// Utility for accessing unaligned data
//
template<typename T>
T UnalignedLoad(const void* ptr)
{
    static_assert(std::is_trivially_copyable_v<T>);
    T result { };
    memcpy(&result, ptr, sizeof(T));
    return result;
}

template<typename T>
void UnalignedStore(void* ptr, T value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    memcpy(ptr, &value, sizeof(T));
}

template<typename T>
using Packed = __attribute__((__packed__, __aligned__(1))) T;
