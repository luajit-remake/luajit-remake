#pragma once

#include "common.h"
#include "misc_llvm_helper.h"

namespace dast {

class InterpreterBytecodeImplCreator;

llvm::Function* WARN_UNUSED GetThrowTValueErrorDispatchTargetFunction(llvm::Module* module);
llvm::Function* WARN_UNUSED GetThrowCStringErrorDispatchTargetFunction(llvm::Module* module);

void DeegenLowerThrowErrorAPIForInterpreter(InterpreterBytecodeImplCreator* ifi, llvm::Function* func);

}   // namespace dast
