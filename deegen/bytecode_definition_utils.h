#pragma once

#include "tvalue.h"

enum class DeegenBytecodeOperandType
{
    INVALID_TYPE,
    BytecodeSlotOrConstant,     // this is a pseudo-type that must be concretized in each Variant()
    BytecodeSlot,
    Constant,
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32
};

namespace detail
{

constexpr bool DeegenBytecodeOperandIsLiteralType(DeegenBytecodeOperandType value)
{
    switch (value)
    {
    case DeegenBytecodeOperandType::INVALID_TYPE:
        ReleaseAssert(false);
    case DeegenBytecodeOperandType::BytecodeSlotOrConstant:
    case DeegenBytecodeOperandType::BytecodeSlot:
    case DeegenBytecodeOperandType::Constant:
        return true;
    case DeegenBytecodeOperandType::Int8:
    case DeegenBytecodeOperandType::UInt8:
    case DeegenBytecodeOperandType::Int16:
    case DeegenBytecodeOperandType::UInt16:
    case DeegenBytecodeOperandType::Int32:
    case DeegenBytecodeOperandType::UInt32:
        return false;
    }
}

}   // namespace detail

struct DeegenBytecodeDefinitionDescriptor
{
    constexpr static size_t x_maxOperands = 10;
    constexpr static size_t x_maxQuickenings = 10;
    constexpr static size_t x_maxVariants = 100;

    struct Operand
    {
        const char* m_name;
        DeegenBytecodeOperandType m_type;

        consteval Operand() : m_name(), m_type(DeegenBytecodeOperandType::INVALID_TYPE) { }
        consteval Operand(const char* name, DeegenBytecodeOperandType ty) : m_name(name), m_type(ty) { }
    };

    static consteval Operand BytecodeSlotOrConstant(const char* name)
    {
        return Operand { name, DeegenBytecodeOperandType::BytecodeSlotOrConstant };
    }

    static consteval Operand BytecodeSlot(const char* name)
    {
        return Operand { name, DeegenBytecodeOperandType::BytecodeSlot };
    }

    static consteval Operand Constant(const char* name)
    {
        return Operand { name, DeegenBytecodeOperandType::Constant };
    }

    enum class SpecializationKind : uint8_t
    {
        NotSpecialized,
        Literal,
        SpeculatedTypeForOptimizer,
        BytecodeSlot,
        BytecodeConstantWithType
    };

    struct SpecializedOperand
    {
        SpecializationKind m_kind;
        // The interpretation of 'm_value' depends on 'm_kind'
        // m_kind == Literal: m_value is the value of the literal casted to uint64_t
        // m_kind == SpeculatedTypeForOptimizer: m_value is the type speculation mask
        // m_kind == BytecodeSlot: m_value is unused
        // m_kind == BytecodeConstantWithType: m_value is the type speculation mask
        //
        uint64_t m_value;
    };

    struct SpecializedOperandRef
    {
        SpecializedOperand m_operand;
        size_t m_ord;
    };

    struct SpecializedVariant
    {
        template<typename... Args>
        consteval SpecializedVariant& Quickening(Args... /*args*/)
        {
            // TODO: append quickening variant
            return *this;
        }

        std::array<SpecializedOperand, x_maxOperands> m_base;
        std::array<std::array<SpecializedOperand, x_maxOperands>, x_maxQuickenings> m_quickenings;
    };

    struct OperandRef
    {
        template<typename T>
        consteval SpecializedOperandRef HasValue(T val)
        {
            static_assert(std::is_integral_v<T>);
            ReleaseAssert(detail::DeegenBytecodeOperandIsLiteralType(m_operand.m_type));
            return { .m_operand = { .m_kind = SpecializationKind::Literal, .m_value = static_cast<uint64_t>(val) }, .m_ord = m_ord };
        }

        template<typename T>
        consteval SpecializedOperandRef HasType()
        {
            static_assert(IsValidTypeSpecialization<T>);
            ReleaseAssert(m_operand.m_type == DeegenBytecodeOperandType::BytecodeSlotOrConstant || m_operand.m_type == DeegenBytecodeOperandType::BytecodeSlot);
            return { .m_operand = { .m_kind = SpecializationKind::SpeculatedTypeForOptimizer, .m_value = x_typeSpeculationMaskFor<T> }, .m_ord = m_ord };
        }

        consteval SpecializedOperandRef IsBytecodeSlot()
        {
            ReleaseAssert(m_operand.m_type == DeegenBytecodeOperandType::BytecodeSlotOrConstant);
            return { .m_operand = { .m_kind = SpecializationKind::BytecodeSlot, .m_value = 0 }, .m_ord = m_ord };
        }

        template<typename T>
        consteval SpecializedOperandRef IsConstant()
        {
            static_assert(IsValidTypeSpecialization<T>);
            ReleaseAssert(m_operand.m_type == DeegenBytecodeOperandType::BytecodeSlotOrConstant);
            return { .m_operand = { .m_kind = SpecializationKind::BytecodeConstantWithType, .m_value = x_typeSpeculationMaskFor<T> }, .m_ord = m_ord };
        }

