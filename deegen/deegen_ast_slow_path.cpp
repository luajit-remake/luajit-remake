#include "deegen_ast_slow_path.h"
#include "deegen_analyze_lambda_capture_pass.h"
#include "deegen_rewrite_closure_call.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_interpreter_function_interface.h"

namespace dast {

constexpr const char* x_slowPathImplLambdaFunctionNamePrefix = "__deegen_internal_slow_path_lambda_impl_";
constexpr const char* x_slowPathAnnotationLambdaFunctionNamePrefix = "__DeegenImpl_AnnotateSlowPath_IdentificationFunc_";

static void AstSlowPathPreprocessOneUse(llvm::Module* module, llvm::CallInst* origin)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    ReleaseAssert(origin->arg_size() >= 1);

    Function* slowPathFn = dyn_cast<Function>(origin->getArgOperand(0));
    if (slowPathFn == nullptr)
    {
        fprintf(stderr, "[ERROR] The EnterSlowPath API did not specify the slow path function in the first parameter! Offending LLVM IR:\n");
        origin->dump();
        abort();
    }

    if (slowPathFn->getLinkage() != GlobalValue::InternalLinkage)
    {
        fprintf(stderr, "[ERROR] The function '%s' called by EnterSlowPath must be marked static!\n", slowPathFn->getName().str().c_str());
        abort();
    }

    if (!slowPathFn->hasFnAttribute(Attribute::AttrKind::NoReturn))
    {
        fprintf(stderr, "[ERROR] The function '%s' called by EnterSlowPath must be NO_RETURN!\n", slowPathFn->getName().str().c_str());
        abort();
    }

    // Rename slowPathFn if it hasn't been renamed yet
    //
    {
        std::string slowPathFnName = slowPathFn->getName().str();
        if (!slowPathFnName.starts_with(x_slowPathImplLambdaFunctionNamePrefix))
        {
            std::string newName = GetFirstAvailableFunctionNameWithPrefix(module, x_slowPathImplLambdaFunctionNamePrefix);
            slowPathFn->setName(newName);
            ReleaseAssert(slowPathFn->getName() == newName);
        }
    }

    // Replace 'origin' by the annotation call for later processing
    //
    std::vector<Type*> tys;
    for (uint32_t i = 0; i < origin->arg_size(); i++) { tys.push_back(origin->getArgOperand(i)->getType()); }
    FunctionType* fty = FunctionType::get(llvm_type_of<void>(ctx) /*result*/, tys, false /*isVarArg*/);
    std::string annotationFnName = GetFirstAvailableFunctionNameWithPrefix(module, x_slowPathAnnotationLambdaFunctionNamePrefix);
    Function* annotationFn = Function::Create(fty, GlobalValue::ExternalLinkage, annotationFnName, module);
    ReleaseAssert(annotationFn->getName() == annotationFnName);
    origin->setCalledFunction(annotationFn);
}

void AstSlowPath::PreprocessModule(llvm::Module* module)
{
    using namespace llvm;
    std::vector<CallInst*> allUses;
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
                        std::string fnName = callee->getName().str();
                        if (IsCXXSymbol(fnName) && DemangleCXXSymbol(fnName).find(" DeegenImpl_MarkEnterSlowPath<") != std::string::npos)
                        {
                            allUses.push_back(callInst);
                        }
                    }
                }
            }
        }
    }

    for (CallInst* ci : allUses)
    {
        AstSlowPathPreprocessOneUse(module, ci);
    }

    ValidateLLVMModule(module);
}

