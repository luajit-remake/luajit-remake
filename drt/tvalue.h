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
    static constexpr uint64_t x_impossible_value = 4;

    constexpr MiscImmediateValue() = default;
    constexpr MiscImmediateValue(uint64_t value)
        : m_value(value)
    {
        assert(m_value == x_nil || m_value == x_true || m_value == x_false || m_value == x_impossible_value);
    }

    constexpr bool IsNil() const
    {
        return m_value == 0;
    }

    constexpr bool IsTrue() const
    {
        return m_value == x_true;
    }

    constexpr bool IsBoolean() const
    {
        return m_value != 0;
    }

    constexpr bool GetBooleanValue() const
    {
        assert(IsBoolean());
        return m_value & 1;
    }

    constexpr MiscImmediateValue WARN_UNUSED FlipBooleanValue() const
    {
        assert(IsBoolean());
        return MiscImmediateValue { m_value ^ 1 };
    }

    static constexpr MiscImmediateValue WARN_UNUSED CreateNil()
    {
        return MiscImmediateValue { x_nil };
    }

    static constexpr MiscImmediateValue WARN_UNUSED CreateBoolean(bool v)
    {
        return MiscImmediateValue { v ? x_true : x_false };
    }

    static constexpr MiscImmediateValue WARN_UNUSED CreateFalse()
    {
        return MiscImmediateValue { x_false };
    }

    static constexpr MiscImmediateValue WARN_UNUSED CreateTrue()
    {
        return MiscImmediateValue { x_true };
    }

    // This value will never show up in the TValue computed by user program
    // This allows us to safely create a TValue which bit pattern is different from all TValues computable by user program
    //
    static constexpr MiscImmediateValue WARN_UNUSED CreateImpossibleValue()
    {
        return MiscImmediateValue { x_impossible_value };
    }

    constexpr bool WARN_UNUSED operator==(const MiscImmediateValue& rhs) const
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

    constexpr ALWAYS_INLINE TValue() = default;
    constexpr explicit ALWAYS_INLINE TValue(uint64_t value) : m_value(value) { }

    static constexpr uint64_t x_int32Tag = 0xFFFBFFFF00000000ULL;
    static constexpr uint64_t x_mivTag = 0xFFFCFFFF0000007FULL;

    // Translates to a single ANDN instruction with BMI1 support
    //
    constexpr bool ALWAYS_INLINE IsInt32() const
    {
        bool result = (m_value & x_int32Tag) == x_int32Tag;
        AssertIff(result, static_cast<uint32_t>(m_value >> 32) == 0xFFFBFFFFU);
        return result;
    }

    // Translates to imm8 LEA instruction + ANDN instruction with BMI1 support
    //
    constexpr bool ALWAYS_INLINE IsMIV() const
    {
        bool result = (m_value & (x_mivTag - 0x7F)) == (x_mivTag - 0x7F);
        AssertIff(result, x_mivTag - 0x7F <= m_value && m_value <= x_mivTag);
        return result;
    }

    constexpr bool ALWAYS_INLINE IsPointer() const
    {
        bool result = (m_value > x_mivTag);
        AssertIff(result, static_cast<uint32_t>(m_value >> 32) >= 0xFFFFFFFCU &&
                          static_cast<uint32_t>(m_value >> 32) <= 0xFFFFFFFEU);
        return result;
    }

    constexpr bool ALWAYS_INLINE IsDouble() const
    {
        bool result = m_value < x_int32Tag;
        AssertIff(result, m_value <= 0xFFFAFFFFFFFFFFFFULL);
        return result;
    }

    constexpr bool ALWAYS_INLINE IsDoubleNotNaN() const
    {
        return !IsNaN(cxx2a_bit_cast<double>(m_value));
    }

    constexpr bool ALWAYS_INLINE IsDoubleNaN() const
    {
        constexpr uint64_t x_pureNaN = 0x7ff8000000000000ULL;
        return m_value == x_pureNaN;
    }

    HeapEntityType ALWAYS_INLINE GetHeapEntityType() const;

    // Interpret the TValue as a double.
    // This means that if it is indeed a double, its value will be returned.
    // If it is not a double, due to the boxing scheme, the returned value will be a double NaN.
    //
    constexpr double ALWAYS_INLINE ViewAsDouble() const
    {
        return cxx2a_bit_cast<double>(m_value);
    }

    constexpr double ALWAYS_INLINE AsDouble() const
    {
        assert(IsDouble() && !IsMIV() && !IsPointer() && !IsInt32());
        return cxx2a_bit_cast<double>(m_value);
    }

    constexpr int32_t ALWAYS_INLINE AsInt32() const
    {
        assert(IsInt32() && !IsMIV() && !IsPointer() && !IsDouble());
        return BitwiseTruncateTo<int32_t>(m_value);
    }

    // Return true if the value is not 'nil' or 'false'
    //
    constexpr bool ALWAYS_INLINE IsTruthy() const
    {
        // This is hacky and unfortunate, but it turns out that if written naively (like
        // the commented out line), LLVM will emit a switch with 2 cases and 1 default
        // even after optimization, and relies on the backend to implement the switch with an
        // OR followed by a CMP.
        //
        // In other words, the optimization of LLVM's SwitchInst happens at the LLVM backend,
        // not at LLVM IR level.
        //
        // While this works for LLVM, it doesn't work for our tag register optimization.
        // If we replace the constant 'x_mivTag' with our opaque tag register,
        // the property that the two comparisons can be reduced to one with an OR instruction
        // is lost. So we need to teach our tag register optimization pass to recognize
        // the switch pattern and optimize it, which requires a lot of unjustified work.
        //
        // So we simply do this optimization by hand right now. It unfortunately relies on
        // the fact that 'MIV::x_nil == 0', 'MIV::x_false == 2', and no MIV has value 1.
        //
        // (The ugly: we do not do write (m_value | 2) == x_mivTag because it turns out that
        // for some reason LLVM would rewrite it to 'm_value & -3 == x_mivTag - 2' instead,
        // and once we change 'x_mivTag' to the tag register, the '-2' part will have to be
        // executed and cannot be optimized out, resulting in one more instruction. I don't
        // think this extra instruction matters at all, but let's avoid it since we can.)
        //
        return (m_value ^ x_mivTag) > 2;
        // return m_value != Nil().m_value && m_value != CreateFalse().m_value;
    }

    template<typename T = void>
    UserHeapPointer<T> ALWAYS_INLINE AsPointer() const
    {
        assert(IsPointer() && !IsMIV() && !IsDouble() && !IsInt32());
        return UserHeapPointer<T> { reinterpret_cast<HeapPtr<T>>(m_value) };
    }

    constexpr MiscImmediateValue ALWAYS_INLINE AsMIV() const
    {
        assert(IsMIV() && !IsDouble() && !IsInt32() && !IsPointer());
        return MiscImmediateValue { m_value ^ x_mivTag };
    }

    static constexpr TValue WARN_UNUSED CreateInt32(int32_t value)
    {
        TValue result { x_int32Tag | ZeroExtendTo<uint64_t>(value) };
        assert(result.AsInt32() == value);
        return result;
    }

    static constexpr TValue WARN_UNUSED CreateDouble(double value)
    {
        TValue result { cxx2a_bit_cast<uint64_t>(value) };
        SUPRESS_FLOAT_EQUAL_WARNING(
            AssertImp(!std::isnan(value), result.AsDouble() == value);
            AssertIff(std::isnan(value), std::isnan(result.AsDouble()));
        )
        return result;
    }

    template<typename T>
    static TValue WARN_UNUSED CreatePointer(UserHeapPointer<T> ptr)
    {
        TValue result { static_cast<uint64_t>(ptr.m_value) };
        assert(result.AsPointer<T>() == ptr);
        return result;
    }

    template<typename T>
    static TValue WARN_UNUSED CreatePointer(HeapPtr<T> ptr)
    {
        uint64_t val = reinterpret_cast<uint64_t>(ptr);
        assert(val >= 0xFFFFFFFC00000000ULL);
        TValue result { val };
        return result;
    }

    template<typename T>
    static TValue WARN_UNUSED CreatePointer(T* ptr)
    {
        return CreatePointer(TranslateToHeapPtr(ptr));
    }

    static constexpr TValue WARN_UNUSED CreateMIV(MiscImmediateValue miv)
    {
        TValue result { miv.m_value ^ x_mivTag };
        assert(result.AsMIV().m_value == miv.m_value);
        return result;
    }

    static constexpr TValue CreateImpossibleValue()
    {
        return TValue::CreateMIV(MiscImmediateValue::CreateImpossibleValue());
    }

    static constexpr TValue CreateTrue()
    {
        return TValue::CreateMIV(MiscImmediateValue::CreateTrue());
    }

    static constexpr TValue CreateFalse()
    {
        return TValue::CreateMIV(MiscImmediateValue::CreateFalse());
    }

    static constexpr TValue CreateBoolean(bool v)
    {
        return TValue::CreateMIV(MiscImmediateValue::CreateBoolean(v));
    }

    static constexpr TValue Nil()
    {
        return TValue::CreateMIV(MiscImmediateValue::CreateNil());
    }

    constexpr bool ALWAYS_INLINE IsNil() const
    {
        return m_value == Nil().m_value;
    }

    template<typename T>
    bool WARN_UNUSED ALWAYS_INLINE Is() const;

    template<typename T>
    auto WARN_UNUSED ALWAYS_INLINE As() const;

    template<typename T, typename = std::enable_if_t<fn_num_args<decltype(&T::encode)> == 1>>
    static TValue WARN_UNUSED Create(arg_nth_t<decltype(&T::encode), 0 /*argOrd*/> val);

    template<typename T, typename = std::enable_if_t<fn_num_args<decltype(&T::encode)> == 0>>
    static TValue WARN_UNUSED Create();

    uint64_t m_value;
};

