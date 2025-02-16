#pragma once

#include "common_utils.h"
#include "x64_register_info.h"
#include "heap_ptr_utils.h"

namespace dfg {

// Emit 'testq %r64, %r64' instruction, always 3 bytes
// This instruction followed by 'jne %dest' implements branch to dest if %r64 is not zero
//
inline void EmitTestRegRegInstruction(uint8_t*& addr /*inout*/, X64Reg reg)
{
    TestAssert(reg.IsGPR());
    UnalignedStore<uint16_t>(addr, static_cast<uint16_t>(0x8548 + ((reg.MachineOrd() >= 8) ? 5 : 0)));
    addr[2] = static_cast<uint8_t>(0xc0 + (reg.MachineOrd() & 7) * 9);
    addr += 3;
}

// Emit 'cmpq $0, imm32(%r64)' instruction, always 8 bytes, except when register is %rsp or %r12, which we will never use
// This instruction followed by 'jne %dest' implements branch to dest if imm32(%r64) is not zero
// For now we don't consider the imm8 version since under our current stack frame layout it is unlikely the offset is < 128
//
inline void EmitI64CmpBaseImm32OffsetZeroInstruction(uint8_t*& addr /*inout*/, X64Reg baseReg, uint32_t offset)
{
    TestAssert(baseReg.IsGPR());
    TestAssert(baseReg != X64Reg::RSP && baseReg != X64Reg::R12);
    TestAssert(offset <= 0x7fffffffU);
    UnalignedStore<uint16_t>(addr, static_cast<uint16_t>(0x8348 + ((baseReg.MachineOrd() >= 8) ? 1 : 0)));
    addr[2] = static_cast<uint8_t>(0xb8 + (baseReg.MachineOrd() & 7));
    UnalignedStore<uint32_t>(addr + 3, offset);
    addr[7] = 0;
    addr += 8;
}

// Emit jne to destAddr, always 6 bytes
//
inline void EmitJneInstruction(uint8_t*& addr /*inout*/, uint8_t* destAddr)
{
    UnalignedStore<uint16_t>(addr, 0x850f);
    int64_t diff = destAddr - (addr + 6);
    TestAssert(IntegerCanBeRepresentedIn<int32_t>(diff));
    UnalignedStore<int32_t>(addr + 2, static_cast<int32_t>(diff));
    addr += 6;
}

// Emit je to destAddr, always 6 bytes
//
inline void EmitJeInstruction(uint8_t*& addr /*inout*/, uint8_t* destAddr)
{
    UnalignedStore<uint16_t>(addr, 0x840f);
    int64_t diff = destAddr - (addr + 6);
    TestAssert(IntegerCanBeRepresentedIn<int32_t>(diff));
    UnalignedStore<int32_t>(addr + 2, static_cast<int32_t>(diff));
    addr += 6;
}

// Emit jmp to destAddr, always 5 bytes
//
inline void EmitJmpInstruction(uint8_t*& addr /*inout*/, uint8_t* destAddr)
{
    addr[0] = 0xe9;
    int64_t diff = destAddr - (addr + 5);
    TestAssert(IntegerCanBeRepresentedIn<int32_t>(diff));
    UnalignedStore<int32_t>(addr + 1, static_cast<int32_t>(diff));
    addr += 5;
}

// Emit ud2 instruction, always 2 bytes
//
inline void EmitUd2Instruction(uint8_t*& addr /*inout*/)
{
    UnalignedStore<uint16_t>(addr, 0x0b0f);
    addr += 2;
}

}   // namespace dfg
