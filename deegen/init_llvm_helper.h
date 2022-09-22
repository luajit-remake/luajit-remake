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

    PassRegistry& Registry = *PassRegistry::getPassRegistry();
    initializeCore(Registry);
    initializeTransformUtils(Registry);
    initializeScalarOpts(Registry);
    initializeObjCARCOpts(Registry);
    initializeVectorization(Registry);
    initializeInstCombine(Registry);
    initializeAggressiveInstCombine(Registry);
    initializeIPO(Registry);
    initializeInstrumentation(Registry);
    initializeAnalysis(Registry);
    initializeCodeGen(Registry);
    initializeGlobalISel(Registry);
    initializeTarget(Registry);
}
