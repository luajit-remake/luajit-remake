#include "deegen_ast_make_call.h"

namespace dast {

std::vector<AstMakeCall> WARN_UNUSED AstMakeCall::GetAllUseInFunction(llvm::Function* func)
{
    using namespace llvm;
    std::vector<AstMakeCall> result;
    for (BasicBlock& bb : *func)
    {
        for (Instruction& inst : bb)
        {
            CallInst* callInst = dyn_cast<CallInst>(&inst);
            if (callInst != nullptr)
            {
                Function* callee = callInst->getCalledFunction();
                if (callee != nullptr && callee->getName().str().starts_with(x_placeholderPrefix))
                {
                    AstMakeCall item;
                    ReleaseAssert(callee->arg_size() >= x_ord_arg_start);
                    ReleaseAssert(callInst->arg_size() == callee->arg_size());
                    item.m_origin = callInst;
                    item.m_isInPlaceCall = GetValueOfLLVMConstantInt<bool>(callInst->getArgOperand(x_ord_inplaceCall));
                    item.m_passVariadicRet = GetValueOfLLVMConstantInt<bool>(callInst->getArgOperand(x_ord_passVariadicRet));
                    item.m_isMustTailCall = GetValueOfLLVMConstantInt<bool>(callInst->getArgOperand(x_ord_isMustTailCall));
                    item.m_target = callInst->getArgOperand(x_ord_target);
                    ReleaseAssert(llvm_value_has_type<uint64_t>(item.m_target));
                    item.m_callOption = static_cast<MakeCallOption>(GetValueOfLLVMConstantInt<size_t>(callInst->getArgOperand(x_ord_callOption)));
                    item.m_continuation = dyn_cast<Function>(callInst->getArgOperand(x_ord_continuation));
                    ReleaseAssert(item.m_continuation != nullptr);

                    uint32_t curOrd = x_ord_arg_start;
                    while (curOrd < callee->arg_size())
                    {
                        Value* arg = callInst->getArgOperand(curOrd);
                        if (llvm_value_has_type<uint64_t>(arg))
                        {
                            item.m_args.push_back(Arg { arg });
                            curOrd += 1;
                        }
                        else
                        {
                            ReleaseAssert(llvm_value_has_type<void*>(arg));
                            ReleaseAssert(curOrd + 1 < callee->arg_size());
                            Value* nextArg = callInst->getArgOperand(curOrd + 1);
                            ReleaseAssert(llvm_value_has_type<uint64_t>(nextArg));
                            item.m_args.push_back(Arg { arg, nextArg });
                            curOrd += 2;
                        }
                    }
                    ReleaseAssert(curOrd == callee->arg_size());

                    result.push_back(std::move(item));
                }
            }
        }
    }
    return result;
}

llvm::Function* WARN_UNUSED AstMakeCall::CreatePlaceholderFunction(llvm::Module* module, const std::vector<bool /*isArgRange*/>& argDesc)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();

    std::string decidedName;
    size_t suffixOrd = 0;
    while (true)
    {
        decidedName = std::string(x_placeholderPrefix) + std::to_string(suffixOrd);
        if (module->getNamedValue(decidedName) == nullptr)
        {
            break;
        }
        suffixOrd++;
    }

    std::vector<Type*> argTypes;
    argTypes.push_back(llvm_type_of<bool>(ctx));
    argTypes.push_back(llvm_type_of<bool>(ctx));
    argTypes.push_back(llvm_type_of<bool>(ctx));
    argTypes.push_back(llvm_type_of<uint64_t>(ctx));
    argTypes.push_back(llvm_type_of<void*>(ctx));
    argTypes.push_back(llvm_type_of<uint64_t>(ctx));

    for (bool isArgRange : argDesc)
    {
        if (isArgRange)
        {
            argTypes.push_back(llvm_type_of<void*>(ctx));
            argTypes.push_back(llvm_type_of<uint64_t>(ctx));
        }
        else
        {
            argTypes.push_back(llvm_type_of<uint64_t>(ctx));
        }
    }

    FunctionType* fty = FunctionType::get(llvm_type_of<void>(ctx) /*result*/, argTypes, false /*isVarArg*/);
    Function* func = Function::Create(fty, GlobalValue::LinkageTypes::ExternalLinkage, decidedName, module);
    func->addFnAttr(Attribute::AttrKind::NoUnwind);
    func->addFnAttr(Attribute::AttrKind::NoReturn);
    return func;
}

