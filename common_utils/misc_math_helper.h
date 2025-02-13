#pragma once

#include "common.h"
#include "constexpr_power_helper.h"

// Perform a sign-extended right-shift, even if T is an unsigned type
//
template<typename T, typename U>
T WARN_UNUSED ALWAYS_INLINE SignExtendedShiftRight(T value, U shift)
{
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool> && std::is_integral_v<U> && !std::is_same_v<U, bool>, "must be integral types");
    if constexpr(std::is_signed_v<U>) { Assert(shift >= 0); }
    if constexpr(std::is_signed_v<T>)
    {
        // C++20 standardized that >> on signed integers is arithmetic right-shift (before C++20 it's undefined behavior).
        // LLVM 19 has a regression that the "soft" arithmetic right-shift (that works without C++20) generates poor code in some cases.
        // So just rely on the C++20 standard. We are already requiring C++20 in a ton of places anyway.
        //
        return value >> shift;
    }
    else
    {
        return static_cast<T>(static_cast<std::make_signed_t<T>>(value) >> shift);
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
    Assert(IntegerCanBeRepresentedIn<Dst>(src));
    return static_cast<Dst>(src);
}

template<size_t mult, typename T>
constexpr T WARN_UNUSED ALWAYS_INLINE RoundUpToMultipleOf(T val)
{
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>);
    static_assert(mult < std::numeric_limits<T>::max());
    if constexpr(std::is_signed_v<T>)
    {
        Assert(val >= 0);
    }
    Assert(val <= std::numeric_limits<T>::max() - static_cast<T>(mult));
    return (val + static_cast<T>(mult) - 1) / static_cast<T>(mult) * static_cast<T>(mult);
}

template<typename T>
constexpr T WARN_UNUSED ALWAYS_INLINE RoundUpToPowerOfTwoMultipleOf(T value, T multMustBePowerOf2)
{
    static_assert(std::is_integral_v<T> && !std::is_signed_v<T> && !std::is_same_v<T, bool>);
    TestAssert(is_power_of_2(multMustBePowerOf2));
    value += multMustBePowerOf2 - 1;
    T mask = ~(multMustBePowerOf2 - 1);
    return value & mask;
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
        return __builtin_saddll_overflow(a, b, reinterpret_cast<long long int*>(c));
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
        return __builtin_uaddll_overflow(a, b, reinterpret_cast<unsigned long long int*>(c));
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
        return __builtin_ssubll_overflow(a, b, reinterpret_cast<long long int*>(c));
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
        return __builtin_usubll_overflow(a, b, reinterpret_cast<unsigned long long int*>(c));
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
        return __builtin_smulll_overflow(a, b, reinterpret_cast<long long int*>(c));
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
        return __builtin_umulll_overflow(a, b, reinterpret_cast<unsigned long long int*>(c));
    }
}

// A sign-extend cast: fill the high bits of Dst with the highest bit of Src
//
template<typename Dst, typename Src>
constexpr Dst WARN_UNUSED SignExtendTo(Src src)
{
    static_assert(std::is_integral_v<Dst> && std::is_integral_v<Src>, "must be integers");
    static_assert(sizeof(Dst) > sizeof(Src), "must be an extending cast");
    return static_cast<Dst>(static_cast<std::make_signed_t<Dst>>(static_cast<std::make_signed_t<Src>>(src)));
}

// A zero-extend cast: fill the high bits of Dst with 0
//
template<typename Dst, typename Src>
constexpr Dst WARN_UNUSED ZeroExtendTo(Src src)
{
    static_assert(std::is_integral_v<Dst> && std::is_integral_v<Src>, "must be integers");
    static_assert(sizeof(Dst) > sizeof(Src), "must be an extending cast");
    return static_cast<Dst>(static_cast<std::make_unsigned_t<Dst>>(static_cast<std::make_unsigned_t<Src>>(src)));
}

// A truncate cast: obtains the value represented by the low bits of Src
//
template<typename Dst, typename Src>
constexpr Dst WARN_UNUSED BitwiseTruncateTo(Src src)
{
    static_assert(std::is_integral_v<Dst> && std::is_integral_v<Src>, "must be integers");
    static_assert(!std::is_same_v<Dst, bool> && !std::is_same_v<Src, bool>, "must not be boolean");
    static_assert(sizeof(Dst) < sizeof(Src), "must be a truncating cast");
    return static_cast<Dst>(static_cast<std::make_unsigned_t<Dst>>(static_cast<std::make_unsigned_t<Src>>(src)));
}