// Below are the type speculation / specialization definitions
//
// TSMDef: the bitmask definition of the type
// x_estimatedCheckCost: the estimated cost to execute a check of this type.
// The estimation is currently a bit arbitrary, but "one single-cycle instruction = 10 cost" is a good baseline
//

struct TypeSpeculationLeaf;
template<typename... Args> struct tsm_or;
template<typename Arg> struct tsm_not;

struct tNil
{
    using TSMDef = TypeSpeculationLeaf;
    static constexpr size_t x_estimatedCheckCost = 10;

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
    static constexpr size_t x_estimatedCheckCost = 60;

    static bool check(TValue v)
    {
        return v.IsMIV() && !v.IsNil();
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

struct tMIV
{
    using TSMDef = tsm_or<tNil, tBool>;
    static constexpr size_t x_estimatedCheckCost = 30;

    static bool check(TValue v)
    {
        return v.IsMIV();
    }

    static TValue encode(MiscImmediateValue v)
    {
        return TValue::CreateMIV(v);
    }

    static MiscImmediateValue decode(TValue v)
    {
        return v.AsMIV();
    }
};

struct tDoubleNotNaN
{
    using TSMDef = TypeSpeculationLeaf;
    static constexpr size_t x_estimatedCheckCost = 10;

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
    static constexpr size_t x_estimatedCheckCost = 30;

    static bool check(TValue v)
    {
        return v.IsDoubleNaN();
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
    static constexpr size_t x_estimatedCheckCost = 20;

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
    static constexpr size_t x_estimatedCheckCost = 10;

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

// This function is only for internal use, user should never call this function
// Don't change this function name: it is hardcoded for our LLVM logic
//
// The compiler cannot automatically figure out that the pointer dereference here always yield the
// same value for the same pointer, since this fact comes from high-level knowledge of the system.
// So we manually add 'readnone' (which is GCC/Clang attribute '__const__') to this function.
//
inline HeapEntityType WARN_UNUSED __attribute__((__const__)) DeegenImpl_TValueGetPointerType(TValue v)
{
    return v.AsPointer<UserHeapGcObjectHeader>().As()->m_type;
}

inline HeapEntityType ALWAYS_INLINE TValue::GetHeapEntityType() const
{
    assert(IsPointer());
    return DeegenImpl_TValueGetPointerType(*this);
}

#define macro(hoi)                                                                                                              \
    struct PP_CAT(t, HOI_ENUM_NAME(hoi)) {                                                                                      \
        using TSMDef = TypeSpeculationLeaf;                                                                                     \
        static constexpr size_t x_estimatedCheckCost = 100;                                                                     \
        using PtrType = HeapObjectTypeForEnum<HeapEntityType::HOI_ENUM_NAME(hoi)>;                                              \
                                                                                                                                \
        static bool check(TValue v)                                                                                             \
        {                                                                                                                       \
            return v.IsPointer() && DeegenImpl_TValueGetPointerType(v) == HeapEntityType::HOI_ENUM_NAME(hoi);                   \
        }                                                                                                                       \
                                                                                                                                \
        static TValue encode(HeapPtr<PtrType> o)                                                                                \
        {                                                                                                                       \
            return TValue::CreatePointer(o);                                                                                    \
        }                                                                                                                       \
                                                                                                                                \
        static HeapPtr<PtrType> decode(TValue v)                                                                                \
        {                                                                                                                       \
            return TranslateToHeapPtr(v.AsPointer<PtrType>().As());                                                                                 \
        }                                                                                                                       \
    };

PP_FOR_EACH(macro, LANGUAGE_EXPOSED_HEAP_OBJECT_INFO_LIST)
#undef macro

struct tHeapEntity
{
#define macro(hoi) , PP_CAT(t, HOI_ENUM_NAME(hoi))
    using TSMDef = tsm_or<tsm_or<> PP_FOR_EACH(macro, LANGUAGE_EXPOSED_HEAP_OBJECT_INFO_LIST)>;
#undef macro

    static constexpr size_t x_estimatedCheckCost = 10;

    static bool check(TValue v)
    {
        return v.IsPointer();
    }

    static TValue encode(HeapPtr<UserHeapGcObjectHeader> v)
    {
        return TValue::CreatePointer(UserHeapPointer<UserHeapGcObjectHeader> { v });
    }

    static HeapPtr<UserHeapGcObjectHeader> decode(TValue v)
    {
        return TranslateToHeapPtr(v.AsPointer().As<UserHeapGcObjectHeader>());
    }
};

// Two special lattice values: the bottom and the top
//
struct tBottom
{
    static constexpr size_t x_estimatedCheckCost = 0;

    static bool check(TValue /*v*/)
    {
        return false;
    }
};

struct tTop
{
    static constexpr size_t x_estimatedCheckCost = 0;

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
  , tMIV
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

}   // namespace detail

constexpr size_t x_numUsefulBitsInTypeSpeculationMask = detail::num_leaves_in_type_speculation_list<TypeSpecializationList>::value;

namespace detail {

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
    constexpr size_t numLeaves = x_numUsefulBitsInTypeSpeculationMask;
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

namespace detail {

template<typename T>
struct get_type_speculation_defs;

template<typename... Args>
struct get_type_speculation_defs<std::tuple<Args...>>
{
    static constexpr size_t count = sizeof...(Args);

    template<typename T>
    static constexpr std::array<std::pair<TypeSpeculationMask, std::string_view>, 1> get_one()
    {
        return std::array<std::pair<TypeSpeculationMask, std::string_view>, 1> { std::make_pair(x_typeSpeculationMaskFor<T>, __stringify_type__<T>()) };
    }

    template<typename First, typename... Remaining>
    static constexpr auto get_impl()
    {
        if constexpr(sizeof...(Remaining) == 0)
        {
            return get_one<First>();
        }
        else
        {
            return constexpr_std_array_concat(get_one<First>(), get_impl<Remaining...>());
        }
    }

    static constexpr auto get()
    {
        return get_impl<Args...>();
    }

    static constexpr std::array<std::pair<TypeSpeculationMask, std::string_view>, count> value = get();

    static constexpr auto get_sorted()
    {
        using ElementT = std::pair<TypeSpeculationMask, std::string_view>;
        auto arr = value;
        std::sort(arr.begin(), arr.end(), [](const ElementT& lhs, const ElementT& rhs) -> bool {
            if (lhs.first != rhs.first)
            {
                return lhs.first > rhs.first;
            }
            return lhs.second < rhs.second;
        });
        return arr;
    }

    static constexpr std::array<std::pair<TypeSpeculationMask, std::string_view>, count> sorted_value = get_sorted();
};

}   // namespace detail

constexpr auto x_list_of_type_speculation_mask_and_name = detail::get_type_speculation_defs<TypeSpecializationList>::sorted_value;

// Returns the human readable definitions of each type speculation mask
//
std::string WARN_UNUSED DumpHumanReadableTypeSpeculationDefinitions();

// Returns the human readable string of a speculation
//
std::string WARN_UNUSED DumpHumanReadableTypeSpeculation(TypeSpeculationMask mask, bool printMaskValue = false);

// Some utility logic and macros for typecheck strength reduction definitions
//
struct tvalue_typecheck_strength_reduction_rule
{
    // The type mask to check
    //
    TypeSpeculationMask m_typeToCheck;
    // The proven type mask precondition
    //
    TypeSpeculationMask m_typePrecondition;
    // The function to do the job
    //
    using Fn = bool(*)(TValue);
    Fn m_implementation;
    // The estimated cost
    //
    size_t m_estimatedCost;
};

// Don't rename: this name is hardcoded for our LLVM logic
//
template<typename T, typename U> struct tvalue_typecheck_strength_reduction_impl_holder /*intentionally undefined*/;

template<int v> struct tvalue_typecheck_strength_reduction_def_helper : tvalue_typecheck_strength_reduction_def_helper<v-1> {};

template<>
struct tvalue_typecheck_strength_reduction_def_helper<-1>
{
    static constexpr std::array<tvalue_typecheck_strength_reduction_rule, 0> value {};
};

// Internal use only, do not call from user code
// Do not change this function name: it is hardcoded for our LLVM logic
//
// Despite that LLVM can deduce those functions are '__const__', we are manually adding
// '__const__' attribute here because it's risky to run LLVM's function attribute inference
// pass early in our transform pipeline (since our transformation can break them).
// However, we do want accurate function attributes to help LLVM desugar the IR, so we
// simply add important attributes manually as a workaround.
//
// We are adding '__flatten__' attribute to workaround a limitation in our
// LLVMRepeatedInliningInhibitor utility: see comments in that class.
//
template<typename T>
bool __attribute__((__const__, __flatten__)) DeegenImpl_TValueIs(TValue val)
{
    return T::check(val);
}

// Internal use only, do not call from user code
// Do not change this function name: it is hardcoded for our LLVM logic
//
template<typename T>
auto __attribute__((__const__, __flatten__)) DeegenImpl_TValueAs(TValue val)
{
    assert(val.Is<T>());
    return T::decode(val);
}

// Some helper code to get the basic versions (no type precondition) of the type check logic
//
namespace detail {

template<typename T>
struct get_basic_tvalue_typecheck_impls;

template<typename... Args>
struct get_basic_tvalue_typecheck_impls<std::tuple<Args...>>
{
    template<typename First, typename... Remaining>
    static constexpr auto get_impl()
    {
        return constexpr_std_array_concat(
            get_basic_tvalue_typecheck_impls<std::tuple<Remaining...>>::value,
            std::array<tvalue_typecheck_strength_reduction_rule, 1> {
                tvalue_typecheck_strength_reduction_rule {
                    .m_typeToCheck = x_typeSpeculationMaskFor<First>,
                    .m_typePrecondition = x_typeSpeculationMaskFor<tTop>,
                    .m_implementation = DeegenImpl_TValueIs<First>,
                    .m_estimatedCost = First::x_estimatedCheckCost
                }
            });
    }

    static constexpr auto get()
    {
        if constexpr(sizeof...(Args) == 0)
        {
            return std::array<tvalue_typecheck_strength_reduction_rule, 0> {};
        }
        else
        {
            return get_impl<Args...>();
        }
    }

    static constexpr auto value = get();
};

// It's really hard (and STL-implementation-dependent) to parse a constexpr std::array in LLVM bitcode. This is why we introduce this helper
//
template<typename T, size_t N>
struct llvm_friendly_std_array
{
    constexpr size_t size() const { return N; }
    T v[N];
};

template<typename T>
struct std_array_to_llvm_friendly_array_impl;

template<typename T, size_t N>
struct std_array_to_llvm_friendly_array_impl<std::array<T, N>>
{
    template<size_t... I>
    static consteval llvm_friendly_std_array<T, N> impl(std::array<T, N> v, std::index_sequence<I...>)
    {
        return llvm_friendly_std_array<T, N> { v[I]... };
    }

    static consteval llvm_friendly_std_array<T, N> get(std::array<T, N> v)
    {
        return impl(v, std::make_index_sequence<N> {});
    }
};

template<typename T>
consteval auto std_array_to_llvm_friendly_array(T v)
{
    return std_array_to_llvm_friendly_array_impl<T>::get(v);
}

}  // namespace detail

#define DEFINE_TVALUE_TYPECHECK_STRENGTH_REDUCTION(typeToCheck, typePrecondition, estimatedCost, argName) DEFINE_TVALUE_TYPECHECK_STRENGTH_REDUCTION_IMPL(typeToCheck, typePrecondition, estimatedCost, argName, __COUNTER__)
#define DEFINE_TVALUE_TYPECHECK_STRENGTH_REDUCTION_IMPL(typeToCheck, typePrecondition, estimatedCost, argName, counter)                              \
    template<>                                                                                                                                       \
    struct tvalue_typecheck_strength_reduction_impl_holder<typeToCheck, typePrecondition>                                                            \
    {                                                                                                                                                \
        static bool impl(TValue);                                                                                                                    \
    };                                                                                                                                               \
    template<>                                                                                                                                       \
    struct tvalue_typecheck_strength_reduction_def_helper<counter>                                                                                   \
    {                                                                                                                                                \
        static_assert((x_typeSpeculationMaskFor<typeToCheck> & x_typeSpeculationMaskFor<typePrecondition>) == x_typeSpeculationMaskFor<typeToCheck>, \
                      "precondition must be a superset of the types to check");                                                                      \
        static constexpr auto value = constexpr_std_array_concat(                                                                                    \
            tvalue_typecheck_strength_reduction_def_helper<counter - 1>::value,                                                                      \
            std::array<tvalue_typecheck_strength_reduction_rule, 1> {                                                                                \
                tvalue_typecheck_strength_reduction_rule {                                                                                           \
                    .m_typeToCheck = x_typeSpeculationMaskFor<typeToCheck>,                                                                          \
                    .m_typePrecondition = x_typeSpeculationMaskFor<typePrecondition>,                                                                \
                    .m_implementation = tvalue_typecheck_strength_reduction_impl_holder<typeToCheck, typePrecondition>::impl,                        \
                    .m_estimatedCost = (estimatedCost) } });                                                                                         \
    };                                                                                                                                               \
    inline bool tvalue_typecheck_strength_reduction_impl_holder<typeToCheck, typePrecondition>::impl(TValue argName)

#define END_OF_TVALUE_TYPECHECK_STRENGTH_REDUCTION_DEFINITIONS                                              \
    __attribute__((__used__)) inline constexpr auto x_list_of_tvalue_typecheck_strength_reduction_rules =   \
        detail::std_array_to_llvm_friendly_array(                                                           \
            constexpr_std_array_concat(                                                                     \
                tvalue_typecheck_strength_reduction_def_helper<__COUNTER__>::value,                         \
                detail::get_basic_tvalue_typecheck_impls<TypeSpecializationList>::value));


// If we are included by a bytecode definition source file, emit information of the strength reduction rules
//
#ifdef DEEGEN_ANNOTATED_SOURCE_FOR_BYTECODE_DEFINITION

// For now, we only need the rules that remove the IsPointer() check
//
#define macro(hoi)                                                                                                  \
    DEFINE_TVALUE_TYPECHECK_STRENGTH_REDUCTION(PP_CAT(t, HOI_ENUM_NAME(hoi)), tHeapEntity, 60 /*estimatedCost*/, v) \
    {                                                                                                               \
        return DeegenImpl_TValueGetPointerType(v) == HeapEntityType::HOI_ENUM_NAME(hoi);                            \
    }

PP_FOR_EACH(macro, LANGUAGE_EXPOSED_HEAP_OBJECT_INFO_LIST)
#undef macro

END_OF_TVALUE_TYPECHECK_STRENGTH_REDUCTION_DEFINITIONS

#endif  // defined(DEEGEN_ANNOTATED_SOURCE_FOR_BYTECODE_DEFINITION)

template<typename T>
bool WARN_UNUSED ALWAYS_INLINE TValue::Is() const
{
    static_assert(IsValidTypeSpecialization<T>);
    return DeegenImpl_TValueIs<T>(*this);
}

template<typename T>
auto WARN_UNUSED ALWAYS_INLINE TValue::As() const
{
    static_assert(IsValidTypeSpecialization<T>);
    return DeegenImpl_TValueAs<T>(*this);
}

template<typename T, typename>
TValue WARN_UNUSED TValue::Create(arg_nth_t<decltype(&T::encode), 0 /*argOrd*/> val)
{
    static_assert(IsValidTypeSpecialization<T>);
    return T::encode(val);
}

template<typename T, typename>
TValue WARN_UNUSED TValue::Create()
{
    static_assert(IsValidTypeSpecialization<T>);
    return T::encode();
}

class FunctionObject;

// stack frame format:
//     [... VarArgs ...] [Header] [... Locals ...]
//                                ^
//                                stack frame pointer (sfp)
//
class alignas(8) StackFrameHeader
{
public:
    // The function corresponding to this stack frame
    // Must be first element: this is expected by a lot of places
    //
    FunctionObject* m_func;
    // The address of the caller stack frame (points to the END of the stack frame header)
    //
    void* m_caller;
    // The return address
    //
    void* m_retAddr;
    // If the function is calling (i.e. not topmost frame), denotes the caller position that performed the call
    // For interpreter, this is pointer to the bytecode (as a HeapPtr) that performed the call
    // For baseline JIT fast path, this is not populated
    // For baseline JIT slow path, this is the lower 32 bits of the SlowPathData pointer
    // TODO: rename this to m_callSiteInfo to better reflect the nature of this field
    //
    SystemHeapPointer<uint8_t> m_callerBytecodePtr;
    // Total number of variadic arguments passed to the function
    //
    uint32_t m_numVariadicArguments;

    static StackFrameHeader* Get(void* stackBase)
    {
        return reinterpret_cast<StackFrameHeader*>(stackBase) - 1;
    }
};

static_assert(sizeof(StackFrameHeader) % sizeof(TValue) == 0);
static constexpr size_t x_numSlotsForStackFrameHeader = sizeof(StackFrameHeader) / sizeof(TValue);
