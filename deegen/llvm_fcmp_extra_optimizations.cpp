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

        // Check whether the fcmp is in the form of 'val, cst' where 'cst' is a non-NaN constant. Returns 'val' if it is the case
        //
        auto isFCmpValueVsNotNanConstant = [](FCmpInst* inst, Value*& val /*out*/) WARN_UNUSED -> bool
        {
            ReleaseAssert(inst->getNumOperands() == 2);
            Value* op1 = inst->getOperand(0);
            Value* op2 = inst->getOperand(1);
            ReleaseAssert(llvm_value_has_type<float>(op1) || llvm_value_has_type<double>(op1));
            ReleaseAssert(op1->getType() == op2->getType());
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
}

}   // namespace dast