void AstSlowPath::CheckWellFormedness(llvm::Function* bytecodeImplFunc)
{
    using namespace llvm;

    // Type-check everything, and check that structures have been decomposed into scalars
    //
    Function* slowPathFn = dyn_cast<Function>(m_origin->getArgOperand(0));
    if (slowPathFn == nullptr)
    {
        fprintf(stderr, "[ERROR] The EnterSlowPath API did not specify the slow path function in the first parameter! Offending LLVM IR:\n");
        m_origin->dump();
        abort();
    }

    if (bytecodeImplFunc->arg_size() > slowPathFn->arg_size())
    {
        fprintf(stderr, "[ERROR] The slow path function '%s' should start with the bytecode arguments.\n", slowPathFn->getName().str().c_str());
        abort();
    }

    for (uint32_t i = 0; i < bytecodeImplFunc->arg_size(); i++)
    {
        Type* expectedTy = bytecodeImplFunc->getArg(i)->getType();
        Type* actualTy = slowPathFn->getArg(i)->getType();
        if (actualTy != expectedTy)
        {
            fprintf(stderr, "[ERROR] The slow path function '%s' has unexpected argument type at arg %u (0-based). Expected %s got %s.\n",
                    slowPathFn->getName().str().c_str(),
                    static_cast<unsigned int>(i),
                    DumpLLVMTypeAsString(expectedTy).c_str(),
                    DumpLLVMTypeAsString(actualTy).c_str());
            abort();
        }
    }

    for (uint32_t i = 0; i < slowPathFn->arg_size(); i++)
    {
        Type* ty = slowPathFn->getArg(i)->getType();
        if (!ty->isPointerTy() && !ty->isIntegerTy() && !ty->isDoubleTy())
        {
            fprintf(stderr, "[ERROR] The slow path function '%s' has unsupported argument type at arg %u (0-based). "
                            "Only pointer, integer and double types are supported. Got %s instead.\n",
                    slowPathFn->getName().str().c_str(), static_cast<unsigned int>(i), DumpLLVMTypeAsString(ty).c_str());
            abort();
        }
    }

    if (bytecodeImplFunc->arg_size() + m_origin->arg_size() - 1 != slowPathFn->arg_size())
    {
        fprintf(stderr, "[ERROR] The slow path function '%s' has unexpected number of arguments. This could either be a bug in your code "
                        "or a deficiency in our system. You might want to workaround it by passing structures at the beginning.\n",
                slowPathFn->getName().str().c_str());
        abort();
    }

    for (uint32_t i = 1; i < m_origin->arg_size(); i++)
    {
        uint32_t slowPathFnArgOrd = static_cast<uint32_t>(bytecodeImplFunc->arg_size()) + i - 1;
        Type* expectedTy = m_origin->getArgOperand(i)->getType();
        Type* actualTy = slowPathFn->getArg(slowPathFnArgOrd)->getType();
        if (actualTy != expectedTy)
        {
            fprintf(stderr, "[ERROR] The slow path function '%s' has unexpected argument type at arg %u (0-based). Expected %s got %s."
                            "This could either be a bug in your code or a deficiency in our system. You might want to workaround it by "
                            "passing structures at the beginning.\n",
                    slowPathFn->getName().str().c_str(),
                    static_cast<unsigned int>(slowPathFnArgOrd),
                    DumpLLVMTypeAsString(expectedTy).c_str(),
                    DumpLLVMTypeAsString(actualTy).c_str());
            abort();
        }
    }
}

llvm::Function* WARN_UNUSED AstSlowPath::GetImplFunction()
{
    using namespace llvm;
    ReleaseAssert(m_origin->arg_size() > 0);
    Value* arg = m_origin->getArgOperand(0);
    Function* func = dyn_cast<Function>(arg);
    ReleaseAssert(func != nullptr);
    return func;
}

std::string WARN_UNUSED AstSlowPath::GetPostProcessSlowPathFunctionNameForInterpreter(llvm::Function* implFunc)
{
    return implFunc->getName().str() + "_interpreter_wrapper";
}

std::vector<AstSlowPath> AstSlowPath::GetAllUseInFunction(llvm::Function* func)
{
    using namespace llvm;
    std::vector<AstSlowPath> res;
    for (BasicBlock& bb : *func)
    {
        for (Instruction& inst : bb)
        {
            CallInst* callInst = dyn_cast<CallInst>(&inst);
            if (callInst != nullptr)
            {
                Function* callee = callInst->getCalledFunction();
                if (callee != nullptr && callee->getName().str().starts_with(x_slowPathAnnotationLambdaFunctionNamePrefix))
                {
                    res.push_back({ .m_origin = callInst });
                }
            }
        }
    }
    return res;
}

