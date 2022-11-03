#include "llvm_flatten_select_structure_pass.h"

namespace dast {

void LLVMFlattenSelectOnStructureValueToBranchPass(llvm::Module* module)
{
    using namespace llvm;
    std::vector<SelectInst*> allCases;
    for (Function& func : *module)
    {
        for (BasicBlock& bb : func)
        {
            for (Instruction& inst : bb)
            {
                SelectInst* si = dyn_cast<SelectInst>(&inst);
                if (si != nullptr && si->getType()->isStructTy() && llvm_value_has_type<bool>(si->getCondition()))
                {
                    allCases.push_back(si);
                }
            }
        }
    }

    for (SelectInst* si : allCases)
    {
        ReleaseAssert(si->getParent() != nullptr);
        Function* func = si->getParent()->getParent();
        ReleaseAssert(func != nullptr);

        Value* cond = si->getCondition();
        ReleaseAssert(llvm_value_has_type<bool>(cond));
        StructType* sty = dyn_cast<StructType>(si->getType());
        ReleaseAssert(sty != nullptr);
        ReleaseAssert(si->getTrueValue()->getType() == sty);
        ReleaseAssert(si->getFalseValue()->getType() == sty);
        // I'm not sure if SelectInst is allowed to take prof metadata, but let's gracefully handle it if so.
        //
        auto getProfMetadata = [](Instruction* inst) -> MDNode*
        {
            MDNode* md = inst->getMetadata(LLVMContext::MD_prof);
            if (md == nullptr)
            {
                return nullptr;
            }
            if (md->getNumOperands() != 3)
            {
                return nullptr;
            }
            MDString* mdName = dyn_cast<MDString>(md->getOperand(0));
            if (mdName == nullptr)
            {
                return nullptr;
            }
            if (mdName->getString() != "branch_weights")
            {
                return nullptr;
            }
            return md;
        };

        MDNode* profMetadata = getProfMetadata(si);
        Instruction* thenBlockTerminator = nullptr;
        Instruction* elseBlockTerminator = nullptr;
        SplitBlockAndInsertIfThenElse(cond, si /*splitBefore*/, &thenBlockTerminator /*out*/, &elseBlockTerminator /*out*/, profMetadata /*branchWeights*/);
        ReleaseAssert(thenBlockTerminator != nullptr && elseBlockTerminator != nullptr);

        AllocaInst* allocaInst = new AllocaInst(sty, 0 /*addrSpace*/, "", &func->getEntryBlock().front() /*insertBefore*/);
        new StoreInst(si->getTrueValue(), allocaInst, thenBlockTerminator /*insertBefore*/);
        new StoreInst(si->getFalseValue(), allocaInst, elseBlockTerminator /*insertBefore*/);
        Value* replacement = new LoadInst(sty, allocaInst, "", si /*insertBefore*/);
        ReleaseAssert(replacement->getType() == si->getType());
        ReleaseAssert(!llvm_value_has_type<void>(replacement));
        si->replaceAllUsesWith(replacement);
        ReleaseAssert(si->use_empty());
        si->eraseFromParent();
    }
}

}   // namespace dast
