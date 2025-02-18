#pragma once

#include "common_utils.h"
#include "dfg_variant_traits.h"
#include "dfg_codegen_protocol.h"
#include "dfg_edge_use_kind.h"
#include "dfg_typemask_overapprox_automata.h"
#include "generated/deegen_dfg_jit_builtin_node_codegen_info.h"

namespace dfg {

namespace detail {

struct ConstructDfgOpcodeInfoTable : DfgConstEvalForEachOpcode<ConstructDfgOpcodeInfoTable>
{
    template<BCKind bcKind, size_t dvOrd, size_t cgOrd>
    consteval void action()
    {
        if constexpr(bcKind == BCKind::X_END_OF_ENUM)
        {
            ReleaseAssert(m_curIdx == x_totalNumDfgUserNodeCodegenFuncs);
        }
        else
        {
            ReleaseAssert(m_curIdx < x_totalNumDfgUserNodeCodegenFuncs);
            using Info = DfgCodegenFuncInfoFor<bcKind, dvOrd, cgOrd>;
            m_jitCodeSizes[m_curIdx] = {
                .m_fastPathCodeLen = Info::fastPathLen,
                .m_slowPathCodeLen = Info::slowPathLen,
                .m_dataSecLen = Info::dataSecLen,
                .m_dataSecAlignment = Info::dataSecAlignment
            };
            m_checkValidityFns[m_curIdx] = Info::checkValidFn;
            m_codegenImplFns[m_curIdx] = Info::codegenFn;
            m_codegenImplFnDebugNames[m_curIdx] = Info::debugCodegenFnName;
            m_curIdx++;
        }
    }

    consteval ConstructDfgOpcodeInfoTable()
    {
        m_curIdx = 0;
        RunActionForEachDfgOpcode();
        ReleaseAssert(m_curIdx == x_totalNumDfgUserNodeCodegenFuncs);
    }

