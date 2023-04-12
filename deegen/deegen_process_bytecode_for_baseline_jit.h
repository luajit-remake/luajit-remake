#pragma once

#include "common.h"
#include "misc_llvm_helper.h"
#include "deegen_baseline_jit_codegen_logic_creator.h"
#include "deegen_bytecode_ir_components.h"

namespace dast {

struct DeegenProcessBytecodeForBaselineJitResult
{
    BytecodeIrInfo* m_bii;
    BytecodeVariantDefinition* m_bytecodeDef;

    DeegenBytecodeBaselineJitInfo m_baselineJitInfo;

    // The ordinal of this opcode in the dispatch table, and the # of fused IC variants
    // Use for populating the opcode trait table: we need to populate range [m_opcodeRawValue, m_opcodeRawValue + m_opcodeNumFuseIcVariants]
    //
    size_t m_opcodeRawValue;
    size_t m_opcodeNumFuseIcVariants;

    struct SlowPathInfo
    {
        std::unique_ptr<llvm::Module> m_module;
        std::string m_funcName;
    };

    // The AOT-compiled slow path logic
    //
    std::vector<SlowPathInfo> m_aotSlowPaths;
    std::vector<SlowPathInfo> m_aotSlowPathReturnConts;

    static DeegenProcessBytecodeForBaselineJitResult WARN_UNUSED Create(BytecodeIrInfo* bii, const BytecodeOpcodeRawValueMap& byOpMap);
};

}   // namespace dast
