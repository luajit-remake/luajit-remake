#pragma once

#include "common.h"
#include "misc_llvm_helper.h"
#include "deegen_bytecode_ir_components.h"
#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_bytecode_operand.h"

namespace dast {

struct DeegenBytecodeBaselineJitInfo
{
    size_t m_fastPathCodeLen;
    size_t m_slowPathCodeLen;
    size_t m_dataSectionCodeLen;
    size_t m_dataSectionAlignment;
    size_t m_numCondBrLatePatches;
    size_t m_slowPathDataLen;

    // The module that contains the full logic of the baseline JIT codegen function for this bytecode, which
    // emits the JIT code, creates the slow path data and dispatches to the codegen function for the next bytecode
    //
    std::unique_ptr<llvm::Module> m_cgMod;

    std::string m_resultFuncName;

    // The list of modules that were used to generate the stencils.
    // The first module is always the main JIT component, followed by the return continuations.
    // For audit purpose only.
    //
    std::vector<std::pair<std::unique_ptr<llvm::Module>, std::string /*fnName*/>> m_implModulesForAudit;

    // Extra list of audit files to be stored into the audit folder
    //
    std::vector<std::pair<std::string /*fileName*/, std::string /*fileContents*/>> m_extraAuditFiles;

    // Extra list of verbose audit files to be stored into the verbose audit folder
    //
    std::vector<std::pair<std::string /*fileName*/, std::string /*fileContents*/>> m_extraVerboseAuditFiles;

    // Human-readable disassembly of what code will be generated, for audit purpose only
    //
    std::string m_disasmForAudit;

    static DeegenBytecodeBaselineJitInfo WARN_UNUSED Create(BytecodeIrInfo& bii, const BytecodeOpcodeRawValueMap& byToOpcodeMap);
};

// Emit the implementation of 'deegen_baseline_jit_do_codegen_impl'
//
void DeegenGenerateBaselineJitCompilerCppEntryFunction(llvm::Module* module);

// Emit the implementation of 'deegen_baseline_jit_codegen_finish'
//
void DeegenGenerateBaselineJitCodegenFinishFunction(llvm::Module* module);

}   // namespace dast
