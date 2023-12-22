#pragma once

#include "common_utils.h"
#include "misc_llvm_helper.h"

namespace dast {

// Return false if the LLVM function must not be an effectful function.
//
// A function is not effectful if it does not write any global memory and does not write through argument pointers.
//
// Note that being not-effectful does not imply being pure or idempotent, as the function can still read global memory / argument pointers.
// The only guarantee is that executing a not-effectful function twice in a row is equivalent to executing the function only once.
//
// excludePointerArgs:
//     If provided, it must be a vector<bool> of length #arguments.
//     If index k is true, argument k must be a pointer, indicating that writes through argument k should be ignored for the purpose of this check.
//
// externalFuncMightBeEffectfulChecker:
//     If provided, it should take in an external function and a vector<bool> of length #arguments,
//     and return true if the function might be effectful, ignoring writes through the pointer arguments if the corresponding bit in the
//     vector<bool> is true.
//     If not provided, any external function is considered potentially effectful unless it has 'readnone' attribute
//
bool WARN_UNUSED DetermineIfLLVMFunctionMightBeEffectful(
    llvm::Function* func,
    std::vector<bool> excludePointerArgs = {},
    std::function<bool(llvm::Function*, const std::vector<bool>&)> externalFuncMightBeEffectfulChecker = nullptr);

}   // namespace dast