    size_t m_curIdx;
    std::array<CodegenFnJitCodeSizeInfo, x_totalNumDfgUserNodeCodegenFuncs> m_jitCodeSizes;
    std::array<DfgVariantValidityCheckerFn, x_totalNumDfgUserNodeCodegenFuncs> m_checkValidityFns;
    std::array<CodegenImplFn, x_totalNumDfgUserNodeCodegenFuncs> m_codegenImplFns;
    std::array<const char*, x_totalNumDfgUserNodeCodegenFuncs> m_codegenImplFnDebugNames;
};

inline constexpr ConstructDfgOpcodeInfoTable x_dfgOpcodeInfoTableData = ConstructDfgOpcodeInfoTable();

}   // namespace detail

inline constexpr std::array<CodegenFnJitCodeSizeInfo, x_totalNumDfgUserNodeCodegenFuncs + x_totalDfgBuiltinNodeStandardCgFns>
    x_dfgOpcodeJitCodeSizeInfoTable = constexpr_std_array_concat(detail::x_dfgOpcodeInfoTableData.m_jitCodeSizes,
                                                                 x_dfgBuiltinNodeStandardCgFnJitCodeSizeArray);

inline constexpr std::array<DfgVariantValidityCheckerFn, x_totalNumDfgUserNodeCodegenFuncs>
    x_dfgOpcodeCheckValidityFnTable = detail::x_dfgOpcodeInfoTableData.m_checkValidityFns;

inline constexpr std::array<CodegenImplFn, x_totalNumDfgUserNodeCodegenFuncs + x_totalDfgBuiltinNodeStandardCgFns>
    x_dfgOpcodeCodegenImplFnTable = constexpr_std_array_concat(detail::x_dfgOpcodeInfoTableData.m_codegenImplFns,
                                                               x_dfgBuiltinNodeStandardCgFnArray);

inline constexpr std::array<const char*, x_totalNumDfgUserNodeCodegenFuncs + x_totalDfgBuiltinNodeStandardCgFns>
    x_dfgOpcodeCodegenFnDebugNameTable = constexpr_std_array_concat(detail::x_dfgOpcodeInfoTableData.m_codegenImplFnDebugNames,
                                                                    x_dfgBuiltinNodeStandardCgFnDebugNameArray);

inline constexpr auto x_dfgEdgeUseKindDebugNameArray = constexpr_std_array_concat(x_dfgEdgeUseKindBuiltinKindNames,
                                                                                  x_dfg_guest_language_usekind_debug_names);
constexpr UseKind UseKind_X_END_OF_ENUM = static_cast<UseKind>(x_dfgEdgeUseKindDebugNameArray.size());

inline constexpr const char* GetEdgeUseKindName(UseKind useKind)
{
    TestAssert(useKind < UseKind_X_END_OF_ENUM);
    return x_dfgEdgeUseKindDebugNameArray[useKind];
}

// For assertion purpose only
//
inline bool WARN_UNUSED CheckUseKindImplementsTypeCheck(TypeMaskOrd checkedMaskOrd, TypeMask precondMask, UseKind useKind)
{
    TypeMask checkMask = GetTypeMaskFromOrd(checkedMaskOrd);
    if (precondMask == x_typeMaskFor<tBottom>)
    {
        return useKind == UseKind_Unreachable;
    }
    else if (checkMask.DisjointFrom(precondMask))
    {
        return useKind == UseKind_AlwaysOsrExit;
    }
    else if (checkMask.SupersetOf(precondMask))
    {
        if (checkMask == x_typeMaskFor<tBoxedValueTop>)
        {
            return useKind == UseKind_Untyped;
        }
        else
        {
            CHECK(UseKind_FirstProvenUseKind <= useKind && useKind < UseKind_FirstUnprovenUseKind);
            return useKind == UseKind_FirstProvenUseKind + static_cast<uint16_t>(checkedMaskOrd) - 1;
        }
    }
    else
    {
        CHECK(UseKind_FirstUnprovenUseKind <= useKind && useKind < UseKind_X_END_OF_ENUM);
        size_t ord = useKind - UseKind_FirstUnprovenUseKind;
        size_t implFn = ord / 2;
        TestAssert(implFn < x_dfg_typecheck_impl_info_list.size());
        TypeMask implCheckMask = x_dfg_typecheck_impl_info_list[implFn].m_checkMask;
        TypeMask implPrecondMask = x_dfg_typecheck_impl_info_list[implFn].m_precondMask;
        implCheckMask = implCheckMask.Cap(implPrecondMask);
        if (ord % 2 == 1)
        {
            implCheckMask = implPrecondMask.Subtract(implCheckMask);
        }
        return implPrecondMask.SupersetOf(precondMask) && implCheckMask.Cap(precondMask) == checkMask.Cap(precondMask);
    }
}

inline UseKind WARN_UNUSED GetEdgeUseKindFromCheckAndPrecondition(TypeMaskOrd checkedMaskOrd, TypeMask preconditionMask)
{
    size_t ord = static_cast<size_t>(checkedMaskOrd);
    TestAssert(ord < x_list_of_type_speculation_masks.size());
    static_assert(x_list_of_type_speculation_masks.size() == x_dfg_typecheck_select_impl_automata_list.size());
    TestAssert(preconditionMask.SubsetOf(x_typeMaskFor<tBoxedValueTop>));
    TypeMaskOverapproxAutomata automata(x_dfg_typecheck_select_impl_automata_list[ord]);
    uint16_t result = automata.RunAutomata(preconditionMask.m_mask);
    TestAssert(result < UseKind_X_END_OF_ENUM);
    TestAssert(CheckUseKindImplementsTypeCheck(checkedMaskOrd, preconditionMask, static_cast<UseKind>(result)));
    return static_cast<UseKind>(result);
}

// Return i where x_list_of_type_speculation_masks[i] is the minimal speculation that is a superset of predictionMask
//
inline TypeMaskOrd WARN_UNUSED GetMinimalSpeculationCoveringPredictionMask(TypeMask predictionMask)
{
    uint16_t res = TypeMaskOverapproxAutomata(x_deegen_dfg_find_minimal_speculation_covering_prediction_mask_automata).RunAutomata(predictionMask.m_mask);
    TestAssert(res < x_list_of_type_speculation_masks.size());
    TestAssert(predictionMask.SubsetOf(x_list_of_type_speculation_masks[res]));
#ifdef TESTBUILD
    for (size_t i = 0; i < x_list_of_type_speculation_masks.size(); i++)
    {
        if (predictionMask.SubsetOf(x_list_of_type_speculation_masks[i]))
        {
            TestAssert(!TypeMask(x_list_of_type_speculation_masks[i]).StrictSubsetOf(x_list_of_type_speculation_masks[res]));
        }
    }
#endif
    return static_cast<TypeMaskOrd>(res);
}

inline UseKind WARN_UNUSED GetCheapestSpecWithinMaskCoveringExistingSpec(TypeMaskOrd curSpec, TypeMask mask)
{
    size_t ord = static_cast<size_t>(curSpec);
    TestAssert(ord < x_list_of_type_speculation_masks.size());
    TestAssert(mask.SubsetOf(x_typeMaskFor<tBoxedValueTop>));
    TestAssert(mask.SupersetOf(x_list_of_type_speculation_masks[ord]));

    uint16_t res = TypeMaskOverapproxAutomata(x_deegen_dfg_find_cheapest_spec_within_mask_automatas[ord]).RunAutomata(x_typeMaskFor<tBoxedValueTop> ^ mask.m_mask);
    TestAssert(res < UseKind_X_END_OF_ENUM);

    UseKind useKind = static_cast<UseKind>(res);
#ifdef TESTBUILD
    if (useKind == UseKind_Unreachable)
    {
        TestAssert(x_list_of_type_speculation_masks[ord] == x_typeMaskFor<tBottom>);
    }
    else if (useKind == UseKind_Untyped)
    {
        TestAssert(mask == x_typeMaskFor<tBoxedValueTop>);
    }
    else
    {
        TestAssert(useKind > UseKind_FirstUnprovenUseKind);
        size_t diff = static_cast<size_t>(useKind) - static_cast<size_t>(UseKind_FirstUnprovenUseKind);
        TestAssert(diff % 2 == 0);
        diff /= 2;
        TestAssert(diff + 2 < x_list_of_type_speculation_masks.size());
        TestAssert(TypeMask(x_list_of_type_speculation_masks[ord]).SubsetOf(x_list_of_type_speculation_masks[diff + 1]));
        TestAssert(mask.SupersetOf(x_list_of_type_speculation_masks[diff + 1]));
    }
#endif

    return useKind;
}

inline const DfgVariantTraits* GetTypeCheckCodegenInfoForUseKind(UseKind useKind)
{
    TestAssert(useKind >= UseKind_FirstUnprovenUseKind);
    TestAssert(useKind < UseKind_X_END_OF_ENUM);
    static_assert(x_dfg_typecheck_impl_codegen_handler.size() == static_cast<size_t>(UseKind_X_END_OF_ENUM) - static_cast<size_t>(UseKind_FirstUnprovenUseKind));
    size_t idx = static_cast<size_t>(useKind) - static_cast<size_t>(UseKind_FirstUnprovenUseKind);
    return x_dfg_typecheck_impl_codegen_handler[idx];
}

// 'useKind' should require a check. Return whether the operand should sit in GPR or FPR.
//
inline bool ShouldTypeCheckOperandUseGPR(UseKind useKind)
{
    TestAssert(UseKindRequiresNonTrivialRuntimeCheck(useKind));
    const DfgVariantTraits* info = GetTypeCheckCodegenInfoForUseKind(useKind);
    TestAssert(info->IsRegAllocEnabled());
    TestAssert(info->NumOperandsForRA() == 1);
    DfgNodeOperandRegBankPref pref = info->Operand(0);
    TestAssert(pref.Valid());
    TestAssert(pref.GprAllowed() || pref.FprAllowed());
    // Each type check should always specify either GPR or FPR, not both
    //
    TestAssert(!pref.HasChoices());
    bool mustUseGpr = pref.GprAllowed();
    return mustUseGpr;
}

// May return nullptr if the builtin node kind requires complex handling
//
inline const DfgVariantTraits* GetCodegenInfoForBuiltinNodeKind(NodeKind nodeKind)
{
    TestAssert(nodeKind < NodeKind_FirstAvailableGuestLanguageNodeKind);
    static_assert(x_dfg_builtin_node_standard_codegen_handler.size() == static_cast<size_t>(NodeKind_FirstAvailableGuestLanguageNodeKind));
    return x_dfg_builtin_node_standard_codegen_handler[static_cast<size_t>(nodeKind)];
}

inline DfgCodegenFuncOrd GetCodegenFnForMaterializingConstant(NodeKind nodeKind, ConstantLikeNodeMaterializeLocation destLoc)
{
    static_assert(x_dfg_codegen_info_for_constant_like_nodes.size() == x_numTotalDfgConstantLikeNodeKinds);
    static_assert(x_dfg_codegen_info_for_constant_like_nodes[0].size() == static_cast<size_t>(ConstantLikeNodeMaterializeLocation::X_END_OF_ENUM));
    TestAssert(IsDfgNodeKindConstantLikeNodeKind(nodeKind) && destLoc < ConstantLikeNodeMaterializeLocation::X_END_OF_ENUM);

    size_t nodeKindIdx = static_cast<size_t>(nodeKind);
    size_t destLocIdx = static_cast<size_t>(destLoc);
    TestAssert(nodeKindIdx < x_dfg_codegen_info_for_constant_like_nodes.size());
    TestAssert(destLocIdx < x_dfg_codegen_info_for_constant_like_nodes[nodeKindIdx].size());
    DfgCodegenFuncOrd res = x_dfg_codegen_info_for_constant_like_nodes[nodeKindIdx][destLocIdx];
    TestAssert(static_cast<size_t>(res) < x_dfgOpcodeJitCodeSizeInfoTable.size());
    return res;
}

inline const DfgVariantTraits* GetCodegenInfoForCustomBuiltinNodeLogicFragment(DfgBuiltinNodeCustomCgFn kind)
{
    static_assert(x_dfg_builtin_node_custom_codegen_handler.size() == static_cast<size_t>(DfgBuiltinNodeCustomCgFn::X_END_OF_ENUM));
    TestAssert(kind < DfgBuiltinNodeCustomCgFn::X_END_OF_ENUM);
    return x_dfg_builtin_node_custom_codegen_handler[static_cast<size_t>(kind)];
}

}   // namespace dfg