void AstSlowPath::LowerAllForInterpreterOrBaselineJIT(DeegenBytecodeImplCreatorBase* ifi, llvm::Function* func)
{
    std::vector<AstSlowPath> list = AstSlowPath::GetAllUseInFunction(func);
    for (AstSlowPath& slowPath : list)
    {
        slowPath.LowerForInterpreterOrBaselineJIT(ifi);
    }
}

static std::vector<uint64_t> WARN_UNUSED AstSlowPathGetInterpreterFunctionArgAssignment(const std::vector<llvm::Type*>& argTys)
{
    using namespace llvm;

    std::vector<uint64_t> gprList = InterpreterFunctionInterface::GetAvaiableGPRListForBytecodeSlowPath();
    std::vector<uint64_t> fprList = InterpreterFunctionInterface::GetAvaiableFPRListForBytecodeSlowPath();

    std::vector<uint64_t> gprArgs;
    std::vector<uint64_t> fprArgs;
    for (size_t i = 0; i < argTys.size(); i++)
    {
        Type* ty = argTys[i];
        if (ty->isFloatingPointTy())
        {
            // Passing a float should be fine at ABI level, but I'm not sure how to do the cast at LLVM IR level..
            // And we don't have such use case right now, so for now let's just abort
            //
            ReleaseAssert(llvm_type_has_type<double>(ty));
            fprArgs.push_back(i);
        }
        else
        {
            if (!ty->isPointerTy())
            {
                if (!ty->isIntegerTy())
                {
                    fprintf(stderr, "[LOCKDOWN] Passing non-word-sized struct to EnterSlowPath is currently unimplemented.\n");
                    abort();
                }
                if (ty->getIntegerBitWidth() > 64)
                {
                    fprintf(stderr, "[LOCKDOWN] Passing >64 bit integer type to EnterSlowPath is currently unimplemented.\n");
                    abort();
                }
            }
            gprArgs.push_back(i);
        }
    }

    if (fprArgs.size() > fprList.size() || gprArgs.size() > gprList.size())
    {
        // We can fix this issue by setting up an area in CoroutineRuntimeContext to pass the additional context
        // But for now let's stay simple and simply fail if there are too much context data
        //
        fprintf(stderr, "[LOCKDOWN] Too much context data for EnterSlowPath API.\n");
        abort();
    }

    std::vector<std::pair<uint64_t, uint64_t>> list;
    for (size_t i = 0; i < gprArgs.size(); i++)
    {
        list.push_back(std::make_pair(gprArgs[i], gprList[i]));
    }
    for (size_t i = 0; i < fprArgs.size(); i++)
    {
        list.push_back(std::make_pair(fprArgs[i], fprList[i]));
    }

    std::sort(list.begin(), list.end());

    std::vector<uint64_t> result;
    std::unordered_set<uint64_t> checkUnique;
    for (size_t i = 0; i < list.size(); i++)
    {
        ReleaseAssert(list[i].first == i);
        uint64_t argOrd = list[i].second;
        result.push_back(argOrd);
        ReleaseAssert(!checkUnique.count(argOrd));
        checkUnique.insert(argOrd);
    }
    ReleaseAssert(result.size() == argTys.size());
    return result;
}

