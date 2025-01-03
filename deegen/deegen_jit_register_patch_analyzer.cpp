#include "deegen_jit_register_patch_analyzer.h"
#include "zydis/zydis.h"
#include "drt/dfg_codegen_register_renamer.h"
#include "drt/dfg_reg_alloc_register_info.h"

namespace dast {

static void NO_RETURN DumpOffendingInstAndAbort(const std::vector<uint8_t>& machineCode, size_t offset)
{
    ReleaseAssert(offset < machineCode.size());
    fprintf(stderr, "Offending instruction:\n");
    for (size_t i = 0; i < machineCode.size() - offset && i < ZYDIS_MAX_INSTRUCTION_LENGTH; i++)
    {
        fprintf(stderr, "%02X ", static_cast<unsigned int>(machineCode[offset + i]));
    }
    fprintf(stderr, "\n");
    abort();
}

static void NO_RETURN DumpOffendingRegisterAndAbort(ZydisRegister reg)
{
    const char* regStr = ZydisRegisterGetString(reg);
    if (regStr == nullptr) { regStr = ""; }
    fprintf(stderr, "unexpected register %s (%d)\n", regStr, static_cast<int>(reg));
    abort();
}

static bool IsRegisterFpr(ZydisRegister reg)
{
    ZydisRegisterClass regClass = ZydisRegisterGetClass(reg);
    switch (regClass)
    {
    case ZYDIS_REGCLASS_XMM:
    case ZYDIS_REGCLASS_YMM:
    case ZYDIS_REGCLASS_ZMM:
    {
        return true;
    }
    default:
    {
        return false;
    }
    }   /*switch*/
}

// Return true if 'reg' is one of the GPR or FPR registers
// Note that RIP is not
//
static bool IsRegisterGprOrFpr(ZydisRegister reg)
{
    ZydisRegisterClass regClass = ZydisRegisterGetClass(reg);
    switch (regClass)
    {
    case ZYDIS_REGCLASS_GPR8:
    case ZYDIS_REGCLASS_GPR16:
    case ZYDIS_REGCLASS_GPR32:
    case ZYDIS_REGCLASS_GPR64:
    case ZYDIS_REGCLASS_XMM:
    case ZYDIS_REGCLASS_YMM:
    case ZYDIS_REGCLASS_ZMM:
    {
        return true;
    }
    default:
    {
        return false;
    }
    }   /*switch*/
}

// This returns the machine ordinal of the *enclosing* register, so AH would return 0 (RAX) instead of the special ordinal for AH etc
//
static size_t GetRegisterMachineOrdinalFromZyRegister(ZydisRegister reg)
{
    if (!IsRegisterGprOrFpr(reg))
    {
        DumpOffendingRegisterAndAbort(reg);
    }

    ZyanI8 id = ZydisRegisterGetId(ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, reg));
    if (id < 0)
    {
        DumpOffendingRegisterAndAbort(reg);
    }
    return static_cast<size_t>(id);
}

static bool IsRegisterTargetForRenaming(StencilRegisterFileContext& ctx, ZydisRegister reg)
{
    ZydisRegisterClass regClass = ZydisRegisterGetClass(reg);
    switch (regClass)
    {
    case ZYDIS_REGCLASS_GPR8:
    case ZYDIS_REGCLASS_GPR16:
    case ZYDIS_REGCLASS_GPR32:
    case ZYDIS_REGCLASS_GPR64:
    {
        size_t mcOrd = GetRegisterMachineOrdinalFromZyRegister(reg);
        return !ctx.IsGprNoRenaming(mcOrd);
    }
    case ZYDIS_REGCLASS_XMM:
    case ZYDIS_REGCLASS_YMM:
    case ZYDIS_REGCLASS_ZMM:
    {
        size_t mcOrd = GetRegisterMachineOrdinalFromZyRegister(reg);
        return !ctx.IsFprNoRenaming(mcOrd);
    }
    default:
    {
        return false;
    }
    }   /*switch*/
}

static ZydisRegister RenameZyRegister(ZydisRegister oldReg, size_t newRegOrd)
{
    ZydisRegister result = ZYDIS_REGISTER_NONE;
    ReleaseAssert(IsRegisterGprOrFpr(oldReg));
    if (ZydisRegisterGetClass(oldReg) == ZYDIS_REGCLASS_GPR8)
    {
        // In ZyDis, AH, BH, CH, DH has ID 4-7, inserted before ID 4 of the original register sequence..
        // This unfortunately relies on how ZyDis is implemented,
        // but we will assert that the result is OK in the end so should be fine..
        //
        size_t regOrdToEncode = newRegOrd;
        if (regOrdToEncode >= 4) { regOrdToEncode += 4; }
        result = ZydisRegisterEncode(ZydisRegisterGetClass(oldReg), SafeIntegerCast<uint8_t>(regOrdToEncode));
    }
    else
    {
        // For other cases, the machine register ID is the same as ZyDis's register ID
        //
        result = ZydisRegisterEncode(ZydisRegisterGetClass(oldReg), SafeIntegerCast<uint8_t>(newRegOrd));
    }

    ReleaseAssert(ZydisRegisterGetClass(result) == ZydisRegisterGetClass(oldReg));
    ReleaseAssert(GetRegisterMachineOrdinalFromZyRegister(result) == newRegOrd);
    return result;
}

static uint8_t GetX64LegacyPrefixGroupNumber(uint8_t prefixVal)
{
    // Intel 64 and IA-32 Architectures Software Developer's  Manual Volume 2A,
    // Section 2.1.1 Instruction Prefixes
    //
    // https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html
    //
    switch (prefixVal)
    {
    case 0xF0:
    case 0xF2:
    case 0xF3:
    {
        return 0;
    }
    case 0x2E:
    case 0x36:
    case 0x3E:
    case 0x26:
    case 0x64:
    case 0x65:
    {
        return 1;
    }
    case 0x66:
    {
        return 2;
    }
    case 0x67:
    {
        return 3;
    }
    default:
    {
        return static_cast<uint8_t>(-1);
    }
    }   /*switch*/
}

