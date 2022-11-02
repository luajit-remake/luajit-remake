#include "tag_register_optimization.h"

#include "misc_llvm_helper.h"

namespace dast {

// Return nullptr if 'target' cannot be replaced
// Otherwise, return the value to replace the target, and 'insertionSet' holds any instruction to be inserted before the current instruction
//
static llvm::Value* WARN_UNUSED TryReplaceConstantByTagRegister(llvm::Constant* c,
                                                                llvm::Argument* tagRegister,
                                                                uint64_t tagRegisterValue,
                                                                std::vector<llvm::Instruction*>& insertionSet /*out*/)
{
    using namespace llvm;
    LLVMContext& ctx = c->getContext();
    auto replaceImpl = [&](uint64_t value) WARN_UNUSED -> llvm::Value*
    {
        // Only replace if the constant is within [-128, 127] range
        // so that it is computable with a imm8-operand x86-64 instruction
        //
        if (tagRegisterValue - 128 <= value && value < tagRegisterValue + 128)
        {
            if (value == tagRegisterValue)
            {
                return tagRegister;
            }
            else if (value < tagRegisterValue)
            {
                Instruction* i = CreateUnsignedSubNoOverflow(tagRegister, CreateLLVMConstantInt<uint64_t>(ctx, tagRegisterValue - value));
                insertionSet.push_back(i);
                return i;
            }
            else
            {
                Instruction* i = CreateUnsignedAddNoOverflow(tagRegister, CreateLLVMConstantInt<uint64_t>(ctx, value - tagRegisterValue));
                insertionSet.push_back(i);
                return i;
            }
        }
        else
        {
            return nullptr;
        }
    };

    if (isa<ConstantInt>(c))
    {
        if (llvm_value_has_type<uint64_t>(c))
        {
            uint64_t cstVal = GetValueOfLLVMConstantInt<uint64_t>(c);
            return replaceImpl(cstVal);
        }

        return nullptr;
    }

    if (isa<ConstantExpr>(c))
    {
        ConstantExpr* expr = cast<ConstantExpr>(c);
        if (expr->getOpcode() == Instruction::CastOps::IntToPtr)
        {
            Constant* op = expr->getOperand(0);
            if (llvm_value_has_type<uint64_t>(op))
            {
                uint64_t cstVal = GetValueOfLLVMConstantInt<uint64_t>(op);
                Value* val = replaceImpl(cstVal);
                if (val == nullptr)
                {
                    return nullptr;
                }
                ReleaseAssert(llvm_value_has_type<uint64_t>(val));
                Instruction* castInst = new IntToPtrInst(val, expr->getType(), "");
                insertionSet.push_back(castInst);
                return castInst;
            }
        }
        return nullptr;
    }

    // TODO: It would be nice if we could also handle aggregate types (ConstantDataSequential/ConstantAggregate).
    // But we currently don't have use cases for them right now (primarily because this pass happens before loop
    // vectorization, so vector constants haven't been created by LLVM yet), so let's stay simple.
    //

    return nullptr;
}

static void TransformConstantToTagRegister(llvm::Function* target, llvm::Argument* tagRegister, uint64_t tagRegisterValue)
{
    using namespace llvm;
    ReleaseAssert(tagRegister != nullptr);
    std::unordered_map<Instruction*, std::vector<Instruction*>> insertionSet;
    std::map<std::pair<Instruction*, uint32_t>, Value*> operandReplacementMap;
    for (BasicBlock& bb : *target)
    {
        for (Instruction& inst : bb)
        {
            if (dyn_cast<SwitchInst>(&inst) != nullptr)
            {
                // SwitchInst requires all the 'case' values to be immediate values, so we cannot process it naively
                //
                // TODO: for now we simply skip it altogether because we have no such use case, but in the future
                // we should think about support rewriting SwitchInst for completeness.
                //
                continue;
            }
            for (uint32_t operandOrd = 0; operandOrd < inst.getNumOperands(); operandOrd++)
            {
                Value* op = inst.getOperand(operandOrd);
                Constant* c = dyn_cast<Constant>(op);
                if (c != nullptr)
                {
                    std::vector<llvm::Instruction*> insBefore;
                    Value* replacement = TryReplaceConstantByTagRegister(c, tagRegister, tagRegisterValue, insBefore /*out*/);
                    if (replacement != nullptr)
                    {
                        ReleaseAssert(replacement->getType() == c->getType());
                        ReleaseAssert(!operandReplacementMap.count(std::make_pair(&inst, operandOrd)));
                        operandReplacementMap[std::make_pair(&inst, operandOrd)] = replacement;
                        auto& lis = insertionSet[&inst];
                        for (llvm::Instruction* i : insBefore)
                        {
                            lis.push_back(i);
                        }
                    }
                }
            }
        }
    }

    for (auto& it : insertionSet)
    {
        Instruction* insertBefore = it.first;
        auto& list = it.second;
        for (Instruction* inst : list)
        {
            ReleaseAssert(inst->getParent() == nullptr);
            inst->insertBefore(insertBefore);
        }
    }

    for (auto& it : operandReplacementMap)
    {
        Instruction* inst = it.first.first;
        uint32_t operandOrd = it.first.second;
        Value* replacement = it.second;
        ReleaseAssert(operandOrd < inst->getNumOperands());
        inst->setOperand(operandOrd, replacement);
    }

    ValidateLLVMFunction(target);
}

void TagRegisterOptimizationPass::Run()
{
    using namespace llvm;
    ReleaseAssert(!m_didOptimization);
    m_didOptimization = true;
    for (auto& it : m_tagRegisterList)
    {
        Argument* tagRegister = it.first;
        uint64_t tagRegisterValue = it.second;
        TransformConstantToTagRegister(m_target, tagRegister, tagRegisterValue);
    }
}

}   // namespace dast
