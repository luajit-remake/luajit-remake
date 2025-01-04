#pragma once

#include "dfg_variant_traits_internal.h"
#include "generated/deegen_dfg_jit_all_generated_info.h"

namespace dfg {

// Utility that provides common logic for constructing a table of information for each DFG opcode
//
// User of this class should define a function
//     template<BCKind bcKind, size_t dvOrd, size_t cgOrd>
//     consteval void action() { ... }
//
// then call RunActionForEachDfgOpcode().
// The action() function will then be called for each DFG opcode in lexicographical order of <bcKind, dvOrd, cgOrd>.
//
template<typename CRTP>
struct DfgConstEvalForEachOpcode
{
private:
    // This function iterates through all subvariants of *one* bcKind
    // We use another wrapper to iterate through all the bcKinds to reduce template instantiation depth
    // (otherwise Clang gives a "stack nearly exhausted, compilation time may suffer" warning)
    //
    template<BCKind bcKind, size_t dvOrd, size_t cgOrd>
    consteval void for_each_subvariant_impl()
    {
        static_cast<CRTP*>(this)->template action<bcKind, dvOrd, cgOrd>();

        if constexpr(bcKind != BCKind::X_END_OF_ENUM)
        {
            static_assert(bcKind < BCKind::X_END_OF_ENUM);
            if constexpr(cgOrd + 1 == DfgVariantTraitFor<bcKind, dvOrd>::numCodegenFuncs)
            {
                if constexpr(dvOrd + 1 == NumDfgVariantsForBCKind<bcKind>::value)
                {
                    return;
                }
                else
                {
                    for_each_subvariant_impl<bcKind, dvOrd + 1, 0>();
                }
            }
            else
            {
                for_each_subvariant_impl<bcKind, dvOrd, cgOrd + 1>();
            }
        }
    }

    template<BCKind bcKind>
    consteval void for_each_bc_kind_impl()
    {
        for_each_subvariant_impl<bcKind, 0, 0>();

        if constexpr(bcKind != BCKind::X_END_OF_ENUM)
        {
            for_each_bc_kind_impl<static_cast<BCKind>(static_cast<size_t>(bcKind) + 1)>();
        }
    }

public:
    consteval void RunActionForEachDfgOpcode()
    {
        for_each_bc_kind_impl<static_cast<BCKind>(0)>();
    }
};

namespace detail {

struct DfgOpcodeMiscInfoCollector : DfgConstEvalForEachOpcode<DfgOpcodeMiscInfoCollector>
{
    template<BCKind bcKind, size_t dvOrd, size_t cgOrd>
    consteval void action()
    {
        if (bcKind < BCKind::X_END_OF_ENUM)
        {
            if (cgOrd == 0)
            {
                m_numDfgVariantsInBCKind[static_cast<size_t>(bcKind)]++;
            }
            m_numCodegenFuncsInBCKind[static_cast<size_t>(bcKind)]++;
        }
    }

    consteval DfgOpcodeMiscInfoCollector()
    {
        for (size_t i = 0; i <= static_cast<size_t>(BCKind::X_END_OF_ENUM); i++)
        {
            m_numDfgVariantsInBCKind[i] = 0;
            m_dfgVariantIdBase[i] = 0;
            m_numCodegenFuncsInBCKind[i] = 0;
            m_codegenFuncOrdBaseInBCKind[i] = 0;
        }
        RunActionForEachDfgOpcode();

        for (size_t i = 0; i < static_cast<size_t>(BCKind::X_END_OF_ENUM); i++)
        {
            m_dfgVariantIdBase[i + 1] = m_dfgVariantIdBase[i] + m_numDfgVariantsInBCKind[i];
            m_codegenFuncOrdBaseInBCKind[i + 1] = m_codegenFuncOrdBaseInBCKind[i] + m_numCodegenFuncsInBCKind[i];
        }
    }

