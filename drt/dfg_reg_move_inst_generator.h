#pragma once

#include "common_utils.h"
#include "heap_ptr_utils.h"
#include "x64_register_info.h"

namespace dfg {

// The stack base register, must agree with Deegen interface (static_asserted by Deegen)
//
constexpr X64Reg x_dfg_stack_base_register = X64Reg::RBX;

// R12 doesn't support ModRM r/m+disp addressing, which is a very bad choice for stack register
// The spill/load instruction generator below also does not handle them correctly
//
static_assert(x_dfg_stack_base_register != X64Reg::RSP && x_dfg_stack_base_register != X64Reg::R12,
              "RSP or R12 must not be used as stackBase register!");

// DEVNOTE:
//     All functions in this file assumes that the operand registers are those that participates in DFG reg alloc.
//     Specifically, we assume that the 'reg' used in [reg+offset] will not be RSP or R12, and FPR registers will be xmm0-7.
//
inline size_t WARN_UNUSED GetRegisterMoveInstructionLength(X64Reg srcReg, X64Reg dstReg)
{
    // movq %gpr, %gpr
    //
    // No matter which GPR, the instruction length is always 3 bytes (since REX byte always needed for 64-bit move)
    //
    constexpr size_t x_x64_gpr_gpr_mov_inst_len = 3;

    // movq %gpr, %fpr
    // movq %fpr, %gpr
    //
    // No matter GPR->FPR or FPR->GPR, the instruction length is always 5 bytes
    //
    constexpr size_t x_x64_gpr_fpr_mov_inst_len = 5;

    // movaps %fpr, %fpr
    //
    // Only move between xmm0-xmm7 is 3 bytes. If any operand is xmm8 or higher, it will be 4 bytes.
    // But we currently do not use xmm8+ for DFG regalloc, so we will never need to manually generate such moves.
    //
    constexpr size_t x_x64_fpr_fpr_mov_inst_len = 3;

    TestAssert(!(srcReg.IsFPR() && srcReg.MachineOrd() >= 8));
    TestAssert(!(dstReg.IsFPR() && dstReg.MachineOrd() >= 8));

    bool isSrcGpr = srcReg.IsGPR();
    bool isDstGpr = dstReg.IsGPR();

    if (isSrcGpr && isDstGpr) { return x_x64_gpr_gpr_mov_inst_len; }
    if (!isSrcGpr && !isDstGpr) { return x_x64_fpr_fpr_mov_inst_len; }
    return x_x64_gpr_fpr_mov_inst_len;
}

inline size_t ALWAYS_INLINE EmitGprToGprMoveInst(uint8_t*& addr /*inout*/, X64Reg srcReg, X64Reg dstReg)
{
    TestAssert(srcReg.IsGPR() && dstReg.IsGPR());

    uint32_t srcMO = srcReg.MachineOrd();
    uint32_t dstMO = dstReg.MachineOrd();
    TestAssert(srcMO < 16 && dstMO < 16);

    UnalignedStore<uint16_t>(addr, static_cast<uint16_t>(0x8948 | ((srcMO & 8) ? 4 : 0) | ((dstMO & 8) ? 1 : 0)));
    addr[2] = static_cast<uint8_t>(0xc0 | ((srcMO & 7) << 3) | ((dstMO & 7) << 0));

    size_t instLen = 3;
    addr += instLen;
    return instLen;
}

inline size_t ALWAYS_INLINE EmitGprToFprMoveInst(uint8_t*& addr /*inout*/, X64Reg srcReg, X64Reg dstReg)
{
    TestAssert(srcReg.IsGPR() && dstReg.IsFPR());

    uint32_t srcMO = srcReg.MachineOrd();
    uint32_t dstMO = dstReg.MachineOrd();
    TestAssert(srcMO < 16 && dstMO < 8);

    UnalignedStore<uint32_t>(addr, static_cast<uint32_t>(0x6e0f4866 | ((srcMO & 8) ? 0x100 : 0)));
    addr[4] = static_cast<uint8_t>(0xc0 | ((srcMO & 7) << 0) | (dstMO << 3));

    size_t instLen = 5;
    addr += instLen;
    return instLen;
}

inline size_t ALWAYS_INLINE EmitFprToGprMoveInst(uint8_t*& addr /*inout*/, X64Reg srcReg, X64Reg dstReg)
{
    TestAssert(srcReg.IsFPR() && dstReg.IsGPR());

    uint32_t srcMO = srcReg.MachineOrd();
    uint32_t dstMO = dstReg.MachineOrd();
    TestAssert(srcMO < 8 && dstMO < 16);

    UnalignedStore<uint32_t>(addr, static_cast<uint32_t>(0x7e0f4866 | ((dstMO & 8) ? 0x100 : 0)));
    addr[4] = static_cast<uint8_t>(0xc0 | (srcMO << 3) | ((dstMO & 7) << 0));

    size_t instLen = 5;
    addr += instLen;
    return instLen;
}

inline size_t ALWAYS_INLINE EmitFprToFprMoveInst(uint8_t*& addr /*inout*/, X64Reg srcReg, X64Reg dstReg)
{
    TestAssert(srcReg.IsFPR() && dstReg.IsFPR());

    uint32_t srcMO = srcReg.MachineOrd();
    uint32_t dstMO = dstReg.MachineOrd();
    TestAssert(srcMO < 8 && dstMO < 8);

    UnalignedStore<uint16_t>(addr, 0x280f);
    addr[2] = static_cast<uint8_t>(0xc0 | (srcMO << 0) | (dstMO << 3));

    size_t instLen = 3;
    addr += instLen;
    return instLen;
}

inline void EmitRegisterRegisterMoveInst(uint8_t*& addr /*inout*/, X64Reg srcReg, X64Reg dstReg)
{
    size_t instLen;
    if (srcReg.IsGPR())
    {
        if (dstReg.IsGPR())
        {
            instLen = EmitGprToGprMoveInst(addr, srcReg, dstReg);
        }
        else
        {
            instLen = EmitGprToFprMoveInst(addr, srcReg, dstReg);
        }
    }
    else
    {
        if (dstReg.IsGPR())
        {
            instLen = EmitFprToGprMoveInst(addr, srcReg, dstReg);
        }
        else
        {
            instLen = EmitFprToFprMoveInst(addr, srcReg, dstReg);
        }
    }
    TestAssert(instLen == GetRegisterMoveInstructionLength(srcReg, dstReg));
    std::ignore = instLen;
}

inline size_t WARN_UNUSED GetRegisterSpillOrLoadInstructionLength(X64Reg reg, uint32_t stackOffsetInBytes)
{
    // movq %gpr, imm8(%stackBase)
    // movq imm8(%stackBase), %gpr
    //
    // Always 4 bytes (imm8 version) or 7 bytes (imm32 version) no matter which GPRs
    //
    // This is correct as long as %stackBase is not RSP or R12
    //
    constexpr size_t x_x64_spill_or_load_gpr_inst_len_short = 4;

    // movsd %fpr, imm8(%stackBase)
    // movsd imm8(%stackBase), %fpr
    //
    // If both %stackBase and %fpr are reg 0-7, then 5 bytes, otherwise 6 bytes
    // But since we only use FPR 0-7 for reg alloc, only need to check stackBase
    //
    // This is correct as long as %stackBase is not RSP or R12
    //
    constexpr size_t x_x64_spill_or_load_fpr_inst_len_short = 5 + (x_dfg_stack_base_register.MachineOrd() < 8 ? 0 : 1);

    size_t extraLength = (stackOffsetInBytes < 128) ? 0 : 3;
    if (reg.IsGPR())
    {
        return x_x64_spill_or_load_gpr_inst_len_short + extraLength;
    }
    else
    {
        return x_x64_spill_or_load_fpr_inst_len_short + extraLength;
    }
}

inline size_t ALWAYS_INLINE EmitGprStoreToMemBaseOffsetImm8Inst(uint8_t*& addr /*inout*/, X64Reg reg, X64Reg baseReg, uint32_t offsetBytes)
{
    TestAssert(baseReg != X64Reg::RSP && baseReg != X64Reg::R12);
    TestAssert(reg.IsGPR() && offsetBytes < 128);

    uint32_t stkMO = baseReg.MachineOrd();
    uint32_t regMO = reg.MachineOrd();
    TestAssert(stkMO < 16 && regMO < 16);

    UnalignedStore<uint32_t>(addr, static_cast<uint32_t>(
                                       0x00408948 |
                                       ((stkMO & 8) ? 1U : 0) |
                                       ((stkMO & 7) << 16) |
                                       ((regMO & 8) ? 4U : 0) |
                                       ((regMO & 7) << 19) |
                                       (offsetBytes << 24)));

    size_t instLen = 4;
    addr += instLen;
    return instLen;
}

inline void ALWAYS_INLINE EmitGprSpillToStackImm8OffsetInst(uint8_t*& addr /*inout*/, X64Reg reg, uint32_t offsetBytes)
{
    size_t instLen = EmitGprStoreToMemBaseOffsetImm8Inst(addr /*inout*/, reg, x_dfg_stack_base_register, offsetBytes);
    TestAssert(instLen == GetRegisterSpillOrLoadInstructionLength(reg, offsetBytes));
    std::ignore = instLen;
}

inline size_t ALWAYS_INLINE EmitGprStoreToMemBaseOffsetImm32Inst(uint8_t*& addr /*inout*/, X64Reg reg, X64Reg baseReg, uint32_t offsetBytes)
{
    TestAssert(baseReg != X64Reg::RSP && baseReg != X64Reg::R12);
    TestAssert(reg.IsGPR() && offsetBytes <= 0x7fffffff);

    uint32_t stkMO = baseReg.MachineOrd();
    uint32_t regMO = reg.MachineOrd();
    TestAssert(stkMO < 16 && regMO < 16);

    UnalignedStore<uint32_t>(addr, static_cast<uint32_t>(
                                       0x00808948 |
                                       ((stkMO & 8) ? 1U : 0) |
                                       ((stkMO & 7) << 16) |
                                       ((regMO & 8) ? 4U : 0) |
                                       ((regMO & 7) << 19)));

    UnalignedStore<uint32_t>(addr + 3, offsetBytes);

    size_t instLen = 7;
    addr += instLen;
    return instLen;
}

inline void ALWAYS_INLINE EmitGprSpillToStackImm32OffsetInst(uint8_t*& addr /*inout*/, X64Reg reg, uint32_t offsetBytes)
{
    size_t instLen = EmitGprStoreToMemBaseOffsetImm32Inst(addr /*inout*/, reg, x_dfg_stack_base_register, offsetBytes);
    TestAssert(instLen == GetRegisterSpillOrLoadInstructionLength(reg, offsetBytes));
    std::ignore = instLen;
}

inline size_t ALWAYS_INLINE EmitFprStoreToMemBaseOffsetImm8Inst(uint8_t*& addr /*inout*/, X64Reg reg, X64Reg baseReg, uint32_t offsetBytes)
{
    TestAssert(baseReg != X64Reg::RSP && baseReg != X64Reg::R12);
    TestAssert(reg.IsFPR() && offsetBytes < 128);

    uint32_t stkMO = baseReg.MachineOrd();
    uint32_t regMO = reg.MachineOrd();
    TestAssert(stkMO < 16 && regMO < 8);

    if (stkMO >= 8)
    {
        UnalignedStore<uint32_t>(addr, 0x110f41f2);
        UnalignedStore<uint16_t>(addr + 4, static_cast<uint16_t>(
                                               0x0040 |
                                               ((stkMO & 7) << 0) |
                                               (regMO << 3) |
                                               (offsetBytes << 8)));

        size_t instLen = 6;
        addr += instLen;
        return instLen;
    }
    else
    {
        UnalignedStore<uint32_t>(addr, static_cast<uint32_t>(
                                           0x40110ff2 |
                                           ((stkMO & 7) << 24) |
                                           (regMO << 27)));
        addr[4] = static_cast<uint8_t>(offsetBytes);

        size_t instLen = 5;
        addr += instLen;
        return instLen;
    }
}

inline void ALWAYS_INLINE EmitFprSpillToStackImm8OffsetInst(uint8_t*& addr /*inout*/, X64Reg reg, uint32_t offsetBytes)
{
    size_t instLen = EmitFprStoreToMemBaseOffsetImm8Inst(addr /*inout*/, reg, x_dfg_stack_base_register, offsetBytes);
    TestAssert(instLen == GetRegisterSpillOrLoadInstructionLength(reg, offsetBytes));
    std::ignore = instLen;
}

inline size_t ALWAYS_INLINE EmitFprStoreToMemBaseOffsetImm32Inst(uint8_t*& addr /*inout*/, X64Reg reg, X64Reg baseReg, uint32_t offsetBytes)
{
    TestAssert(baseReg != X64Reg::RSP && baseReg != X64Reg::R12);
    TestAssert(reg.IsFPR() && offsetBytes <= 0x7fffffff);

    uint32_t stkMO = baseReg.MachineOrd();
    uint32_t regMO = reg.MachineOrd();
    TestAssert(stkMO < 16 && regMO < 8);

    if (stkMO >= 8)
    {
        UnalignedStore<uint32_t>(addr, 0x110f41f2);
        addr[4] = static_cast<uint8_t>(0x80 |
                                       ((stkMO & 7) << 0) |
                                       (regMO << 3));
        UnalignedStore<uint32_t>(addr + 5, offsetBytes);

        size_t instLen = 9;
        addr += instLen;
        return instLen;
    }
    else
    {
        UnalignedStore<uint32_t>(addr, static_cast<uint32_t>(
                                           0x80110ff2 |
                                           ((stkMO & 7) << 24) |
                                           (regMO << 27)));
        UnalignedStore<uint32_t>(addr + 4, offsetBytes);

        size_t instLen = 8;
        addr += instLen;
        return instLen;
    }
}

inline void ALWAYS_INLINE EmitFprSpillToStackImm32OffsetInst(uint8_t*& addr /*inout*/, X64Reg reg, uint32_t offsetBytes)
{
    size_t instLen = EmitFprStoreToMemBaseOffsetImm32Inst(addr /*inout*/, reg, x_dfg_stack_base_register, offsetBytes);
    TestAssert(instLen == GetRegisterSpillOrLoadInstructionLength(reg, offsetBytes));
    std::ignore = instLen;
}

inline void EmitRegisterSpillToStackInst(uint8_t*& addr /*inout*/, X64Reg reg, uint32_t offsetBytes)
{
    if (reg.IsGPR())
    {
        if (offsetBytes < 128)
        {
            EmitGprSpillToStackImm8OffsetInst(addr, reg, offsetBytes);
        }
        else
        {
            EmitGprSpillToStackImm32OffsetInst(addr, reg, offsetBytes);
        }
    }
    else
    {
        if (offsetBytes < 128)
        {
            EmitFprSpillToStackImm8OffsetInst(addr, reg, offsetBytes);
        }
        else
        {
            EmitFprSpillToStackImm32OffsetInst(addr, reg, offsetBytes);
        }
    }
}

// The supposed use case of this function is to emit a store to [reg+offset] where 'reg' is a DFG reg alloc register.
// So this function assumes that 'baseReg' is not RSP or R12, and FPR will only be xmm0-7.
//
inline size_t WARN_UNUSED GetRegisterStoreToMemBaseOffsetInstLength(X64Reg reg, X64Reg baseReg, uint32_t offsetBytes)
{
    TestAssert(baseReg != X64Reg::RSP && baseReg != X64Reg::R12);
    TestAssert(baseReg.IsGPR());
    size_t extraLength = (offsetBytes < 128) ? 0 : 3;
    if (reg.IsGPR())
    {
        return 4 + extraLength;
    }
    else
    {
        return 5 + extraLength + (baseReg.MachineOrd() < 8 ? 0 : 1);
    }
}

// See above, this function assumes that 'baseReg' is not RSP or R12, and FPR will only be xmm0-7.
//
inline void EmitRegisterStoreToMemBaseOffsetInstruction(uint8_t*& addr /*inout*/, X64Reg reg, X64Reg baseReg, uint32_t offsetBytes)
{
    TestAssert(baseReg != X64Reg::RSP && baseReg != X64Reg::R12);
    TestAssert(baseReg.IsGPR());
    size_t instLen;
    if (reg.IsGPR())
    {
        if (offsetBytes < 128)
        {
            instLen = EmitGprStoreToMemBaseOffsetImm8Inst(addr /*inout*/, reg, baseReg, offsetBytes);
        }
        else
        {
            instLen = EmitGprStoreToMemBaseOffsetImm32Inst(addr /*inout*/, reg, baseReg, offsetBytes);
        }
    }
    else
    {
        if (offsetBytes < 128)
        {
            instLen = EmitFprStoreToMemBaseOffsetImm8Inst(addr /*inout*/, reg, baseReg, offsetBytes);
        }
        else
        {
            instLen = EmitFprStoreToMemBaseOffsetImm32Inst(addr /*inout*/, reg, baseReg, offsetBytes);
        }
    }
    TestAssert(instLen == GetRegisterStoreToMemBaseOffsetInstLength(reg, baseReg, offsetBytes));
    std::ignore = instLen;
}

inline size_t ALWAYS_INLINE EmitGprLoadFromMemBaseOffsetImm8Inst(uint8_t*& addr /*inout*/, X64Reg reg, X64Reg baseReg, uint32_t offsetBytes)
{
    TestAssert(baseReg != X64Reg::RSP && baseReg != X64Reg::R12);
    TestAssert(reg.IsGPR() && offsetBytes < 128);

    uint32_t stkMO = baseReg.MachineOrd();
    uint32_t regMO = reg.MachineOrd();
    TestAssert(stkMO < 16 && regMO < 16);

    UnalignedStore<uint32_t>(addr, static_cast<uint32_t>(
                                       0x00408b48 |
                                       ((stkMO & 8) ? 1U : 0) |
                                       ((stkMO & 7) << 16) |
                                       ((regMO & 8) ? 4U : 0) |
                                       ((regMO & 7) << 19) |
                                       (offsetBytes << 24)));

    size_t instLen = 4;
    addr += instLen;
    return instLen;
}

inline void ALWAYS_INLINE EmitGprLoadFromStackImm8OffsetInst(uint8_t*& addr /*inout*/, X64Reg reg, uint32_t offsetBytes)
{
    size_t instLen = EmitGprLoadFromMemBaseOffsetImm8Inst(addr /*inout*/, reg, x_dfg_stack_base_register, offsetBytes);
    TestAssert(instLen == GetRegisterSpillOrLoadInstructionLength(reg, offsetBytes));
    std::ignore = instLen;
}

inline size_t ALWAYS_INLINE EmitGprLoadFromMemBaseOffsetImm32Inst(uint8_t*& addr /*inout*/, X64Reg reg, X64Reg baseReg, uint32_t offsetBytes)
{
    TestAssert(baseReg != X64Reg::RSP && baseReg != X64Reg::R12);
    TestAssert(reg.IsGPR() && offsetBytes <= 0x7fffffff);

    uint32_t stkMO = baseReg.MachineOrd();
    uint32_t regMO = reg.MachineOrd();
    TestAssert(stkMO < 16 && regMO < 16);

    UnalignedStore<uint32_t>(addr, static_cast<uint32_t>(
                                       0x00808b48 |
                                       ((stkMO & 8) ? 1U : 0) |
                                       ((stkMO & 7) << 16) |
                                       ((regMO & 8) ? 4U : 0) |
                                       ((regMO & 7) << 19)));

    UnalignedStore<uint32_t>(addr + 3, offsetBytes);

    size_t instLen = 7;
    addr += instLen;
    return instLen;
}

inline void ALWAYS_INLINE EmitGprLoadFromStackImm32OffsetInst(uint8_t*& addr /*inout*/, X64Reg reg, uint32_t offsetBytes)
{
    size_t instLen = EmitGprLoadFromMemBaseOffsetImm32Inst(addr /*inout*/, reg, x_dfg_stack_base_register, offsetBytes);
    TestAssert(instLen == GetRegisterSpillOrLoadInstructionLength(reg, offsetBytes));
    std::ignore = instLen;
}

inline size_t ALWAYS_INLINE EmitFprLoadFromMemBaseOffsetImm8Inst(uint8_t*& addr /*inout*/, X64Reg reg, X64Reg baseReg, uint32_t offsetBytes)
{
    TestAssert(baseReg != X64Reg::RSP && baseReg != X64Reg::R12);
    TestAssert(reg.IsFPR() && offsetBytes < 128);

    uint32_t stkMO = baseReg.MachineOrd();
    uint32_t regMO = reg.MachineOrd();
    TestAssert(stkMO < 16 && regMO < 8);

    if (stkMO >= 8)
    {
        UnalignedStore<uint32_t>(addr, 0x100f41f2);
        UnalignedStore<uint16_t>(addr + 4, static_cast<uint16_t>(
                                               0x0040 |
                                               ((stkMO & 7) << 0) |
                                               (regMO << 3) |
                                               (offsetBytes << 8)));

        size_t instLen = 6;
        addr += instLen;
        return instLen;
    }
    else
    {
        UnalignedStore<uint32_t>(addr, static_cast<uint32_t>(
                                           0x40100ff2 |
                                           ((stkMO & 7) << 24) |
                                           (regMO << 27)));
        addr[4] = static_cast<uint8_t>(offsetBytes);

        size_t instLen = 5;
        addr += instLen;
        return instLen;
    }
}

inline void ALWAYS_INLINE EmitFprLoadFromStackImm8OffsetInst(uint8_t*& addr /*inout*/, X64Reg reg, uint32_t offsetBytes)
{
    size_t instLen = EmitFprLoadFromMemBaseOffsetImm8Inst(addr /*inout*/, reg, x_dfg_stack_base_register, offsetBytes);
    TestAssert(instLen == GetRegisterSpillOrLoadInstructionLength(reg, offsetBytes));
    std::ignore = instLen;
}

inline size_t ALWAYS_INLINE EmitFprLoadFromMemBaseOffsetImm32Inst(uint8_t*& addr /*inout*/, X64Reg reg, X64Reg baseReg, uint32_t offsetBytes)
{
    TestAssert(baseReg != X64Reg::RSP && baseReg != X64Reg::R12);
    TestAssert(reg.IsFPR() && offsetBytes <= 0x7fffffff);

    uint32_t stkMO = baseReg.MachineOrd();
    uint32_t regMO = reg.MachineOrd();
    TestAssert(stkMO < 16 && regMO < 8);

    if (stkMO >= 8)
    {
        UnalignedStore<uint32_t>(addr, 0x100f41f2);
        addr[4] = static_cast<uint8_t>(0x80 |
                                       ((stkMO & 7) << 0) |
                                       (regMO << 3));
        UnalignedStore<uint32_t>(addr + 5, offsetBytes);

        size_t instLen = 9;
        addr += instLen;
        return instLen;
    }
    else
    {
        UnalignedStore<uint32_t>(addr, static_cast<uint32_t>(
                                           0x80100ff2 |
                                           ((stkMO & 7) << 24) |
                                           (regMO << 27)));
        UnalignedStore<uint32_t>(addr + 4, offsetBytes);

        size_t instLen = 8;
        addr += instLen;
        return instLen;
    }
}

inline void ALWAYS_INLINE EmitFprLoadFromStackImm32OffsetInst(uint8_t*& addr /*inout*/, X64Reg reg, uint32_t offsetBytes)
{
    size_t instLen = EmitFprLoadFromMemBaseOffsetImm32Inst(addr /*inout*/, reg, x_dfg_stack_base_register, offsetBytes);
    TestAssert(instLen == GetRegisterSpillOrLoadInstructionLength(reg, offsetBytes));
    std::ignore = instLen;
}

inline void EmitRegisterLoadFromStackInst(uint8_t*& addr /*inout*/, X64Reg reg, uint32_t offsetBytes)
{
    if (reg.IsGPR())
    {
        if (offsetBytes < 128)
        {
            EmitGprLoadFromStackImm8OffsetInst(addr, reg, offsetBytes);
        }
        else
        {
            EmitGprLoadFromStackImm32OffsetInst(addr, reg, offsetBytes);
        }
    }
    else
    {
        if (offsetBytes < 128)
        {
            EmitFprLoadFromStackImm8OffsetInst(addr, reg, offsetBytes);
        }
        else
        {
            EmitFprLoadFromStackImm32OffsetInst(addr, reg, offsetBytes);
        }
    }
}

// The supposed use case of this function is to emit a load from [reg+offset] where 'reg' is a DFG reg alloc register.
// So this function assumes that 'baseReg' is not RSP or R12, and FPR will only be xmm0-7.
//
inline size_t WARN_UNUSED GetRegisterLoadFromMemBaseOffsetInstLength(X64Reg reg, X64Reg baseReg, uint32_t offsetBytes)
{
    TestAssert(baseReg != X64Reg::RSP && baseReg != X64Reg::R12);
    TestAssert(baseReg.IsGPR());
    size_t extraLength = (offsetBytes < 128) ? 0 : 3;
    if (reg.IsGPR())
    {
        return 4 + extraLength;
    }
    else
    {
        return 5 + extraLength + (baseReg.MachineOrd() < 8 ? 0 : 1);
    }
}

// See above, this function assumes that 'baseReg' is not RSP or R12, and FPR will only be xmm0-7.
//
inline void EmitRegisterLoadFromMemBaseOffsetInstruction(uint8_t*& addr /*inout*/, X64Reg reg, X64Reg baseReg, uint32_t offsetBytes)
{
    TestAssert(baseReg != X64Reg::RSP && baseReg != X64Reg::R12);
    TestAssert(baseReg.IsGPR());
    size_t instLen;
    if (reg.IsGPR())
    {
        if (offsetBytes < 128)
        {
            instLen = EmitGprLoadFromMemBaseOffsetImm8Inst(addr /*inout*/, reg, baseReg, offsetBytes);
        }
        else
        {
            instLen = EmitGprLoadFromMemBaseOffsetImm32Inst(addr /*inout*/, reg, baseReg, offsetBytes);
        }
    }
    else
    {
        if (offsetBytes < 128)
        {
            instLen = EmitFprLoadFromMemBaseOffsetImm8Inst(addr /*inout*/, reg, baseReg, offsetBytes);
        }
        else
        {
            instLen = EmitFprLoadFromMemBaseOffsetImm32Inst(addr /*inout*/, reg, baseReg, offsetBytes);
        }
    }
    TestAssert(instLen == GetRegisterLoadFromMemBaseOffsetInstLength(reg, baseReg, offsetBytes));
    std::ignore = instLen;
}

}   // namespace dfg
