#pragma once

#include "common.h"

namespace dast {

enum class MagicAsmKind
{
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
inline std::string WARN_UNUSED WrapLLVMAsmStringWithMagicPattern(const std::string& llvmAsmStr, MagicAsmKind magicKind)
{
    ReleaseAssert(static_cast<uint32_t>(magicKind) < static_cast<uint32_t>(MagicAsmKind::X_END_OF_ENUM));
    uint32_t val = 100 + static_cast<uint32_t>(magicKind);
    std::string strVal = std::to_string(val);
    std::string prefix =  "hlt;int $$" + strVal + ";";
    std::string suffix = "int $$" + strVal + ";hlt;";
    return prefix + llvmAsmStr + suffix;
}

}   // namespace dast
