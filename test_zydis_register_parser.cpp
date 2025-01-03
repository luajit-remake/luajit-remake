#include "gtest/gtest.h"

#include "zydis/zydis.h"
#include "deegen_jit_register_patch_analyzer.h"

using namespace dast;

TEST(ZydisRegParser, Sanity_1)
{
    constexpr uint8_t code[] = {
        0x89, 0xF0,                                     // mov    eax,esi
        0x44, 0x89, 0xC0,                               // mov    eax,r8d
        0x44, 0x89, 0xC6,                               // mov    esi,r8d
        0x0F, 0xB6, 0xF5,                               // movzx  esi,ch
        0x48, 0x89, 0xFE,                               // mov    rsi,rdi
        0x4C, 0x89, 0xDD,                               // mov    rbp,r11
        0x66, 0x48, 0x0F, 0x7E, 0xCD,                   // movq   rbp,xmm1
        0x66, 0x48, 0x0F, 0x6E, 0xCD,                   // movq   xmm1,rbp
        0x66, 0x49, 0x0F, 0x7E, 0xCB,                   // movq   r11,xmm1
        0x66, 0x49, 0x0F, 0x6E, 0xCB,                   // movq   xmm1,r11
        0x43, 0x8A, 0x74, 0x83, 0x7B,                   // mov    sil,BYTE PTR [r11+r8*4+0x7b]
        0x0F, 0x29, 0x4C, 0x75, 0x00,                   // movaps XMMWORD PTR [rbp+rsi*2+0x0],xmm1 (disp8)
        0x0F, 0x29, 0x8C, 0x75, 0x00, 0x00, 0x00, 0x00, // movaps XMMWORD PTR [rbp+rsi*2+0x0],xmm1 (disp32)
        0x0F, 0x29, 0x0C, 0x6E,                         // movaps XMMWORD PTR [rsi+rbp*2],xmm1
        0x0F, 0x29, 0x4C, 0x6E, 0x01,                   // movaps XMMWORD PTR [rsi+rbp*2+0x1],xmm1 (disp8)
        0x0F, 0x29, 0x8C, 0x6E, 0x01, 0x00, 0x00, 0x00  // movaps XMMWORD PTR [rsi+rbp*2+0x1],xmm1 (disp32)

    };
    constexpr size_t codeLen = std::extent_v<decltype(code)>;

    StencilRegisterFileContext ctx;
    ctx.SetRegPurpose(X64Reg::RAX, PhyRegPurpose::NoRenaming());
    ctx.SetRegPurpose(X64Reg::RCX, PhyRegPurpose::NoRenaming());
    ctx.SetRegPurpose(X64Reg::RDX, PhyRegPurpose::NoRenaming());
    ctx.SetRegPurpose(X64Reg::RBX, PhyRegPurpose::NoRenaming());
    ctx.SetRegPurpose(X64Reg::RSP, PhyRegPurpose::NoRenaming());
    ctx.SetRegPurpose(X64Reg::RBP, PhyRegPurpose::NoRenaming());
    ctx.SetRegPurpose(X64Reg::RSI, PhyRegPurpose::Operand(0));
    ctx.SetRegPurpose(X64Reg::RDI, PhyRegPurpose::Operand(1));
    ctx.SetRegPurpose(X64Reg::R8, PhyRegPurpose::Operand(2));
    ctx.SetRegPurpose(X64Reg::R9, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::R10, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::R11, PhyRegPurpose::Operand(3));
    ctx.SetRegPurpose(X64Reg::R12, PhyRegPurpose::NoRenaming());
    ctx.SetRegPurpose(X64Reg::R13, PhyRegPurpose::NoRenaming());
    ctx.SetRegPurpose(X64Reg::R14, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::R15, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::XMM0, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::XMM1, PhyRegPurpose::Operand(4));
    ctx.SetRegPurpose(X64Reg::XMM2, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::XMM3, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::XMM4, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::XMM5, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::XMM6, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::XMM7, PhyRegPurpose::Scratch());
    ctx.Finalize();

    StencilRegRenameParseResult res;
    std::vector<uint8_t> codeVec { code, code + codeLen };
    res.Parse(ctx, codeVec);

    ReleaseAssert(!res.m_hasIndirectCall);
    ReleaseAssert(res.m_directCallTargetOffsets.size() == 0);

    ReleaseAssert(res.m_usedFpuRegs == 2);  // XMM1 only

    std::vector<size_t> regUseCnt;
    regUseCnt.resize(8);
    for (auto& item : res.m_patches)
    {
        ReleaseAssert(item.m_ident.m_class == StencilRegIdentClass::Operand);
        ReleaseAssert(item.m_ident.m_ord < 5);
        regUseCnt[item.m_ident.m_ord]++;
    }

    ReleaseAssert(regUseCnt[0] == 10);
    ReleaseAssert(regUseCnt[1] == 1);
    ReleaseAssert(regUseCnt[2] == 3);
    ReleaseAssert(regUseCnt[3] == 4);
    ReleaseAssert(regUseCnt[4] == 9);

    // Remapping:
    // RSI -> RDI
    // RDI -> RSI
    // R8 -> R11
    // R11 -> R14
    // XMM1 -> XMM7
    //

    std::vector<size_t> remapping;
    remapping.push_back(X64Reg::RDI.MachineOrd() & 7);
    remapping.push_back(X64Reg::RSI.MachineOrd() & 7);
    remapping.push_back(X64Reg::R11.MachineOrd() & 7);
    remapping.push_back(X64Reg::R14.MachineOrd() & 7);
    remapping.push_back(X64Reg::XMM7.MachineOrd() & 7);

    for (auto& item : res.m_patches)
    {
        ReleaseAssert(item.m_ident.m_ord < remapping.size());
        size_t newVal = remapping[item.m_ident.m_ord];
        if (item.m_isFlipped) { newVal ^= 7; }

        ReleaseAssert(item.m_byteOffset < codeVec.size());
        uint8_t& val = codeVec[item.m_byteOffset];
        ReleaseAssert(item.m_bitOffset < 6);
        val &= (255 ^ (7 << item.m_bitOffset));
        val |= static_cast<uint8_t>(newVal << item.m_bitOffset);
    }

    constexpr uint8_t expectedResult[] = {
        0x89, 0xF8,                                     // mov    eax,edi
        0x44, 0x89, 0xD8,                               // mov    eax,r11d
        0x44, 0x89, 0xDF,                               // mov    edi,r11d
        0x0F, 0xB6, 0xFD,                               // movzx  edi,ch
        0x48, 0x89, 0xF7,                               // mov    rdi,rsi
        0x4C, 0x89, 0xF5,                               // mov    rbp,r14
        0x66, 0x48, 0x0F, 0x7E, 0xFD,                   // movq   rbp,xmm7
        0x66, 0x48, 0x0F, 0x6E, 0xFD,                   // movq   xmm7,rbp
        0x66, 0x49, 0x0F, 0x7E, 0xFE,                   // movq   r14,xmm7
        0x66, 0x49, 0x0F, 0x6E, 0xFE,                   // movq   xmm7,r14
        0x43, 0x8A, 0x7C, 0x9E, 0x7B,                   // mov    dil,BYTE PTR [r14+r11*4+0x7b]
        0x0F, 0x29, 0x7C, 0x7D, 0x00,                   // movaps XMMWORD PTR [rbp+rdi*2+0x0],xmm7
        0x0F, 0x29, 0xBC, 0x7D, 0x00, 0x00, 0x00, 0x00, // movaps XMMWORD PTR [rbp+rdi*2+0x0],xmm7
        0x0F, 0x29, 0x3C, 0x6F,                         // movaps XMMWORD PTR [rdi+rbp*2],xmm7
        0x0F, 0x29, 0x7C, 0x6F, 0x01,                   // movaps XMMWORD PTR [rdi+rbp*2+0x1],xmm7
        0x0F, 0x29, 0xBC, 0x6F, 0x01, 0x00, 0x00, 0x00  // movaps XMMWORD PTR [rdi+rbp*2+0x1],xmm7
    };

    ReleaseAssert(codeVec.size() == std::extent_v<decltype(expectedResult)>);
    for (size_t i = 0; i < codeVec.size(); i++)
    {
        ReleaseAssert(codeVec[i] == expectedResult[i]);
    }
}