// From http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
constexpr uint32_t RoundUpToPowerOfTwo(uint32_t v)
{
    Assert(v <= (1U << 31));
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

template<typename T>
bool WARN_UNUSED ALWAYS_INLINE UnsafeFloatEqual(T a, T b)
{
    static_assert(std::is_floating_point_v<T>);
    SUPRESS_FLOAT_EQUAL_WARNING(
        return a == b;
    )
}

template<typename T>
bool WARN_UNUSED ALWAYS_INLINE IsNaN(T a)
{
    static_assert(std::is_floating_point_v<T>);
    SUPRESS_FLOAT_EQUAL_WARNING(
        return a != a;
    )
}

template<typename T>
constexpr uint32_t WARN_UNUSED ALWAYS_INLINE CountTrailingZeros(T value)
{
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>);
    static_assert(std::is_unsigned_v<T>, "CountTrailingZeros does not make sense for signed types!");
    TestAssert(value != 0);
    if constexpr(sizeof(T) <= 4)
    {
        static_assert(sizeof(unsigned int) == 4);
        return static_cast<uint32_t>(__builtin_ctz(value));
    }
    else
    {
        static_assert(sizeof(T) == 8);
        return static_cast<uint32_t>(__builtin_ctzll(value));
    }
}

template<typename T>
constexpr uint32_t WARN_UNUSED ALWAYS_INLINE CountNumberOfOnes(T value)
{
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>);
    static_assert(std::is_unsigned_v<T>, "CountNumberOfOnes does not make sense for signed types!");
    if constexpr(sizeof(T) <= 4)
    {
        static_assert(sizeof(unsigned int) == 4);
        return static_cast<uint32_t>(__builtin_popcount(value));
    }
    else
    {
        static_assert(sizeof(T) == 8);
        return static_cast<uint32_t>(__builtin_popcountll(value));
    }
}

// Return the ordinal (lowest bit = 0) of the highest one bit
//
template<typename T>
constexpr uint32_t WARN_UNUSED ALWAYS_INLINE FindHighestOneBit(T value)
{
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>);
    static_assert(std::is_unsigned_v<T>, "FindHighestOneBit does not make sense for signed types!");
    TestAssert(value != 0);
    if constexpr(sizeof(T) <= 4)
    {
        static_assert(sizeof(unsigned int) == 4);
        return static_cast<uint32_t>(31 - __builtin_clz(value));
    }
    else
    {
        static_assert(sizeof(T) == 8);
        static_assert(sizeof(unsigned long long) == 8);
        return static_cast<uint32_t>(63 - __builtin_clzll(value));
    }
}

namespace internal
{

template<typename T, uint32_t k>
constexpr T GetMaskBitIsOneIfOrdinalBitKIsOne()
{
    static_assert(std::is_integral_v<T> && !std::is_signed_v<T> && !std::is_same_v<T, bool> && k < 6 && (1ULL << k) < sizeof(T) * 8);
    T result = 0;
    for (uint32_t i = 0; i < sizeof(T) * 8; i++)
    {
        if (i & (1U << k))
        {
            result |= static_cast<T>(1) << i;
        }
    }
    return result;
}

}   // namespace internal

// Get a mask which bit i is 1 iff i's bit k is 1.
//
template<typename T, uint32_t k>
constexpr T x_maskBitIsOneIfOrdinalBitKIsOne = internal::GetMaskBitIsOneIfOrdinalBitKIsOne<T, k>();

// A helper class to define bitfield members in classes
// In C's bitfield definition (e.g. "uint8_t a : 1;"), where exactly the bit is stored is implementation-defined
// This class removes this undefined behavior
//
template<typename CarrierType, typename SelfType, uint32_t start, uint32_t width>
struct BitFieldMember
{
    // Carrier type must be unsigned integer
    //
    static_assert(std::is_integral_v<CarrierType> && !std::is_signed_v<CarrierType> && !std::is_same_v<CarrierType, bool>);

    // Self type must be bool, enum, or unsigned integer
    //
    static_assert(std::is_enum_v<SelfType> || std::is_same_v<SelfType, bool> || (std::is_integral_v<SelfType> && !std::is_signed_v<SelfType>));

    static_assert(start < sizeof(CarrierType) * 8);
    static_assert(start + width <= sizeof(CarrierType) * 8);
    static_assert(0 < width && width < 64);