void AstMakeCall::PreprocessModule(llvm::Module* module)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();

    struct Item
    {
        CallInst* m_inst;
        bool m_isInPlaceCall;
        bool m_passVariadicRet;
        bool m_isMustTailCall;
    };

    std::vector<Item> foundList;
    for (Function& func : *module)
    {
        for (BasicBlock& bb : func)
        {
            for (Instruction& inst : bb)
            {
                CallInst* callInst = dyn_cast<CallInst>(&inst);
                if (callInst != nullptr)
                {
                    Function* callee = callInst->getCalledFunction();
                    if (callee != nullptr)
                    {
                        std::string calleeName = callee->getName().str();
                        if (calleeName == "DeegenImpl_StartMakeCallInfo")
                        {
                            foundList.push_back({ .m_inst = callInst, .m_isInPlaceCall = false, .m_passVariadicRet = false, .m_isMustTailCall = false });
                        }
                        else if (calleeName == "DeegenImpl_StartMakeCallPassingVariadicResInfo")
                        {
                            foundList.push_back({ .m_inst = callInst, .m_isInPlaceCall = false, .m_passVariadicRet = true, .m_isMustTailCall = false });
                        }
                        else if (calleeName == "DeegenImpl_StartMakeInPlaceCallInfo")
                        {
                            foundList.push_back({ .m_inst = callInst, .m_isInPlaceCall = true, .m_passVariadicRet = false, .m_isMustTailCall = false });
                        }
                        else if (calleeName == "DeegenImpl_StartMakeInPlaceCallPassingVariadicResInfo")
                        {
                            foundList.push_back({ .m_inst = callInst, .m_isInPlaceCall = true, .m_passVariadicRet = true, .m_isMustTailCall = false });
                        }
                        else if (calleeName == "DeegenImpl_StartMakeTailCallInfo")
                        {
                            foundList.push_back({ .m_inst = callInst, .m_isInPlaceCall = false, .m_passVariadicRet = false, .m_isMustTailCall = true });
                        }
                        else if (calleeName == "DeegenImpl_StartMakeTailCallPassingVariadicResInfo")
                        {
                            foundList.push_back({ .m_inst = callInst, .m_isInPlaceCall = false, .m_passVariadicRet = true, .m_isMustTailCall = true });
                        }
                        else if (calleeName == "DeegenImpl_StartMakeInPlaceTailCallInfo")
                        {
                            foundList.push_back({ .m_inst = callInst, .m_isInPlaceCall = true, .m_passVariadicRet = false, .m_isMustTailCall = true });
                        }
                        else if (calleeName == "DeegenImpl_StartMakeInPlaceTailCallPassingVariadicResInfo")
                        {
                            foundList.push_back({ .m_inst = callInst, .m_isInPlaceCall = true, .m_passVariadicRet = true, .m_isMustTailCall = true });
                        }
                    }
                }
            }
        }
    }

    for (Item& item : foundList)
    {
        std::vector<CallInst*> callInstsToRemove;

        CallInst* inst = item.m_inst;
        ReleaseAssert(llvm_value_has_type<void*>(inst));
        std::vector<Value*> md;
        std::vector<Arg> args;
        md.resize(x_ord_arg_start);

        while (true)
        {
            callInstsToRemove.push_back(inst);

            ReleaseAssert(++(inst->user_begin()) == inst->user_end());
            User* user = *inst->user_begin();
            Instruction* userInst = dyn_cast<Instruction>(user);
            ReleaseAssert(userInst != nullptr);
            CallInst* userCallInst = dyn_cast<CallInst>(userInst);
            ReleaseAssert(userCallInst != nullptr);
            ReleaseAssert(userCallInst->arg_size() > 0 && userCallInst->getArgOperand(0) == inst);
            Function* callee = userCallInst->getCalledFunction();
            ReleaseAssert(callee != nullptr);
            ReleaseAssert(userCallInst->arg_size() == callee->arg_size());
            std::string calleeName = callee->getName().str();

            if (calleeName == "DeegenImpl_MakeCall_ReportParam")
            {
                ReleaseAssert(userCallInst->arg_size() == 2);
                args.push_back({ userCallInst->getArgOperand(1) });
            }
            else if (calleeName == "DeegenImpl_MakeCall_ReportParamList")
            {
                ReleaseAssert(userCallInst->arg_size() == 3);
                args.push_back({ userCallInst->getArgOperand(1), userCallInst->getArgOperand(2) });
            }
            else if (calleeName == "DeegenImpl_MakeCall_ReportTarget")
            {
                ReleaseAssert(userCallInst->arg_size() == 2);
                ReleaseAssert(md[x_ord_target] == nullptr);
                md[x_ord_target] = userCallInst->getArgOperand(1);
            }
            else if (calleeName == "DeegenImpl_MakeCall_ReportOption")
            {
                ReleaseAssert(userCallInst->arg_size() == 2);
                ReleaseAssert(md[x_ord_callOption] == nullptr);
                md[x_ord_callOption] = userCallInst->getArgOperand(1);
            }
            else if (calleeName == "DeegenImpl_MakeCall_ReportContinuationAfterCall")
            {
                ReleaseAssert(md[x_ord_continuation] == nullptr);
                md[x_ord_continuation] = userCallInst->getArgOperand(1);
                inst = userCallInst;
                callInstsToRemove.push_back(inst);
                break;
            }
            else
            {
                fprintf(stderr, "Unexpected callee name %s\n", calleeName.c_str());
                abort();
            }

            inst = userCallInst;
            ReleaseAssert(llvm_value_has_type<void*>(inst));
        }

        ReleaseAssert(md[x_ord_target] != nullptr);
        ReleaseAssert(llvm_value_has_type<uint64_t>(md[x_ord_target]));

        ReleaseAssert(md[x_ord_continuation] != nullptr);
        ReleaseAssert(dyn_cast<Function>(md[x_ord_continuation]) != nullptr);

        if (md[x_ord_callOption] == nullptr)
        {
            md[x_ord_callOption] = CreateLLVMConstantInt<size_t>(ctx, static_cast<size_t>(MakeCallOption::NoOption));
        }
        else
        {
            ReleaseAssert(llvm_value_has_type<uint64_t>(md[x_ord_callOption]));
            ReleaseAssert(dyn_cast<Constant>(md[x_ord_callOption]) != nullptr);
        }

        md[x_ord_inplaceCall] = CreateLLVMConstantInt<bool>(ctx, item.m_isInPlaceCall);
        md[x_ord_passVariadicRet] = CreateLLVMConstantInt<bool>(ctx, item.m_passVariadicRet);
        md[x_ord_isMustTailCall] = CreateLLVMConstantInt<bool>(ctx, item.m_isMustTailCall);

        std::vector<bool /*isArgRange*/> argDesc;
        for (Arg& arg : args)
        {
            argDesc.push_back(arg.IsArgRange());
        }

        Function* createdFn = CreatePlaceholderFunction(module, argDesc);

        for (Arg& arg : args)
        {
            if (arg.IsArgRange())
            {
                md.push_back(arg.GetArgStart());
                md.push_back(arg.GetArgNum());
            }
            else
            {
                md.push_back(arg.GetArg());
            }
        }

        ReleaseAssert(md.size() == createdFn->arg_size());
        for (size_t i = 0; i < md.size(); i++)
        {
            ReleaseAssert(md[i] != nullptr);
            ReleaseAssert(md[i]->getType() == createdFn->getFunctionType()->getFunctionParamType(static_cast<uint32_t>(i)));
        }

        CallInst* createdCallInst = CallInst::Create(createdFn, md);
        ReleaseAssert(llvm_value_has_type<void>(createdCallInst));
        createdCallInst->insertAfter(inst);

        // Remove in reverse order so the instruction being removed has no user
        // Not sure if LLVM requires this or not, but it definitely doesn't hurt
        //
        std::reverse(callInstsToRemove.begin(), callInstsToRemove.end());
        for (CallInst* callInstToRemove : callInstsToRemove)
        {
            ReleaseAssert(callInstToRemove->use_begin() == callInstToRemove->use_end());
            callInstToRemove->eraseFromParent();
        }
    }
}

}   // namespace dast
