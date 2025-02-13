#pragma once

#include "common_utils.h"
#include "dfg_codegen_operation_base.h"
#include "dfg_reg_move_inst_generator.h"
#include "dfg_reg_alloc_state.h"
#include "dfg_variant_trait_table.h"

namespace dfg {

struct __attribute__((__packed__)) CodegenRegMove final : CodegenOpBase
{
    CodegenRegMove(X64Reg srcReg, X64Reg dstReg)
        : CodegenOpBase(this), m_srcReg(srcReg), m_dstReg(dstReg)
    { }

    void UpdateJITCodeSize(JITCodeSizeInfo& info /*inout*/)
    {
        info.m_fastPathLength += GetRegisterMoveInstructionLength(m_srcReg, m_dstReg);
    }

    X64Reg m_srcReg;
    X64Reg m_dstReg;
};

struct __attribute__((__packed__)) CodegenRegLoad final : CodegenOpBase
{
    CodegenRegLoad(uint32_t stackOffsetBytes, X64Reg reg)
        : CodegenOpBase(this), m_reg(reg), m_stackOffsetBytes(stackOffsetBytes)
    {
        TestAssert(m_stackOffsetBytes <= 0x7fffffff);
    }

    void UpdateJITCodeSize(JITCodeSizeInfo& info /*inout*/)
    {
        info.m_fastPathLength += GetRegisterSpillOrLoadInstructionLength(m_reg, m_stackOffsetBytes);
    }

    X64Reg m_reg;
    uint32_t m_stackOffsetBytes;
};

struct __attribute__((__packed__)) CodegenRegSpill final : CodegenOpBase
{
    CodegenRegSpill(X64Reg srcReg, X64Reg baseReg, uint32_t offsetBytes)
        : CodegenOpBase(this), m_srcReg(srcReg), m_baseReg(baseReg), m_offsetBytes(offsetBytes)
    {
        TestAssert(m_baseReg.IsGPR());
        TestAssert(m_baseReg != X64Reg::RSP && m_baseReg != X64Reg::R12);
        TestAssert(m_offsetBytes <= 0x7fffffff);
    }

    void UpdateJITCodeSize(JITCodeSizeInfo& info /*inout*/)
    {
        info.m_fastPathLength += GetRegisterStoreToMemBaseOffsetInstLength(m_srcReg, m_baseReg, m_offsetBytes);
    }

    X64Reg m_srcReg;
    X64Reg m_baseReg;
    uint32_t m_offsetBytes;
};

// Update the JIT code size after appending a standard-interface stencil into the JIT code with ordinal 'funcOrd'
//
inline void UpdateJITCodeSizeForStandardInterfaceStencil(JITCodeSizeInfo& info /*inout*/, DfgCodegenFuncOrd funcOrd)
{
    size_t idx = static_cast<size_t>(funcOrd);
    TestAssert(idx < x_dfgOpcodeJitCodeSizeInfoTable.size());
    info.Update(x_dfgOpcodeJitCodeSizeInfoTable[idx]);
}

// Update the JIT code size after appending a custom-interface stencil into the JIT code with ordinal 'funcOrd'
//
inline void UpdateJITCodeSizeForCustomInterfaceStencil(JITCodeSizeInfo& info /*inout*/, DfgCodegenFuncOrd funcOrd)
{
    size_t idx = static_cast<size_t>(funcOrd);
    TestAssert(idx < x_dfgBuiltinNodeCustomCgFnJitCodeSizeArray.size());
    info.Update(x_dfgBuiltinNodeCustomCgFnJitCodeSizeArray[idx]);
}

struct __attribute__((__packed__)) CodegenOpRegAllocEnabled final : CodegenOpBase
{
    CodegenOpRegAllocEnabled() : CodegenOpBase(this) { }

    static constexpr size_t ComputeAllocationSize(size_t numInputOperands)
    {
        return offsetof_member_v<&CodegenOpRegAllocEnabled::m_operandConfig> + NodeOperandConfigData::GetAllocationSize(numInputOperands);
    }

    void* WARN_UNUSED GetStructEnd() { return m_operandConfig.GetStructEnd(); }

    void UpdateJITCodeSize(JITCodeSizeInfo& info /*inout*/)
    {
        UpdateJITCodeSizeForStandardInterfaceStencil(info /*inout*/, m_operandConfig.GetCodegenFuncOrd());
    }

    uint8_t* m_nsd;
    RegAllocRegConfig m_regConfig;
    // Must be last member since it has a trailing array
    //
    NodeOperandConfigData m_operandConfig;
};

struct __attribute__((__packed__)) CodegenOpRegAllocDisabled final : CodegenOpBase
{
    CodegenOpRegAllocDisabled() : CodegenOpBase(this) { }

    static constexpr size_t ComputeAllocationSize(size_t numInputOperands)
    {
        return offsetof_member_v<&CodegenOpRegAllocDisabled::m_operandConfig> + NodeOperandConfigData::GetAllocationSize(numInputOperands);
    }

    void* WARN_UNUSED GetStructEnd() { return m_operandConfig.GetStructEnd(); }

    void UpdateJITCodeSize(JITCodeSizeInfo& info /*inout*/)
    {
        UpdateJITCodeSizeForStandardInterfaceStencil(info /*inout*/, m_operandConfig.GetCodegenFuncOrd());
    }

    uint8_t* m_nsd;
    // Must be last member since it has a trailing array
    //
    NodeOperandConfigData m_operandConfig;
};

struct __attribute__((__packed__)) CodegenCustomOpRegAllocEnabled final : CodegenOpBase
{
    CodegenCustomOpRegAllocEnabled() : CodegenOpBase(this) { }

    static constexpr size_t ComputeAllocationSize(size_t numInputOperands)
    {
        return offsetof_member_v<&CodegenCustomOpRegAllocEnabled::m_operandConfig> + NodeOperandConfigData::GetAllocationSize(numInputOperands);
    }

    void* WARN_UNUSED GetStructEnd() { return m_operandConfig.GetStructEnd(); }

    void UpdateJITCodeSize(JITCodeSizeInfo& info /*inout*/)
    {
        UpdateJITCodeSizeForCustomInterfaceStencil(info /*inout*/, m_operandConfig.GetCodegenFuncOrd());
    }

    uint64_t* m_literalData;
    RegAllocRegConfig m_regConfig;
    // Must be last member since it has a trailing array
    //
    NodeOperandConfigData m_operandConfig;
};

struct __attribute__((__packed__)) CodegenCustomOpRegAllocDisabled final : CodegenOpBase
{
    CodegenCustomOpRegAllocDisabled() : CodegenOpBase(this) { }

    static constexpr size_t ComputeAllocationSize(size_t numInputOperands)
    {
        return offsetof_member_v<&CodegenCustomOpRegAllocDisabled::m_operandConfig> + NodeOperandConfigData::GetAllocationSize(numInputOperands);
    }

    void* WARN_UNUSED GetStructEnd() { return m_operandConfig.GetStructEnd(); }

    void UpdateJITCodeSize(JITCodeSizeInfo& info /*inout*/)
    {
        UpdateJITCodeSizeForCustomInterfaceStencil(info /*inout*/, m_operandConfig.GetCodegenFuncOrd());
    }

    uint64_t* m_literalData;
    // Must be last member since it has a trailing array
    //
    NodeOperandConfigData m_operandConfig;
};

}   // namespace dfg
