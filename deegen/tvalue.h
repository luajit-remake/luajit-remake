#pragma once

#include "common_utils.h"
#include "memory_ptr.h"
#include "heap_object_common.h"

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

    static MiscImmediateValue WARN_UNUSED ALWAYS_INLINE CreateBoolean(bool v)
    {
        return MiscImmediateValue { v ? x_true : x_false };
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

    bool ALWAYS_INLINE IsInt32() const
    {
        bool result = (m_value & x_int32Tag) == x_int32Tag;
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

    bool ALWAYS_INLINE IsMIV() const
    {
        bool result = (m_value & (x_mivTag - 0x7F)) == (x_mivTag - 0x7F);
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

    bool ALWAYS_INLINE IsPointer() const
    {
        bool result = (m_value > x_mivTag);
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

    bool ALWAYS_INLINE IsDouble() const
    {
        bool result = m_value < x_int32Tag;
        AssertIff(result, m_value <= 0xFFFAFFFFFFFFFFFFULL);
        return result;
    }

    bool IsDoubleNotNaN() const
    {
        return !IsNaN(cxx2a_bit_cast<double>(m_value));
    }

    bool IsDoubleNaN() const
    {
        static constexpr uint64_t x_pureNaN = 0x7ff8000000000000ULL;
        return m_value == x_pureNaN;
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

    // Return true if the value is not 'nil' or 'false'
    //
    bool IsTruthy() const
    {
        return m_value != Nil().m_value && m_value != CreateFalse().m_value;
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

    MiscImmediateValue ALWAYS_INLINE AsMIV() const
    {
        assert(IsMIV(x_mivTag) && !IsDouble(x_int32Tag) && !IsInt32(x_int32Tag) && !IsPointer(x_mivTag));
        return MiscImmediateValue { m_value ^ x_mivTag };
    }

    static TValue WARN_UNUSED ALWAYS_INLINE CreateInt32(int32_t value, uint64_t int32Tag)
    {
        assert(int32Tag == x_int32Tag);
        TValue result { int32Tag | ZeroExtendTo<uint64_t>(value) };
        assert(result.AsInt32() == value);
        return result;
    }

    static TValue WARN_UNUSED ALWAYS_INLINE CreateInt32(int32_t value)
    {
        TValue result { x_int32Tag | ZeroExtendTo<uint64_t>(value) };
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

    template<typename T>
    static TValue WARN_UNUSED ALWAYS_INLINE CreatePointer(HeapPtr<T> ptr)
    {
        uint64_t val = reinterpret_cast<uint64_t>(ptr);
        assert(val >= 0xFFFFFFFC00000000ULL);
        TValue result { val };
        return result;
    }

    static TValue WARN_UNUSED ALWAYS_INLINE CreateMIV(MiscImmediateValue miv, uint64_t mivTag)
    {
        assert(mivTag == x_mivTag);
        TValue result { miv.m_value ^ mivTag };
        assert(result.AsMIV(mivTag).m_value == miv.m_value);
        return result;
    }

    static TValue CreateTrue()
    {
        return TValue::CreateMIV(MiscImmediateValue::CreateTrue(), x_mivTag);
    }

    static TValue CreateFalse()
    {
        return TValue::CreateMIV(MiscImmediateValue::CreateFalse(), x_mivTag);
    }

    static TValue CreateBoolean(bool v)
    {
        return TValue::CreateMIV(MiscImmediateValue::CreateBoolean(v), x_mivTag);
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

// Below are the type speculation / specialization definitions
//

struct TypeSpeculationLeaf;
template<typename... Args> struct tsm_or;
template<typename Arg> struct tsm_not;

struct tNil
{
    using TSMDef = TypeSpeculationLeaf;

    static bool check(TValue v)
    {
        return v.IsNil();
    }

    static TValue encode()
    {
        return TValue::Nil();
    }
};

struct tBool
{
    using TSMDef = TypeSpeculationLeaf;

    static bool check(TValue v)
    {
        return v.IsMIV() && v.AsMIV().IsBoolean();
    }

    static TValue encode(bool v)
    {
        return TValue::CreateBoolean(v);
    }

    static bool decode(TValue v)
    {
        return v.AsMIV().GetBooleanValue();
    }
};

struct tDoubleNotNaN
{
    using TSMDef = TypeSpeculationLeaf;

    static bool check(TValue v)
    {
        return v.IsDoubleNotNaN();
    }

    static TValue encode(double v)
    {
        assert(!IsNaN(v));
        return TValue::CreateDouble(v);
    }

    static double decode(TValue v)
    {
        return v.AsDouble();
    }
};

struct tDoubleNaN
{
    using TSMDef = TypeSpeculationLeaf;

    static bool check(TValue v)
    {
        return v.IsDoubleNotNaN();
    }

    static TValue encode()
    {
        return TValue::CreateDouble(std::numeric_limits<double>::quiet_NaN());
    }

    static double decode(TValue /*v*/)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
};

struct tDouble
{
    using TSMDef = tsm_or<tDoubleNotNaN, tDoubleNaN>;

    static bool check(TValue v)
    {
        return v.IsDouble();
    }

    static TValue encode(double v)
    {
        return TValue::CreateDouble(v);
    }

    static double decode(TValue v)
    {
        return v.AsDouble();
    }
};

struct tInt32
{
    using TSMDef = TypeSpeculationLeaf;

    static bool check(TValue v)
    {
        return v.IsInt32();
    }

    static TValue encode(int32_t v)
    {
        return TValue::CreateInt32(v);
    }

    static int32_t decode(TValue v)
    {
        return v.AsInt32();
    }
};

#define macro(hoi)                                                                                                              \
    struct PP_CAT(t, HOI_ENUM_NAME(hoi)) {                                                                                      \
        using TSMDef = TypeSpeculationLeaf;                                                                                     \
        using PtrType = HeapObjectTypeForEnum<HeapEntityType::HOI_ENUM_NAME(hoi)>;                                              \
                                                                                                                                \
        static bool check(TValue v)                                                                                             \
        {                                                                                                                       \
            return v.IsPointer() && v.AsPointer<UserHeapGcObjectHeader>().As()->m_type == HeapEntityType::HOI_ENUM_NAME(hoi);   \
        }                                                                                                                       \
                                                                                                                                \
        static TValue encode(HeapPtr<PtrType> o)                                                                                \
        {                                                                                                                       \
            return TValue::CreatePointer(o);                                                                                    \
        }                                                                                                                       \
                                                                                                                                \
        static HeapPtr<PtrType> decode(TValue v)                                                                                \
        {                                                                                                                       \
            return v.AsPointer<PtrType>().As();                                                                                 \
        }                                                                                                                       \
    };

PP_FOR_EACH(macro, LANGUAGE_EXPOSED_HEAP_OBJECT_INFO_LIST)
#undef macro

struct tHeapEntity
{
#define macro(hoi) , PP_CAT(t, HOI_ENUM_NAME(hoi))
    using TSMDef = tsm_or<tsm_or<> PP_FOR_EACH(macro, LANGUAGE_EXPOSED_HEAP_OBJECT_INFO_LIST)>;
#undef macro

    static bool check(TValue v)
    {
        return v.IsPointer();
    }

    static TValue encode(HeapPtr<void> v)
    {
        return TValue::CreatePointer(UserHeapPointer<void> { v });
    }

    static UserHeapPointer<void> decode(TValue v)
    {
        return v.AsPointer();
    }
};

// Two special lattice values: the bottom and the top
//
struct tBottom
{
    static bool check(TValue /*v*/)
    {
        return false;
    }
};

struct tTop
{
    static bool check(TValue /*v*/)
    {
        return true;
    }

    static TValue encode(TValue v)
    {
        return v;
    }

    static TValue decode(TValue v)
    {
        return v;
    }
};

using TypeSpecializationList = std::tuple<
    tNil
  , tBool
  , tDoubleNotNaN
  , tDoubleNaN
  , tInt32
  , tDouble
#define macro(hoi) , PP_CAT(t, HOI_ENUM_NAME(hoi))
    PP_FOR_EACH(macro, LANGUAGE_EXPOSED_HEAP_OBJECT_INFO_LIST)
#undef macro
  , tHeapEntity
  , tBottom
  , tTop
>;

using TypeSpeculationMask = uint32_t;

// Below are the constexpr logic that build up the bitmask definitions
//
namespace detail {

template<typename T> struct is_tuple_typelist_pairwise_distinct;

template<typename... Args>
struct is_tuple_typelist_pairwise_distinct<std::tuple<Args...>>
{
    static constexpr bool value = is_typelist_pairwise_distinct<Args...>;
};

static_assert(is_tuple_typelist_pairwise_distinct<TypeSpecializationList>::value, "TypeSpecializationList must not contain duplicates");

template<typename T> struct is_type_in_speculation_list;

template<typename... Args>
struct is_type_in_speculation_list<std::tuple<Args...>>
{
    template<typename T, typename First, typename... Remaining>
    static constexpr bool get_impl()
    {
        if constexpr(std::is_same_v<T, First>)
        {
            return true;
        }
        else if constexpr(sizeof...(Remaining) > 0)
        {
            return get_impl<T, Remaining...>();
        }
        else
        {
            return false;
        }
    }

    template<typename T>
    static constexpr bool get()
    {
        return get_impl<T, Args...>();
    }
};

}   // namespace detail

template<typename T>
constexpr bool IsValidTypeSpecialization = detail::is_type_in_speculation_list<TypeSpecializationList>::get<T>();

namespace detail {

template<typename T>
static constexpr bool is_type_speculation_leaf_impl()
{
    if constexpr(std::is_same_v<T, tTop> || std::is_same_v<T, tBottom>)
    {
        return false;
    }
    else
    {
        return std::is_same_v<typename T::TSMDef, TypeSpeculationLeaf>;
    }
}

template<typename T>
constexpr bool is_type_speculation_leaf = is_type_speculation_leaf_impl<T>();

template<typename T> struct num_leaves_in_type_speculation_list;

template<typename... Args>
struct num_leaves_in_type_speculation_list<std::tuple<Args...>>
{
    template<typename First, typename... Remaining>
    static constexpr size_t count_leaves()
    {
        size_t r = is_type_speculation_leaf<First>;
        if constexpr(sizeof...(Remaining) > 0)
        {
            r += count_leaves<Remaining...>();
        }
        return r;
    }

    static constexpr size_t value = count_leaves<Args...>();
};

template<typename T> struct leaf_ordinal_in_type_speculation_list;

template<typename... Args>
struct leaf_ordinal_in_type_speculation_list<std::tuple<Args...>>
{
    template<typename T, typename First, typename... Remaining>
    static constexpr size_t get_impl()
    {
        if constexpr(std::is_same_v<T, First>)
        {
            static_assert(is_type_speculation_leaf<First>);
            return 0;
        }
        else
        {
            static_assert(sizeof...(Remaining) > 0);
            return is_type_speculation_leaf<First> + get_impl<T, Remaining...>();
        }
    }

    template<typename T>
    static constexpr size_t get()
    {
        return get_impl<T, Args...>();
    }
};

template<typename T>
constexpr size_t type_speculation_leaf_ordinal = leaf_ordinal_in_type_speculation_list<TypeSpecializationList>::get<T>();

static_assert(std::is_unsigned_v<TypeSpeculationMask> && std::is_integral_v<TypeSpeculationMask>);

constexpr TypeSpeculationMask ComputeTypeSpeculationMaskForTop()
{
    constexpr size_t numLeaves = num_leaves_in_type_speculation_list<TypeSpecializationList>::value;
    static_assert(numLeaves <= sizeof(TypeSpeculationMask) * 8);
    return static_cast<TypeSpeculationMask>(-1) >> (sizeof(TypeSpeculationMask) * 8 - numLeaves);
}

constexpr TypeSpeculationMask type_speculation_mask_for_top = ComputeTypeSpeculationMaskForTop();

struct compute_type_speculation_mask
{
    template<typename T>
    struct eval_operator
    {
        static constexpr TypeSpeculationMask eval()
        {
            static_assert(IsValidTypeSpecialization<T>);
            return compute_type_speculation_mask::value<T>;
        }
    };

    template<typename... Args>
    struct eval_operator<tsm_or<Args...>>
    {
        template<typename First, typename... Remaining>
        static constexpr TypeSpeculationMask eval_impl()
        {
            TypeSpeculationMask r = eval_operator<First>::eval();
            if constexpr(sizeof...(Remaining) > 0)
            {
                r |= eval_impl<Remaining...>();
            }
            return r;
        }

        static constexpr TypeSpeculationMask eval()
        {
            if constexpr(sizeof...(Args) == 0)
            {
                return 0;
            }
            else
            {
                return eval_impl<Args...>();
            }
        }
    };

    template<typename Arg>
    struct eval_operator<tsm_not<Arg>>
    {
        static constexpr TypeSpeculationMask eval()
        {
            constexpr TypeSpeculationMask argVal = eval_operator<Arg>::eval();
            static_assert((argVal & type_speculation_mask_for_top) == argVal);
            return type_speculation_mask_for_top ^ argVal;
        }
    };

    template<typename T>
    static constexpr TypeSpeculationMask compute()
    {
        static_assert(IsValidTypeSpecialization<T>);
        if constexpr(std::is_same_v<T, tBottom>)
        {
            return 0;
        }
        else if constexpr(std::is_same_v<T, tTop>)
        {
            return type_speculation_mask_for_top;
        }
        else if constexpr(is_type_speculation_leaf<T>)
        {
            static_assert(type_speculation_leaf_ordinal<T> < sizeof(TypeSpeculationMask) * 8);
            constexpr TypeSpeculationMask result = static_cast<TypeSpeculationMask>(1) << type_speculation_leaf_ordinal<T>;
            static_assert(result <= type_speculation_mask_for_top);
            return result;
        }
        else
        {
            using Def = typename T::TSMDef;
            constexpr TypeSpeculationMask result = eval_operator<Def>::eval();
            static_assert(0 < result && result <= type_speculation_mask_for_top);
            return result;
        }
    }

    template<typename T>
    static constexpr TypeSpeculationMask value = compute<T>();
};

}   // namespace detail

template<typename T>
constexpr TypeSpeculationMask x_typeSpeculationMaskFor = detail::compute_type_speculation_mask::value<T>;
