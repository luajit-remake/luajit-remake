#pragma once

#include "misc_llvm_helper.h"
#include "deegen_bytecode_ir_components.h"
#include "deegen_register_pinning_scheme.h"
#include "deegen_ast_slow_path.h"

namespace dast {

struct DfgAotSlowPathSaveRegStubCreator
{
    static std::string WARN_UNUSED GetResultFunctionName(BytecodeIrComponent& bic);
    static std::unique_ptr<llvm::Module> WARN_UNUSED Create(BytecodeIrComponent& bic);
};

}   // namespace dast