void StencilRegRenameParseResult::Parse(StencilRegisterFileContext& ctx, std::vector<uint8_t> machineCode)
{
    ReleaseAssert(ctx.IsFinalized());

    // RAX, RBX, RCX, RDX cannot be renamed, since AH, BH, CH, DH has no corresponding versions for other registers
    // (and also some instructions hardcode them as operands, and also due to some details about how we do IC calls..)
    //
    ReleaseAssert(ctx.IsRegNoRenaming(X64Reg::RAX));
    ReleaseAssert(ctx.IsRegNoRenaming(X64Reg::RBX));
    ReleaseAssert(ctx.IsRegNoRenaming(X64Reg::RCX));
    ReleaseAssert(ctx.IsRegNoRenaming(X64Reg::RDX));

    // RBP/R13 cannot be trivially renamed due to idiosyncrasy with addressing mode SIB byte encoding
    // This can be workarounded if needed, but we don't need it right now
    //
    ReleaseAssert(ctx.IsRegNoRenaming(X64Reg::RBP));
    ReleaseAssert(ctx.IsRegNoRenaming(X64Reg::R13));

    // R12 cannot be trivially renamed due to idiosyncrasy with ModRM r/m+disp addressing mode encoding
    //
    ReleaseAssert(ctx.IsRegNoRenaming(X64Reg::R12));

    // RSP obviously cannot be renamed
    //
    ReleaseAssert(ctx.IsRegNoRenaming(X64Reg::RSP));

    uint8_t* code = machineCode.data();
    size_t codeLength = machineCode.size();

    ZydisDecoder decoder;
    ReleaseAssert(ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64)));

    ZyanUSize offset = 0;
    ZydisDecodedInstruction inst;
    ZydisDecodedOperand opsInfo[ZYDIS_MAX_OPERAND_COUNT];

    struct InstRawData
    {
        uint8_t data[ZYDIS_MAX_INSTRUCTION_LENGTH];
        ZyanUSize length;
    };

    // This is the instruction with "padded" immediate values to avoid Zydis from choosing a smaller imm width in re-encoding
    //
    InstRawData curInst;

    while (offset < codeLength)
    {
        // We have to use the "complex" API to access the internal decoderContext.cd8_scale value...
        //
        ZydisDecoderContext decoderContext;
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder, &decoderContext, code + offset, codeLength - offset, &inst /*out*/)))
        {
            fprintf(stderr, "Failed to decode instruction.\n");
            DumpOffendingInstAndAbort(machineCode, offset);
        }
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeOperands(&decoder, &decoderContext, &inst, opsInfo /*out*/, inst.operand_count)))
        {
            fprintf(stderr, "Failed to decode instruction operands.\n");
            DumpOffendingInstAndAbort(machineCode, offset);
        }
        memset(&opsInfo[inst.operand_count], 0, (ZYDIS_MAX_OPERAND_COUNT - inst.operand_count) * sizeof(opsInfo[0]));

        size_t cd8_scale = decoderContext.cd8_scale;
        if (cd8_scale == 0)
        {
            cd8_scale = 1;
        }
        ReleaseAssert(is_power_of_2(cd8_scale));

        auto getEncodedInstImpl = [&](bool tryHintOpcode, InstRawData& out /*out*/) WARN_UNUSED -> bool
        {
            InstRawData res;
            ZydisEncoderRequest enc_req;
            if (!ZYAN_SUCCESS(
                    ZydisEncoderDecodedInstructionToEncoderRequest(
                        &inst,
                        opsInfo,
                        inst.operand_count_visible,
                        &enc_req)))
            {
                fprintf(stderr, "Failed to generate encoder request after register/immval change.\n");
                DumpOffendingInstAndAbort(machineCode, offset);
            }

            // Give hint so that the ordering of the legacy prefixes are same as the original instruction
            // (the ordering of the legacy prefixes does not matter (https://wiki.osdev.org/X86-64_Instruction_Encoding),
            // but there is no reason to change the stuffs Clang generated, and it triggers spurious assertion in our code
            //
            {
                uint64_t groupIdxShowedUpMask = 0;
                uint8_t groupPriority[4] = { 3, 3, 3, 3 };
                uint8_t curPrio = 0;
                for (size_t idx = 0; idx < inst.raw.prefix_count; idx++)
                {
                    uint8_t groupIdx = GetX64LegacyPrefixGroupNumber(inst.raw.prefixes[idx].value);
                    if (groupIdx == static_cast<uint8_t>(-1))
                    {
                        continue;
                    }
                    ReleaseAssert(groupIdx < 4 && (groupIdxShowedUpMask & (1U << groupIdx)) == 0);
                    groupPriority[groupIdx] = curPrio;
                    curPrio++;
                    groupIdxShowedUpMask |= 1U << groupIdx;
                }
                uint8_t encodedPrioVal = 0;
                for (size_t i = 0; i < 4; i++)
                {
                    ReleaseAssert(groupPriority[i] < 4);
                    encodedPrioVal |= static_cast<uint8_t>(groupPriority[i] << (2 * i));
                }
                enc_req.legacy_prefix_ordering = encodedPrioVal;
            }

            if (tryHintOpcode)
            {
                enc_req.has_opcode_hint = ZYAN_TRUE;
                enc_req.opcode_hint = inst.opcode;
                enc_req.opcode_map_hint = inst.opcode_map;
            }

            res.length = ZYDIS_MAX_INSTRUCTION_LENGTH;
            if (!ZYAN_SUCCESS(ZydisEncoderEncodeInstruction(&enc_req, res.data, &res.length)))
            {
                return false;
            }

            out = res;
            return true;
        };

        auto getEncodedInst = [&]() WARN_UNUSED -> InstRawData
        {
            InstRawData data;

            // Try to encode with opcode hint hack first. If this succeeds we are good.
            //
            if (getEncodedInstImpl(true /*tryHintOpcode*/, data /*out*/))
            {
                return data;
            }

            // However, there are cases where the opcode of the instruction encodes more info than the opcode...
            // In which case the call above will fail, and we will retry here without the opcode hint hack...
            //
            if (getEncodedInstImpl(false /*tryHintOpcode*/, data /*out*/))
            {
                return data;
            }

            fprintf(stderr, "Failed to re-encode instruction after register/immval change.\n");
            DumpOffendingInstAndAbort(machineCode, offset);
        };

        // "Pad" all the immediate values to fit their current physical width,
        // so Zydis won't choose a smaller width during re-encoding
        //
        {
            // A bitmask indicating whether each instruction byte is used by an imm that we changed
            //
            uint64_t usedByImmBitmask = 0;

            size_t immOrd = 0;
            bool foundMemOperand = false;
            for (size_t opIdx = 0; opIdx < inst.operand_count; opIdx++)
            {
                ZydisDecodedOperand& op = opsInfo[opIdx];

                // By looking at Zydis source code, it seems like the rules about immediate operands can be summarized as follow:
                // 1. Certain instructions have "implicit immediate operands", which does not take a slot in the raw.imm field
                // 2. 8086 mode segmented memory takes two imm operands, which we don't care
                // 3. Excluding the special cases above, each raw.imm field corresponds to an immediate operand, in the same order they show up
                //
                if (op.type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
                {
                    if (op.visibility != ZYDIS_OPERAND_VISIBILITY_EXPLICIT)
                    {
                        // Implicit operand, don't care and don't increment immOrd
                        //
                        continue;
                    }

                    ReleaseAssert(immOrd < 2);
                    // Zydis uses 'size > 0' to identify if this imm is valid or not
                    //
                    ReleaseAssert(inst.raw.imm[immOrd].size > 0);

                    ReleaseAssert(inst.raw.imm[immOrd].size % 8 == 0);
                    size_t immSizeBytes = inst.raw.imm[immOrd].size / 8;
                    size_t immOffset = inst.raw.imm[immOrd].offset;
                    ReleaseAssert(immOffset + immSizeBytes <= inst.length);

                    for (size_t k = immOffset; k < immOffset + immSizeBytes; k++)
                    {
                        ReleaseAssert((usedByImmBitmask & (1ULL << k)) == 0);
                        usedByImmBitmask |= (1ULL << k);
                    }

                    // 8-bit immediate has special cases of IS4 register encoding, where the higher 4 bits of the immediate is
                    // used to represent a register ordinal. In other cases, the immediate value in the raw data should just
                    // be the value of the immediate operand. So assert that the value is the same if the operand is >8 bits.
                    //
                    ReleaseAssert(inst.raw.imm[immOrd].is_signed == op.imm.is_signed);
                    if (immSizeBytes > 1)
                    {
                        ReleaseAssert(inst.raw.imm[immOrd].value.u == op.imm.value.u);
                        ReleaseAssert(immSizeBytes == 2 || immSizeBytes == 4 || immSizeBytes == 8);

                        // Change it to a larger value
                        //
                        if (op.imm.is_signed)
                        {
                            op.imm.value.s = static_cast<ZyanI64>(-(1LL << (immSizeBytes * 8 - 2)));
                            inst.raw.imm[immOrd].value.s = op.imm.value.s;
                        }
                        else
                        {
                            op.imm.value.u = (3ULL << (immSizeBytes * 8 - 2));
                            inst.raw.imm[immOrd].value.u = op.imm.value.u;
                        }
                    }
                    else
                    {
                        if (inst.raw.imm[immOrd].value.u != op.imm.value.u)
                        {
                            // IS4 special encoding, must keep higher 4 bits unchanged
                            //
                            ReleaseAssert(op.imm.value.u < 16);
                            ReleaseAssert(inst.raw.imm[immOrd].value.u < 256);
                            ReleaseAssert((inst.raw.imm[immOrd].value.u & 15) == op.imm.value.u);
                            ReleaseAssert(!op.imm.is_signed);
                            uint64_t compositeVal = static_cast<uint8_t>(inst.raw.imm[immOrd].value.u) & 0xF0;
                            // Encode constant '1' for this immediate (anything not 0 should be fine)
                            //
                            compositeVal += 1;
                            op.imm.value.u = 1;
                            inst.raw.imm[immOrd].value.u = compositeVal;
                        }
                        else
                        {
                            if (op.imm.is_signed)
                            {
                                op.imm.value.s = -64;
                                inst.raw.imm[immOrd].value.s = -64;
                            }
                            else
                            {
                                op.imm.value.u = 192;
                                inst.raw.imm[immOrd].value.u = 192;
                            }
                        }
                    }

                    immOrd++;
                }
                else if (op.type == ZYDIS_OPERAND_TYPE_MEMORY)
                {
                    ReleaseAssert(!foundMemOperand);
                    foundMemOperand = true;
                    ReleaseAssertIff(op.mem.disp.has_displacement, inst.raw.disp.size > 0);
                    if (op.mem.disp.has_displacement)
                    {
                        ReleaseAssert(inst.raw.disp.size % 8 == 0);
                        size_t dispSizeBytes = inst.raw.disp.size / 8;
                        ReleaseAssert(dispSizeBytes == 1 || dispSizeBytes == 2 || dispSizeBytes == 4 || dispSizeBytes == 8);
                        // if the disp is 8-bit, there seems to be a 'cd8_scale' thing, so that the real operand disp
                        // is the raw.disp value times the cd8_scale
                        //
                        if (dispSizeBytes > 1)
                        {
                            ReleaseAssert(cd8_scale == 1);
                        }
                        ReleaseAssert(op.mem.disp.value == inst.raw.disp.value * static_cast<int64_t>(cd8_scale));

                        size_t dispOffset = inst.raw.disp.offset;
                        ReleaseAssert(dispOffset + dispSizeBytes <= inst.length);
                        for (size_t k = dispOffset; k < dispOffset + dispSizeBytes; k++)
                        {
                            ReleaseAssert((usedByImmBitmask & (1ULL << k)) == 0);
                            usedByImmBitmask |= (1ULL << k);
                        }

                        inst.raw.disp.value = static_cast<ZyanI64>(1LL << (dispSizeBytes * 8 - 2));
                        op.mem.disp.value = inst.raw.disp.value * static_cast<int64_t>(cd8_scale);
                    }
                }
            }

            ReleaseAssert(immOrd <= 2);
            // We should have consumed all the raw.imm entries listed
            //
            ReleaseAssertImp(immOrd < 2, inst.raw.imm[immOrd].size == 0);
            // No raw.disp entry should exist if a memory operand does not exist
            //
            ReleaseAssertImp(!foundMemOperand, inst.raw.disp.size == 0);

            // Re-encode the instruction
            //
            curInst = getEncodedInst();

            // Assert that the instruction length didn't change,
            // and everything except the bytes carrying the imm values didn't change
            //
            if (curInst.length != inst.length)
            {
                fprintf(stderr, "Unexpected instruction length change after changing immediate values.\n");
                DumpOffendingInstAndAbort(machineCode, offset);
            }

            for (size_t k = 0; k < curInst.length; k++)
            {
                if ((usedByImmBitmask & (1ULL << k)) == 0)
                {
                    if (curInst.data[k] != code[offset + k])
                    {
                        fprintf(stderr, "Unexpected instruction content change at byte %d after changing immediate values.\n", static_cast<int>(k));
                        fprintf(stderr, "New instruction after changing imm values:\n");
                        for (size_t i = 0; i < curInst.length; i++)
                        {
                            fprintf(stderr, "%02X ", static_cast<unsigned int>(curInst.data[i]));
                        }
                        fprintf(stderr, "\n");
                        DumpOffendingInstAndAbort(machineCode, offset);
                    }
                }
            }

            // Decode the instruction again for sanity
            //
            size_t oldNumOperands = inst.operand_count;
            ReleaseAssert(ZYAN_SUCCESS(
                ZydisDecoderDecodeFull(
                    &decoder,
                    curInst.data,
                    curInst.length,
                    &inst /*out*/,
                    opsInfo /*out*/)));

            ReleaseAssert(inst.length == curInst.length);
            ReleaseAssert(oldNumOperands == inst.operand_count);
        }

        std::vector<ZydisRegister*> regs;

        auto handleReg = [&](ZydisRegister& regRef, bool isExplicit)
        {
            if (ZydisRegisterGetClass(regRef) == ZYDIS_REGCLASS_MMX)
            {
                fprintf(stderr, "Use of MMX register is unexpected since we assume XMM\n");
                DumpOffendingInstAndAbort(machineCode, offset);
            }

            // We currently have hand-written code that assumes FPU register is XMM
            //
            if (ZydisRegisterGetClass(regRef) == ZYDIS_REGCLASS_YMM || ZydisRegisterGetClass(regRef) == ZYDIS_REGCLASS_ZMM)
            {
                fprintf(stderr, "Use of YMM/ZMM register is unexpected since we assume XMM right now\n");
                DumpOffendingInstAndAbort(machineCode, offset);
            }

            if (IsRegisterFpr(regRef))
            {
                size_t fprOrd = GetRegisterMachineOrdinalFromZyRegister(regRef);
                ReleaseAssert(fprOrd < 64);
                m_usedFpuRegs |= (1ULL << fprOrd);
            }

            // We only care about GPR and FPR
            //
            if (!IsRegisterGprOrFpr(regRef))
            {
                return;
            }

            // We only care about registers that are requested to be renameable
            //
            if (!IsRegisterTargetForRenaming(ctx, regRef))
            {
                return;
            }

            if (!isExplicit)
            {
                // This register is hardcoded by opcode, but it is requested to be renamable, this is a bug
                //
                fprintf(stderr, "Unexpected: Instruction uses an opcode-hardcoded register that needs to be renamed.\n");
                DumpOffendingInstAndAbort(machineCode, offset);
            }

            regs.push_back(&regRef);
        };

        struct RegBitLocInInst
        {
            uint8_t byteOffset;
            uint8_t bitOffset;
            bool isFlipped;
        };

        // Assert that the detection result of where the register resides in the instruction is correct
        // We do this by modifying the instruction based on the detection result to use each possible valid register,
        // decode the instruction, and check that nothing except the target register changed.
        //
        auto checkDetectedRegBitInfoCorrectness = [&](ZydisRegister* regPtr, RegBitLocInInst detectionResult)
        {
            uintptr_t opsArrayAddr = reinterpret_cast<uintptr_t>(opsInfo);
            uintptr_t opsArrayAddrEnd = reinterpret_cast<uintptr_t>(opsInfo + ZYDIS_MAX_OPERAND_COUNT);
            ReleaseAssert(opsArrayAddrEnd == opsArrayAddr + sizeof(ZydisDecodedOperand) * ZYDIS_MAX_OPERAND_COUNT);
            uintptr_t regPtr64 = reinterpret_cast<uintptr_t>(regPtr);
            ReleaseAssert(opsArrayAddr <= regPtr64 && regPtr64 < opsArrayAddrEnd);
            size_t operandIdxForReg = (regPtr64 - opsArrayAddr) / sizeof(ZydisDecodedOperand);
            ReleaseAssert(operandIdxForReg < inst.operand_count);
            size_t offsetOfRegInOperandStruct = (regPtr64 - opsArrayAddr) % sizeof(ZydisDecodedOperand);
            ReleaseAssert(offsetOfRegInOperandStruct + sizeof(ZydisRegister) <= sizeof(ZydisDecodedOperand));

            ZydisDecodedInstruction alteredInst;
            ZydisDecodedOperand alteredOpsInfo[ZYDIS_MAX_OPERAND_COUNT];

            uint8_t alteredInstData[ZYDIS_MAX_INSTRUCTION_LENGTH];

            ReleaseAssert(curInst.length <= ZYDIS_MAX_INSTRUCTION_LENGTH);
            memcpy(alteredInstData, curInst.data, curInst.length);

            ZydisRegister oldReg = *regPtr;
            ReleaseAssert(IsRegisterGprOrFpr(oldReg));
            bool isGPR = !IsRegisterFpr(oldReg);
            size_t oldRegId = GetRegisterMachineOrdinalFromZyRegister(oldReg);
            X64Reg oldRegD = isGPR ? X64Reg::GPR(oldRegId) : X64Reg::FPR(oldRegId);
            size_t oldRegHighBit = oldRegId & 8;

            for (size_t newRegLowBits = 0; newRegLowBits < 8; newRegLowBits++)
            {
                X64Reg newRegD;
                if (isGPR)
                {
                    newRegD = X64Reg::GPR(oldRegHighBit + newRegLowBits);
                }
                else
                {
                    newRegD = X64Reg::FPR(oldRegHighBit + newRegLowBits);
                }
                if (ctx.IsRegNoRenaming(newRegD))
                {
                    // Skip regs that are not candidates for renaming
                    //
                    continue;
                }

                ZydisRegister newRegZy = RenameZyRegister(oldReg, oldRegHighBit + newRegLowBits);

                // Change the instruction bytes
                //
                ReleaseAssert(detectionResult.byteOffset < curInst.length);
                ReleaseAssert(detectionResult.bitOffset <= 5);
                {
                    uint8_t byteVal = alteredInstData[detectionResult.byteOffset];
                    byteVal &= static_cast<uint8_t>(255 ^ (7 << detectionResult.bitOffset));
                    uint8_t orMask = static_cast<uint8_t>(newRegLowBits);
                    if (detectionResult.isFlipped) { orMask ^= 7; }
                    byteVal |= static_cast<uint8_t>(orMask << detectionResult.bitOffset);
                    alteredInstData[detectionResult.byteOffset] = byteVal;
                }

                // Decode the altered instruction
                //
                ZydisDecoderContext tmpDecoderContext;
                ReleaseAssert(ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder, &tmpDecoderContext, alteredInstData, curInst.length, &alteredInst /*out*/)));
                ReleaseAssert(ZYAN_SUCCESS(ZydisDecoderDecodeOperands(&decoder, &tmpDecoderContext, &alteredInst, alteredOpsInfo /*out*/, alteredInst.operand_count)));

                auto compareAndValidate = [&]() WARN_UNUSED -> bool
                {
                    // Assert that all instruction fields are equal
                    // Note that we unfortunately cannot check the 'inst.opcode' field since it may embed the register thus get changed
                    //
                    CHECK_LOG_ERROR(inst.mnemonic == alteredInst.mnemonic);
                    CHECK_LOG_ERROR(inst.length == alteredInst.length);
                    CHECK_LOG_ERROR(inst.encoding == alteredInst.encoding);
                    CHECK_LOG_ERROR(inst.opcode_map == alteredInst.opcode_map);
                    CHECK_LOG_ERROR(inst.stack_width == alteredInst.stack_width);
                    CHECK_LOG_ERROR(inst.operand_width == alteredInst.operand_width);
                    CHECK_LOG_ERROR(inst.address_width == alteredInst.address_width);
                    CHECK_LOG_ERROR(inst.operand_count == alteredInst.operand_count);
                    CHECK_LOG_ERROR(inst.operand_count_visible == alteredInst.operand_count_visible);
                    CHECK_LOG_ERROR(inst.attributes == alteredInst.attributes);
                    CHECK_LOG_ERROR(inst.cpu_flags == alteredInst.cpu_flags);
                    CHECK_LOG_ERROR(inst.fpu_flags == alteredInst.fpu_flags);

                    // Assert that all operands are equal, except the register that we changed
                    //
                    for (size_t opIdx = 0; opIdx < inst.operand_count; opIdx++)
                    {
                        uint8_t* oldOpInfo = reinterpret_cast<uint8_t*>(opsInfo + opIdx);
                        uint8_t* newOpInfo = reinterpret_cast<uint8_t*>(alteredOpsInfo + opIdx);
                        if (opIdx != operandIdxForReg)
                        {
                            CHECK_LOG_ERROR(memcmp(oldOpInfo, newOpInfo, sizeof(ZydisDecodedOperand)) == 0);
                        }
                        else
                        {
                            if (offsetOfRegInOperandStruct > 0)
                            {
                                CHECK_LOG_ERROR(memcmp(oldOpInfo, newOpInfo, offsetOfRegInOperandStruct) == 0);
                            }
                            if (offsetOfRegInOperandStruct + sizeof(ZydisRegister) < sizeof(ZydisDecodedOperand))
                            {
                                CHECK_LOG_ERROR(memcmp(oldOpInfo + offsetOfRegInOperandStruct + sizeof(ZydisRegister),
                                                       newOpInfo + offsetOfRegInOperandStruct + sizeof(ZydisRegister),
                                                       sizeof(ZydisDecodedOperand) - offsetOfRegInOperandStruct - sizeof(ZydisRegister)) == 0);
                            }
                            ReleaseAssert(oldOpInfo + offsetOfRegInOperandStruct == reinterpret_cast<uint8_t*>(regPtr));
                            ZydisRegister* newRegPtr = reinterpret_cast<ZydisRegister*>(newOpInfo + offsetOfRegInOperandStruct);
                            CHECK_LOG_ERROR(*newRegPtr == newRegZy);
                        }
                    }
                    return true;
                };

                if (!compareAndValidate())
                {
                    fprintf(stderr, "Instruction register location validation failed! Original instruction:\n");
                    for (size_t i = 0; i < machineCode.size() - offset && i < ZYDIS_MAX_INSTRUCTION_LENGTH; i++)
                    {
                        fprintf(stderr, "%02X ", static_cast<unsigned int>(machineCode[offset + i]));
                    }
                    fprintf(stderr, "\nModified instruction:\n");
                    for (size_t i = 0; i < curInst.length; i++)
                    {
                        fprintf(stderr, "%02X ", static_cast<unsigned int>(alteredInstData[i]));
                    }
                    fprintf(stderr, "\nOriginal register: %s, new register: %s\n", oldRegD.GetName(), newRegD.GetName());
                    abort();
                }
            }
        };

        // Returns the start bit-offset inside the *instruction* where the register resides
        //
        auto detectRegBitInfo = [&](ZydisRegister* reg) WARN_UNUSED -> RegBitLocInInst
        {
            size_t originalId = GetRegisterMachineOrdinalFromZyRegister(*reg);
            ReleaseAssert(originalId < 16);
            size_t highBit = originalId & 8;

            ZydisRegister oldReg = *reg;
            ZydisRegister testReg1 = RenameZyRegister(oldReg, highBit + 6);
            ZydisRegister testReg2 = RenameZyRegister(oldReg, highBit + 7);

            *reg = testReg1;
            InstRawData inst1 = getEncodedInst();
            *reg = testReg2;
            InstRawData inst2 = getEncodedInst();
            *reg = oldReg;

            if (inst1.length != inst.length)
            {
                fprintf(stderr, "Changing a register unexpectedly changed instruction length!\n");
                DumpOffendingInstAndAbort(machineCode, offset);
            }

            if (inst2.length != inst.length)
            {
                fprintf(stderr, "Changing a register unexpectedly changed instruction length!\n");
                DumpOffendingInstAndAbort(machineCode, offset);
            }

            size_t differingByteLoc = static_cast<size_t>(-1);
            for (size_t i = 0; i < inst1.length; i++)
            {
                if (inst1.data[i] != inst2.data[i])
                {
                    if (differingByteLoc != static_cast<size_t>(-1))
                    {
                        fprintf(stderr, "Changing a register unexpectedly changed >1 position!\n");
                        DumpOffendingInstAndAbort(machineCode, offset);
                    }
                    differingByteLoc = i;
                }
            }
            if (differingByteLoc == static_cast<size_t>(-1))
            {
                fprintf(stderr, "Changing a register unexpectedly changed no position!\n");
                DumpOffendingInstAndAbort(machineCode, offset);
            }

            uint8_t changedBitVal = inst1.data[differingByteLoc] ^ inst2.data[differingByteLoc];
            if (!is_power_of_2(changedBitVal))
            {
                fprintf(stderr, "Changing a register by one bit unexpectedly changed >1 bits!\n");
                DumpOffendingInstAndAbort(machineCode, offset);
            }

            bool shouldFlip = false;
            if (inst1.data[differingByteLoc] & changedBitVal)
            {
                // Inst1 is the lowbit = 0 version, if it gets 1 in instruction, the register value is flipped
                //
                shouldFlip = true;
            }

            size_t bitOffset = 0;
            while (changedBitVal != 1) { bitOffset++; changedBitVal /= 2; }

            ReleaseAssert(bitOffset < 6);

            // As a sanity check, assert that the register value of the original instruction and new instruction matches expected
            //
            auto sanityCheckInst = [&](uint8_t instByte, size_t expectedOrd)
            {
                instByte >>= bitOffset;
                instByte &= 7;
                if (shouldFlip) { instByte ^= 7; }
                ReleaseAssert(instByte + highBit == expectedOrd);
            };

            sanityCheckInst(inst1.data[differingByteLoc], highBit + 6);
            sanityCheckInst(curInst.data[differingByteLoc], GetRegisterMachineOrdinalFromZyRegister(oldReg));

            RegBitLocInInst result;
            result.byteOffset = SafeIntegerCast<uint8_t>(differingByteLoc);
            result.bitOffset = SafeIntegerCast<uint8_t>(bitOffset);
            result.isFlipped = shouldFlip;

            checkDetectedRegBitInfoCorrectness(reg, result);

            return result;
        };

        // Loop through all operands and figure out all uses of registers
        //
        for (size_t opIdx = 0; opIdx < inst.operand_count; opIdx++)
        {
            ZydisDecodedOperand& op = opsInfo[opIdx];

            bool isExplicitOperand;
            switch (op.visibility)
            {
            case ZYDIS_OPERAND_VISIBILITY_EXPLICIT:
            {
                isExplicitOperand = true;
                break;
            }
            case ZYDIS_OPERAND_VISIBILITY_IMPLICIT:
            case ZYDIS_OPERAND_VISIBILITY_HIDDEN:
            {
                isExplicitOperand = false;
                break;
            }
            default:
            {
                ReleaseAssert(false && "unexpected ZydisOperandVisibility value");
            }
            }   /*switch*/

            switch (op.type)
            {
            case ZYDIS_OPERAND_TYPE_REGISTER:
            {
                // Register operand, log it
                //
                ZydisRegister& reg = op.reg.value;
                handleReg(reg, isExplicitOperand);
                break;
            }
            case ZYDIS_OPERAND_TYPE_MEMORY:
            {
                // Memory operand may consist of segment register (which we don't care), base register and index register
                // base and index register are always populated: they are populated to NONE if doesn't used
                // We don't care about op.mem.type: the only weird thing is ZYDIS_MEMOP_TYPE_MIB where 'index' exists but
                // is not used, but we don't care about this case either (and it seems like this type is only used for
                // legacy MPX instructions).
                //
                // REP-prefixed instructions hardcode memory operands to rsi/rdi, but modern compilers don't seem to use
                // them so should be fine for now.
                //
                ReleaseAssert(isExplicitOperand || op.mem.segment == ZydisRegister::ZYDIS_REGISTER_SS);
                ZydisDecodedOperandMem& mem = op.mem;
                handleReg(mem.base, true /*isExplicit*/);
                handleReg(mem.index, true /*isExplicit*/);
                break;
            }
            case ZYDIS_OPERAND_TYPE_POINTER:
            {
                // 8086 seg:offset should not show up in 64-bit code..
                //
                fprintf(stderr, "ZYDIS_OPERAND_TYPE_POINTER operand is not expected\n");
                DumpOffendingInstAndAbort(machineCode, offset);
            }
            case ZYDIS_OPERAND_TYPE_IMMEDIATE:
            {
                // immediate value operand, not a register, nothing to do
                //
                break;
            }
            default:
            {
                fprintf(stderr, "unknown operand type %d\n", static_cast<int>(op.type));
                DumpOffendingInstAndAbort(machineCode, offset);
            }
            }   /*switch*/
        }

        // For each register, figure out its bit location in the instruction
        //
        // We do this by changing the register to RSI (ord 6) and RDI (ord 7) (or R14/R15 for high-bit=1 case)
        // and figure out the bit that changed.
        // We chose this since it seems like no instruction have specializations on those registers (in contrast,
        // RAX would not work since some instructions have variants that hardcode RAX as operand)
        //
        for (ZydisRegister* reg : regs)
        {
            RegBitLocInInst bitLoc = detectRegBitInfo(reg);
            StencilRegRenamePatchItem item;
            size_t byteOffset = bitLoc.byteOffset + offset;
            if (byteOffset > 65535)
            {
                fprintf(stderr, "Code snippet too long (>=64KB)\n");
                abort();
            }
            ReleaseAssert(offset <= byteOffset && byteOffset < offset + inst.length);
            item.m_byteOffset = SafeIntegerCast<uint16_t>(byteOffset);
            item.m_bitOffset = bitLoc.bitOffset;
            item.m_isFlipped = bitLoc.isFlipped;

            ReleaseAssert(IsRegisterGprOrFpr(*reg));
            PhyRegUseInfo useInfo = ctx.GetPhyRegInfoAndMarkUsedByMachineCode(
                !IsRegisterFpr(*reg) /*isGPR*/, GetRegisterMachineOrdinalFromZyRegister(*reg));
            item.m_ident = useInfo.GetIdent();
            ReleaseAssert(item.m_ident.IsValid());
            ReleaseAssert(item.m_ident.m_class != StencilRegIdentClass::X_END_OF_ENUM);
            ReleaseAssert(item.m_ident.m_ord < 8);

            m_patches.push_back(item);
        }

        // If this is a call instruction, the callee assumes arguments from certain registers, and may clobber caller-saved registers
        // For the clobbering part, since we only want to support IC slow path calls which uses preserve_most convention, we don't need
        // to worry about GPR clobbering (and our JIT'ed call stub will be responsible for saving FPR state),
        // so all we need to take care of is the argument-passing registers. Specifically, we must treat all argument-passing registers
        // as "used" (it's possible that it's not used in other places, e.g., an operand that passes directedly to the callee),
        // so we can assign expected values to it in the JIT'ed call stub
        //
        if (inst.mnemonic == ZYDIS_MNEMONIC_CALL)
        {
            // Zydis reports 4 operands: #0 is the callee, #1 is %rip, #2 is %rsp, #3 is memory write to the stack
            //
            ReleaseAssert(inst.operand_count == 4);
            ReleaseAssert(opsInfo[0].visibility == ZYDIS_OPERAND_VISIBILITY_EXPLICIT);
            ReleaseAssert(opsInfo[1].visibility == ZYDIS_OPERAND_VISIBILITY_HIDDEN);
            ReleaseAssert(opsInfo[2].visibility == ZYDIS_OPERAND_VISIBILITY_HIDDEN);
            ReleaseAssert(opsInfo[3].visibility == ZYDIS_OPERAND_VISIBILITY_HIDDEN);

            if (opsInfo[0].type != ZYDIS_OPERAND_TYPE_IMMEDIATE)
            {
                m_hasIndirectCall = true;
            }
            else
            {
                ReleaseAssert(inst.length == 5);
                ReleaseAssert(code[offset] == 0xe8);
                m_directCallTargetOffsets.push_back(offset + 1);
            }

            auto getCallRegIdent = [&](ZydisRegister regName) -> StencilRegIdent
            {
                PhyRegUseInfo useInfo = ctx.GetPhyRegInfoAndMarkUsedByMachineCode(
                    !IsRegisterFpr(regName) /*isGPR*/, GetRegisterMachineOrdinalFromZyRegister(regName));
                return useInfo.GetIdent();
            };

            m_rsi = getCallRegIdent(ZYDIS_REGISTER_RSI);
            m_rdi = getCallRegIdent(ZYDIS_REGISTER_RDI);
            m_r8 = getCallRegIdent(ZYDIS_REGISTER_R8);
            m_r9 = getCallRegIdent(ZYDIS_REGISTER_R9);
        }

        offset += inst.length;
    }
    ReleaseAssert(offset == codeLength);

    // Sort all the patch records
    //
    {
        std::vector<std::vector<StencilRegRenamePatchItem>> sorter;
        for (StencilRegRenamePatchItem& item : m_patches)
        {
            ReleaseAssert(item.m_bitOffset < 8);
            size_t bitLoc = item.m_byteOffset * 8 + item.m_bitOffset;
            if (bitLoc >= sorter.size())
            {
                sorter.resize(bitLoc + 1);
            }
            sorter[bitLoc].push_back(item);
        }

        size_t oldSize = m_patches.size();
        m_patches.clear();
        for (auto& v : sorter)
        {
            for (StencilRegRenamePatchItem& item : v)
            {
                m_patches.push_back(item);
            }
        }
        ReleaseAssert(oldSize == m_patches.size());
    }
}