void AstSlowPath::LowerForInterpreterOrBaselineJIT(DeegenBytecodeImplCreatorBase* ifi)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();

    std::string dispatchFnName;
    if (ifi->IsInterpreter())
    {
        dispatchFnName = GetPostProcessSlowPathFunctionNameForInterpreter(GetImplFunction());
    }
    else
    {
        ReleaseAssert(ifi->IsBaselineJIT());
        std::string implFnName = GetImplFunction()->getName().str();
        ReleaseAssert(implFnName.ends_with("_impl"));
        dispatchFnName = implFnName.substr(0, implFnName.length() - strlen("_impl"));
    }

    std::vector<uint64_t> argOrdList;
    {
        std::vector<Type*> argTys;
        for (uint32_t i = 1; i < m_origin->arg_size(); i++)
        {
            argTys.push_back(m_origin->getArgOperand(i)->getType());
        }
        argOrdList = AstSlowPathGetInterpreterFunctionArgAssignment(argTys);
    }
    ReleaseAssert(m_origin->arg_size() == argOrdList.size() + 1);

    std::vector<Value*> convertedArgs;
    FunctionType* fty = InterpreterFunctionInterface::GetType(ctx);
    for (uint32_t i = 0; i < argOrdList.size(); i++)
    {
        Value* src = m_origin->getArgOperand(i + 1);
        ReleaseAssert(argOrdList[i] < fty->getNumParams());
        Type* dstTy = fty->getParamType(static_cast<uint32_t>(argOrdList[i]));

        Value* cvt = nullptr;
        if (llvm_value_has_type<double>(src))
        {
            ReleaseAssert(llvm_type_has_type<double>(dstTy));
            cvt = src;
        }
        else
        {
            ReleaseAssert(llvm_type_has_type<uint64_t>(dstTy) || llvm_type_has_type<void*>(dstTy));
            if (src->getType()->isPointerTy())
            {
                if (llvm_type_has_type<uint64_t>(dstTy))
                {
                    cvt = new PtrToIntInst(src, dstTy, "", m_origin);
                }
                else
                {
                    if (llvm_value_has_type<void*>(src))
                    {
                        cvt = src;
                    }
                    else
                    {
                        ReleaseAssert(llvm_value_has_type<HeapPtr<void>>(src));
                        cvt = new AddrSpaceCastInst(src, dstTy, "", m_origin);
                    }
                }
            }
            else
            {
                Value* u64 = src;
                ReleaseAssert(src->getType()->isIntegerTy());
                ReleaseAssert(src->getType()->getIntegerBitWidth() <= 64);
                if (src->getType()->getIntegerBitWidth() < 64)
                {
                    u64 = new ZExtInst(src, llvm_type_of<uint64_t>(ctx), "", m_origin);
                }
                if (llvm_type_has_type<uint64_t>(dstTy))
                {
                    cvt = u64;
                }
                else
                {
                    cvt = new IntToPtrInst(u64, dstTy, "", m_origin);
                }
            }
        }
        ReleaseAssert(cvt != nullptr && cvt->getType() == dstTy);
        convertedArgs.push_back(cvt);
    }

    Function* dispatchFn = ifi->GetModule()->getFunction(dispatchFnName);
    if (dispatchFn == nullptr)
    {
        dispatchFn = InterpreterFunctionInterface::CreateFunction(ifi->GetModule(), dispatchFnName);
    }
    ReleaseAssert(dispatchFn->getFunctionType() == fty);

    Value* callSiteInfo;
    Value* cbOrBcb;
    if (ifi->IsInterpreter())
    {
        // For interpreter, we need to pass the bytecode pointer to the slow path
        //
        InterpreterBytecodeImplCreator* ibc = assert_cast<InterpreterBytecodeImplCreator*>(ifi);
        callSiteInfo = ibc->GetCurBytecode();
        cbOrBcb = ibc->GetInterpreterCodeBlock();
    }
    else
    {
        ReleaseAssert(ifi->IsBaselineJIT());
        // For baseline JIT, if we are in JIT'ed code, we need to pass the BaselineJitSlowPathData pointer by a stencil hole.
        // If we are already in baseline JIT slow path, we already have our BaselineJitSlowPathData pointer and just need to pass it around.
        //
        BaselineJitImplCreator* j = assert_cast<BaselineJitImplCreator*>(ifi);
        cbOrBcb = j->GetBaselineCodeBlock();
        if (j->IsBaselineJitSlowPath())
        {
            callSiteInfo = j->GetJitSlowPathData();
        }
        else
        {
            Value* offset = j->GetSlowPathDataOffsetFromJitFastPath(m_origin);
            Value* baselineJitCodeBlock = j->GetBaselineCodeBlock();
            ReleaseAssert(llvm_value_has_type<void*>(baselineJitCodeBlock));
            callSiteInfo = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), baselineJitCodeBlock, { offset }, "", m_origin);
        }
    }
    ReleaseAssert(llvm_value_has_type<void*>(callSiteInfo));

    CallInst* callInst = InterpreterFunctionInterface::CreateDispatchToBytecodeSlowPath(
        dispatchFn, ifi->GetCoroutineCtx(), ifi->GetStackBase(), callSiteInfo, cbOrBcb, m_origin /*insertBefore*/);

    for (size_t i = 0; i < argOrdList.size(); i++)
    {
        uint32_t argOrd = static_cast<uint32_t>(argOrdList[i]);
        Value* val = convertedArgs[i];
        ReleaseAssert(argOrd < callInst->arg_size());
        ReleaseAssert(callInst->getArgOperand(argOrd)->getType() == val->getType());
        callInst->setArgOperand(argOrd, val);
    }

    AssertInstructionIsFollowedByUnreachable(m_origin);
    Instruction* unreachableInst = m_origin->getNextNode();
    m_origin->eraseFromParent();
    unreachableInst->eraseFromParent();
    m_origin = nullptr;
}

