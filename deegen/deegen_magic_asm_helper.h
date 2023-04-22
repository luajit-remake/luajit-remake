#pragma once

#include "common.h"
#include "misc_llvm_helper.h"
#include "llvm/IR/InlineAsm.h"

namespace dast {

enum class MagicAsmKind
{
    IndirectBrMarkerForCfgRecovery,
    CallIcDirectCall,
    CallIcClosureCall,
    X_END_OF_ENUM
};
static_assert(static_cast<uint32_t>(MagicAsmKind::X_END_OF_ENUM) <= 155);     // int $XXX can only go up to 255

// All ASM magic are required to have the following pattern:
//     hlt
//     int $XXX   # XXX := 100 + magicKind to avoid int3
//     <opaque instruction sequence>
//     int $XXX
//     hlt
//
// This allows us to reliably identify the pattern from the assembly code.
//
struct MagicAsm
{
    static std::string WARN_UNUSED WrapLLVMAsmPayload(const std::string& llvmAsmStr, MagicAsmKind magicKind)
    {
        ReleaseAssert(static_cast<uint32_t>(magicKind) < static_cast<uint32_t>(MagicAsmKind::X_END_OF_ENUM));
        uint32_t val = 100 + static_cast<uint32_t>(magicKind);
        std::string strVal = std::to_string(val);
        std::string prefix =  "hlt;int $$" + strVal + ";";
        std::string suffix = "int $$" + strVal + ";hlt;";
        return prefix + llvmAsmStr + suffix;
    }

    static bool WARN_UNUSED IsMagic(llvm::Instruction* inst, MagicAsmKind& magicKind /*out*/, std::string& magicStr /*out*/)
    {
        using namespace llvm;
        CallInst* ci = dyn_cast<CallInst>(inst);
        if (ci == nullptr)
        {
            return false;
        }
        InlineAsm* ia = dyn_cast<InlineAsm>(ci->getCalledOperand());
        if (ia == nullptr)
        {
            return false;
        }
        std::string asmStr = ia->getAsmString();
        if (!asmStr.starts_with("hlt;int $$"))
        {
            return false;
        }
        asmStr = asmStr.substr(strlen("hlt;int $$"));
        size_t loc = asmStr.find(";");
        ReleaseAssert(loc != std::string::npos);
        ReleaseAssert(loc > 0);

        std::string magicKindStr = asmStr.substr(0, loc);
        int val = StoiOrFail(magicKindStr);
        ReleaseAssert(100 <= val && val < 100 + static_cast<int>(MagicAsmKind::X_END_OF_ENUM));
        magicKind = static_cast<MagicAsmKind>(val - 100);

        asmStr = asmStr.substr(loc + 1);

        std::string expectedSuffix = "int $$" + magicKindStr + ";hlt;";
        ReleaseAssert(asmStr.ends_with(expectedSuffix));
        asmStr = asmStr.substr(0, asmStr.length() - expectedSuffix.length());
        magicStr = asmStr;
        return true;
    }

    static bool WARN_UNUSED IsMagic(llvm::Instruction* inst)
    {
        MagicAsmKind magicKind;
        std::string magicStr;
        return IsMagic(inst, magicKind /*out, unused*/, magicStr /*out, unused*/);
    }

    static MagicAsmKind WARN_UNUSED GetKind(llvm::Instruction* inst)
    {
        MagicAsmKind magicKind;
        std::string magicStr;
        ReleaseAssert(IsMagic(inst, magicKind /*out*/, magicStr /*out, unused*/));
        return magicKind;
    }

    static std::string WARN_UNUSED GetPayload(llvm::Instruction* inst)
    {
        MagicAsmKind magicKind;
        std::string magicStr;
        ReleaseAssert(IsMagic(inst, magicKind /*out, unused*/, magicStr /*out*/));
        return magicStr;
    }
};

}   // namespace dast