std::unordered_map<X64Reg, StencilRegIdent> WARN_UNUSED StencilRegisterFileContext::GetRegPurposeForAllDfgRegisters()
{
    std::unordered_map<X64Reg, StencilRegIdent> regMap;
    ForEachDfgRegAllocRegister([&](X64Reg reg) {
        PhyRegPurpose purpose = GetPhyRegPurpose(reg);
        StencilRegIdent ident;
        switch (purpose.Kind())
        {
        case PhyRegUseKind::OperandUse:
        {
            ReleaseAssert(purpose.HasOrd());
            ident.m_class = StencilRegIdentClass::Operand;
            ident.m_ord = purpose.Ord();
            break;
        }
        case PhyRegUseKind::ScratchUse:
        case PhyRegUseKind::PassThruUse:
        {
            if (!purpose.HasOrd())
            {
                return;
            }
            ident.m_class = PhyRegUseInfo::GetIdentClassFromUseAndReg(purpose.Kind(), reg);
            ident.m_ord = purpose.Ord();
            break;
        }
        case PhyRegUseKind::NoRenaming:
        {
            ReleaseAssert(false);
        }
        case PhyRegUseKind::X_END_OF_ENUM:
        {
            ReleaseAssert(false);
        }
        }   /*switch*/

        ReleaseAssert(ident.IsValid());
        ReleaseAssert(ident.m_class != StencilRegIdentClass::X_END_OF_ENUM);
        ReleaseAssert(ident.m_ord < 8);

        ReleaseAssert(!regMap.count(reg));
        regMap[reg] = ident;
    });

    return regMap;
}

