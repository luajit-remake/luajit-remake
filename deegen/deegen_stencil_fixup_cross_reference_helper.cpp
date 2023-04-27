#include "deegen_stencil_fixup_cross_reference_helper.h"

namespace dast {

void DeegenStencilFixupCrossRefHelper::RunOnFunction(llvm::Function* mainFn, llvm::GlobalValue* gvToReplace, llvm::Instruction* replacementValue)
{
    using namespace llvm;

    ReleaseAssert(llvm_value_has_type<void*>(replacementValue));

    // Figure out all ConstantExpr users that uses 'gvToReplace'
    //
    std::unordered_set<Constant*> cstUsers;
    cstUsers.insert(gvToReplace);

    {
        std::queue<Constant*> worklist;
        worklist.push(gvToReplace);
        while (!worklist.empty())
        {
            Constant* cst = worklist.front();
            worklist.pop();
            for (User* u : cst->users())
            {
                if (isa<Constant>(u))
                {
                    if (isa<ConstantExpr>(u))
                    {
                        ConstantExpr* ce = cast<ConstantExpr>(u);
                        if (!cstUsers.count(ce))
                        {
                            cstUsers.insert(ce);
                            worklist.push(ce);
                        }
                    }
                    else
                    {
                        // This shouldn't happen for our module..
                        //
                        fprintf(stderr, "[ERROR] Unexpected non-ConstantExpr use of constant, a bug?\n");
                        cst->dump();
                        mainFn->getParent()->dump();
                        abort();
                    }
                }
            }
        }
    }

    // Replace every use of constant in 'cstUsers'
    //
    std::unordered_map<Constant*, Instruction*> replacementMap;
    replacementMap[gvToReplace] = replacementValue;

    std::function<Instruction*(Constant*, Instruction*)> handleConstant = [&](Constant* cst, Instruction* insertBefore) WARN_UNUSED -> Instruction*
    {
        if (replacementMap.count(cst))
        {
            Instruction* inst = replacementMap[cst];
            ReleaseAssert(inst != nullptr);
            return inst;
        }

        ReleaseAssert(isa<ConstantExpr>(cst));
        ConstantExpr* ce = cast<ConstantExpr>(cst);
        Instruction* inst = ce->getAsInstruction(insertBefore);
        ReleaseAssert(inst != nullptr);

        // Expanding ConstantExpr should never result in cycle. Fire assert if a cycle is detected.
        //
        replacementMap[cst] = nullptr;
        for (Use& u : inst->operands())
        {
            Value* val = u.get();
            if (isa<Constant>(val))
            {
                Constant* c = cast<Constant>(val);
                if (cstUsers.count(c))
                {
                    Instruction* replacement = handleConstant(c, inst /*insertBefore*/);
                    u.set(replacement);
                }
            }
        }

        replacementMap[cst] = inst;
        return inst;
    };

    std::vector<Instruction*> instList;
    for (BasicBlock& bb : *mainFn)
    {
        for (Instruction& inst : bb)
        {
            instList.push_back(&inst);
        }
    }

    Instruction* insPt = replacementValue->getNextNode();
    ReleaseAssert(insPt != nullptr);

    for (Instruction* inst : instList)
    {
        for (Use& u : inst->operands())
        {
            Value* val = u.get();
            if (isa<Constant>(val))
            {
                Constant* c = cast<Constant>(val);
                if (cstUsers.count(c))
                {
                    Instruction* replacement = handleConstant(c, insPt /*insertBefore*/);
                    u.set(replacement);
                }
            }
        }
    }

    gvToReplace->removeDeadConstantUsers();

    // Catch bugs as early as possible if any of the crap above went wrong..
    //
    ValidateLLVMFunction(mainFn);
}

}   // namespace dast
