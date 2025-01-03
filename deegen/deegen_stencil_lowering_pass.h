#pragma once

#include "common_utils.h"
#include "misc_llvm_helper.h"
#include "deegen_ast_inline_cache.h"
#include "deegen_parse_asm_text.h"
#include "deegen_call_inline_cache.h"

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
    DeegenStencilLoweringPass() = default;

public:
    // Run the LLVM IR phase (IR -> IR transformation)
    //
    // If 'isLastStencilInBytecode' is true, 'fallthroughPlaceholderName' may be provided to tell us the placeholder name that
    // represents the fallthrough function for the next bytecode.
    // In this case, we will attempt to optimize the code so that the code can fallthrough (instead of jump) to the next bytecode,
    // and optimize the other branches to the fallthrough bytecode to use short (imm8) branch if possible.
    // If 'isLastStencilInBytecode' is false, 'fallthroughPlaceholderName' is not useful.
    //
    static DeegenStencilLoweringPass WARN_UNUSED RunIrRewritePhase(
        llvm::Function* f, bool isLastStencilInBytecode, const std::string& fallthroughPlaceholderName);

    // Parse ASM file into m_workFile
    //
    void ParseAsmFile(const std::string& asmFile);

    // Run the ASM phase (ASM -> ASM transformation) on m_workFile, this populates the relavent fields in this struct
    //
    void RunAsmRewritePhase();

    InjectedMagicDiLocationInfo m_diInfo;
    std::vector<llvm::BasicBlock*> m_coldBlocks;
    bool m_isLastStencilInBytecode;
    bool m_shouldAssertNoGenericIcWithInlineSlab;
    uint32_t m_locIdentForJmpToFallthroughCandidate;
    std::string m_nextBytecodeFallthroughPlaceholderName;

    // The rawly-parsed input ASM file without any transformation (except the parser-applied ones) applied yet, for audit and debug purpose only
    //
    std::unique_ptr<X64AsmFile> m_rawInputFileForAudit;

    std::unique_ptr<X64AsmFile> m_workFile;

    std::string m_primaryPostTransformAsmFile;
    std::vector<DeegenCallIcLogicCreator::JitAsmLoweringResult> m_callIcLoweringResults;
    std::vector<AstInlineCache::JitAsmLoweringResult> m_genericIcLoweringResults;

    // Assorted metrics used by caller
    //
    size_t m_numInstructionsInFastPath;
    size_t m_numInstructionsInSlowPath;
    size_t m_numStackOperationsInFastPath;
    size_t m_numStackOperationsInSlowPath;
    size_t m_numTotalStackOperationsInIc;
    size_t m_numStackPopBeforeTailCallInFastPath;
    size_t m_numStackPopBeforeTailCallInSlowPath;
    size_t m_numTotalStackPopBeforeTailCallInIc;
};

}   // namespace dast
