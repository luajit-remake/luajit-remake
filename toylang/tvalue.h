#pragma once

#include "common_utils.h"
#include "memory_ptr.h"

namespace ToyLang
{

using namespace CommonUtils;

struct MiscImmediateValue
{
    // All misc immediate values must be between [0, 127]
    // 0 is more efficient to use than the others in that a XOR instruction is not needed to create the value
    //
    static constexpr uint64_t x_nil = 0;
    static constexpr uint64_t x_false = 2;
    static constexpr uint64_t x_true = 3;

    MiscImmediateValue() : m_value(0) { }
    MiscImmediateValue(uint64_t value)
        : m_value(value)
    {
        assert(m_value == x_nil || m_value == x_true || m_value == x_false);
    }

    bool ALWAYS_INLINE IsNil() const
    {
        return m_value == 0;
    }

    bool ALWAYS_INLINE IsBoolean() const
    {
        return m_value != 0;
    }

    bool ALWAYS_INLINE GetBooleanValue() const
    {
        assert(IsBoolean());
        return m_value & 1;
    }

    MiscImmediateValue WARN_UNUSED ALWAYS_INLINE FlipBooleanValue() const
    {
        assert(IsBoolean());
        return MiscImmediateValue { m_value ^ 1 };
    }

    static MiscImmediateValue WARN_UNUSED ALWAYS_INLINE CreateNil()
    {
        return MiscImmediateValue { x_nil };
    }

    static MiscImmediateValue WARN_UNUSED ALWAYS_INLINE CreateFalse()
    {
        return MiscImmediateValue { x_false };
    }

    static MiscImmediateValue WARN_UNUSED ALWAYS_INLINE CreateTrue()
    {
        return MiscImmediateValue { x_true };
    }

    bool WARN_UNUSED ALWAYS_INLINE operator==(const MiscImmediateValue& rhs) const
    {
        return m_value == rhs.m_value;
    }

    uint64_t m_value;
};

struct TValue
{
    // We use the following NaN boxing scheme:
    //          / 0000 **** **** ****
    // double  {          ...
    //          \ FFFA **** **** ****
    // int        FFFB FFFF **** ****
    // other      FFFC FFFF 0000 00** (00 - 7F only)
    //          / FFFF 0000 **** **** (currently only >= FFFF FFFC 0000 0000)
    // pointer {
    //          \ FFFF FFFE **** ****
    //
    // The pointer space in theory can index 2^48 - 2^32 bytes, almost the same as x64 limitation (2^48 bytes),
    // but under current GS-based 4-byte-pointer VM design we can only index 12GB.
    //
    // If we are willing to give up 4-byte-pointer, we can use a bitwise-NOT to decode the boxed pointer,
    // thus removing the 12GB memory limitation.
    //
    // Another potential use of the FFFE word in pointer is to carry tagging info (fine as long as it's not 0xFFFF),
    // but currently we don't need it either.
    //

    TValue() : m_value(0) { }
    TValue(uint64_t value) : m_value(value) { }

    static constexpr uint64_t x_int32Tag = 0xFFFBFFFF00000000ULL;
    static constexpr uint64_t x_mivTag = 0xFFFCFFFF0000007FULL;

    // Translates to a single ANDN instruction with BMI1 support
    //
    bool ALWAYS_INLINE IsInt32(uint64_t int32Tag) const
    {
        assert(int32Tag == x_int32Tag);
        bool result = (m_value & int32Tag) == int32Tag;
        AssertIff(result, static_cast<uint32_t>(m_value >> 32) == 0xFFFBFFFFU);
        return result;
    }

    // Translates to imm8 LEA instruction + ANDN instruction with BMI1 support
    //
    bool ALWAYS_INLINE IsMIV(uint64_t mivTag) const
    {
        assert(mivTag == x_mivTag);
        bool result = (m_value & (mivTag - 0x7F)) == (mivTag - 0x7F);
        AssertIff(result, x_mivTag - 0x7F <= m_value && m_value <= x_mivTag);
        return result;
    }

    bool ALWAYS_INLINE IsPointer(uint64_t mivTag) const
    {
        assert(mivTag == x_mivTag);
        bool result = (m_value > mivTag);
        AssertIff(result, static_cast<uint32_t>(m_value >> 32) >= 0xFFFFFFFCU &&
                          static_cast<uint32_t>(m_value >> 32) <= 0xFFFFFFFEU);
        return result;
    }

    bool ALWAYS_INLINE IsDouble(uint64_t int32Tag) const
    {
        assert(int32Tag == x_int32Tag);
        bool result = m_value < int32Tag;
        AssertIff(result, m_value <= 0xFFFAFFFFFFFFFFFFULL);
        return result;
    }

    double ALWAYS_INLINE AsDouble() const
    {
        assert(IsDouble(x_int32Tag) && !IsMIV(x_mivTag) && !IsPointer(x_mivTag) && !IsInt32(x_int32Tag));
        return cxx2a_bit_cast<double>(m_value);
    }

    int32_t ALWAYS_INLINE AsInt32() const
    {
        assert(IsInt32(x_int32Tag) && !IsMIV(x_mivTag) && !IsPointer(x_mivTag) && !IsDouble(x_int32Tag));
        return BitwiseTruncateTo<int32_t>(m_value);
    }

    template<typename T = void>
    UserHeapPointer<T> ALWAYS_INLINE AsPointer() const
    {
        assert(IsPointer(x_mivTag) && !IsMIV(x_mivTag) && !IsDouble(x_int32Tag) && !IsInt32(x_int32Tag));
        return UserHeapPointer<T> { reinterpret_cast<HeapPtr<T>>(m_value) };
    }

    MiscImmediateValue ALWAYS_INLINE AsMIV(uint64_t mivTag) const
    {
        assert(mivTag == x_mivTag && IsMIV(x_mivTag) && !IsDouble(x_int32Tag) && !IsInt32(x_int32Tag) && !IsPointer(x_mivTag));
        return MiscImmediateValue { m_value ^ mivTag };
    }

    static TValue WARN_UNUSED ALWAYS_INLINE CreateInt32(int32_t value, uint64_t int32Tag)
    {
        assert(int32Tag == x_int32Tag);
        TValue result { int32Tag | ZeroExtendTo<uint64_t>(value) };
        assert(result.AsInt32() == value);
        return result;
    }

    static TValue WARN_UNUSED ALWAYS_INLINE CreateDouble(double value)
    {
        TValue result { cxx2a_bit_cast<uint64_t>(value) };
        SUPRESS_FLOAT_EQUAL_WARNING(
            AssertImp(!std::isnan(value), result.AsDouble() == value);
            AssertIff(std::isnan(value), std::isnan(result.AsDouble()));
        )
        return result;
    }

    template<typename T>
    static TValue WARN_UNUSED ALWAYS_INLINE CreatePointer(UserHeapPointer<T> ptr)
    {
        TValue result { static_cast<uint64_t>(ptr.m_value) };
        assert(result.AsPointer<T>() == ptr);
        return result;
    }

    static TValue WARN_UNUSED ALWAYS_INLINE CreateMIV(MiscImmediateValue miv, uint64_t mivTag)
    {
        assert(mivTag == x_mivTag);
        TValue result { miv.m_value ^ mivTag };
        assert(result.AsMIV(mivTag).m_value == miv.m_value);
        return result;
    }

    static TValue Nil()
    {
        return TValue::CreateMIV(MiscImmediateValue::CreateNil(), x_mivTag);
    }

    bool IsNil() const
    {
        return m_value == Nil().m_value;
    }

    uint64_t m_value;
};

}   // namespace ToyLang