std::vector<llvm::Value*> WARN_UNUSED AstSlowPath::CreateCallArgsInSlowPathWrapperFunction(uint32_t extraArgBegin, llvm::Function* implFunc, llvm::BasicBlock* bb)
{
    using namespace llvm;
    LLVMContext& ctx = implFunc->getContext();
    std::vector<uint64_t> argOrdList;
    {
        std::vector<Type*> argTys;
        for (uint32_t i = extraArgBegin; i < implFunc->arg_size(); i++)
        {
            argTys.push_back(implFunc->getArg(i)->getType());
        }
        argOrdList = AstSlowPathGetInterpreterFunctionArgAssignment(argTys);
    }

    Function* func = bb->getParent();
    ReleaseAssert(func != nullptr);

    std::vector<llvm::Value*> result;
    for (uint32_t i = 0; i < argOrdList.size(); i++)
    {
        ReleaseAssert(argOrdList[i] < func->arg_size());
        Value* src = func->getArg(static_cast<uint32_t>(argOrdList[i]));
        Type* dstTy = implFunc->getArg(extraArgBegin + i)->getType();

        Value* cvt = nullptr;
        if (llvm_value_has_type<double>(src))
        {
            ReleaseAssert(llvm_type_has_type<double>(dstTy));
            cvt = src;
        }
        else if (dstTy->isIntegerTy())
        {
            ReleaseAssert(llvm_value_has_type<void*>(src) || llvm_value_has_type<uint64_t>(src));
            Value* u64 = src;
            if (llvm_value_has_type<void*>(src))
            {
                u64 = new PtrToIntInst(src, llvm_type_of<uint64_t>(ctx), "", bb);
            }
            if (dstTy->getIntegerBitWidth() < 64)
            {
                cvt = new TruncInst(u64, dstTy, "", bb);
            }
            else
            {
                ReleaseAssert(dstTy->getIntegerBitWidth() == 64);
                cvt = u64;
            }
        }
        else
        {
            ReleaseAssert(dstTy->isPointerTy());
            ReleaseAssert(llvm_value_has_type<void*>(src) || llvm_value_has_type<uint64_t>(src));
            if (llvm_value_has_type<uint64_t>(src))
            {
                cvt = new IntToPtrInst(src, dstTy, "", bb);
            }
            else
            {
                if (llvm_type_has_type<void*>(dstTy))
                {
                    cvt = src;
                }
                else
                {
                    cvt = new AddrSpaceCastInst(src, dstTy, "", bb);
                }
            }
        }
        ReleaseAssert(cvt != nullptr && cvt->getType() == dstTy);
        result.push_back(cvt);
    }
    ReleaseAssert(extraArgBegin + result.size() == implFunc->arg_size());
    return result;
}

}   // namespace dast