TEST(ZydisRegParser, Sanity_2)
{
    constexpr uint8_t code[] = {
        0x89, 0xF0,                                     // mov    eax,esi
        0x44, 0x89, 0xC0,                               // mov    eax,r8d
        0x44, 0x89, 0xC6,                               // mov    esi,r8d
        0x0F, 0xB6, 0xF5,                               // movzx  esi,ch
        0x48, 0x89, 0xFE,                               // mov    rsi,rdi
        0x4C, 0x89, 0xDD,                               // mov    rbp,r11
        0x66, 0x48, 0x0F, 0x7E, 0xCD,                   // movq   rbp,xmm1
        0x66, 0x48, 0x0F, 0x6E, 0xCD,                   // movq   xmm1,rbp
        0x66, 0x49, 0x0F, 0x7E, 0xCB,                   // movq   r11,xmm1
        0x66, 0x49, 0x0F, 0x6E, 0xCB,                   // movq   xmm1,r11
        0x43, 0x8A, 0x74, 0x83, 0x7B,                   // mov    sil,BYTE PTR [r11+r8*4+0x7b]
        0x0F, 0x29, 0x4C, 0x75, 0x00,                   // movaps XMMWORD PTR [rbp+rsi*2+0x0],xmm1 (disp8)
        0x0F, 0x29, 0x8C, 0x75, 0x00, 0x00, 0x00, 0x00, // movaps XMMWORD PTR [rbp+rsi*2+0x0],xmm1 (disp32)
        0x0F, 0x29, 0x0C, 0x6E,                         // movaps XMMWORD PTR [rsi+rbp*2],xmm1
        0x0F, 0x29, 0x4C, 0x6E, 0x01,                   // movaps XMMWORD PTR [rsi+rbp*2+0x1],xmm1 (disp8)
        0x0F, 0x29, 0x8C, 0x6E, 0x01, 0x00, 0x00, 0x00  // movaps XMMWORD PTR [rsi+rbp*2+0x1],xmm1 (disp32)

    };
    constexpr size_t codeLen = std::extent_v<decltype(code)>;

    StencilRegisterFileContext ctx;
    ctx.SetRegPurpose(X64Reg::RAX, PhyRegPurpose::NoRenaming());
    ctx.SetRegPurpose(X64Reg::RCX, PhyRegPurpose::NoRenaming());
    ctx.SetRegPurpose(X64Reg::RDX, PhyRegPurpose::NoRenaming());
    ctx.SetRegPurpose(X64Reg::RBX, PhyRegPurpose::NoRenaming());
    ctx.SetRegPurpose(X64Reg::RSP, PhyRegPurpose::NoRenaming());
    ctx.SetRegPurpose(X64Reg::RBP, PhyRegPurpose::NoRenaming());
    ctx.SetRegPurpose(X64Reg::RSI, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::RDI, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::R8, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::R9, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::R10, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::R11, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::R12, PhyRegPurpose::NoRenaming());
    ctx.SetRegPurpose(X64Reg::R13, PhyRegPurpose::NoRenaming());
    ctx.SetRegPurpose(X64Reg::R14, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::R15, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::XMM0, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::XMM1, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::XMM2, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::XMM3, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::XMM4, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::XMM5, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::XMM6, PhyRegPurpose::Scratch());
    ctx.SetRegPurpose(X64Reg::XMM7, PhyRegPurpose::Scratch());
    ctx.Finalize();

    StencilRegRenameParseResult res;
    std::vector<uint8_t> codeVec { code, code + codeLen };
    res.Parse(ctx, codeVec);

    ReleaseAssert(!res.m_hasIndirectCall);
    ReleaseAssert(res.m_directCallTargetOffsets.size() == 0);

    ReleaseAssert(res.m_usedFpuRegs == 2);  // XMM1 only

    std::vector<size_t> regUseCnt1, regUseCnt2;
    regUseCnt1.resize(2);
    regUseCnt2.resize(2);
    size_t regUseCnt3 = 0;
    for (auto& item : res.m_patches)
    {
        switch (item.m_ident.m_class)
        {
        case StencilRegIdentClass::ScNonExtG:
        {
            ReleaseAssert(item.m_ident.m_ord < 2);
            regUseCnt1[item.m_ident.m_ord]++;
            break;
        }
        case StencilRegIdentClass::ScExtG:
        {
            ReleaseAssert(item.m_ident.m_ord < 2);
            regUseCnt2[item.m_ident.m_ord]++;
            break;
        }
        case StencilRegIdentClass::ScF:
        {
            ReleaseAssert(item.m_ident.m_ord == 0);
            regUseCnt3++;
            break;
        }
        default:
        {
            ReleaseAssert(false);
        }
        }   /*switch*/
    }

    ReleaseAssert((regUseCnt1[0] == 10 && regUseCnt1[1] == 1) || (regUseCnt1[0] == 1 && regUseCnt1[1] == 10));
    ReleaseAssert((regUseCnt2[0] == 3 && regUseCnt2[1] == 4) || (regUseCnt2[0] == 4 && regUseCnt2[1] == 3));
    ReleaseAssert(regUseCnt3 == 9);
}

