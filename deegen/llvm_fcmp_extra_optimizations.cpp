#include "llvm_fcmp_extra_optimizations.h"

namespace dast {

static void FuseTwoNaNChecksIntoOne(llvm::Function* func)
{
    using namespace llvm;

    struct Item
    {
        // When isUno is true, 'Item' describes the following pattern:
        //     %fcmp1 = fcmp uno %lhs, cst
        //     %fcmp2 = fcmp uno %rhs, cst
        //     %res = or %fcmp1, %fcmp2
        //
        // Otherwise, 'Item' describes the following pattern:
        //     %fcmp1 = fcmp ord %lhs, cst
        //     %fcmp2 = fcmp ord %rhs, cst
        //     %res = and %fcmp1, %fcmp2
        //
        bool isUno;
        Value* lhs;
        Value* rhs;
        Instruction* fcmp1;
        Instruction* fcmp2;
        Instruction* res;
    };

    auto isAndInstruction = [](Instruction* inst, Value*& lhs /*out*/, Value*& rhs /*out*/) WARN_UNUSED -> bool
    {
        // We currently support pattern-matching two forms of 'and':
        // %res = and %0, %1
        // %res = select %0, %1, false
        //
        if (!llvm_value_has_type<bool>(inst))
        {
            return false;
        }
        if (isa<BinaryOperator>(inst))
        {
            BinaryOperator* bo = cast<BinaryOperator>(inst);
            if (bo->getOpcode() == Instruction::BinaryOps::And)
            {
                lhs = bo->getOperand(0);
                rhs = bo->getOperand(1);
                return true;
            }
        }
        if (isa<SelectInst>(inst))
        {
            SelectInst* si = cast<SelectInst>(inst);
            Value* cond = si->getCondition();
            Value* tv = si->getTrueValue();
            Value* fv = si->getFalseValue();
            ReleaseAssert(llvm_value_has_type<bool>(cond));
            ReleaseAssert(llvm_value_has_type<bool>(tv));
            ReleaseAssert(llvm_value_has_type<bool>(fv));
            if (isa<Constant>(fv))
            {
                if (GetValueOfLLVMConstantInt<bool>(cast<Constant>(fv)) == false)
                {
                    lhs = cond;
                    rhs = tv;
                    return true;
                }
            }
        }
        return false;
    };

    auto isOrInstruction = [](Instruction* inst, Value*& lhs /*out*/, Value*& rhs /*out*/) WARN_UNUSED -> bool
    {
        // We currently support pattern-matching two forms of 'or':
        // %res = or %0, %1
        // %res = select %0, true, %1
        //
        if (!llvm_value_has_type<bool>(inst))
        {
            return false;
        }
        if (isa<BinaryOperator>(inst))
        {
            BinaryOperator* bo = cast<BinaryOperator>(inst);
            if (bo->getOpcode() == Instruction::BinaryOps::Or)
            {
                lhs = bo->getOperand(0);
                rhs = bo->getOperand(1);
                return true;
            }
        }
        if (isa<SelectInst>(inst))
        {
            SelectInst* si = cast<SelectInst>(inst);
            Value* cond = si->getCondition();
            Value* tv = si->getTrueValue();
            Value* fv = si->getFalseValue();
            ReleaseAssert(llvm_value_has_type<bool>(cond));
            ReleaseAssert(llvm_value_has_type<bool>(tv));
            ReleaseAssert(llvm_value_has_type<bool>(fv));
            if (isa<Constant>(tv))
            {
                if (GetValueOfLLVMConstantInt<bool>(cast<Constant>(tv)) == true)
                {
                    lhs = cond;
                    rhs = fv;
                    return true;
                }
            }
        }
        return false;
    };

    auto tryCreateItem = [](Instruction* boolInst, FCmpInst* lhs, FCmpInst* rhs, bool isUno) WARN_UNUSED -> std::optional<Item>
    {
        if (lhs == rhs)
        {
            return {};
        }
        if (!lhs->hasOneUse())
        {
            return {};
        }
        ReleaseAssert(*lhs->user_begin() == boolInst);
        if (!rhs->hasOneUse())
        {
            return {};
        }
        ReleaseAssert(*rhs->user_begin() == boolInst);
        CmpInst::Predicate expectedPred = isUno ? CmpInst::Predicate::FCMP_UNO : CmpInst::Predicate::FCMP_ORD;
        if (lhs->getPredicate() != expectedPred)
        {
            return {};
        }
        if (rhs->getPredicate() != expectedPred)
        {
            return {};
        }

        // Check whether the fcmp is in one of the two forms:
        //   fcmp val, cst where 'cst' is a non-NaN constant
        //   fcmp val, val
        // Since the predicate keyword of fcmp is known to be either uno or ord, it means this fcmp is checking whether 'val' is a NaN.
        // Returns true and populate 'val' if that is the case.
        //
        auto isFCmpValueVsNotNanConstant = [](FCmpInst* inst, Value*& val /*out*/) WARN_UNUSED -> bool
        {
            ReleaseAssert(inst->getNumOperands() == 2);
            Value* op1 = inst->getOperand(0);
            Value* op2 = inst->getOperand(1);
            ReleaseAssert(llvm_value_has_type<float>(op1) || llvm_value_has_type<double>(op1));
            ReleaseAssert(op1->getType() == op2->getType());
            if (op1 == op2)
            {
                val = op1;
                return true;
            }
            if (isa<ConstantFP>(op1))
            {
                ConstantFP* fp = cast<ConstantFP>(op1);
                if (!fp->isNaN())
                {
                    val = op2;
                    return true;
                }
            }
            if (isa<ConstantFP>(op2))
            {
                ConstantFP* fp = cast<ConstantFP>(op2);
                if (!fp->isNaN())
                {
                    val = op1;
                    return true;
                }
            }
            return false;
        };

        Value* lhsV = nullptr;
        if (!isFCmpValueVsNotNanConstant(lhs, lhsV /*out*/))
        {
            return {};
        }

        Value* rhsV = nullptr;
        if (!isFCmpValueVsNotNanConstant(rhs, rhsV /*out*/))
        {
            return {};
        }

        return Item {
            .isUno = isUno,
            .lhs = lhsV,
            .rhs = rhsV,
            .fcmp1 = lhs,
            .fcmp2 = rhs,
            .res = boolInst
        };
    };

    std::vector<Item> allItems;

    for (BasicBlock& bb : *func)
    {
        for (Instruction& inst : bb)
        {
            if (llvm_value_has_type<bool>(&inst))
            {
                Value* lhs;
                Value* rhs;
                if (isAndInstruction(&inst, lhs /*out*/, rhs /*out*/))
                {
                    if (isa<FCmpInst>(lhs) && isa<FCmpInst>(rhs))
                    {
                        std::optional<Item> optItem = tryCreateItem(&inst, cast<FCmpInst>(lhs), cast<FCmpInst>(rhs), false /*isUno*/);
                        if (optItem.has_value())
                        {
                            allItems.push_back(optItem.value());
                        }
                    }
                }
                else if (isOrInstruction(&inst, lhs /*out*/, rhs /*out*/))
                {
                    if (isa<FCmpInst>(lhs) && isa<FCmpInst>(rhs))
                    {
                        std::optional<Item> optItem = tryCreateItem(&inst, cast<FCmpInst>(lhs), cast<FCmpInst>(rhs), true /*isUno*/);
                        if (optItem.has_value())
                        {
                            allItems.push_back(optItem.value());
                        }
                    }
                }
            }
        }
    }

    // Just sanity check that no instruction ever appeared in two items
    //
    {
        std::unordered_set<Instruction*> chkUnique;
        for (Item& item : allItems)
        {
            ReleaseAssert(!chkUnique.count(item.fcmp1));
            chkUnique.insert(item.fcmp1);
            ReleaseAssert(!chkUnique.count(item.fcmp2));
            chkUnique.insert(item.fcmp2);
            ReleaseAssert(!chkUnique.count(item.res));
            chkUnique.insert(item.res);
        }
    }

    // Now, perform the rewrite
    //
    for (Item& item : allItems)
    {
        CmpInst::Predicate pred = item.isUno ? CmpInst::Predicate::FCMP_UNO : CmpInst::Predicate::FCMP_ORD;
        Instruction* replacement = new FCmpInst(item.res /*insertBefore*/, pred, item.lhs, item.rhs);
        item.res->replaceAllUsesWith(replacement);
        item.res->eraseFromParent();
        ReleaseAssert(item.fcmp1->use_empty());
        ReleaseAssert(item.fcmp2->use_empty());
        item.fcmp1->eraseFromParent();
        item.fcmp2->eraseFromParent();
    }
}

void DeegenExtraLLVMOptPass_FuseTwoNaNChecksIntoOne(llvm::Module* module)
{
    using namespace llvm;
    for (Function& f : *module)
    {
        if (f.empty())
        {
            continue;
        }
        FuseTwoNaNChecksIntoOne(&f);
    }

    ValidateLLVMModule(module);
}

static void FuseNaNAndCmpCheckIntoOne(llvm::Function* func)
{
    using namespace llvm;

    for (BasicBlock& bb : *func)
    {
        Instruction* terminator = bb.getTerminator();
        if (terminator == nullptr)
        {
            continue;
        }
        if (isa<BranchInst>(terminator))
        {
            BranchInst* inst = cast<BranchInst>(terminator);
            if (inst->isConditional())
            {
                Value* cond = inst->getCondition();
                BasicBlock* trueBB = inst->getSuccessor(0);
                BasicBlock* falseBB = inst->getSuccessor(1);
                if (isa<FCmpInst>(cond))
                {
                    FCmpInst* condInst = cast<FCmpInst>(cond);
                    // Flags register is very fragile since it can be clobbered by a large variety of instructions.
                    // So we choose to only proceed if 'condInst' immediately precedes the conditional branch.
                    //
                    // We don't want our homebrewed pass to mess up with LLVM-optimized code unless we can prove that our change will be useful.
                    // This is also the philosophy behind many checks below.
                    //
                    if (condInst->getNextNode() == terminator)
                    {
                        CmpInst::Predicate pred = condInst->getPredicate();
                        if (pred == CmpInst::Predicate::FCMP_UNO || pred == CmpInst::Predicate::FCMP_ORD)
                        {
                            BasicBlock* bbIfOrdered;
                            if (pred == CmpInst::Predicate::FCMP_UNO)
                            {
                                bbIfOrdered = falseBB;
                            }
                            else
                            {
                                bbIfOrdered = trueBB;
                            }
                            // If 'bbIfOrdered' has more than one predecessor, then our optimization won't apply because one can't be certain that control comes from the fcmp BB
                            // Also 'SinglePredecessor' is important (i.e., we must not use 'UniquePredecessor'), because it tells us there is only one control-flow edge,
                            // which must be the 'if-ordered' edge. So we can know for sure that the two operands of 'condInst' are ordered when we reach this BB.
                            //
                            if (bbIfOrdered->getSinglePredecessor() != nullptr)
                            {
                                ReleaseAssert(bbIfOrdered->getSinglePredecessor() == &bb);

                                // We only proceed if the only content in 'bbIfOrdered' should be a fcmp followed by a branch,
                                // in that case we know for certain that the flags register will survive to the end of the BB.
                                //
                                if (bbIfOrdered->sizeWithoutDebug() == 2)
                                {
                                    Instruction* inst1 = &(*bbIfOrdered->begin());
                                    Instruction* inst2 = &(*(++(bbIfOrdered->begin())));
                                    if (isa<FCmpInst>(inst1) && isa<BranchInst>(inst2))
                                    {
                                        FCmpInst* ci = cast<FCmpInst>(inst1);
                                        BranchInst* bi = cast<BranchInst>(inst2);
                                        if (bi->isConditional() && bi->getCondition() == ci)
                                        {
                                            if (ci->hasOneUse())
                                            {
                                                ReleaseAssert(*ci->user_begin() == bi);

                                                Value* cmp1Lhs = condInst->getOperand(0);
                                                Value* cmp1Rhs = condInst->getOperand(1);

                                                Value* cmp2Lhs = ci->getOperand(0);
                                                Value* cmp2Rhs = ci->getOperand(1);

                                                if ((cmp1Lhs == cmp2Lhs && cmp1Rhs == cmp2Rhs) || (cmp1Lhs == cmp2Rhs && cmp1Rhs == cmp2Lhs))
                                                {
                                                    // We know at this point the two operands must be ordered (i.e., neither is NaN)
                                                    // So we can change the 'unordered or' term in the comparison predicate to 'ordered and'
                                                    //
                                                    bool ok = true;
                                                    CmpInst::Predicate key = ci->getPredicate();
                                                    CmpInst::Predicate newKey = key;
                                                    switch (key)
                                                    {
                                                    case CmpInst::Predicate::FCMP_FALSE:
                                                    case CmpInst::Predicate::FCMP_TRUE:
                                                    case CmpInst::Predicate::FCMP_UNO:
                                                    case CmpInst::Predicate::FCMP_ORD:
                                                    {
                                                        // It's weird to see those cases. LLVM optimizer should have optimized them out..
                                                        // But let's just be conservative and not proceed in those cases.
                                                        //
                                                        ok = false;
                                                        break;
                                                    }
                                                    case CmpInst::Predicate::FCMP_OEQ:
                                                    case CmpInst::Predicate::FCMP_OGT:
                                                    case CmpInst::Predicate::FCMP_OGE:
                                                    case CmpInst::Predicate::FCMP_OLT:
                                                    case CmpInst::Predicate::FCMP_OLE:
                                                    case CmpInst::Predicate::FCMP_ONE:
                                                    {
                                                        break;
                                                    }
                                                    case CmpInst::Predicate::FCMP_UEQ:
                                                    {
                                                        newKey = CmpInst::Predicate::FCMP_OEQ;
                                                        break;
                                                    }
                                                    case CmpInst::Predicate::FCMP_UGT:
                                                    {
                                                        newKey = CmpInst::Predicate::FCMP_OGT;
                                                        break;
                                                    }
                                                    case CmpInst::Predicate::FCMP_UGE:
                                                    {
                                                        newKey = CmpInst::Predicate::FCMP_OGE;
                                                        break;
                                                    }
                                                    case CmpInst::Predicate::FCMP_ULT:
                                                    {
                                                        newKey = CmpInst::Predicate::FCMP_OLT;
                                                        break;
                                                    }
                                                    case CmpInst::Predicate::FCMP_ULE:
                                                    {
                                                        newKey = CmpInst::Predicate::FCMP_OLE;
                                                        break;
                                                    }
                                                    case CmpInst::Predicate::FCMP_UNE:
                                                    {
                                                        newKey = CmpInst::Predicate::FCMP_ONE;
                                                        break;
                                                    }
                                                    default:
                                                    {
                                                        ReleaseAssert(false);
                                                    }
                                                    }   /*switch key*/

                                                    if (ok)
                                                    {
                                                        ci->setPredicate(newKey);

                                                        if (!(cmp1Lhs == cmp2Lhs && cmp1Rhs == cmp2Rhs))
                                                        {
                                                            ReleaseAssert(cmp1Lhs == cmp2Rhs && cmp1Rhs == cmp2Lhs);
                                                            ci->swapOperands();
                                                        }

                                                        ReleaseAssert(ci->getOperand(0) == condInst->getOperand(0) && ci->getOperand(1) == condInst->getOperand(1));

                                                        // Now, fix up the fcmp so that its comparison key is OGT/OGE/ONE, not OLT/OLE/OEQ
                                                        //
                                                        // Note that 'swapOperands' has changed the comparator key, so we must get again
                                                        //
                                                        key = ci->getPredicate();
                                                        if (key == CmpInst::Predicate::FCMP_OEQ || key == CmpInst::Predicate::FCMP_OLT || key == CmpInst::Predicate::FCMP_OLE)
                                                        {
                                                            if (key == CmpInst::Predicate::FCMP_OEQ)
                                                            {
                                                                newKey = CmpInst::Predicate::FCMP_ONE;
                                                            }
                                                            else if (key == CmpInst::Predicate::FCMP_OLT)
                                                            {
                                                                newKey = CmpInst::Predicate::FCMP_OGE;
                                                            }
                                                            else
                                                            {
                                                                ReleaseAssert(key == CmpInst::Predicate::FCMP_OLE);
                                                                newKey = CmpInst::Predicate::FCMP_OGT;
                                                            }

                                                            // Negate the comparison key and swap the BranchInst targets
                                                            // This is fine because we have checked that 'ci' has only one use, which is the condition of 'bi'
                                                            //
                                                            ci->setPredicate(newKey);
                                                            bi->swapSuccessors();
                                                        }

                                                        key = ci->getPredicate();
                                                        ReleaseAssert(key == CmpInst::Predicate::FCMP_OGT || key == CmpInst::Predicate::FCMP_OGE || key == CmpInst::Predicate::FCMP_ONE);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void DeegenExtraLLVMOptPass_FuseNaNAndCmpCheckIntoOne(llvm::Module* module)
{
    using namespace llvm;
    for (Function& f : *module)
    {
        if (f.empty())
        {
            continue;
        }
        FuseNaNAndCmpCheckIntoOne(&f);
    }

    ValidateLLVMModule(module);
}

}   // namespace dast
