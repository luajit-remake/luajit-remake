#include "deegen_ast_slow_path.h"
#include "deegen_analyze_lambda_capture_pass.h"
#include "deegen_rewrite_closure_call.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_interpreter_function_interface.h"

namespace dast {

constexpr const char* x_slowPathImplLambdaFunctionNamePrefix = "__deegen_internal_slow_path_lambda_impl_";
constexpr const char* x_slowPathAnnotationLambdaFunctionNamePrefix = "__DeegenImpl_AnnotateSlowPath_IdentificationFunc_";

static void AstSlowPathPreprocessOneUse(llvm::Module* module, llvm::CallInst* origin)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    ReleaseAssert(origin->arg_size() == 2);
    Value* captureV = origin->getArgOperand(0);
    ReleaseAssert(llvm_value_has_type<void*>(captureV));
    Value* lambdaPtrPtr = origin->getArgOperand(1);
    ReleaseAssert(llvm_value_has_type<void*>(lambdaPtrPtr));

    AllocaInst* capture = dyn_cast<AllocaInst>(captureV);
    ReleaseAssert(capture != nullptr);
    Function* lambda = DeegenAnalyzeLambdaCapturePass::GetLambdaFunctionFromPtrPtr(lambdaPtrPtr);

    {
        // We don't really care what is captured in the lambda -- the RewriteClosureCall logic will
        // gracefully handle them for us any way. The only thing we are concerned here is to ascertain
        // that the lambda did not capture anything by reference: since we will eventually transform
        // this call to a tail call, if the user captured anything by reference they will get invalidated.
        //
        // Note that this is only best-effort, not complete -- user may still create a local variable,
        // take its address and store it in another variable, and capture that variable by value.
        // We will not be able to catch illegal uses like that and will just generate buggy code...
        //
        CallInst* lambdaInfoDummyCall = DeegenAnalyzeLambdaCapturePass::GetLambdaCaptureInfo(capture);

        using CaptureKind = DeegenAnalyzeLambdaCapturePass::CaptureKind;
        for (uint32_t i = 1; i < lambdaInfoDummyCall->arg_size(); i += 3)
        {
            Value* arg2 = lambdaInfoDummyCall->getArgOperand(i + 1);
            ReleaseAssert(llvm_value_has_type<uint64_t>(arg2));
            CaptureKind captureKind = GetValueOfLLVMConstantInt<CaptureKind>(arg2);
            ReleaseAssert(captureKind < CaptureKind::Invalid);

            if (captureKind == CaptureKind::ByRefCaptureOfLocalVar)
            {
                fprintf(stderr, "[ERROR] The slow path body lambda should not capture anything by reference!\n");
                abort();
            }

            if (captureKind != CaptureKind::ByValueCaptureOfLocalVar)
            {
                fprintf(stderr, "[ERROR] The slow path body lambda should be defined in a function, not in another lambda!\n");
                abort();
            }
        }

        // Having parsed the capture info, we can remove the dummy call
        //
        ReleaseAssert(lambdaInfoDummyCall->use_empty());
        lambdaInfoDummyCall->eraseFromParent();
        lambdaInfoDummyCall = nullptr;
    }

    // Now, rewrite the function
    // Note that RewriteClosureCall expects us to temporarily remove the use of 'capture' in the call, so do it now
    //
    origin->setArgOperand(0, UndefValue::get(llvm_type_of<void*>(ctx)));
    std::string fnName = GetFirstAvailableFunctionNameWithPrefix(module, x_slowPathImplLambdaFunctionNamePrefix);
    RewriteClosureToFunctionCall::Result res = RewriteClosureToFunctionCall::Run(lambda, capture, fnName);
    ReleaseAssert(res.m_newFunc->getName() == fnName);

    // Replace 'origin' by the annotation call for later processing
    //
    std::vector<Value*> args;
    args.push_back(res.m_newFunc);
    for (Value* val : res.m_args) { args.push_back(val); }

    std::vector<Type*> tys;
    for (Value* val : args) { tys.push_back(val->getType()); }
    FunctionType* fty = FunctionType::get(llvm_type_of<void>(ctx) /*result*/, tys, false /*isVarArg*/);
    std::string annotationFnName = GetFirstAvailableFunctionNameWithPrefix(module, x_slowPathAnnotationLambdaFunctionNamePrefix);
    Function* annotationFn = Function::Create(fty, GlobalValue::ExternalLinkage, annotationFnName, module);
    ReleaseAssert(annotationFn->getName() == annotationFnName);
    CallInst* replacement = CallInst::Create(annotationFn, args, "", origin /*insertBefore*/);
    ReleaseAssert(llvm_value_has_type<void>(origin));
    ReleaseAssert(llvm_value_has_type<void>(replacement));
    ReleaseAssert(origin->use_empty());
    origin->eraseFromParent();
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
                    if (callee != nullptr && callee->getName().str() == "DeegenImpl_EnterSlowPathLambda")
                    {
                        allUses.push_back(callInst);
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

void AstSlowPath::LowerAllForInterpreter(InterpreterBytecodeImplCreator* ifi, llvm::Function* func)
{
    std::vector<AstSlowPath> list = AstSlowPath::GetAllUseInFunction(func);
    for (AstSlowPath& slowPath : list)
    {
        slowPath.LowerForInterpreter(ifi);
    }
}

static std::vector<uint64_t> WARN_UNUSED AstSlowPathGetInterpreterFunctionArgAssignment(const std::vector<llvm::Type*>& argTys)
{
    using namespace llvm;

    std::vector<uint64_t> gprList = InterpreterFunctionInterface::GetAvaiableGPRListForBytecode();
    std::vector<uint64_t> fprList = InterpreterFunctionInterface::GetAvaiableFPRListForBytecode();

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

void AstSlowPath::LowerForInterpreter(InterpreterBytecodeImplCreator* ifi)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();

    std::string dispatchFnName = GetPostProcessSlowPathFunctionNameForInterpreter(GetImplFunction());
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

    InterpreterFunctionInterface::CreateDispatchToBytecode(dispatchFn, ifi->GetCoroutineCtx(), ifi->GetStackBase(), ifi->GetCurBytecode(), ifi->GetCodeBlock(), m_origin /*insertBefore*/);

    CallInst* callInst = dyn_cast<CallInst>(m_origin->getPrevNode()->getPrevNode());
    ReleaseAssert(callInst != nullptr);
    ReleaseAssert(callInst->getCalledFunction() == dispatchFn);

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

std::vector<llvm::Value*> WARN_UNUSED AstSlowPath::CreateCallArgsInSlowPathWrapperFunction(llvm::Function* implFunc, llvm::BasicBlock* bb)
{
    using namespace llvm;
    LLVMContext& ctx = implFunc->getContext();
    std::vector<uint64_t> argOrdList;
    {
        std::vector<Type*> argTys;
        for (uint32_t i = 0; i < implFunc->arg_size(); i++)
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
        Type* dstTy = implFunc->getArg(i)->getType();

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
    ReleaseAssert(result.size() == implFunc->arg_size());
    return result;
}

}   // namespace dast
