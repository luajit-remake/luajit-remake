#pragma once

#include "llvm/Support/CommandLine.h"

namespace cl = llvm::cl;

enum FpsCommand
{
    BadFpsCommand,
    FpsCommand_GenerateInterpreterFunctionEntryLogic
};

inline cl::OptionCategory FPSOptions("Control options", "");

inline cl::opt<FpsCommand> cl_mainCommand(
    cl::desc("Main command"),
    cl::values(
        clEnumValN(FpsCommand_GenerateInterpreterFunctionEntryLogic,
                   "generate-interp-fn-entry-logic",
                   "Generate the interpreter function entry logic that fixes up overflowing/insufficient parameters and dispatches to the first bytecode.")
        ),
    cl::init(BadFpsCommand),
    cl::cat(FPSOptions));

inline cl::opt<std::string> cl_headerOutputFilename("hdr-output", cl::desc("The output file name for the generated C++ header"), cl::value_desc("filename"), cl::init(""), cl::cat(FPSOptions));
inline cl::opt<std::string> cl_cppOutputFilename("cpp-output", cl::desc("The output file name for the generated CPP file"), cl::value_desc("filename"), cl::init(""), cl::cat(FPSOptions));
inline cl::opt<std::string> cl_assemblyOutputFilename("asm-output", cl::desc("The output file name for the generated assembly"), cl::value_desc("filename"), cl::init(""), cl::cat(FPSOptions));

void FPS_EmitHeaderFileCommonHeader(FILE* fp);
void FPS_EmitCPPFileCommonHeader(FILE* fp);

// Generate a header file with a function:
//
//     void* WARN_UNUSED GetGuestLanguageFunctionEntryPointForInterpreter(bool takesVarArgs, size_t numFixedParams)
//
// and the '.s' file containing all the runtime logic.
//
void FPS_GenerateInterpreterFunctionEntryLogic();
