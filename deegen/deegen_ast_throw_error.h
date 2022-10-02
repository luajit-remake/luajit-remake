#pragma once

#include "common.h"
#include "misc_llvm_helper.h"

namespace dast {

llvm::Function* WARN_UNUSED GetThrowTValueErrorDispatchTargetFunction(llvm::Module* module);
llvm::Function* WARN_UNUSED GetThrowCStringErrorDispatchTargetFunction(llvm::Module* module);

}   // namespace dast
