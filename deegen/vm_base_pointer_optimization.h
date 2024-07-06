#pragma once

#include "misc_llvm_helper.h"
#include "tvalue.h"


namespace dast {

void RunVMBasePointerOptimizationPass(llvm::Function* func);

}  // namespace dast
