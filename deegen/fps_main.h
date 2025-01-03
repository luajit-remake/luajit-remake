#pragma once

#include "llvm/Support/CommandLine.h"
#include "common_utils.h"

namespace cl = llvm::cl;

enum FpsCommand
{
    BadFpsCommand,
    FpsCommand_GenerateInterpreterFunctionEntryLogic,
    FpsCommand_ProcessUserBuiltinLib,
    FpsCommand_ProcessBytecodeDefinitionForInterpreter,
    FpsCommand_GenerateBytecodeBuilderApiHeader,
    FpsCommand_ProcessBytecodeDefinitionForBaselineJit,
    FpsCommand_GenerateBaselineJitDispatchAndBytecodeTraitTable,
    FpsCommand_GenerateBaselineJitFunctionEntryLogic,
    FpsCommand_GenerateBytecodeOpcodeTraitTable,
    FpsCommand_GenerateDfgJitSpecializedBytecodeInfo,
    FpsCommand_GenerateDfgJitBytecodeInfoApiHeader,
    FpsCommand_ProcessDfgJitBuiltinNodes,
    FpsCommand_GenerateDfgJitCCallWrapperStubs,
    FpsCommand_PostProcessLinkImplementations
};

inline cl::OptionCategory FPSOptions("Control options", "");

inline cl::opt<FpsCommand> cl_mainCommand(
    cl::desc("Main command"),
    cl::values(
        clEnumValN(FpsCommand_GenerateInterpreterFunctionEntryLogic,
                   "generate-interp-fn-entry-logic",
                   "Generate the interpreter function entry logic that fixes up overflowing/insufficient parameters and dispatches to the first bytecode.")
      , clEnumValN(FpsCommand_ProcessUserBuiltinLib,
                   "process-user-builtin-lib",
                   "Process a user builtin library source file to lower the Deegen API constructs.")
      , clEnumValN(FpsCommand_ProcessBytecodeDefinitionForInterpreter,
                   "process-bytecode-definition-for-interpreter",
                   "Process a bytecode definition source file for interpreter lowering.")
      , clEnumValN(FpsCommand_GenerateBytecodeBuilderApiHeader,
                   "generate-bytecode-builder-api-header",
                   "Generate the bytecode builder API header file.")
      , clEnumValN(FpsCommand_ProcessBytecodeDefinitionForBaselineJit,
                   "process-bytecode-definition-for-baseline-jit",
                   "Process a bytecode definition source file for baseline JIT lowering.")
      , clEnumValN(FpsCommand_GenerateBaselineJitDispatchAndBytecodeTraitTable,
                   "generate-baseline-jit-dispatch-and-bytecode-trait-table",
                   "Generate the codegen function dispatch table and bytecode trait table for baseline JIT.")
      , clEnumValN(FpsCommand_GenerateBaselineJitFunctionEntryLogic,
                   "generate-baseline-jit-function-entry-logic",
                   "Generate the function entry logic emitter for baseline JIT.")
      , clEnumValN(FpsCommand_GenerateBytecodeOpcodeTraitTable,
                   "generate-bytecode-opcode-trait-table",
                   "Generate the bytecode opcode trait table.")
      , clEnumValN(FpsCommand_GenerateDfgJitSpecializedBytecodeInfo,
                   "generate-dfg-jit-specialized-bytecode-info",
                   "Generate information about the specialized bytecodes (call IC, generic IC, etc) for DFG.")
      , clEnumValN(FpsCommand_GenerateDfgJitBytecodeInfoApiHeader,
                   "generate-dfg-jit-bytecode-info-api-header",
                   "Generate the header file for accessing all the DFG JIT bytecode info.")
      , clEnumValN(FpsCommand_ProcessDfgJitBuiltinNodes,
                   "process-dfg-jit-builtin-nodes",
                   "Generate the code generators for DFG builtin nodes.")
      , clEnumValN(FpsCommand_GenerateDfgJitCCallWrapperStubs,
                   "generate-dfg-jit-c-call-wrapper-stubs",
                   "Generate the assembly file for all the DFG JIT C call wrapper stubs.")
      , clEnumValN(FpsCommand_PostProcessLinkImplementations,
                   "post-process-link-implementations",
                   "Link all the generated logic back into the original bytecode definition file to produce the final implementation.")

    ),
    cl::init(BadFpsCommand),
    cl::cat(FPSOptions));

