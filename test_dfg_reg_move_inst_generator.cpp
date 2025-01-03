#include "gtest/gtest.h"

#include "test_util_helper.h"
#include "drt/dfg_reg_move_inst_generator.h"
#include "misc_llvm_helper.h"

#include "zydis/zydis.h"

using namespace dfg;
using namespace dast;

namespace {

std::vector<X64Reg> GetGprRegList()
{
    std::vector<X64Reg> res;
    for (size_t idx = 0; idx < 16; idx++) { res.push_back(X64Reg::GPR(idx)); }
    return res;
}

std::vector<X64Reg> GetFprFirst8RegList()
{
    std::vector<X64Reg> res;
    for (size_t idx = 0; idx < 8; idx++) { res.push_back(X64Reg::FPR(idx)); }
    return res;
}

std::vector<X64Reg> GetRegListForTest()
{
    std::vector<X64Reg> regs = GetGprRegList();
    std::vector<X64Reg> fprs = GetFprFirst8RegList();
    regs.insert(regs.end(), fprs.begin(), fprs.end());
    return regs;
}

std::string FormatInstructionATT(std::vector<uint8_t>& inst, size_t length)
{
    ReleaseAssert(length <= inst.size());

    ZydisFormatter formatter;
    ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_ATT);

    ZydisFormatterSetProperty(&formatter, ZYDIS_FORMATTER_PROP_DISP_BASE, ZYDIS_NUMERIC_BASE_DEC);
    ZydisFormatterSetProperty(&formatter, ZYDIS_FORMATTER_PROP_DISP_PADDING, ZYDIS_PADDING_DISABLED);

    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    ReleaseAssert(ZYAN_SUCCESS(ZydisDecoderDecodeFull(
        &decoder, inst.data(), length, &instruction, operands)));

    ReleaseAssert(instruction.length == length);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"

    char buffer[256];
    memset(buffer, 0, sizeof(buffer));
    ReleaseAssert(ZYAN_SUCCESS(ZydisFormatterFormatInstruction(
        &formatter, &instruction, operands, instruction.operand_count_visible, buffer, sizeof(buffer), ZYDIS_RUNTIME_ADDRESS_NONE, nullptr)));

#pragma clang diagnostic pop

    return std::string(buffer);
}

TEST(DfgRegMoveInstGen, RegRegMove)
{
    std::vector<X64Reg> regs = GetRegListForTest();
    for (X64Reg srcReg : regs)
    {
        for (X64Reg dstReg : regs)
        {
            std::vector<uint8_t> inst;
            inst.resize(100, 0 /*value*/);

            uint8_t* instEnd = inst.data();
            EmitRegisterRegisterMoveInst(instEnd /*inout*/, srcReg, dstReg);

            ReleaseAssert(instEnd > inst.data());
            size_t instLen = SafeIntegerCast<size_t>(instEnd - inst.data());

            ReleaseAssert(instLen == GetRegisterMoveInstructionLength(srcReg, dstReg));

            std::string instStr = FormatInstructionATT(inst, instLen);

            std::string instKeyword;
            if (srcReg.IsGPR())
            {
                if (dstReg.IsGPR()) { instKeyword = "mov"; } else { instKeyword = "movq"; }
            }
            else
            {
                if (dstReg.IsGPR()) { instKeyword = "movq"; } else { instKeyword = "movaps"; }
            }

            std::string expectedInst = instKeyword + " %" + srcReg.GetName() + ", %" + dstReg.GetName();
            expectedInst = ConvertStringToLowerCase(expectedInst);

            if (expectedInst != instStr)
            {
                fprintf(stderr, "Reg move %s -> %s, expected '%s', got '%s'\n",
                        srcReg.GetName(), dstReg.GetName(), expectedInst.c_str(), instStr.c_str());
                abort();
            }
        }
    }
}

