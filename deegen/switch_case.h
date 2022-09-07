#pragma once

#include "common.h"

#include "lambda_parser.h"
#include "deegen_api.h"

namespace dast
{

class SwitchCase
{
public:
    struct CaseClause
    {
        CXXLambda m_cond;
        CXXLambda m_action;
    };

    llvm::Function* m_owner;
    std::vector<CaseClause> m_cases;
    bool m_hasDefaultClause;
    CXXLambda m_defaultClause;

    static std::vector<SwitchCase> WARN_UNUSED ParseAll(llvm::Module* module, DAPILambdaMap& lm /*inout*/)
    {
        using namespace llvm;
        LLVMContext& ctx = module->getContext();
        std::vector<SwitchCase> result;
        std::unordered_set<CallInst*> processedItems;
        for (auto& lmIterator : lm)
        {
            CallInst* callSite = lmIterator.first;
            std::vector<CXXLambda>& list = lmIterator.second;
            Function* callee = callSite->getCalledFunction();
            if (IsSwitchCaseAPI(callee))
            {
                ReleaseAssert(!processedItems.count(callSite));
                processedItems.insert(callSite);

                SwitchCase item;
                item.m_hasDefaultClause = false;
                item.m_owner = callSite->getParent()->getParent();

                std::unordered_map<AllocaInst*, CXXLambda> allocaToLambdaMap;
                for (CXXLambda& lambda : list)
                {
                    AllocaInst* inst = lambda.m_capture;
                    ReleaseAssert(inst->getParent()->getParent() == item.m_owner);
                    ReleaseAssert(!allocaToLambdaMap.count(inst));
                    allocaToLambdaMap[inst] = lambda;
                }

                auto getAndRemoveFromMap = [&](AllocaInst* inst) WARN_UNUSED -> CXXLambda
                {
                    ReleaseAssert(inst != nullptr);
                    ReleaseAssert(allocaToLambdaMap.count(inst));
                    CXXLambda lambda = allocaToLambdaMap[inst];
                    allocaToLambdaMap.erase(allocaToLambdaMap.find(inst));
                    return lambda;
                };

                uint32_t numArgs = callSite->arg_size();
                ReleaseAssert(numArgs > 0);

                // arg 0 is the hidden 'this' pointer
                //
                ReleaseAssert(callSite->getArgOperand(0)->getType()->isPointerTy());

                static_assert(static_cast<int>(::detail::SwitchCaseTag::tag) != static_cast<int>(::detail::SwitchDefaultTag::tag));

                uint32_t curArg = 1;
                while (curArg < numArgs)
                {
                    ReleaseAssert(callSite->getArgOperand(curArg)->getType() == Type::getInt32PtrTy(ctx));
                    Value* arg = callSite->getArgOperand(curArg);
                    GlobalVariable* gv = dyn_cast<GlobalVariable>(arg);
                    ReleaseAssert(gv != nullptr);
                    ReleaseAssert(gv->isConstant());
                    ReleaseAssert(gv->getType() == Type::getInt32PtrTy(ctx));
                    int32_t val = GetValueOfLLVMConstantInt<int32_t>(gv->getInitializer());
                    ReleaseAssert(val == static_cast<int32_t>(::detail::SwitchCaseTag::tag) || val == static_cast<int32_t>(::detail::SwitchDefaultTag::tag));
                    if (val == static_cast<int32_t>(::detail::SwitchCaseTag::tag))
                    {
                        ReleaseAssert(curArg + 3 <= numArgs);

                        AllocaInst* cond = dyn_cast<AllocaInst>(callSite->getArgOperand(curArg + 1));
                        ReleaseAssert(cond != nullptr);
                        CXXLambda condLambda = getAndRemoveFromMap(cond);
                        ReleaseAssert(condLambda.ReturnType() == llvm_type_of<bool>(ctx));

                        AllocaInst* action = dyn_cast<AllocaInst>(callSite->getArgOperand(curArg + 2));
                        ReleaseAssert(action != nullptr);
                        CXXLambda actionLambda = getAndRemoveFromMap(action);
                        ReleaseAssert(actionLambda.ReturnType() == llvm_type_of<void>(ctx));

                        item.m_cases.push_back({ .m_cond = condLambda, .m_action = actionLambda });

                        curArg += 3;
                    }
                    else
                    {
                        ReleaseAssert(curArg + 2 == numArgs && "default clause of switch-case must show up as the last clause");
                        ReleaseAssert(!item.m_hasDefaultClause);
                        item.m_hasDefaultClause = true;

                        AllocaInst* action = dyn_cast<AllocaInst>(callSite->getArgOperand(curArg + 1));
                        ReleaseAssert(action != nullptr);
                        CXXLambda actionLambda = getAndRemoveFromMap(action);
                        ReleaseAssert(actionLambda.ReturnType() == llvm_type_of<void>(ctx));

                        item.m_defaultClause = actionLambda;

                        curArg += 2;
                    }
                }
                ReleaseAssert(curArg == numArgs);
                ReleaseAssert(allocaToLambdaMap.size() == 0);
                result.push_back(item);
            }
        }

        for (CallInst* inst : processedItems)
        {
            ReleaseAssert(lm.count(inst));
            lm.erase(lm.find(inst));
        }

        return result;
    }

private:
    static constexpr const char* x_switch_case_api_signature = "SwitchOnMutuallyExclusiveCases::SwitchOnMutuallyExclusiveCases<";

    static bool IsSwitchCaseAPI(llvm::Function* func)
    {
        return IsCXXSymbol(func->getName().str()) && GetDemangledName(func).starts_with(x_switch_case_api_signature);
    }
};

}   // namespace dast
