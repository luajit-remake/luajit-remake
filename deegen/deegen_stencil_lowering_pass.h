#pragma once

#include "common_utils.h"
#include "misc_llvm_helper.h"
#include "deegen_parse_asm_text.h"

namespace dast {

// This pass transforms an LLVM function so that it can be compiled and back-parsed into a copy-and-patch stencil.
//
// It does the following transformation:
// 1. Split the function into two parts: the fast path and the slow path.
//    It uses BlockFrequencyInfo to identify the slow path logic and adds proper annotations to the LLVM IR,
//    so that it can identify and split out the slow path in the generated assembly later.
// 2. A simple hueristic that transforms the assembly so that the function fallthroughs to the next bytecode,
//    if such transformation might be beneficial.
//
// The pass has two phases: one phase is executed at LLVM IR level (IR -> IR transformation), and one phase
// is executed at ASM (.s file) level (ASM -> ASM transformation).
//
// Note that the IR-level rewrite pass must be executed right before the LLVM module is compiled to assembly.
// After it, no further transformation to the LLVM IR is allowed.
//
class DeegenStencilLoweringPass
{
public:
    // Run the LLVM IR phase (IR -> IR transformation)
    // 'fallthroughPlaceholderName' is optionally the placeholder name that represents the fallthrough function
    // for the next bytecode. If provided, we will attempt to optimize the code so that the code can fallthrough
    // (instead of jump) to the next bytecode. Pass "" if such rewrite is not desired.
    //
    static DeegenStencilLoweringPass WARN_UNUSED RunIrRewritePhase(llvm::Function* f, const std::string& fallthroughPlaceholderName);

    // Run the ASM phase (ASM -> ASM transformation), returns the transformed ASM file
    //
    void RunAsmRewritePhase(const std::string& asmFile);

    InjectedMagicDiLocationInfo m_diInfo;
    std::vector<llvm::BasicBlock*> m_coldBlocks;
    uint32_t m_locIdentForJmpToFallthroughCandidate;
    std::string m_nextBytecodeFallthroughPlaceholderName;

    // The rawly-parsed input ASM file without any transformation (except the parser-applied ones) applied yet, for audit and debug purpose only
    //
    std::unique_ptr<X64AsmFile> m_rawInputFileForAudit;

    struct CallIcAsmInfo
    {
        // Assembly files for the extracted DirectCall and ClosureCall IC logic
        //
        std::string m_directCallLogicAsm;
        std::string m_closureCallLogicAsm;
        // The label for the self-modifying code
        // The length of the region is measured in the primary assembly file
        //
        std::string m_smcBlockLabel;
        // The label for the IC miss slow path
        // Its offset from slow path start is measured in the primary assembly file
        //
        std::string m_ccIcMissPathLabel;
        std::string m_dcIcMissPathLabel;
    };

    std::string m_primaryPostTransformAsmFile;
    std::map<uint64_t /*callIcUniqId*/, CallIcAsmInfo> m_callIcLogicAsmFiles;
};

}   // namespace dast