// See comments in drt/dfg_codegen_register_renamer.h
//
EncodedStencilRegPatchStream WARN_UNUSED EncodedStencilRegPatchStream::Create(
    std::vector<uint8_t>& machineCode /*inout*/,
    const std::vector<StencilRegRenamePatchItem>& patches)
{
    std::vector<uint8_t> numRegsInEachCodeByte;
    numRegsInEachCodeByte.resize(machineCode.size());
    for (size_t i = 0; i < machineCode.size(); i++) { numRegsInEachCodeByte[i] = 0; }

    std::vector<uint16_t> res;
    res.push_back(0);       // #chunks, to be fixed in the end

    constexpr size_t x_bytesPerChunk = dfg::RuntimeStencilRegRenamingPatchItem::x_bytesPerChunk;
    size_t numChunks = 0;
    size_t curIdx = 0;
    while (curIdx < patches.size())
    {
        numChunks++;

        // #itemsInChunk, to be fixed at the end of the loop
        //
        size_t numItemsThisChunkIdx = res.size();
        res.push_back(0);

        size_t numItemsThisChunk = 0;
        while (curIdx < patches.size() && patches[curIdx].m_byteOffset < numChunks * x_bytesPerChunk)
        {
            StencilRegRenamePatchItem item = patches[curIdx];

            // Assert that the sequence is ordered
            //
            if (curIdx > 0)
            {
                ReleaseAssert(item.m_byteOffset > patches[curIdx - 1].m_byteOffset ||
                              (item.m_byteOffset == patches[curIdx - 1].m_byteOffset &&
                               item.m_bitOffset >= patches[curIdx - 1].m_bitOffset + 3));
            }

            ReleaseAssert((numChunks - 1) * x_bytesPerChunk <= item.m_byteOffset && item.m_byteOffset < numChunks * x_bytesPerChunk);
            ReleaseAssert(item.m_byteOffset < machineCode.size());

            ReleaseAssert(item.m_byteOffset < numRegsInEachCodeByte.size());
            numRegsInEachCodeByte[item.m_byteOffset]++;

            // The 3 target bits in machineCode must be zero'ed out!
            //
            {
                size_t bitOffset = item.m_bitOffset;
                ReleaseAssert(bitOffset <= 5);
                uint8_t mask = static_cast<uint8_t>(255 ^ (7 << bitOffset));
                machineCode[item.m_byteOffset] &= mask;
            }

            numItemsThisChunk++;
            dfg::RuntimeStencilRegRenamingPatchItem pk {
                .byteOffset = item.m_byteOffset % x_bytesPerChunk,
                .bitOffset = item.m_bitOffset,
                .regClass = static_cast<size_t>(item.m_ident.m_class),
                .ordInClass = item.m_ident.m_ord,
                .shouldFlip = item.m_isFlipped
            };
            uint16_t value = pk.Crunch();
            res.push_back(value);
            curIdx++;
        }

        ReleaseAssert(numItemsThisChunkIdx < res.size() && res[numItemsThisChunkIdx] == 0);
        ReleaseAssert(numItemsThisChunk <= std::numeric_limits<uint16_t>::max());
        res[numItemsThisChunkIdx] = static_cast<uint16_t>(numItemsThisChunk);
    }

    ReleaseAssert(curIdx == patches.size());

    ReleaseAssert(res.size() > 0 && res[0] == 0);
    ReleaseAssert(numChunks <= std::numeric_limits<uint16_t>::max());
    res[0] = static_cast<uint16_t>(numChunks);

    // Do a test run just to be extra certain.. There is no safecheck at runtime, and if this data stream
    // is ill-formed it would cause memory corruption bugs that will be very anonying to debug...
    //
    {
        size_t totalChunks = res[0];
        size_t idx = 1;
        size_t totalItems = 0;
        for (size_t curChunk = 0; curChunk < totalChunks; curChunk++)
        {
            ReleaseAssert(idx < res.size());
            size_t numItems = res[idx];
            idx++;
            idx += numItems;
            totalItems += numItems;
        }
        ReleaseAssert(idx == res.size());
        ReleaseAssert(totalItems == patches.size());
    }

    return EncodedStencilRegPatchStream { .m_data = res, .m_numRegsInEachCodeByte = numRegsInEachCodeByte };
}

