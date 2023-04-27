#pragma once

#include "common.h"
#include "misc_llvm_helper.h"

namespace dast {

// One bytecode implementation may consists of multiple stencils (the main JIT stencil plus stencils for any return continuations
// directly or indirectly reachable from the main JIT stencil), and they may reference each other.
//
// When we produce code for one stencil, the logic is unaware of the other stencils, so reference to other stencils show up as global symbols.
// However, when we put everything togther in the end, we need to fix up these reference to make them point to the correct JIT'ed address.
// This class is a utility class for this purpose.
//
struct DeegenStencilFixupCrossRefHelper
{
    // Conceptually what we are trying to do is pretty simple: replace all use of global value 'gv' in function 'mainFn' with Value* 'val'.
    // However, due to the complexity and constraints of LLVM Constant/ConstantExpr, the logic is not as simple as it seems...
    //
    static void RunOnFunction(llvm::Function* mainFn, llvm::GlobalValue* gvToReplace, llvm::Instruction* replacementValue);
};

}   // namespace dast
