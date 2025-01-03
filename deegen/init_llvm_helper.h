#pragma once

#include "llvm/InitializePasses.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"

inline void LLVMInitializeEverything()
{
    using namespace llvm;

    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmPrinters();
    InitializeAllAsmParsers();
    InitializeAllDisassemblers();

    // Copied from llvm/tools/opt/optdriver.cpp, no idea what these are...
    //
    // Initialize passes
    PassRegistry &Registry = *PassRegistry::getPassRegistry();
    initializeCore(Registry);
    initializeScalarOpts(Registry);
    initializeVectorization(Registry);
    initializeIPO(Registry);
    initializeAnalysis(Registry);
    initializeTransformUtils(Registry);
    initializeInstCombine(Registry);
    initializeTarget(Registry);
    // For codegen passes, only passes that do IR to IR transformation are
    // supported.
    initializeExpandLargeDivRemLegacyPassPass(Registry);
    initializeExpandLargeFpConvertLegacyPassPass(Registry);
    initializeExpandMemCmpLegacyPassPass(Registry);
    initializeScalarizeMaskedMemIntrinLegacyPassPass(Registry);
    initializeSelectOptimizePass(Registry);
    initializeCallBrPreparePass(Registry);
    initializeCodeGenPrepareLegacyPassPass(Registry);
    initializeAtomicExpandLegacyPass(Registry);
    initializeWinEHPreparePass(Registry);
    initializeDwarfEHPrepareLegacyPassPass(Registry);
    initializeSafeStackLegacyPassPass(Registry);
    initializeSjLjEHPreparePass(Registry);
    initializePreISelIntrinsicLoweringLegacyPassPass(Registry);
    initializeGlobalMergePass(Registry);
    initializeIndirectBrExpandLegacyPassPass(Registry);
    initializeInterleavedLoadCombinePass(Registry);
    initializeInterleavedAccessPass(Registry);
    initializePostInlineEntryExitInstrumenterPass(Registry);
    initializeUnreachableBlockElimLegacyPassPass(Registry);
    initializeExpandReductionsPass(Registry);
    initializeWasmEHPreparePass(Registry);
    initializeWriteBitcodePassPass(Registry);
    initializeReplaceWithVeclibLegacyPass(Registry);
    initializeJMCInstrumenterPass(Registry);
}