TEST(DfgRegMoveInstGen, RegSpillAndLoad)
{
    std::vector<X64Reg> regs = GetRegListForTest();
    std::vector<X64Reg> baseRegs = GetGprRegList();
    std::vector<uint32_t> offsetsForTest { 1, 8, 16, 32, 64, 89, 122, 128, 233, 12345, 12345678, 0x7fffffff };

    for (X64Reg srcReg : regs)
    {
        for (uint32_t offsetBytes : offsetsForTest)
        {
            std::vector<uint8_t> inst;
            inst.resize(100, 0 /*value*/);

            uint8_t* instEnd = inst.data();
            EmitRegisterSpillToStackInst(instEnd /*inout*/, srcReg, offsetBytes);

            ReleaseAssert(instEnd > inst.data());
            size_t instLen = SafeIntegerCast<size_t>(instEnd - inst.data());

            ReleaseAssert(instLen == GetRegisterSpillOrLoadInstructionLength(srcReg, offsetBytes));

            std::string instStr = FormatInstructionATT(inst, instLen);

            std::string instKeyword;
            if (srcReg.IsGPR()) { instKeyword = "mov"; } else { instKeyword = "movsd"; }

            std::string expectedInst = instKeyword + " %" + srcReg.GetName() + ", " +
                std::to_string(offsetBytes) + "(%" + x_dfg_stack_base_register.GetName() + ")";

            expectedInst = ConvertStringToLowerCase(expectedInst);

            if (expectedInst != instStr)
            {
                fprintf(stderr, "Reg spill %s -> stk @ %d, expected '%s', got '%s'\n",
                        srcReg.GetName(), static_cast<int>(offsetBytes), expectedInst.c_str(), instStr.c_str());
                abort();
            }
        }
    }

    for (X64Reg reg : regs)
    {
        for (X64Reg baseReg : baseRegs)
        {
            if (baseReg == X64Reg::RSP || baseReg == X64Reg::R12)
            {
                continue;
            }
            for (uint32_t offsetBytes : offsetsForTest)
            {
                std::vector<uint8_t> inst;
                inst.resize(100, 0 /*value*/);

                uint8_t* instEnd = inst.data();
                EmitRegisterStoreToMemBaseOffsetInstruction(instEnd /*inout*/, reg, baseReg, offsetBytes);

                ReleaseAssert(instEnd > inst.data());
                size_t instLen = SafeIntegerCast<size_t>(instEnd - inst.data());

                ReleaseAssert(instLen == GetRegisterStoreToMemBaseOffsetInstLength(reg, baseReg, offsetBytes));

                std::string instStr = FormatInstructionATT(inst, instLen);

                std::string instKeyword;
                if (reg.IsGPR()) { instKeyword = "mov"; } else { instKeyword = "movsd"; }

                std::string expectedInst = instKeyword + " %" + reg.GetName() + ", " +
                    std::to_string(offsetBytes) + "(%" + baseReg.GetName() + ")";

                expectedInst = ConvertStringToLowerCase(expectedInst);

                if (expectedInst != instStr)
                {
                    fprintf(stderr, "Reg spill %s -> %s @ %d, expected '%s', got '%s'\n",
                            reg.GetName(), baseReg.GetName(), static_cast<int>(offsetBytes), expectedInst.c_str(), instStr.c_str());
                    abort();
                }
            }
        }
    }

    for (X64Reg dstReg : regs)
    {
        for (uint32_t offsetBytes : offsetsForTest)
        {
            std::vector<uint8_t> inst;
            inst.resize(100, 0 /*value*/);

            uint8_t* instEnd = inst.data();
            EmitRegisterLoadFromStackInst(instEnd /*inout*/, dstReg, offsetBytes);

            ReleaseAssert(instEnd > inst.data());
            size_t instLen = SafeIntegerCast<size_t>(instEnd - inst.data());

            ReleaseAssert(instLen == GetRegisterSpillOrLoadInstructionLength(dstReg, offsetBytes));

            std::string instStr = FormatInstructionATT(inst, instLen);

            std::string instKeyword;
            if (dstReg.IsGPR()) { instKeyword = "mov"; } else { instKeyword = "movsdq"; }

            std::string expectedInst = instKeyword + " " + std::to_string(offsetBytes) + "(%" + x_dfg_stack_base_register.GetName()
                + "), %" + dstReg.GetName();

            expectedInst = ConvertStringToLowerCase(expectedInst);

            if (expectedInst != instStr)
            {
                fprintf(stderr, "Reg load stk @ %d -> %s, expected '%s', got '%s'\n",
                        static_cast<int>(offsetBytes), dstReg.GetName(), expectedInst.c_str(), instStr.c_str());
                abort();
            }
        }
    }

    for (X64Reg reg : regs)
    {
        for (X64Reg baseReg : baseRegs)
        {
            if (baseReg == X64Reg::RSP || baseReg == X64Reg::R12)
            {
                continue;
            }
            for (uint32_t offsetBytes : offsetsForTest)
            {
                std::vector<uint8_t> inst;
                inst.resize(100, 0 /*value*/);

                uint8_t* instEnd = inst.data();
                EmitRegisterLoadFromMemBaseOffsetInstruction(instEnd /*inout*/, reg, baseReg, offsetBytes);

                ReleaseAssert(instEnd > inst.data());
                size_t instLen = SafeIntegerCast<size_t>(instEnd - inst.data());

                ReleaseAssert(instLen == GetRegisterLoadFromMemBaseOffsetInstLength(reg, baseReg, offsetBytes));

                std::string instStr = FormatInstructionATT(inst, instLen);

                std::string instKeyword;
                if (reg.IsGPR()) { instKeyword = "mov"; } else { instKeyword = "movsdq"; }

                std::string expectedInst = instKeyword + " " + std::to_string(offsetBytes) + "(%" + baseReg.GetName()
                    + "), %" + reg.GetName();

                expectedInst = ConvertStringToLowerCase(expectedInst);

                if (expectedInst != instStr)
                {
                    fprintf(stderr, "Reg load %s @ %d -> %s, expected '%s', got '%s'\n",
                            baseReg.GetName(), static_cast<int>(offsetBytes), reg.GetName(), expectedInst.c_str(), instStr.c_str());
                    abort();
                }
            }
        }
    }
}

}   // anonymous namespace
