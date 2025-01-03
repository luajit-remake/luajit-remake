#pragma once

#include "common_utils.h"
#include "dfg_codegen_operation_base.h"
#include "dfg_reg_move_inst_generator.h"

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
    { }

    void UpdateJITCodeSize(JITCodeSizeInfo& info /*inout*/)
    {
        info.m_fastPathLength += GetRegisterSpillOrLoadInstructionLength(m_reg, m_stackOffsetBytes);
    }

    X64Reg m_reg;
    uint32_t m_stackOffsetBytes;
};

struct __attribute__((__packed__)) CodegenRegSpill final : CodegenOpBase
{
    CodegenRegSpill(X64Reg reg, uint32_t stackOffsetBytes)
        : CodegenOpBase(this), m_reg(reg), m_stackOffsetBytes(stackOffsetBytes)
    { }

    void UpdateJITCodeSize(JITCodeSizeInfo& info /*inout*/)
    {
        info.m_fastPathLength += GetRegisterSpillOrLoadInstructionLength(m_reg, m_stackOffsetBytes);
    }

    X64Reg m_reg;
    uint32_t m_stackOffsetBytes;
};

}   // namespace dfg