    std::array<size_t, static_cast<size_t>(BCKind::X_END_OF_ENUM) + 1> m_numDfgVariantsInBCKind;
    std::array<size_t, static_cast<size_t>(BCKind::X_END_OF_ENUM) + 1> m_dfgVariantIdBase;
    std::array<size_t, static_cast<size_t>(BCKind::X_END_OF_ENUM) + 1> m_numCodegenFuncsInBCKind;
    std::array<size_t, static_cast<size_t>(BCKind::X_END_OF_ENUM) + 1> m_codegenFuncOrdBaseInBCKind;
};

inline constexpr DfgOpcodeMiscInfoCollector x_dfg_omic = DfgOpcodeMiscInfoCollector();

}   // namespace detail

template<BCKind bcKind>
constexpr size_t x_numDfgVariantsForBCKind = detail::x_dfg_omic.m_numDfgVariantsInBCKind[static_cast<size_t>(bcKind)];

template<BCKind bcKind>
constexpr size_t x_dfgVariantIdBaseForBCKind = detail::x_dfg_omic.m_dfgVariantIdBase[static_cast<size_t>(bcKind)];

template<BCKind bcKind>
constexpr size_t x_numCodegenFuncsInBCKind = detail::x_dfg_omic.m_numCodegenFuncsInBCKind[static_cast<size_t>(bcKind)];

template<BCKind bcKind>
constexpr size_t x_codegenFuncOrdBaseForBCKind = detail::x_dfg_omic.m_codegenFuncOrdBaseInBCKind[static_cast<size_t>(bcKind)];

constexpr size_t x_totalNumDfgUserNodeCodegenFuncs = x_codegenFuncOrdBaseForBCKind<BCKind::X_END_OF_ENUM>;

namespace detail {

struct DfgCodegenFuncOrdBaseForDfgVariantConstructor : DfgConstEvalForEachOpcode<DfgCodegenFuncOrdBaseForDfgVariantConstructor>
{
    template<BCKind bcKind, size_t dvOrd, size_t cgOrd>
    consteval void action()
    {
        if constexpr(bcKind < BCKind::X_END_OF_ENUM)
        {
            static_assert(dvOrd < x_numDfgVariantsForBCKind<bcKind>);
            size_t variantOrd = x_dfgVariantIdBaseForBCKind<bcKind> + dvOrd;
            m_numCodegenFuncsInVariant[variantOrd]++;
        }
    }

    consteval DfgCodegenFuncOrdBaseForDfgVariantConstructor()
    {
        for (size_t i = 0; i <= x_numVariants; i++)
        {
            m_numCodegenFuncsInVariant[i] = 0;
            m_codegenFuncOrdBaseForVariant[i] = 0;
        }
        RunActionForEachDfgOpcode();
        for (size_t i = 0; i < x_numVariants; i++)
        {
            m_codegenFuncOrdBaseForVariant[i + 1] = m_codegenFuncOrdBaseForVariant[i] + m_numCodegenFuncsInVariant[i];
        }
        ReleaseAssert(m_codegenFuncOrdBaseForVariant[x_numVariants] == x_totalNumDfgUserNodeCodegenFuncs);
    }

    static constexpr size_t x_numVariants = x_dfgVariantIdBaseForBCKind<BCKind::X_END_OF_ENUM>;
    std::array<size_t, x_numVariants + 1> m_numCodegenFuncsInVariant;
    std::array<size_t, x_numVariants + 1> m_codegenFuncOrdBaseForVariant;
};

inline constexpr DfgCodegenFuncOrdBaseForDfgVariantConstructor x_dfg_cgfnOrdBaseInfo = DfgCodegenFuncOrdBaseForDfgVariantConstructor();

template<BCKind bcKind, size_t dvOrd>
struct CodegenFuncOrdBaseForDfgVariantImpl
{
    static_assert(dvOrd < x_numDfgVariantsForBCKind<bcKind>);
    constexpr static size_t variantOrd = x_dfgVariantIdBaseForBCKind<bcKind> + dvOrd;
    constexpr static size_t numCodegenFns = x_dfg_cgfnOrdBaseInfo.m_numCodegenFuncsInVariant[variantOrd];
    constexpr static size_t codegenFnOrdBase = x_dfg_cgfnOrdBaseInfo.m_codegenFuncOrdBaseForVariant[variantOrd];
};

}   // namespace detail

template<BCKind bcKind, size_t dvOrd>
constexpr size_t x_numCodegenFuncsForDfgVariant = detail::CodegenFuncOrdBaseForDfgVariantImpl<bcKind, dvOrd>::numCodegenFns;

template<BCKind bcKind, size_t dvOrd>
constexpr size_t x_codegenFuncOrdBaseForDfgVariant = detail::CodegenFuncOrdBaseForDfgVariantImpl<bcKind, dvOrd>::codegenFnOrdBase;

}   // namespace dfg
