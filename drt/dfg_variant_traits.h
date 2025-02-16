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
                ReleaseAssert(m_numDfgVariantsInBCKind[static_cast<size_t>(bcKind)] < 65535);
                m_numDfgVariantsInBCKind[static_cast<size_t>(bcKind)]++;
            }
            ReleaseAssert(m_numCodegenFuncsInBCKind[static_cast<size_t>(bcKind)] < 65535);
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
            {
                size_t tmp = static_cast<size_t>(m_dfgVariantIdBase[i]) + m_numDfgVariantsInBCKind[i];
                ReleaseAssert(tmp <= 65535);
                m_dfgVariantIdBase[i + 1] = static_cast<uint16_t>(tmp);
            }
            {
                size_t tmp = static_cast<size_t>(m_codegenFuncOrdBaseInBCKind[i]) + m_numCodegenFuncsInBCKind[i];
                ReleaseAssert(tmp <= 65535);
                m_codegenFuncOrdBaseInBCKind[i + 1] = static_cast<uint16_t>(tmp);
            }
        }
    }

    std::array<uint16_t, static_cast<size_t>(BCKind::X_END_OF_ENUM) + 1> m_numDfgVariantsInBCKind;
    std::array<uint16_t, static_cast<size_t>(BCKind::X_END_OF_ENUM) + 1> m_dfgVariantIdBase;
    std::array<uint16_t, static_cast<size_t>(BCKind::X_END_OF_ENUM) + 1> m_numCodegenFuncsInBCKind;
    std::array<uint16_t, static_cast<size_t>(BCKind::X_END_OF_ENUM) + 1> m_codegenFuncOrdBaseInBCKind;
};

inline constexpr DfgOpcodeMiscInfoCollector x_dfg_omic = DfgOpcodeMiscInfoCollector();

}   // namespace detail

inline constexpr std::array<uint16_t, static_cast<size_t>(BCKind::X_END_OF_ENUM) + 1> x_numDfgVariantsForBCKind = detail::x_dfg_omic.m_numDfgVariantsInBCKind;
inline constexpr std::array<uint16_t, static_cast<size_t>(BCKind::X_END_OF_ENUM) + 1> x_dfgVariantIdBaseForBCKind = detail::x_dfg_omic.m_dfgVariantIdBase;
inline constexpr std::array<uint16_t, static_cast<size_t>(BCKind::X_END_OF_ENUM) + 1> x_numCodegenFuncsInBCKind = detail::x_dfg_omic.m_numCodegenFuncsInBCKind;
inline constexpr std::array<uint16_t, static_cast<size_t>(BCKind::X_END_OF_ENUM) + 1> x_codegenFuncOrdBaseForBCKind = detail::x_dfg_omic.m_codegenFuncOrdBaseInBCKind;

constexpr size_t x_totalNumDfgUserNodeCodegenFuncs = x_codegenFuncOrdBaseForBCKind[static_cast<size_t>(BCKind::X_END_OF_ENUM)];

namespace detail {

struct DfgCodegenFuncOrdBaseForDfgVariantConstructor : DfgConstEvalForEachOpcode<DfgCodegenFuncOrdBaseForDfgVariantConstructor>
{
    template<BCKind bcKind, size_t dvOrd, size_t cgOrd>
    consteval void action()
    {
        if constexpr(bcKind < BCKind::X_END_OF_ENUM)
        {
            static_assert(dvOrd < x_numDfgVariantsForBCKind[static_cast<size_t>(bcKind)]);
            size_t variantOrd = x_dfgVariantIdBaseForBCKind[static_cast<size_t>(bcKind)] + dvOrd;
            m_numCodegenFuncsInVariant[variantOrd]++;
            m_codegenInfoForVariant[variantOrd] = DfgVariantTraitFor<bcKind, dvOrd>::trait;
            m_slowPathDataNeedsRegConfig[variantOrd] = DfgVariantTraitFor<bcKind, dvOrd>::needRegConfigInSlowPathData;
            m_slowPathDataLength[variantOrd] = DfgVariantTraitFor<bcKind, dvOrd>::slowPathDataLen;
        }
    }

    consteval DfgCodegenFuncOrdBaseForDfgVariantConstructor()
    {
        for (size_t i = 0; i <= x_numVariants; i++)
        {
            m_numCodegenFuncsInVariant[i] = 0;
            m_codegenFuncOrdBaseForVariant[i] = 0;
            m_codegenInfoForVariant[i] = nullptr;
            m_slowPathDataNeedsRegConfig[i] = false;
            m_slowPathDataLength[i] = 0;
        }
        RunActionForEachDfgOpcode();
        for (size_t i = 0; i < x_numVariants; i++)
        {
            m_codegenFuncOrdBaseForVariant[i + 1] = m_codegenFuncOrdBaseForVariant[i] + m_numCodegenFuncsInVariant[i];
            ReleaseAssert(m_codegenInfoForVariant[i] != nullptr);
        }
        ReleaseAssert(m_codegenFuncOrdBaseForVariant[x_numVariants] == x_totalNumDfgUserNodeCodegenFuncs);
    }