llvm::GlobalVariable* WARN_UNUSED EncodedStencilRegPatchStream::EmitDataAsLLVMConstantGlobal(llvm::Module* module) const
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();

    ReleaseAssert(!IsEmpty());

    std::vector<Constant*> constants;
    for (uint16_t value : m_data)
    {
        constants.push_back(CreateLLVMConstantInt<uint16_t>(ctx, value));
    }

    ArrayType* aty = ArrayType::get(llvm_type_of<uint16_t>(ctx), m_data.size());
    Constant* ca = ConstantArray::get(aty, constants);

    std::string gvName;
    {
        size_t suffix = 0;
        while (true)
        {
            gvName = "__deegen_dfg_reg_patch_data_stream_" + std::to_string(suffix);
            if (module->getNamedValue(gvName) == nullptr)
            {
                break;
            }
            suffix++;
        }
    }

    ReleaseAssert(gvName != "" && module->getNamedValue(gvName) == nullptr);
    GlobalVariable* gv = new GlobalVariable(*module, aty, true /*isConstant*/, GlobalValue::PrivateLinkage, ca /*initializer*/, gvName);
    ReleaseAssert(gv->getName() == gvName);
    gv->setAlignment(Align(2));
    gv->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

    return gv;
}

std::vector<uint8_t> WARN_UNUSED EncodedStencilRegPatchStream::ApplyOnCode(const std::vector<uint8_t>& code, StencilRegisterFileContext* regCtx) const
{
    std::unordered_map<X64Reg, StencilRegIdent> regMap = regCtx->GetRegPurposeForAllDfgRegisters();

    std::map<std::pair<size_t, size_t>, X64Reg> identMap;
    for (auto& it : regMap)
    {
        X64Reg reg = it.first;
        StencilRegIdent ident = it.second;
        std::pair<size_t, size_t> key = std::make_pair(static_cast<size_t>(ident.m_class), ident.m_ord);
        ReleaseAssert(!identMap.count(key));
        identMap[key] = reg;
    }

    std::vector<uint8_t> resCode = code;

    ReleaseAssert(m_data.size() > 0);
    size_t totalChunks = m_data[0];
    size_t idx = 1;
    size_t codeChunkSizeBytes = dfg::RuntimeStencilRegRenamingPatchItem::x_bytesPerChunk;
    for (size_t curChunk = 0; curChunk < totalChunks; curChunk++)
    {
        ReleaseAssert(idx < m_data.size());
        size_t numItems = m_data[idx];
        idx++;
        for (size_t curItem = 0; curItem < numItems; curItem++)
        {
            ReleaseAssert(idx + curItem < m_data.size());
            uint16_t value = m_data[idx + curItem];

            dfg::RuntimeStencilRegRenamingPatchItem pk = dfg::RuntimeStencilRegRenamingPatchItem::Parse(value);

            std::pair<size_t, size_t> key = std::make_pair(pk.regClass, pk.ordInClass);
            ReleaseAssert(identMap.count(key));
            X64Reg reg = identMap[key];
            ReleaseAssert(IsRegisterUsedForDfgRegAllocation(reg));

            size_t baseOffset = codeChunkSizeBytes * curChunk;
            ReleaseAssert(baseOffset + pk.byteOffset < resCode.size());
            pk.Apply(resCode.data() + baseOffset, reg.MachineOrd() & 7);
        }
        idx += numItems;
    }
    ReleaseAssert(idx == m_data.size());

    return resCode;
}

