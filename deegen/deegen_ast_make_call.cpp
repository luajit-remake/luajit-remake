#include "deegen_ast_make_call.h"
#include "deegen_interpreter_interface.h"

#include "bytecode.h"

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
                    item.m_passVariadicRes = GetValueOfLLVMConstantInt<bool>(callInst->getArgOperand(x_ord_passVariadicRes));
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
        bool m_passVariadicRes;
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
                            foundList.push_back({ .m_inst = callInst, .m_isInPlaceCall = false, .m_passVariadicRes = false, .m_isMustTailCall = false });
                        }
                        else if (calleeName == "DeegenImpl_StartMakeCallPassingVariadicResInfo")
                        {
                            foundList.push_back({ .m_inst = callInst, .m_isInPlaceCall = false, .m_passVariadicRes = true, .m_isMustTailCall = false });
                        }
                        else if (calleeName == "DeegenImpl_StartMakeInPlaceCallInfo")
                        {
                            foundList.push_back({ .m_inst = callInst, .m_isInPlaceCall = true, .m_passVariadicRes = false, .m_isMustTailCall = false });
                        }
                        else if (calleeName == "DeegenImpl_StartMakeInPlaceCallPassingVariadicResInfo")
                        {
                            foundList.push_back({ .m_inst = callInst, .m_isInPlaceCall = true, .m_passVariadicRes = true, .m_isMustTailCall = false });
                        }
                        else if (calleeName == "DeegenImpl_StartMakeTailCallInfo")
                        {
                            foundList.push_back({ .m_inst = callInst, .m_isInPlaceCall = false, .m_passVariadicRes = false, .m_isMustTailCall = true });
                        }
                        else if (calleeName == "DeegenImpl_StartMakeTailCallPassingVariadicResInfo")
                        {
                            foundList.push_back({ .m_inst = callInst, .m_isInPlaceCall = false, .m_passVariadicRes = true, .m_isMustTailCall = true });
                        }
                        else if (calleeName == "DeegenImpl_StartMakeInPlaceTailCallInfo")
                        {
                            foundList.push_back({ .m_inst = callInst, .m_isInPlaceCall = true, .m_passVariadicRes = false, .m_isMustTailCall = true });
                        }
                        else if (calleeName == "DeegenImpl_StartMakeInPlaceTailCallPassingVariadicResInfo")
                        {
                            foundList.push_back({ .m_inst = callInst, .m_isInPlaceCall = true, .m_passVariadicRes = true, .m_isMustTailCall = true });
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
        md[x_ord_passVariadicRes] = CreateLLVMConstantInt<bool>(ctx, item.m_passVariadicRes);
        md[x_ord_isMustTailCall] = CreateLLVMConstantInt<bool>(ctx, item.m_isMustTailCall);

        if (item.m_isInPlaceCall)
        {
            ReleaseAssert(args.size() == 1);
            ReleaseAssert(args[0].IsArgRange());
        }

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

llvm::Function* WARN_UNUSED AstMakeCall::GetContinuationDispatchTarget()
{
    using namespace llvm;

    std::string rcImplName = m_continuation->getName().str();
    ReleaseAssert(rcImplName.ends_with("_impl"));
    std::string rcFinalName = rcImplName.substr(0, rcImplName.length() - strlen("_impl"));

    Module* module = m_continuation->getParent();
    ReleaseAssert(module != nullptr);

    LLVMContext& ctx = module->getContext();
    FunctionType* fty = InterpreterFunctionInterface::GetInterfaceFunctionType(ctx);

    Function* func = module->getFunction(rcFinalName);
    if (func != nullptr)
    {
        ReleaseAssert(func->getFunctionType() == fty);
        return func;
    }
    else
    {
        ReleaseAssert(module->getNamedValue(rcFinalName) == nullptr);
        func = Function::Create(fty, GlobalVariable::LinkageTypes::ExternalLinkage, rcFinalName, module);
        ReleaseAssert(func != nullptr && func->getName() == rcFinalName);
        return func;
    }
}

void AstMakeCall::DoLoweringForInterpreter(InterpreterFunctionInterface* ifi)
{
    // Currently our call scheme is as follows:
    // Caller side:
    // 1. Construct the callee stack frame assuming callee does not take varargs.
    // 2. Move the stack frame if the call is required to be a tail call.
    // 3. Pass (coroCtx, newStackBase, #args, isMustTail) to callee.
    //
    // Callee side entry point does the following:
    // 1. Get codeblock and decode info like whether it takes varargs, # of static args, bytecode, etc.
    // 2. Pad nils if # of args is insufficient, memmove stack if # of args overflows and it takes varargs
    //    (note that special care is needed for the varargs stack rearrangement if the call is mustTail).
    // 3. Pass (coroCtx, fixedUpStackBase, bytecode, codeBlock) to first bytecode
    //
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();

    size_t numRangeArgs = 0;
    for (Arg& arg : m_args)
    {
        if (arg.IsArgRange())
        {
            numRangeArgs++;
        }
    }

    if (numRangeArgs > 1)
    {
        fprintf(stderr, "Support for calls with more than one ranged-arguments are currently unimplemented.\n");
        abort();
    }

    Value* newSfBase = nullptr;
    Value* totalNumArgs = nullptr;
    if (!m_isMustTailCall)
    {
        if (m_isInPlaceCall)
        {
            ReleaseAssert(m_args.size() == 1 && m_args[0].IsArgRange());
            Value* argStart = m_args[0].GetArgStart();
            Value* argNum = m_args[0].GetArgNum();
            if (!m_passVariadicRes)
            {
                newSfBase = argStart;
                totalNumArgs = argNum;
            }
            else
            {
                newSfBase = argStart;

                // Compute 'totalNumArgs = argNum + numVRes'
                //
                Value* numVRes = ifi->CallDeegenCommonSnippet("GetNumVariadicResults", { ifi->GetCoroutineCtx() }, m_origin /*insertBefore*/);
                ReleaseAssert(llvm_value_has_type<uint32_t>(numVRes));
                Value* numVRes64 = new ZExtInst(numVRes, llvm_type_of<uint64_t>(ctx), "", m_origin /*insertBefore*/);
                totalNumArgs = CreateUnsignedAddNoOverflow(argNum, numVRes64, m_origin /*insertBefore*/);

                // Set up the call frame by copying variadic results to the right position.
                // In this case, using CopyVariadicResultsToArgumentsForward is fine:
                //     1. If the variadic result is in the varargs region, then it doesn't overlap with the dest region
                //     2. If the variadic result comes from a function return, then its address is > dest region
                // In both cases it is correct to do a forward copy.
                //
                Value* copyDst = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx) /*pointeeType*/, argStart, { argNum }, "", m_origin /*insertBefore*/);
                std::ignore = ifi->CallDeegenCommonSnippet("CopyVariadicResultsToArgumentsForward", { copyDst, ifi->GetStackBase(), ifi->GetCoroutineCtx() }, m_origin /*insertBefore*/);
            }

            std::ignore = ifi->CallDeegenCommonSnippet(
                "PopulateNewCallFrameHeader",
                {
                    newSfBase,
                    ifi->GetStackBase() /*oldStackBase*/,
                    ifi->GetCodeBlock(),
                    ifi->GetCurBytecode(),
                    m_target,
                    GetContinuationDispatchTarget() /*onReturn*/,
                    CreateLLVMConstantInt<bool>(ctx, true) /*doNotFillFunc*/
                },
                m_origin /*insertBefore*/);
        }
        else
        {
            // Figure out how many arguments (excluding the variadic results part) we have
            //
            uint64_t numSingletonArgs = 0;
            for (Arg& arg : m_args)
            {
                if (!arg.IsArgRange())
                {
                    numSingletonArgs++;
                }
            }
            Value* argNum = CreateLLVMConstantInt<uint64_t>(ctx, numSingletonArgs);
            for (Arg& arg : m_args)
            {
                if (arg.IsArgRange())
                {
                    argNum = CreateUnsignedAddNoOverflow(argNum, arg.GetArgNum(), m_origin /*insertBefore*/);
                }
            }

            // Figure out the base of the new stack frame
            //
            Value* endOfStackFrame = ifi->CallDeegenCommonSnippet("GetEndOfCallFrame", { ifi->GetStackBase(), ifi->GetCodeBlock() }, m_origin /*insertBefore*/);
            ReleaseAssert(llvm_value_has_type<void*>(endOfStackFrame));

            newSfBase = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx) /*pointeeType*/, endOfStackFrame, { CreateLLVMConstantInt<uint64_t>(ctx, x_numSlotsForStackFrameHeader) }, "", m_origin);

            // If the call shall pass variadic results, copy it now.
            // A forward copy can be incorrect here since the fixed arg part may have make copyDest > copySrc,
            // so for now, we unconditionally use memmove. We might be able to do better, but the important case right now
            // for !inPlaceCall does not pass variadic results
            //
            if (m_passVariadicRes)
            {
                Value* numVRes = ifi->CallDeegenCommonSnippet("GetNumVariadicResults", { ifi->GetCoroutineCtx() }, m_origin /*insertBefore*/);
                ReleaseAssert(llvm_value_has_type<uint32_t>(numVRes));
                Value* numVRes64 = new ZExtInst(numVRes, llvm_type_of<uint64_t>(ctx), "", m_origin /*insertBefore*/);
                totalNumArgs = CreateUnsignedAddNoOverflow(argNum, numVRes64, m_origin /*insertBefore*/);

                Value* copyDst = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx) /*pointeeType*/, newSfBase, { argNum }, "", m_origin /*insertBefore*/);
                ifi->CallDeegenCommonSnippet("CopyVariadicResultsToArguments", { copyDst, ifi->GetStackBase(), ifi->GetCoroutineCtx() }, m_origin /*insertBefore*/);
            }
            else
            {
                totalNumArgs = argNum;
            }

            // Now, populate all the remaining arguments
            //
            Value* curOffset = CreateLLVMConstantInt<uint64_t>(ctx, 0);
            for (Arg& arg : m_args)
            {
                Value* dstStart = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx) /*pointeeType*/, newSfBase, { curOffset }, "", m_origin);
                if (arg.IsArgRange())
                {
                    Function* memcpyFunc = Intrinsic::getDeclaration(ifi->GetModule(), Intrinsic::memcpy, { llvm_type_of<uint64_t>(ctx) });
                    Value* bytesToCpy = CreateUnsignedMulNoOverflow(arg.GetArgNum(), CreateLLVMConstantInt<uint64_t>(ctx, sizeof(uint64_t)), m_origin);
                    CallInst::Create(memcpyFunc, { dstStart, arg.GetArgStart(), bytesToCpy, CreateLLVMConstantInt<bool>(ctx, false) /*isVolatile*/ }, "", m_origin);
                    curOffset = CreateUnsignedAddNoOverflow(curOffset, arg.GetArgNum(), m_origin);
                }
                else
                {
                    Value* argValue = arg.GetArg();
                    ReleaseAssert(llvm_value_has_type<uint64_t>(argValue));
                    std::ignore = new StoreInst(argValue, dstStart, false /*isVolatile*/, Align(8), m_origin);
                    curOffset = CreateUnsignedAddNoOverflow(curOffset, CreateLLVMConstantInt<uint64_t>(ctx, 1), m_origin);
                }
            }

            std::ignore = ifi->CallDeegenCommonSnippet(
                "PopulateNewCallFrameHeader",
                {
                    newSfBase,
                    ifi->GetStackBase() /*oldStackBase*/,
                    ifi->GetCodeBlock(),
                    ifi->GetCurBytecode(),
                    m_target,
                    GetContinuationDispatchTarget() /*onReturn*/,
                    CreateLLVMConstantInt<bool>(ctx, false) /*doNotFillFunc*/
                },
                m_origin /*insertBefore*/);
        }
    }
    else
    {
        ReleaseAssert(false && "unimplemented");
    }

    ReleaseAssert(newSfBase != nullptr);
    ReleaseAssert(totalNumArgs != nullptr);

    Value* entryPoint = ifi->CallDeegenCommonSnippet("GetCalleeEntryPoint", { m_target }, m_origin /*insertBefore*/);
    ReleaseAssert(llvm_value_has_type<void*>(entryPoint));

    ifi->CreateDispatchToCallee(entryPoint, ifi->GetCoroutineCtx(), newSfBase, totalNumArgs, CreateLLVMConstantInt<bool>(ctx, m_isMustTailCall), m_origin /*insertBefore*/);

    AssertInstructionIsFollowedByUnreachable(m_origin);
    Instruction* unreachableInst = m_origin->getNextNode();
    m_origin->eraseFromParent();
    unreachableInst->eraseFromParent();
    m_origin = nullptr;
}

}   // namespace dast
