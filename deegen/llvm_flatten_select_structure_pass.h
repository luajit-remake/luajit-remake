#pragma once

#include "misc_llvm_helper.h"

namespace dast {

// It turns out that LLVM's reg2mem pass cannot fully put everything back to register
// form (so that SROA can work fully and decompose all the structures). Specifically,
// if the LLVM IR contains 'select' instruction on structures, reg2mem sometimes cannot
// put this structure value back to register form.
//
// So we handwrite this pass to workaround this limitation of reg2mem, which simply
// rewrite '%res = select i1 %1, structTy %2, structTy %3' to branch and alloca:
//     %tmp = alloca structTy
//     br %1, true_bb, false_bb
// true_bb:
//     store %2, %tmp
//     br join_bb
// false_bb:
//     store %3, %tmp
//     br join_bb
// join_bb:
//     %res = load %tmp
//
// After this transform and the reg2mem pass, (hopefully) the SROA pass should be able
// to work at its full potential.
//
void LLVMFlattenSelectOnStructureValueToBranchPass(llvm::Module* module);

}   // namespace dast