TEST(ZydisRegParser, EdgeCasePatch)
{
    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    // The following two instructions are equivalent:
    //     65 48 8b 04 25 00 00 00 40   mov %gs:0x40000000,%rax
    //     65 67 48 a1 00 00 00 40      addr32 mov %gs:0x40000000,%rax
    // but the second version is 1 byte shorter.
    //
    // It turns out that LLVM is not smart enough to generate the second version, and will always use the first version.
    // But.. Zydis **is** smart enough to rewrite the first version to the second version!
    // which.. unfortunately breaks our analysis and fires assertion.
    //
    // So as a workaround, we "patched" Zydis by adding an "opcode hint" to hint it not use the second version.
    // This test is just here to be sure that our "patch" is working as intended.
    //
    uint8_t bytes[ZYDIS_MAX_INSTRUCTION_LENGTH] = { 0x65, 0x48, 0x8B, 0x04, 0x25, 0x00, 0x00, 0x00, 0x40 };
    size_t numBytes = 9;

    ZydisDecodedInstruction instr;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    ZydisDecoderDecodeFull(&decoder, bytes, numBytes, &instr, operands);

    ZydisEncoderRequest enc_req;
    ZydisEncoderDecodedInstructionToEncoderRequest(&instr, operands, instr.operand_count_visible, &enc_req);

    enc_req.has_opcode_hint = ZYAN_TRUE;
    enc_req.opcode_hint = instr.opcode;
    enc_req.opcode_map_hint = instr.opcode_map;

    uint8_t new_bytes[ZYDIS_MAX_INSTRUCTION_LENGTH];
    ZyanUSize new_instr_length = sizeof(new_bytes);
    ZydisEncoderEncodeInstruction(&enc_req, new_bytes, &new_instr_length);

    ReleaseAssert(new_instr_length == numBytes);
    ReleaseAssert(memcmp(bytes, new_bytes, numBytes) == 0);
}