std::string WARN_UNUSED StencilRegIdent::ToPrettyString()
{
    ReleaseAssert(IsValid());
    switch (m_class)
    {
    case StencilRegIdentClass::Operand:
    {
        return "%operand" + std::to_string(m_ord);
    }
    case StencilRegIdentClass::ScNonExtG:
    {
        return "%scratch.g" + std::to_string(m_ord);
    }
    case StencilRegIdentClass::ScExtG:
    {
        return "%scratch.eg" + std::to_string(m_ord);
    }
    case StencilRegIdentClass::ScF:
    {
        return "%scratch.f" + std::to_string(m_ord);
    }
    case StencilRegIdentClass::PtNonExtG:
    {
        return "%passthru.g" + std::to_string(m_ord);
    }
    case StencilRegIdentClass::PtExtG:
    {
        return "%passthru.eg" + std::to_string(m_ord);
    }
    case StencilRegIdentClass::PtF:
    {
        return "%passthru.f" + std::to_string(m_ord);
    }
    default:
    {
        ReleaseAssert(false);
    }
    }   /*switch*/
}

StencilRegisterFileContextSetupHelper::StencilRegisterFileContextSetupHelper()
{
    m_ctx.reset(new StencilRegisterFileContext());

    // Any register that does not participate in reg alloc doesn't need to be renamed
    //
    for (size_t i = 0; i < m_ctx->x_numGprRegs; i++)
    {
        X64Reg reg = X64Reg::GPR(i);
        if (!IsRegisterUsedForDfgRegAllocation(reg))
        {
            Ctx()->SetRegPurpose(reg, PhyRegPurpose::NoRenaming());
        }
    }
    for (size_t i = 0; i < m_ctx->x_numFprRegs; i++)
    {
        X64Reg reg = X64Reg::FPR(i);
        if (!IsRegisterUsedForDfgRegAllocation(reg))
        {
            Ctx()->SetRegPurpose(reg, PhyRegPurpose::NoRenaming());
        }
    }

    m_freeRegLists.resize(static_cast<size_t>(RegClass::X_END_OF_ENUM));

    // The lists of available registers for allocation
    //
    // 0 = GPR group1, 1 = GPR group2, 2 = FPR
    //
    ForEachDfgRegAllocGPR(
        [&](X64Reg reg)
        {
            if (reg.MachineOrd() < 8)
            {
                GetFreeRegList(RegClass::GprGroup1).push_back(reg);
            }
            else
            {
                GetFreeRegList(RegClass::GprGroup2).push_back(reg);
            }
        });
    ForEachDfgRegAllocFPR(
        [&](X64Reg reg)
        {
            GetFreeRegList(RegClass::Fpr).push_back(reg);
        });


    // We consume register by pop_back, reverse all the lists so that the consume order is same as the listed order
    //
    for (size_t listClass = 0; listClass < static_cast<size_t>(RegClass::X_END_OF_ENUM); listClass++)
    {
        std::vector<X64Reg>& freeList = GetFreeRegList(static_cast<RegClass>(listClass));
        std::reverse(freeList.begin(), freeList.end());
    }
}