    static constexpr uint32_t x_fieldBitStart = start;
    static constexpr uint32_t x_fieldBitWidth = width;
    static constexpr CarrierType x_maskForGet = static_cast<CarrierType>(((static_cast<CarrierType>(1) << width) - 1) << start);
    static constexpr CarrierType x_maskForSet = static_cast<CarrierType>(~x_maskForGet);

    static constexpr uint32_t BitOffset()
    {
        return x_fieldBitStart;
    }

    static constexpr uint32_t BitWidth()
    {
        return x_fieldBitWidth;
    }

    static constexpr void ALWAYS_INLINE Set(CarrierType& c, SelfType v)
    {
        CarrierType value;
        if constexpr(std::is_enum_v<SelfType>)
        {
            if constexpr(std::is_signed_v<std::underlying_type_t<SelfType>>)
            {
                Assert(static_cast<int64_t>(v) >= 0);
            }
            using T = std::make_unsigned_t<std::underlying_type_t<SelfType>>;
            T raw = static_cast<T>(v);
            Assert(raw < (1ULL << width));
            value = static_cast<CarrierType>(raw);
        }
        else
        {
            if constexpr(!std::is_same_v<SelfType, bool>)
            {
                Assert(v < (1ULL << width));
            }
            value = static_cast<CarrierType>(v);
        }
        c = static_cast<CarrierType>((c & x_maskForSet) | (value << start));
        Assert(Get(c) == v);
    }

    static constexpr SelfType ALWAYS_INLINE WARN_UNUSED Get(const CarrierType& c)
    {
        if constexpr(std::is_same_v<SelfType, bool>)
        {
            return (c & x_maskForGet);
        }
        else
        {
            return static_cast<SelfType>((c & x_maskForGet) >> start);
        }
    }
};

// Equivalent to memcpy, but asserts that the address range does not overlap
//
inline void ALWAYS_INLINE SafeMemcpy(void* dst, const void* src, size_t len)
{
    Assert(reinterpret_cast<uintptr_t>(dst) + len <= reinterpret_cast<uintptr_t>(src) || reinterpret_cast<uintptr_t>(src) + len <= reinterpret_cast<uintptr_t>(dst));
    memcpy(dst, src, len);
}

template<typename T>
constexpr T WARN_UNUSED SingletonBitmask(size_t bitOrd)
{
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool> && std::is_unsigned_v<T>);
    Assert(bitOrd < sizeof(T) * 8);
    return static_cast<T>(static_cast<T>(1) << bitOrd);
}

namespace internal {

inline constexpr std::array<uint64_t, 5> x_nibbleLookupMaskForFirstKOneBits = []() {
    std::array<uint64_t, 5> r;
    for (size_t k = 0; k < 5; k++)
    {
        uint64_t res = 0;
        for (uint64_t x = 0; x < 16; x++)
        {
            uint64_t v = 0, cnt = 0;
            for (size_t i = 0; i < 4; i++)
            {
                if (cnt < k && (x & (1U << i)) > 0)
                {
                    cnt++;
                    v |= (1U << i);
                }
            }
            res |= v << (x * 4);
        }
        r[k] = res;
    }
    return r;
}();

}   // namespace internal

// Given 'val', return the value consisting of the first k 1-bits in 'val'
// k must be <= # of 1-bits in 'val'
//
constexpr uint8_t ALWAYS_INLINE WARN_UNUSED GetFirstKOnesInUint8Val(uint8_t val, size_t k)
{
    TestAssert(k <= CountNumberOfOnes(val));
    uint8_t lowNibble = val & 15;
    uint32_t onesInLowNibble = CountNumberOfOnes(lowNibble);
    if (k > onesInLowNibble)
    {
        size_t desiredOnesInHighNibble = k - onesInLowNibble;
        TestAssert(desiredOnesInHighNibble <= 4);
        uint64_t lookupMask = internal::x_nibbleLookupMaskForFirstKOneBits[desiredOnesInHighNibble];
        size_t highNibble = val >> 4;
        uint8_t resultHigh = static_cast<uint8_t>((lookupMask >> (highNibble * 4)) & 15);
        return static_cast<uint8_t>((resultHigh << 4) + lowNibble);
    }
    else
    {
        uint64_t lookupMask = internal::x_nibbleLookupMaskForFirstKOneBits[k];
        uint8_t result = static_cast<uint8_t>((lookupMask >> (lowNibble * 4)) & 15);
        return result;
    }
}
