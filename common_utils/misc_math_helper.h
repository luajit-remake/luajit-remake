#pragma once

#include "common.h"

namespace CommonUtils
{

// Left/right shift on negative number in C is undefined behavior.
// The two functions below avoids this UB by doing a sign-extended shift.
// The compiler is smart enough to just generate an arithmetic shift instruction as one would normally expect.
//
template<typename T, typename U>
T WARN_UNUSED ALWAYS_INLINE ArithmeticShiftRight(T value, U shift)
{
    static_assert(std::is_integral_v<T> && std::is_integral_v<U>, "must be integral types");
    if constexpr(std::is_signed_v<U>) { assert(shift >= 0); }
    if constexpr(std::is_signed_v<T>)
    {
        return (static_cast<std::make_unsigned_t<T>>(value) & (static_cast<std::make_unsigned_t<T>>(1) << (sizeof(T) * 8 - 1)))
                ? (~((~value) >> shift)) : (value >> shift);
    }
    else
    {
        return value >> shift;
    }
}

template<typename T, typename U>
T WARN_UNUSED ALWAYS_INLINE ArithmeticShiftLeft(T value, U shift)
{
    static_assert(std::is_integral_v<T> && std::is_integral_v<U>, "must be integral types");
    if constexpr(std::is_signed_v<U>) { assert(shift >= 0); }
    if constexpr(std::is_signed_v<T>)
    {
        return static_cast<T>(static_cast<std::make_unsigned_t<T>>(value) << shift);
    }
    else
    {
        return value << shift;
    }
}

// Check if the operand fits in the range of the target integer type
//
template<typename Dst, typename Src>
constexpr bool WARN_UNUSED ALWAYS_INLINE IntegerCanBeRepresentedIn(Src src)
{
    static_assert(std::is_integral_v<Dst> && std::is_integral_v<Src>, "only works for integer types");
    if constexpr(std::is_signed_v<Dst> == std::is_signed_v<Src>)
    {
        // signed-to-signed or unsigned-to-unsigned cast
        //
        if constexpr(sizeof(Dst) < sizeof(Src))
        {
            return static_cast<Src>(std::numeric_limits<Dst>::min()) <= src && src <= static_cast<Src>(std::numeric_limits<Dst>::max());
        }
        else
        {
            return true;
        }
    }
    else if constexpr(std::is_signed_v<Src>)
    {
        // signed-to-unsigned cast
        //
        if (src < 0)
        {
            return false;
        }
        if constexpr(sizeof(Dst) < sizeof(Src))
        {
            return src <= static_cast<Src>(std::numeric_limits<Dst>::max());
        }
        else
        {
            return true;
        }
    }
    else
    {
        // unsigned-to-signed cast
        //
        if constexpr(sizeof(Dst) <= sizeof(Src))
        {
            return src <= static_cast<Src>(std::numeric_limits<Dst>::max());
        }
        else
        {
            return true;
        }
    }
}

// Equivalent to static_cast, but asserts that the operand fits in the range of the target integer type
//
template<typename Dst, typename Src>
constexpr Dst WARN_UNUSED ALWAYS_INLINE SafeIntegerCast(Src src)
{
    static_assert(std::is_integral_v<Dst> && std::is_integral_v<Src>, "only works for integer cast");
    assert(IntegerCanBeRepresentedIn<Dst>(src));
    return static_cast<Dst>(src);
}

template<size_t mult, typename T>
constexpr T RoundUpToMultipleOf(T val)
{
    static_assert(std::is_integral_v<T>);
    static_assert(mult < std::numeric_limits<T>::max());
    if constexpr(std::is_signed_v<T>)
    {
        assert(val >= 0);
    }
    assert(val <= std::numeric_limits<T>::max() - static_cast<T>(mult));
    return (val + static_cast<T>(mult) - 1) / static_cast<T>(mult) * static_cast<T>(mult);
}

// *c = a + b (wrapping), return true if overflowed
//
template<typename T>
bool WARN_UNUSED AddWithOverflowCheck(T a, T b, T* c /*out*/)
{
    static_assert(std::is_integral_v<T>, "T must be integral");
    static_assert(sizeof(T) == 4 || sizeof(T) == 8, "T must be 4 or 8 bytes");
    if constexpr(std::is_signed_v<T> && sizeof(T) == 4)
    {
        static_assert(sizeof(int) == 4);
        return __builtin_sadd_overflow(a, b, c);
    }
    else if constexpr(std::is_signed_v<T> && sizeof(T) == 8)
    {
        static_assert(sizeof(long long int) == 8);
        return __builtin_saddll_overflow(a, b, c);
    }
    else if constexpr(!std::is_signed_v<T> && sizeof(T) == 4)
    {
        static_assert(sizeof(unsigned int) == 4);
        return __builtin_uadd_overflow(a, b, c);
    }
    else
    {
        static_assert(!std::is_signed_v<T> && sizeof(T) == 8, "unexpected type T");
        static_assert(sizeof(unsigned long long int) == 8);
        return __builtin_uaddll_overflow(a, b, c);
    }
}

// *c = a + b (wrapping), return true if overflowed
//
template<typename T>
bool WARN_UNUSED SubWithOverflowCheck(T a, T b, T* c /*out*/)
{
    static_assert(std::is_integral_v<T>, "T must be integral");
    static_assert(sizeof(T) == 4 || sizeof(T) == 8, "T must be 4 or 8 bytes");
    if constexpr(std::is_signed_v<T> && sizeof(T) == 4)
    {
        static_assert(sizeof(int) == 4);
        return __builtin_ssub_overflow(a, b, c);
    }
    else if constexpr(std::is_signed_v<T> && sizeof(T) == 8)
    {
        static_assert(sizeof(long long int) == 8);
        return __builtin_ssubll_overflow(a, b, c);
    }
    else if constexpr(!std::is_signed_v<T> && sizeof(T) == 4)
    {
        static_assert(sizeof(unsigned int) == 4);
        return __builtin_usub_overflow(a, b, c);
    }
    else
    {
        static_assert(!std::is_signed_v<T> && sizeof(T) == 8, "unexpected type T");
        static_assert(sizeof(unsigned long long int) == 8);
        return __builtin_usubll_overflow(a, b, c);
    }
}

// *c = a * b (wrapping), return true if overflowed
//
template<typename T>
bool WARN_UNUSED MulWithOverflowCheck(T a, T b, T* c /*out*/)
{
    static_assert(std::is_integral_v<T>, "T must be integral");
    static_assert(sizeof(T) == 4 || sizeof(T) == 8, "T must be 4 or 8 bytes");
    if constexpr(std::is_signed_v<T> && sizeof(T) == 4)
    {
        static_assert(sizeof(int) == 4);
        return __builtin_smul_overflow(a, b, c);
    }
    else if constexpr(std::is_signed_v<T> && sizeof(T) == 8)
    {
        static_assert(sizeof(long long int) == 8);
        return __builtin_smulll_overflow(a, b, c);
    }
    else if constexpr(!std::is_signed_v<T> && sizeof(T) == 4)
    {
        static_assert(sizeof(unsigned int) == 4);
        return __builtin_umul_overflow(a, b, c);
    }
    else
    {
        static_assert(!std::is_signed_v<T> && sizeof(T) == 8, "unexpected type T");
        static_assert(sizeof(unsigned long long int) == 8);
        return __builtin_umulll_overflow(a, b, c);
    }
}

// A sign-extend cast: fill the high bits of Dst with the highest bit of Src
//
template<typename Dst, typename Src>
Dst WARN_UNUSED SignExtendTo(Src src)
{
    static_assert(std::is_integral_v<Dst> && std::is_integral_v<Src>, "must be integers");
    static_assert(sizeof(Dst) > sizeof(Src), "must be an extending cast");
    return static_cast<Dst>(static_cast<std::make_signed_t<Dst>>(static_cast<std::make_signed_t<Src>>(src)));
}

// A zero-extend cast: fill the high bits of Dst with 0
//
template<typename Dst, typename Src>
Dst WARN_UNUSED ZeroExtendTo(Src src)
{
    static_assert(std::is_integral_v<Dst> && std::is_integral_v<Src>, "must be integers");
    static_assert(sizeof(Dst) > sizeof(Src), "must be an extending cast");
    return static_cast<Dst>(static_cast<std::make_unsigned_t<Dst>>(static_cast<std::make_unsigned_t<Src>>(src)));
}

// A truncate cast: obtains the value represented by the low bits of Src
//
template<typename Dst, typename Src>
Dst WARN_UNUSED BitwiseTruncateTo(Src src)
{
    static_assert(std::is_integral_v<Dst> && std::is_integral_v<Src>, "must be integers");
    static_assert(!std::is_same_v<Dst, bool> && !std::is_same_v<Src, bool>, "must not be boolean");
    static_assert(sizeof(Dst) < sizeof(Src), "must be a truncating cast");
    return static_cast<Dst>(static_cast<std::make_unsigned_t<Dst>>(static_cast<std::make_unsigned_t<Src>>(src)));
}

}   // namespace CommonUtils
