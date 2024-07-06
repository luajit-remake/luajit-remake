#pragma once

#include "common_utils.h"

template<typename T>
using RestrictPtr = T* __restrict__;

template<typename T>
using ConstRestrictPtr = RestrictPtr<const T>;

#define CLANG_GS_ADDRESS_SPACE_IDENTIFIER 256
#define CLANG_FS_ADDRESS_SPACE_IDENTIFIER 257

#define CLANG_ADDRESS_SPACE_IDENTIFIER_FOR_HEAP_PTR CLANG_GS_ADDRESS_SPACE_IDENTIFIER

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

// A severe limitation with HeapPtr classes is that it cannot call member method (since C++ always assumes that the hidden 'this' pointer is
// in the normal address space, but our HeapPtr is in the GS address space, which cannot be converted to normal address space automatically)
// However, in many cases we are just using class as a wrapper for some word-sized data. In this case it's trivial to make a copy of the class,
// so we can workaround this limitation.
//
// This is what the 'TCGet' and 'TCSet' (TC stands for TriviallyCopyable) is for.
//

template<typename T>
void ALWAYS_INLINE TCSet(T& target, T obj)
{
    static_assert(std::is_trivially_copyable_v<T>);
    target = obj;
}
