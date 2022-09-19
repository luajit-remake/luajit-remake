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
                    if (item.m_isMustTailCall)
                    {
                        ReleaseAssert(dyn_cast<ConstantPointerNull>(callInst->getArgOperand(x_ord_continuation)) != nullptr);
                        item.m_continuation = nullptr;
                    }
                    else
                    {
                        item.m_continuation = dyn_cast<Function>(callInst->getArgOperand(x_ord_continuation));
                        ReleaseAssert(item.m_continuation != nullptr);
                    }

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
        if (item.m_isMustTailCall)
        {
            ReleaseAssert(dyn_cast<ConstantPointerNull>(md[x_ord_continuation]) != nullptr);
        }
        else
        {
            ReleaseAssert(dyn_cast<Function>(md[x_ord_continuation]) != nullptr);
        }

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

    ReleaseAssert(m_continuation != nullptr);
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
                // In this case, using CopyVariadicResultsToArgumentsForwardMayOvercopy is fine:
                //     1. If the variadic result is in the varargs region, then it doesn't overlap with the dest region
                //     2. If the variadic result comes from a function return, then its address is > dest region
                // In both cases it is correct to do a forward copy.
                //
                Value* copyDst = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx) /*pointeeType*/, argStart, { argNum }, "", m_origin /*insertBefore*/);
                ifi->CallDeegenCommonSnippet(
                    "CopyVariadicResultsToArgumentsForwardMayOvercopy",
                    {
                        copyDst,
                        ifi->GetStackBase(),
                        ifi->GetCoroutineCtx()
                    },
                    m_origin /*insertBefore*/);
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
            // for !inPlaceCall is those which does not pass variadic results
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
                    Value* bytesToCpy = CreateUnsignedMulNoOverflow(arg.GetArgNum(), CreateLLVMConstantInt<uint64_t>(ctx, sizeof(uint64_t)), m_origin);
                    EmitLLVMIntrinsicMemcpy(ifi->GetModule(), dstStart, arg.GetArgStart(), bytesToCpy, m_origin);
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

            ValidateLLVMModule(ifi->GetModule());
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
        // This is a tail call.
        // Basically we need to replace the current call frame by the new call frame.
        // Note that if the current call frame has var args, we must put the new frame at the beginning of the var arg region,
        // so that unbounded tail call will not cause unbounded stack growth.
        //
        // As in the non-tail case, the callee side is responsible for shuffling the call frame to set up the varargs region
        // (and to pay special attention not to change the call frame pointer, so that the no-unbounded-stack-growth guarantee is maintained).
        //
        if (!m_passVariadicRes)
        {
            // The call does not pass variadic results.
            // This is the good case, since we don't need to worry about the possibility of clobbering the variadic results.
            //
            // 1. If the current frame has variadic args (unlikely), move the frame header to the true frame beginning
            // 2. Populate 'm_func' in call frame header and fill 'numVarArgs' field to 0.
            //
            Value* newStackFrameBase = ifi->CallDeegenCommonSnippet(
                "MoveCallFrameHeaderForTailCall",
                {
                    ifi->GetStackBase(),
                    m_target
                },
                m_origin /*insertBefore*/);
            ReleaseAssert(llvm_value_has_type<void*>(newStackFrameBase));

            // 3. Set up the arguments
            //
            if (m_isInPlaceCall)
            {
                ReleaseAssert(m_args.size() == 1 && m_args[0].IsArgRange());
                Value* argStart = m_args[0].GetArgStart();
                Value* argNum = m_args[0].GetArgNum();

                // In-place call has only one argument range, just move it to the right place
                // Note that simply moving left-to-right is always correct in this case
                //
                ifi->CallDeegenCommonSnippet(
                    "SimpleLeftToRightCopyMayOvercopy",
                    {
                        newStackFrameBase,
                        argStart,
                        argNum
                    },
                    m_origin /*insertBefore*/);

                newSfBase = newStackFrameBase;
                totalNumArgs = argNum;
            }
            else
            {
                // For not-in-place call, since currently we only support one argument range,
                // we can move the argument range first (a memmove is required), then set up the remaining arguments
                //
                bool foundArgRange = false;
                {
                    size_t argRangeOffset = 0;
                    Value* argStart = nullptr;
                    Value* argNum = nullptr;
                    for (Arg& arg : m_args)
                    {
                        if (arg.IsArgRange())
                        {
                            foundArgRange = true;
                            argStart = arg.GetArgStart();
                            argNum = arg.GetArgNum();
                            break;
                        }
                        else
                        {
                            argRangeOffset++;
                        }
                    }

                    if (foundArgRange)
                    {
                        Value* dstStart = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx) /*pointeeType*/, newStackFrameBase, { CreateLLVMConstantInt<uint64_t>(ctx, argRangeOffset) }, "", m_origin);
                        Value* bytesToCpy = CreateUnsignedMulNoOverflow(argNum, CreateLLVMConstantInt<uint64_t>(ctx, sizeof(uint64_t)), m_origin);
                        EmitLLVMIntrinsicMemmove(ifi->GetModule(), dstStart, argStart, bytesToCpy, m_origin);
                    }
                }

                // Now, fill in every singleton argument
                //
                bool expectFoundArgRange = false;
                Value* curOffset = CreateLLVMConstantInt<uint64_t>(ctx, 0);
                for (Arg& arg : m_args)
                {
                    if (arg.IsArgRange())
                    {
                        ReleaseAssert(foundArgRange);
                        ReleaseAssert(!expectFoundArgRange);
                        expectFoundArgRange = true;
                        curOffset = CreateUnsignedAddNoOverflow(curOffset, arg.GetArgNum(), m_origin);
                    }
                    else
                    {
                        Value* dst = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx) /*pointeeType*/, newStackFrameBase, { curOffset }, "", m_origin);
                        Value* argValue = arg.GetArg();
                        ReleaseAssert(llvm_value_has_type<uint64_t>(argValue));
                        std::ignore = new StoreInst(argValue, dst, false /*isVolatile*/, Align(8), m_origin);
                        curOffset = CreateUnsignedAddNoOverflow(curOffset, CreateLLVMConstantInt<uint64_t>(ctx, 1), m_origin);
                    }
                }
                ReleaseAssertIff(foundArgRange, expectFoundArgRange);

                newSfBase = newStackFrameBase;
                totalNumArgs = curOffset;
            }
        }
        else
        {
            if (m_isInPlaceCall)
            {
                // This is still a relatively good case. We can set up the frame by the following:
                //
                // 1. Move argument part to the start of the current call frame, a left-to-right copy is fine
                // 2. Move variadic results part after the arguments (note that this must happen after the first step,
                //    as otherwise the argument part could be clobbered), a left-to-right copy is fine
                // 3. If the the current frame has variadic args (unlikely), move everything to the true frame beginning, a left-to-right copy is fine
                // 4. Populate 'm_func' in call frame header and fill 'numVarArgs' field to 0.
                //
                // We could have done better for the 'current frame has variadic args' case if we are willing to add more branches..
                // But for now let's stay simple.. Variadic function tail call is really corner case any way.
                //
                ReleaseAssert(m_args.size() == 1 && m_args[0].IsArgRange());
                Value* argStart = m_args[0].GetArgStart();
                Value* argNum = m_args[0].GetArgNum();

                ifi->CallDeegenCommonSnippet(
                    "SimpleLeftToRightCopyMayOvercopy",
                    {
                        ifi->GetStackBase(),
                        argStart,
                        argNum
                    },
                    m_origin /*insertBefore*/);

                Value* numVRes = ifi->CallDeegenCommonSnippet("GetNumVariadicResults", { ifi->GetCoroutineCtx() }, m_origin /*insertBefore*/);
                ReleaseAssert(llvm_value_has_type<uint32_t>(numVRes));
                Value* numVRes64 = new ZExtInst(numVRes, llvm_type_of<uint64_t>(ctx), "", m_origin /*insertBefore*/);
                totalNumArgs = CreateUnsignedAddNoOverflow(argNum, numVRes64, m_origin /*insertBefore*/);

                Value* vaDst = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx) /*pointeeType*/, ifi->GetStackBase(), { argNum }, "", m_origin);
                ifi->CallDeegenCommonSnippet(
                    "CopyVariadicResultsToArgumentsForwardMayOvercopy",
                    {
                        vaDst,
                        ifi->GetStackBase(),
                        ifi->GetCoroutineCtx()
                    },
                    m_origin /*insertBefore*/);

                newSfBase = ifi->CallDeegenCommonSnippet(
                    "MoveCallFrameForTailCall",
                    {
                        ifi->GetStackBase(),
                        m_target,
                        totalNumArgs
                    },
                    m_origin /*insertBefore*/);
                ReleaseAssert(llvm_value_has_type<void*>(newSfBase));
            }
            else
            {
                // This is an annoying case, but also a very rare case (in LJR it only happens for a variadic tail call that invokes a call metatable)
                // There is no fixed order which guarantees that nothing is clobbered.
                // Currently we just try to stay simple by doing the following (there's a lot we can improve
                // to do better, but as mentioned above this case is rare so not worth optimizing for now):
                //    1. Compute the place the variadic res is supposed to be. If the current variadic res is not pointing to the vararg region
                //       and is to the left of its expected location, we need to move it. (if the varres is pointing at vararg region,
                //       we should not move it as it can clobber the existing arg range..)
                //    2. Move the arguments to their expected place.
                //    3. Move the vararg to the expected place, if it hasn't been moved in step 1.
                //    4. If the the current frame has variadic args (unlikely), move everything to the true frame beginning, a left-to-right copy is fine
                //    5. Populate 'm_func' in call frame header and fill 'numVarArgs' field to 0.
                //

                bool foundArgRange = false;
                uint64_t numSingletonArgs = 0;
                size_t argRangeStartOffset = 0;
                Arg* argRange = nullptr;
                for (Arg& arg : m_args)
                {
                    if (!arg.IsArgRange())
                    {
                        numSingletonArgs++;
                    }
                    else
                    {
                        ReleaseAssert(!foundArgRange);
                        foundArgRange = true;
                        argRangeStartOffset = numSingletonArgs;
                        argRange = &arg;
                    }
                }
                totalNumArgs = CreateLLVMConstantInt<uint64_t>(ctx, numSingletonArgs);
                for (Arg& arg : m_args)
                {
                    if (arg.IsArgRange())
                    {
                        totalNumArgs = CreateUnsignedAddNoOverflow(totalNumArgs, arg.GetArgNum(), m_origin /*insertBefore*/);
                    }
                }

                Value* varResMoved = ifi->CallDeegenCommonSnippet(
                    "MoveVariadicResultsForVariadicNotInPlaceTailCall",
                    {
                        ifi->GetStackBase(),
                        ifi->GetCoroutineCtx(),
                        totalNumArgs
                    },
                    m_origin /*insertBefore*/);
                ReleaseAssert(llvm_value_has_type<bool>(varResMoved));

                // Since currently we only support one argument range,
                // we can move the argument range first (a memmove is required), then set up the remaining arguments
                //
                if (foundArgRange)
                {
                    Value* dstStart = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx) /*pointeeType*/, ifi->GetStackBase(), { CreateLLVMConstantInt<uint64_t>(ctx, argRangeStartOffset) }, "", m_origin);
                    Value* bytesToCpy = CreateUnsignedMulNoOverflow(argRange->GetArgNum(), CreateLLVMConstantInt<uint64_t>(ctx, sizeof(uint64_t)), m_origin);
                    EmitLLVMIntrinsicMemmove(ifi->GetModule(), dstStart, argRange->GetArgStart(), bytesToCpy, m_origin);
                }

                // Now, fill in every singleton argument
                //
                bool expectFoundArgRange = false;
                Value* curOffset = CreateLLVMConstantInt<uint64_t>(ctx, 0);
                for (Arg& arg : m_args)
                {
                    if (arg.IsArgRange())
                    {
                        ReleaseAssert(foundArgRange);
                        ReleaseAssert(!expectFoundArgRange);
                        expectFoundArgRange = true;
                        curOffset = CreateUnsignedAddNoOverflow(curOffset, arg.GetArgNum(), m_origin);
                    }
                    else
                    {
                        Value* dst = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx) /*pointeeType*/, ifi->GetStackBase(), { curOffset }, "", m_origin);
                        Value* argValue = arg.GetArg();
                        ReleaseAssert(llvm_value_has_type<uint64_t>(argValue));
                        std::ignore = new StoreInst(argValue, dst, false /*isVolatile*/, Align(8), m_origin);
                        curOffset = CreateUnsignedAddNoOverflow(curOffset, CreateLLVMConstantInt<uint64_t>(ctx, 1), m_origin);
                    }
                }
                ReleaseAssertIff(foundArgRange, expectFoundArgRange);

                totalNumArgs = ifi->CallDeegenCommonSnippet(
                    "CopyVariadicResultsToArgumentsForVariadicNotInPlaceTailCall",
                    {
                        varResMoved,
                        totalNumArgs,
                        ifi->GetStackBase(),
                        ifi->GetCoroutineCtx()
                    },
                    m_origin /*insertBefore*/);
                ReleaseAssert(llvm_value_has_type<uint64_t>(totalNumArgs));

                newSfBase = ifi->CallDeegenCommonSnippet(
                    "MoveCallFrameForTailCall",
                    {
                        ifi->GetStackBase(),
                        m_target,
                        totalNumArgs
                    },
                    m_origin /*insertBefore*/);
                ReleaseAssert(llvm_value_has_type<void*>(newSfBase));
            }
        }
    }

    ReleaseAssert(newSfBase != nullptr);
    ReleaseAssert(llvm_value_has_type<void*>(newSfBase));
    ReleaseAssert(totalNumArgs != nullptr);
    ReleaseAssert(llvm_value_has_type<uint64_t>(totalNumArgs));

    Value* codeBlockAndEntryPoint = ifi->CallDeegenCommonSnippet("GetCalleeEntryPoint", { m_target }, m_origin /*insertBefore*/);
    ReleaseAssert(codeBlockAndEntryPoint->getType()->isAggregateType());

    Value* calleeCbHeapPtr = ExtractValueInst::Create(codeBlockAndEntryPoint, { 0 /*idx*/ }, "", m_origin /*insertBefore*/);
    Value* codePointer = ExtractValueInst::Create(codeBlockAndEntryPoint, { 1 /*idx*/ }, "", m_origin /*insertBefore*/);
    ReleaseAssert(llvm_value_has_type<void*>(codePointer));

    ifi->CreateDispatchToCallee(codePointer, ifi->GetCoroutineCtx(), newSfBase, calleeCbHeapPtr, totalNumArgs, CreateLLVMConstantInt<bool>(ctx, m_isMustTailCall), m_origin /*insertBefore*/);

    AssertInstructionIsFollowedByUnreachable(m_origin);
    Instruction* unreachableInst = m_origin->getNextNode();
    m_origin->eraseFromParent();
    unreachableInst->eraseFromParent();
    m_origin = nullptr;
}

}   // namespace dast