std::unique_ptr<StencilRegisterFileContext> WARN_UNUSED StencilRegisterFileContextSetupHelper::FinalizeAndGet()
{
    ReleaseAssert(m_ctx.get() != nullptr);

    // Set all remaining registers as scratch registers
    //
    for (size_t listClass = 0; listClass < static_cast<size_t>(RegClass::X_END_OF_ENUM); listClass++)
    {
        while (HasFreeRegInList(static_cast<RegClass>(listClass)))
        {
            ConsumeFromRegList(static_cast<RegClass>(listClass), PhyRegPurpose::Scratch());
        }
    }

    Ctx()->Finalize();

    // For sanity, validate that everything has been set up properly
    //
    ForEachX64Register([&](X64Reg reg) {
        bool isRegAllocReg = IsRegisterUsedForDfgRegAllocation(reg);
        PhyRegUseKind purpose = Ctx()->GetPhyRegPurpose(reg).Kind();
        ReleaseAssert(purpose != PhyRegUseKind::X_END_OF_ENUM);
        ReleaseAssertImp(!isRegAllocReg, purpose == PhyRegUseKind::NoRenaming);
        ReleaseAssertImp(isRegAllocReg, purpose == PhyRegUseKind::OperandUse || purpose == PhyRegUseKind::ScratchUse || purpose == PhyRegUseKind::PassThruUse);
    });

    for (auto& list : m_freeRegLists) { ReleaseAssert(list.empty()); }

    return std::move(m_ctx);
}

}   // namespace dast
