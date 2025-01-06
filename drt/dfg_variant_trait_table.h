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
        if (checkMask == x_typeMaskFor<tTop>)
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
    TestAssert(preconditionMask.SubsetOf(x_typeMaskFor<tTop>));
    TypeMaskOverapproxAutomata automata(x_dfg_typecheck_select_impl_automata_list[ord]);
    uint16_t result = automata.RunAutomata(preconditionMask.m_mask);
    TestAssert(result < UseKind_X_END_OF_ENUM);
    TestAssert(CheckUseKindImplementsTypeCheck(checkedMaskOrd, preconditionMask, static_cast<UseKind>(result)));
    return static_cast<UseKind>(result);
}

}   // namespace dfg