    static constexpr size_t x_numVariants = x_dfgVariantIdBaseForBCKind[static_cast<size_t>(BCKind::X_END_OF_ENUM)];
    std::array<size_t, x_numVariants + 1> m_numCodegenFuncsInVariant;
    std::array<size_t, x_numVariants + 1> m_codegenFuncOrdBaseForVariant;
    std::array<const DfgVariantTraits*, x_numVariants + 1> m_codegenInfoForVariant;
    std::array<bool, x_numVariants + 1> m_slowPathDataNeedsRegConfig;
    std::array<uint32_t, x_numVariants + 1> m_slowPathDataLength;
};

inline constexpr DfgCodegenFuncOrdBaseForDfgVariantConstructor x_dfg_cgfnOrdBaseInfo = DfgCodegenFuncOrdBaseForDfgVariantConstructor();

template<BCKind bcKind, size_t dvOrd>
struct CodegenFuncOrdBaseForDfgVariantImpl
{
    static_assert(dvOrd < x_numDfgVariantsForBCKind[static_cast<size_t>(bcKind)]);
    constexpr static size_t variantOrd = x_dfgVariantIdBaseForBCKind[static_cast<size_t>(bcKind)] + dvOrd;
    constexpr static size_t numCodegenFns = x_dfg_cgfnOrdBaseInfo.m_numCodegenFuncsInVariant[variantOrd];
    constexpr static size_t codegenFnOrdBase = x_dfg_cgfnOrdBaseInfo.m_codegenFuncOrdBaseForVariant[variantOrd];
};

}   // namespace detail

template<BCKind bcKind, size_t dvOrd>
constexpr size_t x_numCodegenFuncsForDfgVariant = detail::CodegenFuncOrdBaseForDfgVariantImpl<bcKind, dvOrd>::numCodegenFns;

template<BCKind bcKind, size_t dvOrd>
constexpr size_t x_codegenFuncOrdBaseForDfgVariant = detail::CodegenFuncOrdBaseForDfgVariantImpl<bcKind, dvOrd>::codegenFnOrdBase;

inline constexpr auto x_dfgCodegenInfoForVariantId = detail::x_dfg_cgfnOrdBaseInfo.m_codegenInfoForVariant;
inline constexpr auto x_dfgVariantNeedsRegConfigInSlowPathData = detail::x_dfg_cgfnOrdBaseInfo.m_slowPathDataNeedsRegConfig;
inline constexpr auto x_dfgVariantSlowPathDataLen = detail::x_dfg_cgfnOrdBaseInfo.m_slowPathDataLength;

inline const DfgVariantTraits* GetCodegenInfoForDfgVariant(BCKind bcKind, size_t variantOrd)
{
    TestAssert(bcKind < BCKind::X_END_OF_ENUM);
    TestAssert(variantOrd < x_numDfgVariantsForBCKind[static_cast<size_t>(bcKind)]);
    size_t dfgVariantId = x_dfgVariantIdBaseForBCKind[static_cast<size_t>(bcKind)] + variantOrd;
    TestAssert(dfgVariantId < x_dfgCodegenInfoForVariantId.size());
    return x_dfgCodegenInfoForVariantId[dfgVariantId];
}

inline bool DfgVariantNeedsRegConfigInSlowPathData(BCKind bcKind, size_t variantOrd)
{
    TestAssert(bcKind < BCKind::X_END_OF_ENUM);
    TestAssert(variantOrd < x_numDfgVariantsForBCKind[static_cast<size_t>(bcKind)]);
    size_t dfgVariantId = x_dfgVariantIdBaseForBCKind[static_cast<size_t>(bcKind)] + variantOrd;
    TestAssert(dfgVariantId < x_dfgVariantNeedsRegConfigInSlowPathData.size());
    return x_dfgVariantNeedsRegConfigInSlowPathData[dfgVariantId];
}

inline uint32_t GetDfgVariantSlowPathDataLength(BCKind bcKind, size_t variantOrd)
{
    TestAssert(bcKind < BCKind::X_END_OF_ENUM);
    TestAssert(variantOrd < x_numDfgVariantsForBCKind[static_cast<size_t>(bcKind)]);
    size_t dfgVariantId = x_dfgVariantIdBaseForBCKind[static_cast<size_t>(bcKind)] + variantOrd;
    TestAssert(dfgVariantId < x_dfgVariantSlowPathDataLen.size());
    return x_dfgVariantSlowPathDataLen[dfgVariantId];
}

}   // namespace dfg
