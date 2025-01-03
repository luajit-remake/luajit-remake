#include "deegen_rewrite_closure_call.h"

namespace dast {

RewriteClosureToFunctionCall::Result WARN_UNUSED RewriteClosureToFunctionCall::Run(llvm::Function* lambda, llvm::AllocaInst* capture, const std::string& newFnName)
{
    using namespace llvm;
    Module* module = lambda->getParent();
    LLVMContext& ctx = module->getContext();

    ReleaseAssert(lambda->arg_size() == 1);
    ReleaseAssert(llvm_value_has_type<void*>(lambda->getArg(0)));

    std::vector<Instruction*> instructionToRemove;
    std::vector<std::function<void(Value* newAlloca, Value* arg, BasicBlock* bb)>> rewriteFns;
    std::vector<Value*> closureRewriteArgs;

    // TODO: ideally we should identify word-sized structs which only element is a double, and pass them in FPR
    // for less overhead (I believe if the struct is passed directly it's going to be in GPR but I'm not fully sure).
    // However, currently we don't have such use case so we ignore it for now.
    //
    for (Use& u : capture->uses())
    {
        User* usr = u.getUser();
        if (isa<CallInst>(usr))
        {
            CallInst* ci = cast<CallInst>(usr);
            Function* callee = ci->getCalledFunction();
            ReleaseAssert(callee != nullptr);
            ReleaseAssert(callee->isIntrinsic());
            ReleaseAssert(callee->getIntrinsicID() == Intrinsic::lifetime_start || callee->getIntrinsicID() == Intrinsic::lifetime_end);
            ReleaseAssert(ci->arg_size() == 2);
            ReleaseAssert(&u == &ci->getOperandUse(1));
            instructionToRemove.push_back(ci);
            continue;
        }

        ReleaseAssert(isa<StoreInst>(usr) || isa<GetElementPtrInst>(usr));
        if (isa<StoreInst>(usr))
        {
            StoreInst* si = cast<StoreInst>(usr);
            ReleaseAssert(u.getOperandNo() == si->getPointerOperandIndex());
            ReleaseAssert(capture == si->getPointerOperand());
            rewriteFns.push_back([=](Value* newAlloca, Value* arg, BasicBlock* bb) {
                StoreInst* clone = dyn_cast<StoreInst>(si->clone());
                ReleaseAssert(clone != nullptr);
                clone->setOperand(si->getPointerOperandIndex(), newAlloca);
                ReleaseAssert(arg->getType() == clone->getValueOperand()->getType());
                clone->setOperand(0 /*valueOperandIndex*/, arg);
                ReleaseAssert(clone->getValueOperand() == arg);
                clone->insertBefore(bb->end());
            });
            closureRewriteArgs.push_back(si->getValueOperand());
            instructionToRemove.push_back(si);
        }
        else
        {
            ReleaseAssert(isa<GetElementPtrInst>(usr));
            GetElementPtrInst* gep = cast<GetElementPtrInst>(usr);
            ReleaseAssert(u.getOperandNo() == gep->getPointerOperandIndex());
            ReleaseAssert(gep->hasOneUse());
            User* gepUser = *gep->user_begin();
            // TODO: this is incomplete. For struct >8 bytes LLVM may generate a memcpy, and we should handle this case for completeness
            // However, we don't have this use case right now so we ignore it.
            //
            ReleaseAssert(isa<StoreInst>(gepUser));
            StoreInst* si = cast<StoreInst>(gepUser);
            ReleaseAssert(si->getPointerOperand() == gep);
            rewriteFns.push_back([=](Value* newAlloca, Value* arg, BasicBlock* bb) {
                GetElementPtrInst* gepClone = dyn_cast<GetElementPtrInst>(gep->clone());
                ReleaseAssert(gepClone != nullptr);
                StoreInst* siClone = dyn_cast<StoreInst>(si->clone());
                ReleaseAssert(siClone != nullptr);
                gepClone->setOperand(gepClone->getPointerOperandIndex(), newAlloca);
                siClone->setOperand(si->getPointerOperandIndex(), gepClone);
                ReleaseAssert(arg->getType() == siClone->getValueOperand()->getType());
                siClone->setOperand(0 /*valueOperandIndex*/, arg);
                ReleaseAssert(siClone->getValueOperand() == arg);
                gepClone->insertBefore(bb->end());
                siClone->insertBefore(bb->end());
            });
            closureRewriteArgs.push_back(si->getValueOperand());
            instructionToRemove.push_back(si);
            instructionToRemove.push_back(gep);
        }
    }

    ReleaseAssert(closureRewriteArgs.size() == rewriteFns.size());
    std::vector<Type*> closureWrapperFnArgTys;
    for (Value* val : closureRewriteArgs)
    {
        closureWrapperFnArgTys.push_back(val->getType());
    }

    FunctionType* fty = FunctionType::get(lambda->getReturnType() /*result*/, closureWrapperFnArgTys, false /*isVarArg*/);
    Function* wrapper = Function::Create(fty, GlobalValue::LinkageTypes::InternalLinkage, newFnName, module);
    ReleaseAssert(wrapper->getName().str() == newFnName);
    wrapper->addFnAttr(Attribute::NoUnwind);
    CopyFunctionAttributes(wrapper, lambda);

    {
        BasicBlock* entryBlock = BasicBlock::Create(ctx, "", wrapper);
        AllocaInst* newClosureState = new AllocaInst(capture->getAllocatedType(), 0 /*addrSpace*/, "", entryBlock);
        for (size_t i = 0; i < rewriteFns.size(); i++)
        {
            ReleaseAssert(i < wrapper->arg_size());
            rewriteFns[i](newClosureState, wrapper->getArg(static_cast<uint32_t>(i)), entryBlock);
        }
        CallInst* callToClosure = CallInst::Create(lambda, { newClosureState }, "", entryBlock);
        if (llvm_value_has_type<void>(callToClosure))
        {
            ReturnInst::Create(ctx, nullptr /*retVoid*/, entryBlock);
        }
        else
        {
            ReturnInst::Create(ctx, callToClosure, entryBlock);
        }
    }

    ValidateLLVMFunction(wrapper);
    if (lambda->hasFnAttribute(Attribute::NoInline))
    {
        lambda->removeFnAttr(Attribute::NoInline);
        wrapper->addFnAttr(Attribute::NoInline);
    }
    lambda->addFnAttr(Attribute::AlwaysInline);

    instructionToRemove.push_back(capture);

    {
        std::unordered_set<Instruction*> checkUnique;
        for (Instruction* i : instructionToRemove)
        {
            ReleaseAssert(!checkUnique.count(i));
            checkUnique.insert(i);
        }
    }

    for (Instruction* i : instructionToRemove)
    {
        ReleaseAssert(i->use_empty());
        i->eraseFromParent();
    }

    return {
        .m_newFunc = wrapper,
        .m_args = closureRewriteArgs
    };
}

}   // namespace dast