        size_t m_ord;
        Operand m_operand;
    };

    template<typename... Args>
    consteval SpecializedVariant& Variant(Args... args)
    {
        ReleaseAssert(m_operandTypeListInitialized);

        constexpr size_t n = sizeof...(Args);
        std::array<SpecializedOperandRef, n> arr { args... };

        for (size_t i = 0; i < n; i++)
        {
            for (size_t j = 0; j < i; j++)
            {
                ReleaseAssert(arr[i].m_ord != arr[j].m_ord);
            }
        }

        ReleaseAssert(m_numVariants < x_maxVariants);
        SpecializedVariant& r = m_variants[m_numVariants];
        m_numVariants++;

        for (size_t i = 0; i < n; i++)
        {
            ReleaseAssert(arr[i].m_ord < m_numOperands);
            r.m_base[arr[i].m_ord] = arr[i].m_operand;
        }

        for (size_t i = 0; i < m_numOperands; i++)
        {
            SpecializedOperand o = r.m_base[i];
            switch (m_operandTypes[i].m_type)
            {
            case DeegenBytecodeOperandType::INVALID_TYPE:
            {
                ReleaseAssert(false);
                break;
            }
            case DeegenBytecodeOperandType::BytecodeSlotOrConstant:
            {
                ReleaseAssert((o.m_kind == SpecializationKind::BytecodeSlot || o.m_kind == SpecializationKind::BytecodeConstantWithType) && "All BytecodeSlotOrConstant must be specialized in each variant");
                break;
            }
            case DeegenBytecodeOperandType::BytecodeSlot:
            {
                ReleaseAssert(o.m_kind == SpecializationKind::NotSpecialized);
                break;
            }
            case DeegenBytecodeOperandType::Constant:
            {
                ReleaseAssert(o.m_kind == SpecializationKind::NotSpecialized || o.m_kind == SpecializationKind::BytecodeConstantWithType);
                break;
            }
            case DeegenBytecodeOperandType::Int8:
            case DeegenBytecodeOperandType::UInt8:
            case DeegenBytecodeOperandType::Int16:
            case DeegenBytecodeOperandType::UInt16:
            case DeegenBytecodeOperandType::Int32:
            case DeegenBytecodeOperandType::UInt32:
            {
                ReleaseAssert(o.m_kind == SpecializationKind::NotSpecialized || o.m_kind == SpecializationKind::Literal);
                break;
            }
            }
        }

        return r;
    }

    template<typename... Args>
    consteval void Operands(Args... args)
    {
        ReleaseAssert(!m_operandTypeListInitialized);
        m_operandTypeListInitialized = true;

        constexpr size_t n = sizeof...(Args);
        static_assert(n < x_maxOperands);

        m_numOperands = n;
        std::array<Operand, n> arr { args... };
        for (size_t i = 0; i < n; i++)
        {
            for (size_t j = 0; j < i; j++)
            {
                ReleaseAssert(std::string_view { arr[i].m_name } != std::string_view { arr[j].m_name });
            }
            ReleaseAssert(arr[i].m_type != DeegenBytecodeOperandType::INVALID_TYPE);
            m_operandTypes[i] = arr[i];
        }
    }

    template<typename T>
    consteval void Implementation(T v)
    {
        ReleaseAssert(!m_implementationInitialized);
        m_implementationInitialized = true;
        m_implementationFn = FOLD_CONSTEXPR(reinterpret_cast<void*>(v));
    }

    consteval OperandRef Op(std::string_view name)
    {
        ReleaseAssert(m_operandTypeListInitialized);
        for (size_t i = 0; i < m_numOperands; i++)
        {
            if (std::string_view { m_operandTypes[i].m_name } == name)
            {
                return { .m_ord = i, .m_operand = m_operandTypes[i] };
            }
        }
        ReleaseAssert(false);
    }

    consteval DeegenBytecodeDefinitionDescriptor()
        : m_operandTypeListInitialized(false)
        , m_implementationInitialized(false)
        , m_implementationFn(nullptr)
        , m_numOperands(0)
        , m_numVariants(0)
        , m_operandTypes()
        , m_variants()
    { }

    bool m_operandTypeListInitialized;
    bool m_implementationInitialized;
    void* m_implementationFn;
    size_t m_numOperands;
    size_t m_numVariants;
    std::array<Operand, x_maxOperands> m_operandTypes;
    std::array<SpecializedVariant, x_maxVariants> m_variants;
};

