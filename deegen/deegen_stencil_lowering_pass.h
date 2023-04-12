#pragma once

#include "common_utils.h"
#include "misc_llvm_helper.h"

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
class DeegenStencilLoweringPass
{
public:
    // Run the LLVM IR phase (IR -> IR transformation)
    // 'fallthroughPlaceholderName' is optionally the placeholder name that represents the fallthrough function
    // for the next bytecode. If provided, we will attempt to optimize the code so that the code can fallthrough
    // (instead of jump) to the next bytecode. Pass "" if such rewrite is not desired.
    //
    static void RunIrRewritePhase(llvm::Function* f, const std::string& fallthroughPlaceholderName);

    // Run the ASM phase (ASM -> ASM transformation), returns the transformed ASM file
    //
    static std::string WARN_UNUSED RunAsmRewritePhase(const std::string& asmFile, const std::string& funcName);

    // Return the start offset of the hot-cold splitting barrier in the machine code
    // [0, offset) is the fast path, and [offset + x_hotColdSplittingBarrierSize, end) is the slow path
    //
    static size_t WARN_UNUSED LocateHotColdSplittingBarrier(const std::string& machineCode);

    static constexpr size_t x_hotColdSplittingBarrierSize = 160;
};

}   // namespace dast
