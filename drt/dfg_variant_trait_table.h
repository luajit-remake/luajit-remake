#pragma once

#include "common_utils.h"
#include "dfg_variant_traits.h"
#include "dfg_codegen_protocol.h"

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

inline constexpr std::array<CodegenFnJitCodeSizeInfo, x_totalNumDfgUserNodeCodegenFuncs>
    x_dfgOpcodeJitCodeSizeInfoTable = detail::x_dfgOpcodeInfoTableData.m_jitCodeSizes;

inline constexpr std::array<DfgVariantValidityCheckerFn, x_totalNumDfgUserNodeCodegenFuncs>
    x_dfgOpcodeCheckValidityFnTable = detail::x_dfgOpcodeInfoTableData.m_checkValidityFns;

inline constexpr std::array<CodegenImplFn, x_totalNumDfgUserNodeCodegenFuncs>
    x_dfgOpcodeCodegenImplFnTable = detail::x_dfgOpcodeInfoTableData.m_codegenImplFns;

}   // namespace dfg
