#pragma once

#include "misc_llvm_helper.h"

namespace dast {

std::unique_ptr<llvm::Module> WARN_UNUSED ProcessBytecodeDefinitionForInterpreter(std::unique_ptr<llvm::Module> module);

}   // namespace dast
