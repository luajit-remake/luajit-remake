#pragma once

#include "misc_llvm_helper.h"

namespace dast {

// It turns out that LLVM's floating-point-related optimizations (when NaNs are involved) are not 100% satisfactory.
// Two of the unfortunate patterns that can be observed in our use case are:
//
// Pattern 1:
//     %2 = fcmp ord double %0, 0.000000e+00
//     %3 = fcmp ord double %1, 0.000000e+00
//     %4 = select i1 %2, i1 %3, i1 false
//
// The above is effectively checking whether neither %0 nor %1 are NaN, which is equivalent to:
//     %4 = fcmp ord double %0, %1
// But for some reason LLVM cannot accomplish this rewrite by itself. So we implement this rewrite by hand:
//
void DeegenExtraLLVMOptPass_FuseTwoNaNChecksIntoOne(llvm::Module* module);

// Pattern 2:
//     %2 = fcmp ord double %0, %1
//     br i1 %2, label L1, label L2
// L1: %3 = fcmp [xx] double %0, %1
//     br i1 %3, label L3, label L4
//
// The above can be lowered to assembly
//     ucomisd %0, %1
//     jp L2
//     jcc L3
// That is, 'ucomisd' can only be executed once since it simutanuously tells if the comparison is ordered and the ordered comparison result.
// However, LLVM backend is often not smart enough, and generates an unnecessary 'ucomisd' instructions between the 'jp' and 'jcc'.
//
// It turns out that the second fcmp must be in the following form in order to make the backend happy:
// (1) The two operands must be in the same order as the first 'fcmp'
// (2) The fcmp keyword must be a 'o' comparison, not a 'u' comparison
// Only in that case can LLVM backend realize that it can omit the second 'ucomisd'. So we implement a pass to make that always the case.
//
// Note that another alternative is to identify the pattern and replace it with the following inline assembly:
//   asm goto ("ucomisd %1, %0; jp %l2; ja %l3" : /*out*/ : "x"(op1), "x"(op2) /*in*/ : "cc" /*clobber*/ : has_nan, larger /*labels*/);
// It should be more reliable than relying on the LLVM backend (given that optimizations involving the flags register are always fragile),
// but I don't know if using inline asm would create other obstacles to LLVM backend codegen and cause regressions elsewhere, so I'm not using it for now.
//
// This pass should run after 'DeegenExtraLLVMOptPass_FuseTwoNaNChecksIntoOne'.
//
void DeegenExtraLLVMOptPass_FuseNaNAndCmpCheckIntoOne(llvm::Module* module);

}   // namespace dast
