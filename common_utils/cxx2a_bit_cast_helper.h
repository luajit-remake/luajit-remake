#pragma once

#include <type_traits>
#include "common.h"

// std::bit_cast is only supported in C++20, which is still officially in experimental state.
//
// Fortunately the compiler magic '__builtin_bit_cast' (internally used by clang++ STL
// to implement std::bit_cast) is available regardless of C++-standard-version.
//
// This helper imports the std::bit_cast utility. Code directly stolen from libc++ source code.
//
template<class ToType, class FromType>
constexpr ToType cxx2a_bit_cast(FromType const& from) noexcept
{
    static_assert(sizeof(ToType) == sizeof(FromType));
    static_assert(std::is_trivially_copyable_v<ToType>);
    static_assert(std::is_trivially_copyable_v<FromType>);
    return __builtin_bit_cast(ToType, from);
}

template<typename T>
bool WARN_UNUSED is_all_underlying_bits_zero(T t)
{
    // Apparently clang explicitly warns that "*reinterpret_cast<uint64_t*>(double)" has undefined behavior.
    // After consulting cppreference.com:
    //     "When it is needed to interpret the bytes of an object as a value of a different type,
    //      std::memcpy or std::bit_cast (since C++20) can be used"
    //
    static_assert(std::is_fundamental<T>::value || std::is_pointer<T>::value, "Must be primitive type");
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8, "Unexpected size");
    if constexpr(sizeof(T) == 1)
    {
        return cxx2a_bit_cast<uint8_t>(t) == 0;
    }
    else if constexpr(sizeof(T) == 2)
    {
        return cxx2a_bit_cast<uint16_t>(t) == 0;
    }
    else if constexpr(sizeof(T) == 4)
    {
        return cxx2a_bit_cast<uint32_t>(t) == 0;
    }
    else
    {
        static_assert(sizeof(T) == 8, "Unexpected size");
        return cxx2a_bit_cast<uint64_t>(t) == 0;
    }
}

template<typename T>
constexpr T WARN_UNUSED get_all_bits_zero_value()
{
    static_assert(std::is_fundamental<T>::value || std::is_pointer<T>::value, "Must be primitive type");
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8, "Unexpected size");
    if constexpr(std::is_same<T, bool>::value)
    {
        return false;
    }
    else if constexpr(std::is_pointer<T>::value)
    {
        // Technically speaking, 'nullptr' doesn't imply its binary representation is '0',
        // but this is true on all commercial architectures, at least the ones we care.
        //
        return nullptr;
    }
    else if constexpr(sizeof(T) == 1)
    {
        return cxx2a_bit_cast<T>(static_cast<uint8_t>(0));
    }
    else if constexpr(sizeof(T) == 2)
    {
        return cxx2a_bit_cast<T>(static_cast<uint16_t>(0));
    }
    else if constexpr(sizeof(T) == 4)
    {
        return cxx2a_bit_cast<T>(static_cast<uint32_t>(0));
    }
    else
    {
        static_assert(sizeof(T) == 8, "Unexpected size");
        return cxx2a_bit_cast<T>(static_cast<uint64_t>(0));
    }
}