namespace detail
{

template<int v> struct deegen_end_bytecode_definitions_macro_used : deegen_end_bytecode_definitions_macro_used<v-1> { };
template<> struct deegen_end_bytecode_definitions_macro_used<-1> { static constexpr bool value = false; };

template<int v> struct deegen_bytecode_definition_info : deegen_bytecode_definition_info<v-1> { using tuple_type = typename deegen_bytecode_definition_info<v-1>::tuple_type; };
template<> struct deegen_bytecode_definition_info<-1> {
    using tuple_type = std::tuple<>;
    static constexpr std::array<const char*, 0> value { };
};

template<typename T>
struct deegen_get_bytecode_def_list_impl;

template<>
struct deegen_get_bytecode_def_list_impl<std::tuple<>>
{
    static constexpr std::array<DeegenBytecodeDefinitionDescriptor, 0> value { };
};

template<typename Arg1, typename... Args>
struct deegen_get_bytecode_def_list_impl<std::tuple<Arg1, Args...>>
{
    static constexpr Arg1 curv {};

    static_assert(std::is_base_of_v<DeegenBytecodeDefinitionDescriptor, Arg1>);
    static_assert(curv.m_operandTypeListInitialized);
    static_assert(curv.m_implementationInitialized);
    static_assert(curv.m_numVariants > 0);

    static constexpr auto value = constexpr_std_array_concat(std::array<DeegenBytecodeDefinitionDescriptor, 1> { curv }, deegen_get_bytecode_def_list_impl<std::tuple<Args...>>::value);
};

}   // namespace detail

// DEEGEN_END_BYTECODE_DEFINITIONS:
//   Must be used exactly once per translation unit
//   Must be put after all uses of 'DEEGEN_DEFINE_BYTECODE'
//
#define DEEGEN_END_BYTECODE_DEFINITIONS DEEGEN_END_BYTECODE_DEFINITIONS_IMPL(__COUNTER__)
#define DEEGEN_END_BYTECODE_DEFINITIONS_IMPL(counter)                                                                                                                                                       \
    static_assert(!detail::deegen_end_bytecode_definitions_macro_used<counter>::value, "DEEGEN_END_BYTECODE_DEFINITIONS should only be used once per translation unit, after all DEEGEN_DEFINE_BYTECODE");  \
    namespace detail { template<> struct deegen_end_bytecode_definitions_macro_used<counter + 1> { static constexpr bool value = true; }; }                                                                 \
    __attribute__((__used__)) inline constexpr auto x_deegen_impl_all_bytecode_names_in_this_tu = detail::deegen_bytecode_definition_info<counter>::value;                                                  \
    __attribute__((__used__)) inline constexpr auto x_deegen_impl_all_bytecode_defs_in_this_tu = detail::deegen_get_bytecode_def_list_impl<                                                                 \
        typename detail::deegen_bytecode_definition_info<counter>::tuple_type>::value;                                                                                                                      \
    static_assert(x_deegen_impl_all_bytecode_names_in_this_tu.size() == x_deegen_impl_all_bytecode_defs_in_this_tu.size());

// DEEGEN_DEFINE_BYTECODE:
//   Define a bytecode
//
#define DEEGEN_DEFINE_BYTECODE(name) DEEGEN_DEFINE_BYTECODE_IMPL(name, __COUNTER__)
#define DEEGEN_DEFINE_BYTECODE_IMPL(name, counter)      \
    static_assert(!detail::deegen_end_bytecode_definitions_macro_used<counter>::value, "DEEGEN_DEFINE_BYTECODE should not be used after DEEGEN_END_BYTECODE_DEFINITIONS");  \
    namespace {                                                                                                                                                             \
    /* define in anonymous namespace to trigger compiler warning if user forgot to write 'DEEGEN_END_BYTECODE_DEFINITIONS' at the end of the file */                        \
    struct DeegenUserBytecodeDefinitionImpl_ ## name final : public DeegenBytecodeDefinitionDescriptor {                                                                    \
        consteval DeegenUserBytecodeDefinitionImpl_ ## name ();                                                                                                             \
    };                                                                                                                                                                      \
    }   /* anonymous namespace */                                                                                                                                           \
    namespace detail {                                                                                                                                                      \
    template<> struct deegen_bytecode_definition_info<counter> {                                                                                                            \
        using tuple_type = tuple_append_element_t<typename deegen_bytecode_definition_info<counter-1>::tuple_type, DeegenUserBytecodeDefinitionImpl_ ## name>;              \
        static constexpr auto value = constexpr_std_array_concat(                                                                                                           \
                    deegen_bytecode_definition_info<counter-1>::value, std::array<const char*, 1> { PP_STRINGIFY(name) });                                                  \
    };                                                                                                                                                                      \
    }   /* namespace detail */                                                                                                                                              \
    consteval DeegenUserBytecodeDefinitionImpl_ ## name :: DeegenUserBytecodeDefinitionImpl_ ## name()

// Example usage:
// DEEGEN_DEFINE_BYTECODE(add) { ... }
// DEEGEN_DEFINE_BYTECODE(sub) { ... }
// DEEGEN_DEFINE_BYTECODE(mul) { ... }
// DEEGEN_END_BYTECODE_DEFINITIONS
//