inline cl::opt<std::string> cl_irInputFilename("ir-input", cl::desc("The input LLVM IR file name"), cl::value_desc("filename"), cl::init(""), cl::cat(FPSOptions));
inline cl::opt<std::string> cl_inputListFilenames("input-list", cl::desc("A comma-separated list of input files"), cl::value_desc("filenames"), cl::init(""), cl::cat(FPSOptions));
inline cl::opt<std::string> cl_bytecodeNameTable("bytecode-name-table", cl::desc("A JSON file containing the list of all bytecode names, in the same order as the dispatch table"), cl::value_desc("filename"), cl::init(""), cl::cat(FPSOptions));
inline cl::opt<std::string> cl_bytecodeTraitTable("bytecode-trait-table", cl::desc("A JSON file containing the list of all bytecode traits, in the same order as the dispatch table"), cl::value_desc("filename"), cl::init(""), cl::cat(FPSOptions));
inline cl::opt<std::string> cl_jsonInputFilename("json-input", cl::desc("The JSON input file name"), cl::value_desc("filename"), cl::init(""), cl::cat(FPSOptions));
inline cl::opt<std::string> cl_jsonInputFilename2("json-input-2", cl::desc("The JSON input file name #2"), cl::value_desc("filename"), cl::init(""), cl::cat(FPSOptions));
inline cl::opt<std::string> cl_jsonInputFilename3("json-input-3", cl::desc("The JSON input file name #3"), cl::value_desc("filename"), cl::init(""), cl::cat(FPSOptions));
inline cl::opt<std::string> cl_headerOutputFilename("hdr-output", cl::desc("The output file name for the generated C++ header"), cl::value_desc("filename"), cl::init(""), cl::cat(FPSOptions));
inline cl::opt<std::string> cl_cppOutputFilename("cpp-output", cl::desc("The output file name for the generated CPP file"), cl::value_desc("filename"), cl::init(""), cl::cat(FPSOptions));
inline cl::opt<std::string> cl_cppOutputFilename2("cpp-output-2", cl::desc("The output file name for the generated CPP file #2"), cl::value_desc("filename"), cl::init(""), cl::cat(FPSOptions));
inline cl::opt<std::string> cl_assemblyOutputFilename("asm-output", cl::desc("The output file name for the generated assembly"), cl::value_desc("filename"), cl::init(""), cl::cat(FPSOptions));
inline cl::opt<std::string> cl_jsonOutputFilename("json-output", cl::desc("The output file name for the generated JSON"), cl::value_desc("filename"), cl::init(""), cl::cat(FPSOptions));
inline cl::opt<std::string> cl_jsonOutputFilename2("json-output-2", cl::desc("The output file name for the generated JSON #2"), cl::value_desc("filename"), cl::init(""), cl::cat(FPSOptions));
inline cl::opt<std::string> cl_auditDirPath("audit-dir", cl::desc("The directory for outputting audit information. These are not used for the build, but for human inspection only."), cl::value_desc("path"), cl::init(""), cl::cat(FPSOptions));

std::vector<std::string> WARN_UNUSED ParseCommaSeparatedFileList(const std::string& commaSeparatedFiles);

void FPS_EmitHeaderFileCommonHeader(FILE* fp);
void FPS_EmitCPPFileCommonHeader(FILE* fp);

// Generate a header file with a function:
//
//     void* WARN_UNUSED GetGuestLanguageFunctionEntryPointForInterpreter(bool takesVarArgs, size_t numFixedParams)
//
// and the '.s' file containing all the runtime logic.
//
void FPS_GenerateInterpreterFunctionEntryLogic();

// Process a user builtin library source file to lower the Deegen API constructs.
// Takes a IR file as input, outputs the '.s' file as if the file were compiled normally by Clang
//
void FPS_ProcessUserBuiltinLib();

// Process a bytecode definition source file for interpreter lowering
// Takes a IR file as input, outputs a '.json' file that contains information about the interpreter implementation
// Note that we cannot compile to '.s' file at this step. The compilation is done after the baseline JIT lowering is also complete
//
void FPS_ProcessBytecodeDefinitionForInterpreter();

// Generate the bytecode builder API heade file
//
void FPS_GenerateBytecodeBuilderAPIHeader();

// Process a bytecode definition source file for baseline JIT lowering
// Takes the '.json' file from interpreter lowering as input, outputs the '.s' file as if the original input IR file were compiled normally by Clang
//
void FPS_ProcessBytecodeDefinitionForBaselineJit();

// Generate the baseline JIT codegen function dispatch table (__deegen_baseline_jit_codegen_dispatch_table)
// and the baseline JIT bytecode trait table (deegen_baseline_jit_bytecode_trait_table)
// and some wrapper logic for call from C++
//
void FPS_GenerateDispatchTableAndBytecodeTraitTableForBaselineJit();

// Generate the baseline JIT function entry logic
//
void FPS_GenerateBaselineJitFunctionEntryLogic();

// Generate the bytecode opcode trait table
//
void FPS_GenerateBytecodeOpcodeTraitTable();

// Generate information about the specialized bytecodes for DFG
//
void FPS_GenerateDfgSpecializedBytecodeInfo();

// Generate the header file that allows accessing all the generated DFG info
//
void FPS_GenerateDfgBytecodeInfoApiHeader();

// Generate the assembly file for all the DFG JIT C call wrapper stubs
//
void Fps_GenerateDfgJitCCallWrapperStubs();

// Link all the generated logic back into the original bytecode definition file to produce the final implementation.
//
void Fps_PostProcessLinkImplementations();

// Generate the code generators for DFG builtin nodes
//
void FPS_ProcessDfgBuiltinNodes();

// Given the desired file name in the audit directory, returns the full file path.
// This also creates the audit directory if it doesn't exist yet.
//
std::string WARN_UNUSED FPS_GetAuditFilePath(const std::string& filename);

// Similar to above, except that the file is created at audit_${dirSuffix}/${filename}
// This also creates the audit directory if it doesn't exist yet.
//
std::string WARN_UNUSED FPS_GetAuditFilePathWithTwoPartName(const std::string& dirSuffix, const std::string& filename);

// A simple function that returns the file name from the absolute file path
//
std::string WARN_UNUSED FPS_GetFileNameFromAbsolutePath(const std::string& filename);
