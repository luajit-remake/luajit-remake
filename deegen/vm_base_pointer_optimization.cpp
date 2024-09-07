#include <vm_base_pointer_optimization.h>

#include "misc_llvm_helper.h"

void dast::RunVMBasePointerOptimizationPass(llvm::Function* func)
{
    using namespace llvm;

    std::vector<CallInst*> removeList;

    for (Function::iterator b = func->begin(), be = func->end(); b != be; ++b) {
        func->getArg(7)->setName("vmBasePointer");

        for (BasicBlock::iterator i = b->begin(), ie = b->end(); i != ie; ++i) {
            CallInst* ci = dyn_cast<CallInst>(i);
            if (!ci)
                continue;
            if (!ci->getCalledFunction())
                continue;
            if (ci->getCalledFunction()->getName() == "DeegenImpl_GetVMBasePointer") {
                removeList.push_back(ci);
            }
        }
    }

    for (CallInst* ci : removeList) {
        ci->replaceAllUsesWith(func->getArg(7));
        ci->eraseFromParent();
    }
}
