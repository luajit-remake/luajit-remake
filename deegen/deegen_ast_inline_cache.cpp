#include "deegen_ast_inline_cache.h"
#include "deegen_analyze_lambda_capture_pass.h"

namespace dast {

namespace {

// Get the lambda function from the passed-in lambda_functor_member_pointer_pointer_v value
//
static llvm::Function* WARN_UNUSED GetLambdaFunctionFromPtrPtr(llvm::Value* lambdaPtrPtr)
{
    using namespace llvm;
    LLVMContext& ctx = lambdaPtrPtr->getContext();
    GlobalVariable* gv = dyn_cast<GlobalVariable>(lambdaPtrPtr);
    ReleaseAssert(gv != nullptr);
    ReleaseAssert(gv->isConstant());
    Constant* iv =  gv->getInitializer();
    ConstantStruct* ivs = dyn_cast<ConstantStruct>(iv);
    ReleaseAssert(ivs != nullptr);
    StructType* cstType = dyn_cast<StructType>(ivs->getType());
    ReleaseAssert(cstType != nullptr);
    ReleaseAssert(cstType->getNumElements() == 2);
    ReleaseAssert(cstType->getStructElementType(0) == Type::getInt64Ty(ctx));
    ReleaseAssert(cstType->getStructElementType(1) == Type::getInt64Ty(ctx));

    Constant* v1 = ivs->getAggregateElement(1);
    ReleaseAssert(v1->isZeroValue());

    Constant* v0 = ivs->getAggregateElement(static_cast<unsigned int>(0));
    ConstantExpr* expr = dyn_cast<ConstantExpr>(v0);
    ReleaseAssert(expr != nullptr);
    ReleaseAssert(expr->getOpcode() == Instruction::PtrToInt);
    ReleaseAssert(expr->getNumOperands() == 1);
    Constant* x = expr->getOperand(0);
    Function* result = dyn_cast<Function>(x);
    ReleaseAssert(result != nullptr);
    return result;
}

constexpr const char* x_createIcInitPlaceholderFunctionPrefix = "__DeegenInternal_AstIC_GetICPtr_IdentificationFunc_";
constexpr const char* x_createIcBodyPlaceholderFunctionPrefix = "__DeegenInternal_AstIC_Body_IdentificationFunc_";
constexpr const char* x_createIcRegisterEffectPlaceholderFunctionPrefix = "__DeegenInternal_AstIC_RegisterEffect_IdentificationFunc_";
constexpr const char* x_icEffectPlaceholderFunctionPrefix = "__DeegenInternal_AstICEffect_IdentificationFunc_";
constexpr const char* x_icBodyClosureWrapperFunctionPrefix = "__deegen_inline_cache_body_";
constexpr const char* x_icEffectClosureWrapperFunctionPrefix = "__deegen_inline_cache_effect_";
constexpr const char* x_decodeICStateToEffectLambdaCaptureFunctionPrefix = "__deegen_inline_cache_decode_ic_state_";
constexpr const char* x_encodeICStateFunctionPrefix = "__deegen_inline_cache_encode_ic_state_";

static std::string WARN_UNUSED GetFirstAvailableFunctionNameWithPrefix(llvm::Module* module, const std::string& prefix)
{
    std::string decidedName;
    size_t suffixOrd = 0;
    while (true)
    {
        decidedName = prefix + std::to_string(suffixOrd);
        if (module->getNamedValue(decidedName) == nullptr)
        {
            break;
        }
        suffixOrd++;
    }
    return decidedName;
}

// This transform transforms a closure (a function with a capture) to a normal function call.
//
// The transform looks like the following: before transform we have IR
//    %capture = alloca ...
//    ... populate capture ...
//    # the call to the closure would be %lambda(%capture)
//
// and we transform it to:
//    function lambda_wrapper(all args stored to capture)
//        %capture = alloca ...
//        ... populate capture ...
//        return %lambda(%capture)
//
//    # now the call to the closure would be %lambda_wrapper(%args...)
//    # where args is the return value of this function
//
// This function expects that the only uses of 'capture' are the instructions that fill its value
//
struct RewriteClosureToFunctionCallResult
{
    llvm::Function* m_newFunc;
    std::vector<llvm::Value*> m_args;
};

static RewriteClosureToFunctionCallResult WARN_UNUSED RewriteClosureToFunctionCall(llvm::Function* lambda, llvm::AllocaInst* capture, const std::string& newFnName)
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
                bb->getInstList().push_back(clone);
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
                bb->getInstList().push_back(gepClone);
                bb->getInstList().push_back(siClone);
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
    wrapper->addFnAttr(Attribute::AttrKind::NoUnwind);
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
    lambda->addFnAttr(Attribute::AttrKind::AlwaysInline);

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

struct PreprocessIcEffectApiResult
{
    llvm::CallInst* m_annotationInst;
    llvm::Function* m_annotationFn;
    llvm::Function* m_effectWrapperFn;
    std::vector<llvm::Value*> m_mainFunctionCapturedValues;
};

static PreprocessIcEffectApiResult WARN_UNUSED PreprocessIcEffectApi(
    llvm::Module* module,
    llvm::CallInst* origin,
    std::map<size_t /*ordInCaptureStruct*/, std::pair<DeegenAnalyzeLambdaCapturePass::CaptureKind, llvm::Value*>> icCaptureInfo)
{
    using namespace llvm;
    using CaptureKind = DeegenAnalyzeLambdaCapturePass::CaptureKind;
    LLVMContext& ctx = module->getContext();
    ReleaseAssert(origin->arg_size() == 3);
    Value* captureV = origin->getArgOperand(1);
    ReleaseAssert(llvm_value_has_type<void*>(captureV));
    ReleaseAssert(isa<AllocaInst>(captureV));
    AllocaInst* capture = cast<AllocaInst>(captureV);
    Value* lambdaPtrPtr = origin->getArgOperand(2);
    ReleaseAssert(llvm_value_has_type<void*>(lambdaPtrPtr));
    Function* lambda = GetLambdaFunctionFromPtrPtr(lambdaPtrPtr);

    // Figure out the capture info of the effect lambda
    //
    CallInst* captureInfoCall = DeegenAnalyzeLambdaCapturePass::GetLambdaCaptureInfo(capture);
    ReleaseAssert(captureInfoCall != nullptr);
    ReleaseAssert(captureInfoCall->arg_size() % 3 == 1);

    std::map<size_t /*ordInCaptureStruct*/, Value*> effectFnLocalCaptures;
    std::map<size_t /*ordInCaptureStruct*/, std::pair<CaptureKind, uint64_t /*ordInClosureCaptureStruct*/>> effectFnParentCaptures;

    for (uint32_t i = 1; i < captureInfoCall->arg_size(); i += 3)
    {
        Value* arg1 = captureInfoCall->getArgOperand(i);
        Value* arg2 = captureInfoCall->getArgOperand(i + 1);
        Value* arg3 = captureInfoCall->getArgOperand(i + 2);
        ReleaseAssert(llvm_value_has_type<uint64_t>(arg1));
        ReleaseAssert(llvm_value_has_type<uint64_t>(arg2));
        uint64_t ordInCaptureStruct = GetValueOfLLVMConstantInt<uint64_t>(arg1);
        CaptureKind captureKind = GetValueOfLLVMConstantInt<CaptureKind>(arg2);
        ReleaseAssert(captureKind < CaptureKind::Invalid);

        ReleaseAssert(!effectFnLocalCaptures.count(ordInCaptureStruct));
        ReleaseAssert(!effectFnParentCaptures.count(ordInCaptureStruct));

        // The IC body lambda should be defined in the bytecode function, so it naturally should not
        // have any capture of the "enclosing lambda"
        //
        if (captureKind == CaptureKind::ByRefCaptureOfLocalVar)
        {
            fprintf(stderr, "[ERROR] The IC effect lambda must capture values defined in the IC body lambda by value, not by reference!\n");
            abort();
        }
        if (captureKind == CaptureKind::ByValueCaptureOfLocalVar)
        {
            effectFnLocalCaptures[ordInCaptureStruct] = arg3;
        }
        else
        {
            ReleaseAssert(llvm_value_has_type<uint64_t>(arg3));
            uint64_t ordInParentCapture = GetValueOfLLVMConstantInt<uint64_t>(arg3);
            effectFnParentCaptures[ordInCaptureStruct] = std::make_pair(captureKind, ordInParentCapture);
        }
    }

    // Create the IC effect wrapper function
    // It should take the IC state (containing all the values defined and captured in the IC body)
    // and a list of parameters representing the list of values transitively captured in the main function
    //
    std::vector<Type*> icEffectWrapperFnArgTys;
    icEffectWrapperFnArgTys.push_back(llvm_type_of<void*>(ctx));
    std::vector<std::pair<size_t /*ordInCapture*/, Value*>> capturedValuesInMainFn;
    for (auto& it : effectFnParentCaptures)
    {
        size_t ordInCaptureStruct = it.first;
        CaptureKind ck = it.second.first;
        size_t ordInParentCapture = it.second.second;

        ReleaseAssert(icCaptureInfo.count(ordInParentCapture));
        CaptureKind parentCk = icCaptureInfo[ordInParentCapture].first;
        Value* parentCv = icCaptureInfo[ordInParentCapture].second;

        switch (ck)
        {
        case CaptureKind::ByRefCaptureOfLocalVar:
        case CaptureKind::ByValueCaptureOfLocalVar:
        case CaptureKind::Invalid:
        {
            ReleaseAssert(false);
        }
        case CaptureKind::ByRefCaptureOfByValue:
        case CaptureKind::ByValueCaptureOfByRef:
        {
            ReleaseAssertImp(ck == CaptureKind::ByRefCaptureOfByValue, parentCk == CaptureKind::ByValueCaptureOfLocalVar);
            ReleaseAssertImp(ck == CaptureKind::ByValueCaptureOfByRef, parentCk == CaptureKind::ByRefCaptureOfLocalVar);
            // There's no problem with supporting these two cases, but for now we choose
            // to lock it down because these use cases don't make sense:
            // (1) If the IC body is capturing some state by value (which means it's already const)
            // what is the point of the IC effect lambda to capture this constant by reference?
            // (2) Similarly, if the IC body is capturing some state by reference (which means either
            // it's a large struct or the IC intends to change it), then the IC effect lambda
            // should also capture it by reference, because the IC effect lambda is the only logic
            // that is allowed to change its value.
            //
            // Also, locking them down removes some case-discussion to create our wrapper function.
            //
            fprintf(stderr, "If the IC effect lambda captures some value captured by the IC body lambda, "
                            "the captures should either be both by value, or be both by reference! (see comment above)\n");
            abort();
        }
        case CaptureKind::SameCaptureKindAsEnclosingLambda:
        {
            break;
        }
        } /* switch ck */
        ReleaseAssert(ck == CaptureKind::SameCaptureKindAsEnclosingLambda);

        if (parentCk == CaptureKind::ByRefCaptureOfLocalVar)
        {
            ReleaseAssert(isa<AllocaInst>(parentCv));
        }
        else
        {
            ReleaseAssert(parentCk == CaptureKind::ByValueCaptureOfLocalVar);
        }

        // Since the capture kind in the IC body lambda is the same as the IC effect lambda,
        // there is no need to dereference any pointer to load the value. We are just passing the value around.
        //
        capturedValuesInMainFn.push_back(std::make_pair(ordInCaptureStruct, parentCv));
        icEffectWrapperFnArgTys.push_back(parentCv->getType());
    }

    FunctionType* effectWrapperFnTy = FunctionType::get(origin->getType() /*result*/, icEffectWrapperFnArgTys, false /*isVarArg*/);
    std::string effectWrapperFnName = GetFirstAvailableFunctionNameWithPrefix(module, x_icEffectClosureWrapperFunctionPrefix);
    Function* effectWrapperFn = Function::Create(effectWrapperFnTy, GlobalValue::InternalLinkage, effectWrapperFnName, module);
    ReleaseAssert(effectWrapperFn->getName().str() == effectWrapperFnName);
    effectWrapperFn->addFnAttr(Attribute::AttrKind::NoUnwind);
    CopyFunctionAttributes(effectWrapperFn, lambda);

    // Create the body of the effect wrapper
    // Basically what we need to do is to create the closure capture expected by the effect lambda, then call the effect lambda
    //
    BasicBlock* entryBlock = BasicBlock::Create(ctx, "", effectWrapperFn);
    ReleaseAssert(isa<StructType>(capture->getAllocatedType()));
    StructType* captureST = cast<StructType>(capture->getAllocatedType());
    AllocaInst* ai = new AllocaInst(captureST, 0 /*addrSpace*/, "", entryBlock);

    // Create a decode function placeholder to populate the IC state into the capture
    // Future lowering logic will lower this call to concrete logic
    //
    std::vector<Value*> decoderFnArgs;
    decoderFnArgs.push_back(ai);
    decoderFnArgs.push_back(effectWrapperFn->getArg(0 /*icState*/));
    for (auto& it : effectFnLocalCaptures)
    {
        size_t ordInCapture = it.first;
        decoderFnArgs.push_back(CreateLLVMConstantInt<uint64_t>(ctx, ordInCapture));
    }
    std::vector<Type*> decoderFnArgTys;
    for (Value* val : decoderFnArgs)
    {
        decoderFnArgTys.push_back(val->getType());
    }
    FunctionType* decoderFnTy = FunctionType::get(llvm_type_of<void>(ctx) /*result*/, decoderFnArgTys, false /*isVarArg*/);
    std::string decoderFnName = GetFirstAvailableFunctionNameWithPrefix(module, x_decodeICStateToEffectLambdaCaptureFunctionPrefix);
    Function* decoderFn = Function::Create(decoderFnTy, GlobalValue::ExternalLinkage, decoderFnName, module);
    ReleaseAssert(decoderFn->getName().str() == decoderFnName);
    decoderFn->addFnAttr(Attribute::AttrKind::NoUnwind);
    CallInst::Create(decoderFn, decoderFnArgs, "", entryBlock);

    // Now, populate every value from the main function into the lambda
    //
    for (size_t i = 0; i < capturedValuesInMainFn.size(); i++)
    {
        size_t ordInStruct = capturedValuesInMainFn[i].first;
        Value* src = effectWrapperFn->getArg(static_cast<uint32_t>(i + 1));
        ReleaseAssert(ordInStruct < captureST->getNumElements());
        ReleaseAssert(src->getType() == captureST->getElementType(static_cast<uint32_t>(ordInStruct)));
        GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(
            captureST, ai,
            {
                CreateLLVMConstantInt<uint64_t>(ctx, 0),
                // LLVM requires struct index to be 32-bit (other width won't work!), so this must be uint32_t
                //
                CreateLLVMConstantInt<uint32_t>(ctx, static_cast<uint32_t>(ordInStruct))
            },
            "", entryBlock);
        new StoreInst(src, gep, entryBlock);
    }

    // At this stage we have the capture expected by the effect lambda, so we can call it now
    //
    ReleaseAssert(lambda->arg_size() == 1);
    ReleaseAssert(llvm_value_has_type<void*>(lambda->getArg(0)));
    ReleaseAssert(lambda->getReturnType() == effectWrapperFn->getReturnType());
    CallInst* ci = CallInst::Create(lambda, { ai },  "", entryBlock);
    if (llvm_value_has_type<void>(ci))
    {
        ReturnInst::Create(ctx, nullptr /*returnVoid*/, entryBlock);
    }
    else
    {
        ReturnInst::Create(ctx, ci, entryBlock);
    }

    ValidateLLVMFunction(effectWrapperFn);

    // Create the placeholder for the IC state encoding function
    //
    std::vector<Type*> encoderFnArgTys;
    encoderFnArgTys.push_back(llvm_type_of<void*>(ctx) /*out*/);
    for (auto& it : effectFnLocalCaptures)
    {
        Value* val = it.second;
        encoderFnArgTys.push_back(val->getType());
    }
    FunctionType* encoderFnTy = FunctionType::get(llvm_type_of<void>(ctx) /*result*/, encoderFnArgTys, false /*isVarArgs*/);
    std::string encoderFnName = GetFirstAvailableFunctionNameWithPrefix(module, x_encodeICStateFunctionPrefix);
    Function* encoderFn = Function::Create(encoderFnTy, GlobalValue::ExternalLinkage, encoderFnName, module);
    ReleaseAssert(encoderFn->getName().str() == encoderFnName);
    encoderFn->addFnAttr(Attribute::NoUnwind);

    // Finally, rewrite the origin CallInst so all the information are conveyed
    //
    std::vector<Value*> annotationFnArgs;
    annotationFnArgs.push_back(origin->getArgOperand(0) /*icPtr*/);
    annotationFnArgs.push_back(capture);
    annotationFnArgs.push_back(lambda);
    annotationFnArgs.push_back(effectWrapperFn);
    annotationFnArgs.push_back(decoderFn);
    annotationFnArgs.push_back(encoderFn);
    annotationFnArgs.push_back(CreateLLVMConstantInt(ctx, effectFnLocalCaptures.size()) /*numValuesInIcState*/);
    for (auto& it : effectFnLocalCaptures)
    {
        Value* val = it.second;
        annotationFnArgs.push_back(val);
    }
    std::vector<Type*> annotationFnArgTys;
    for (Value* val : annotationFnArgs)
    {
        annotationFnArgTys.push_back(val->getType());
    }
    std::string annotationFnName = GetFirstAvailableFunctionNameWithPrefix(module, x_icEffectPlaceholderFunctionPrefix);
    FunctionType* annotationFnTy = FunctionType::get(lambda->getReturnType(), annotationFnArgTys, false /*isVarArgs*/);
    Function* annotationFn = Function::Create(annotationFnTy, GlobalValue::ExternalLinkage, annotationFnName, module);
    ReleaseAssert(annotationFn->getName().str() == annotationFnName);
    annotationFn->addFnAttr(Attribute::AttrKind::NoUnwind);
    CallInst* replacement = CallInst::Create(annotationFn, annotationFnArgs, "", origin);
    ReleaseAssert(replacement->getType() == origin->getType());
    if (!llvm_value_has_type<void>(origin))
    {
        origin->replaceAllUsesWith(replacement);
    }
    ReleaseAssert(origin->use_empty());
    origin->eraseFromParent();

    ValidateLLVMFunction(replacement->getFunction());

    // Construct the result
    //
    PreprocessIcEffectApiResult res;
    res.m_annotationInst = replacement;
    res.m_annotationFn = annotationFn;
    res.m_effectWrapperFn = effectWrapperFn;
    for (auto& it : capturedValuesInMainFn)
    {
        res.m_mainFunctionCapturedValues.push_back(it.second);
    }
    return res;
}

static void PreprocessCreateIcApi(llvm::CallInst* origin)
{
    using namespace llvm;
    LLVMContext& ctx = origin->getContext();
    ReleaseAssert(origin->getParent() != nullptr);
    Function* originFn = origin->getParent()->getParent();
    ReleaseAssert(originFn != nullptr);
    Module* module = originFn->getParent();
    ReleaseAssert(module != nullptr);

    ReleaseAssert(origin->arg_size() > 0);
    Function* fn = origin->getCalledFunction();
    if (fn->hasParamAttribute(0, Attribute::AttrKind::StructRet))
    {
        fprintf(stderr, "[LOCKDOWN] Inline cache with >16 byte return value is currently unsupported.\n");
        abort();
    }
    ReleaseAssert(origin->arg_size() == 3);
    Value* ic = origin->getArgOperand(0);
    ReleaseAssert(llvm_value_has_type<void*>(ic));
    Value* closurePtr = origin->getArgOperand(1);
    ReleaseAssert(llvm_value_has_type<void*>(closurePtr));
    Value* lambdaPtrPtr = origin->getArgOperand(2);
    ReleaseAssert(llvm_value_has_type<void*>(lambdaPtrPtr));
    Function* lambdaFunc = GetLambdaFunctionFromPtrPtr(lambdaPtrPtr);
    ReleaseAssert(lambdaFunc->arg_size() == 1);
    ReleaseAssert(llvm_value_has_type<void*>(lambdaFunc->getArg(0)));

    {
        // Assert that the value 'ic' is created by our MakeInlineCache() API
        //
        CallInst* def = dyn_cast<CallInst>(ic);
        if (def == nullptr)
        {
            if (dyn_cast<LoadInst>(ic) != nullptr)
            {
                fprintf(stderr, "[ERROR] It seems like mem2reg failed to promote the IC pointer to SSA. "
                        "Did you accidentally passed it by reference to somewhere else? (e.g. did the main lambda capture it by reference?)\n");
            }
            else
            {
                fprintf(stderr, "[ERROR] Invalid use of inline cache API: the ICHandler pointer is not created by MakeInlineCache()!\n");
            }
            abort();
        }
        Function* defFn = def->getCalledFunction();
        ReleaseAssert(defFn != nullptr);
        ReleaseAssert(IsCXXSymbol(defFn->getName().str()));
        ReleaseAssert(DemangleCXXSymbol(defFn->getName().str()) == "MakeInlineCache()");

        // Replace the callee by a unique function, to prevent the optimizer from merging calls
        // We already used '__nomerge__' attribute to prevent it from happening, but doesn't hurt to be 100% safe by just unique them here
        //
        std::string icInitFnName = GetFirstAvailableFunctionNameWithPrefix(module, x_createIcInitPlaceholderFunctionPrefix);
        FunctionType* icInitFnTy = FunctionType::get(llvm_type_of<void*>(ctx) /*result*/, { } /*args*/, false /*isVarArg*/);
        Function* uniqueInitFn = Function::Create(icInitFnTy, GlobalValue::LinkageTypes::ExternalLinkage, icInitFnName, module);
        ReleaseAssert(uniqueInitFn->getName().str() == icInitFnName);
        uniqueInitFn->addFnAttr(Attribute::AttrKind::NoUnwind);
        ReleaseAssert(def->getFunctionType() == uniqueInitFn->getFunctionType());
        def->setCalledFunction(uniqueInitFn);
    }

    std::vector<Instruction*> instructionToRemove;

    // Find all use of ic to figure out the configuration details
    //
    Value* icKey = nullptr;
    Value* icKeyImpossibleValue = nullptr;
    for (Use& u : ic->uses())
    {
        User* usr = u.getUser();
        if (isa<StoreInst>(usr))
        {
            // If it is a store, it should only be a store to the main lambda capture
            //
            StoreInst* si = cast<StoreInst>(usr);
            ReleaseAssert(si->getPointerOperand() == closurePtr);
        }
        else
        {
            // Otherwise, it must be a call instruction
            //
            CallInst* icApiCall = dyn_cast<CallInst>(usr);
            ReleaseAssert(icApiCall != nullptr);
            Function* apiFn = icApiCall->getCalledFunction();
            if (apiFn != nullptr && DeegenAnalyzeLambdaCapturePass::IsAnnotationFunction(apiFn->getName().str()))
            {
                continue;
            }

            // We always pass 'ic' as the first parameter in all APIs, so we can simply assert it's the first param.
            //
            ReleaseAssert(&u == &icApiCall->getOperandUse(0));
            ReleaseAssert(apiFn != nullptr);
            ReleaseAssert(IsCXXSymbol(apiFn->getName().str()));
            std::string symName = DemangleCXXSymbol(apiFn->getName().str());
            if (symName.find(" DeegenImpl_MakeIC_AddKey<") != std::string::npos)
            {
                ReleaseAssert(apiFn->arg_size() == 2);
                ReleaseAssert(llvm_value_has_type<void*>(icApiCall));
                Value* key = icApiCall->getArgOperand(1);
                ReleaseAssert(key->getType()->isIntegerTy());
                if (icKey != nullptr)
                {
                    fprintf(stderr, "[Lockdown] Inline cache with more than 1 key is currently unsupported.\n");
                    abort();
                }
                icKey = key;

                // Scan through all ICKey API use to figure out all details of this key
                //
                for (Use& ku : icApiCall->uses())
                {
                    User* kusr = ku.getUser();
                    CallInst* icKeyApiCall = dyn_cast<CallInst>(kusr);
                    ReleaseAssert(icKeyApiCall != nullptr);
                    instructionToRemove.push_back(icKeyApiCall);

                    ReleaseAssert(&ku == &icKeyApiCall->getOperandUse(0));
                    Function* icKeyApiFn = icKeyApiCall->getCalledFunction();
                    ReleaseAssert(icKeyApiFn != nullptr);
                    ReleaseAssert(IsCXXSymbol(icKeyApiFn->getName().str()));
                    std::string icKeyApiName = DemangleCXXSymbol(icKeyApiFn->getName().str());
                    if (icKeyApiName.find(" DeegenImpl_MakeIC_SetICKeyImpossibleValue<") != std::string::npos)
                    {
                        ReleaseAssert(icKeyApiFn->arg_size() == 2);
                        Value* impossibleValue = icKeyApiCall->getArgOperand(1);
                        ReleaseAssert(impossibleValue->getType() == icKey->getType());
                        if (icKeyImpossibleValue != nullptr)
                        {
                            fprintf(stderr, "[ERROR] SetImpossibleValue API should not be called twice on the same key!\n");
                            abort();
                        }
                        icKeyImpossibleValue = impossibleValue;
                    }
                    else
                    {
                        fprintf(stderr, "[ERROR] Unknown ICkey API function %s\n", icKeyApiName.c_str());
                        abort();
                    }
                }
                instructionToRemove.push_back(icApiCall);
            }
            else if (symName.find(" DeegenImpl_MakeIC_SetMainLambda<") != std::string::npos)
            {
                ReleaseAssert(icApiCall == origin);
                instructionToRemove.push_back(icApiCall);
            }
            else
            {
                fprintf(stderr, "[ERROR] Unknown inline cache API function %s\n", symName.c_str());
                abort();
            }
        }
    }

    if (icKey == nullptr)
    {
        fprintf(stderr, "[ERROR] The inline cache did not set up a key!\n");
        abort();
    }
    if (icKeyImpossibleValue == nullptr)
    {
        icKeyImpossibleValue = UndefValue::get(icKey->getType());
    }

    AllocaInst* closureAlloca = dyn_cast<AllocaInst>(closurePtr);
    ReleaseAssert(closureAlloca != nullptr);
    // Stash the 'closurePtr' use as the call parameter, so that the closure capture
    // alloca has no unexpected uses (which is required by our rewrite logic)
    //
    origin->setArgOperand(1, UndefValue::get(llvm_type_of<void*>(ctx)));

    ReleaseAssert(isa<StructType>(closureAlloca->getAllocatedType()));
    StructType* closureAllocaST = cast<StructType>(closureAlloca->getAllocatedType());

    // Now, the only CallInst use of 'closurePtr' should be the dummy call that conveys the lambda capture info,
    // inserted by the AnalyzeLambdaCapture pass. Locate that call and retrieve the info.
    //
    using CaptureKind = DeegenAnalyzeLambdaCapturePass::CaptureKind;
    CallInst* lambdaInfoDummyCall = DeegenAnalyzeLambdaCapturePass::GetLambdaCaptureInfo(closureAlloca);
    ReleaseAssert(lambdaInfoDummyCall != nullptr);
    ReleaseAssert(lambdaInfoDummyCall->arg_size() % 3 == 1);
    ReleaseAssert(lambdaInfoDummyCall->getArgOperand(0) == closureAlloca);

    std::map<size_t /*ordInCaptureStruct*/, std::pair<CaptureKind, Value*>> icCaptureInfo;
    for (uint32_t i = 1; i < lambdaInfoDummyCall->arg_size(); i += 3)
    {
        Value* arg1 = lambdaInfoDummyCall->getArgOperand(i);
        Value* arg2 = lambdaInfoDummyCall->getArgOperand(i + 1);
        Value* arg3 = lambdaInfoDummyCall->getArgOperand(i + 2);
        ReleaseAssert(llvm_value_has_type<uint64_t>(arg1));
        ReleaseAssert(llvm_value_has_type<uint64_t>(arg2));
        uint64_t ordInCaptureStruct = GetValueOfLLVMConstantInt<uint64_t>(arg1);
        CaptureKind captureKind = GetValueOfLLVMConstantInt<CaptureKind>(arg2);
        ReleaseAssert(captureKind < CaptureKind::Invalid);

        // The IC body lambda should be defined in the bytecode function, so it naturally should not
        // have any capture of the "enclosing lambda"
        //
        if (captureKind != CaptureKind::ByValueCaptureOfLocalVar && captureKind != CaptureKind::ByRefCaptureOfLocalVar)
        {
            fprintf(stderr, "[ERROR] The IC body lambda should be defined in a function, not in another lambda!\n");
            abort();
        }

        ReleaseAssert(ordInCaptureStruct < closureAllocaST->getStructNumElements());
        if (captureKind == CaptureKind::ByValueCaptureOfLocalVar)
        {
            ReleaseAssert(arg3->getType() == closureAllocaST->getElementType(static_cast<uint32_t>(ordInCaptureStruct)));
        }
        else
        {
            ReleaseAssert(llvm_type_has_type<void*>(closureAllocaST->getElementType(static_cast<uint32_t>(ordInCaptureStruct))));
        }

        ReleaseAssert(!icCaptureInfo.count(ordInCaptureStruct));
        icCaptureInfo[ordInCaptureStruct] = std::make_pair(captureKind, arg3);
    }

    // Having parsed the capture info, we can remove the dummy call
    //
    ReleaseAssert(lambdaInfoDummyCall->use_empty());
    lambdaInfoDummyCall->eraseFromParent();
    lambdaInfoDummyCall = nullptr;

    // Now, preprocess the dependent IC API calls in the lambda body
    // Currently the only API that we need to preprocess is the 'Effect' API call
    //
    // The rewrite looks like the following: before we have
    //     function icEffect(%0) {
    //         ...
    //     }
    //     /* IC body */
    //     %capture = alloca ...
    //     ... build capture ...
    //     MarkICEffect(icEffect, capture)
    //
    // And we want to create a wrapper of 'icEffect' that is suitable for being called directly from the main function.
    //
    // Note that the capture can contains values that are defined in the IC body, or defined in the main function
    // and transitively captured by the IC body.
    // For the first kind, our protocol is that they should be treated as constants in future IC-hit executions.
    // For the second kind, our protocol is that they should see the fresh value in future IC-hit executions.
    //
    // So we want to create a struct that contains only the constant part (which is effectively the state of an IC entry),
    // and create the wrapper that creates the struct, and the wrapper that runs the IC effect function based on the
    // struct and the main function state.
    //
    // To do this, we first figure out which captured values are from the IC body lambda, and which are from the main function.
    // Then we create two declarations:
    //    function encodeIcCapture(%icState, %allocas...)
    // which parameters contains all the values captured in the IC body, and serializes them into the state of one IC entry.
    //
    // We create another function
    //    function decodeICCapture(%originalCapture, %icState, %ord...)
    // where 'icState' is the output of 'encodeIcCapture', 'originalCapture' is the original lambda capture, 'ord' is the
    // list of element offsets that maps the values in 'icState' to 'originalCapture'. The function will read the state
    // of the IC entry, and populate the lambda capture expected by the IC effect lambda.
    //
    // Then we add the following annotation: before 'MarkICEffect' we add
    //     AnnotateICEffectCapture(%allocas...)
    // future lowering will lower this call to the concrete logic that set up the IC entry.
    //
    // and before the IC body call in the main function we add
    //     AnnotateICEffect(run_effect_wrapper, %ssa...)
    // which tells us how to run each IC effect from the main function.
    //
    // The run_effect_wrapper will be a wrapper function:
    //     function run_effect_wrapper(%icState, %valuesInMainFn)
    //         %originalCapture = alloca ...
    //         ... populate originalCapture using valuesInMainFn to populate the values defined in the main function...
    //         # call decodeICCapture to populate the values defined in the IC body
    //         call decodeICCapture(%originalCapture, %icState, %ord...)
    //         # now we can call the original IC effect lambda. LLVM optimizer will inline it and clean up all the code.
    //         %res = call icEffect(%originalCapture)
    //

    std::vector<CallInst*> effectCalls;
    {
        // First, find out the capture that corresponds to our ic pointer
        //
        size_t icPtrOrd = static_cast<size_t>(-1);
        for (auto& it : icCaptureInfo)
        {
            size_t ord = it.first;
            Value* val = it.second.second;
            CaptureKind ck = it.second.first;
            if (val == ic)
            {
                ReleaseAssert(icPtrOrd == static_cast<size_t>(-1));
                icPtrOrd = ord;
                ReleaseAssert(ck == CaptureKind::ByValueCaptureOfLocalVar);
            }
        }
        if (icPtrOrd == static_cast<size_t>(-1))
        {
            fprintf(stderr, "Failed to locate IC pointer in the IC body. Did you forget to capture it, or captured it by reference?\n");
            abort();
        }

        // Now, figure out all the IC::Effect calls
        //
        llvm::DataLayout dataLayout(module);
        const llvm::StructLayout* closureAllocaSL = dataLayout.getStructLayout(closureAllocaST);
        uint64_t icPtrOffsetBytesInCapture = closureAllocaSL->getElementOffset(static_cast<uint32_t>(icPtrOrd));

        auto assertOperandIsIcPtr = [&dataLayout, lambdaFunc, icPtrOffsetBytesInCapture](Value* val)
        {
            ReleaseAssert(llvm_value_has_type<void*>(val));
            ReleaseAssert(isa<LoadInst>(val));
            LoadInst* li = cast<LoadInst>(val);
            Value* po = li->getPointerOperand();
            if (po == lambdaFunc->getArg(0))
            {
                ReleaseAssert(icPtrOffsetBytesInCapture == 0);
            }
            else
            {
                ReleaseAssert(isa<GetElementPtrInst>(po));
                GetElementPtrInst* gi = cast<GetElementPtrInst>(po);
                APInt offset(64 /*numBits*/, 0);
                ReleaseAssert(gi->accumulateConstantOffset(dataLayout, offset /*out*/));
                ReleaseAssert(offset.getZExtValue() == icPtrOffsetBytesInCapture);
            }
        };

        for (BasicBlock& bb : *lambdaFunc)
        {
            for (Instruction& inst : bb)
            {
                CallInst* ci = dyn_cast<CallInst>(&inst);
                if (ci != nullptr)
                {
                    Function* callee = ci->getCalledFunction();
                    if (callee != nullptr && IsCXXSymbol(callee->getName().str()))
                    {
                        std::string symName = DemangleCXXSymbol(callee->getName().str());
                        if (symName.find(" DeegenImpl_MakeIC_MarkEffect<") != std::string::npos)
                        {
                            ReleaseAssert(ci->arg_size() == 3);
                            assertOperandIsIcPtr(ci->getArgOperand(0));
                            ReleaseAssert(ci->getType() == origin->getType());
                            effectCalls.push_back(ci);
                        }
                    }
                }
            }
        }
    }

    // Process each of the effect call found
    //
    for (CallInst* effectCall : effectCalls)
    {
        PreprocessIcEffectApiResult res = PreprocessIcEffectApi(module, effectCall, icCaptureInfo);

        // Annotate the info of the IC effect in the main function
        //
        std::vector<Value*> icEffectAnnotationArgs;
        icEffectAnnotationArgs.push_back(ic);
        icEffectAnnotationArgs.push_back(res.m_annotationFn);
        icEffectAnnotationArgs.push_back(res.m_effectWrapperFn);
        for (Value* val : res.m_mainFunctionCapturedValues)
        {
            icEffectAnnotationArgs.push_back(val);
        }
        std::vector<Type*> icEffectAnnotationArgTys;
        for (Value* val : icEffectAnnotationArgs)
        {
            icEffectAnnotationArgTys.push_back(val->getType());
        }

        FunctionType* icEffectAnnotationFty = FunctionType::get(llvm_type_of<void>(ctx) /*result*/, icEffectAnnotationArgTys, false /*isVarArg*/);
        std::string annotationFnName = GetFirstAvailableFunctionNameWithPrefix(module, x_createIcRegisterEffectPlaceholderFunctionPrefix);
        Function* icEffectAnnotationFn = Function::Create(icEffectAnnotationFty, GlobalValue::ExternalLinkage, annotationFnName, module);
        ReleaseAssert(icEffectAnnotationFn->getName().str() == annotationFnName);
        icEffectAnnotationFn->addFnAttr(Attribute::AttrKind::NoUnwind);

        CallInst::Create(icEffectAnnotationFn, icEffectAnnotationArgs, "", origin);
    }

    // Now, rewrite the IC body from a lambda to a normal function.
    // The IC body is the slow path which probably won't be inlined. This rewrite reduces the overhead of this
    // slowpath call and probably also reduces code size.
    //
    std::string wrapperFnName = GetFirstAvailableFunctionNameWithPrefix(module, x_icBodyClosureWrapperFunctionPrefix);
    RewriteClosureToFunctionCallResult rr = RewriteClosureToFunctionCall(lambdaFunc, closureAlloca, wrapperFnName);

    std::vector<Type*> closureArgTys;
    for (Value* val : rr.m_args) { closureArgTys.push_back(val->getType()); }

    Function* closureWrapper = rr.m_newFunc;
    Type* resType = origin->getType();
    ReleaseAssert(resType == lambdaFunc->getReturnType());
    ReleaseAssert(resType == closureWrapper->getReturnType());
    CallInst* replacement;
    {
        std::vector<Value*> icBodyArgs;
        icBodyArgs.push_back(ic);
        icBodyArgs.push_back(icKey);
        icBodyArgs.push_back(icKeyImpossibleValue);
        icBodyArgs.push_back(closureWrapper);
        for (Value* val : rr.m_args) { icBodyArgs.push_back(val); }

        std::vector<Type*> icBodyArgTys;
        for (Value* val : icBodyArgs)
        {
            icBodyArgTys.push_back(val->getType());
        }

        FunctionType* icBodyAnnotationFty = FunctionType::get(resType, icBodyArgTys, false /*isVarArg*/);
        std::string icBodyAnnotationFnName = GetFirstAvailableFunctionNameWithPrefix(module, x_createIcBodyPlaceholderFunctionPrefix);
        Function* icBodyAnnotationFn = Function::Create(icBodyAnnotationFty, GlobalValue::ExternalLinkage, icBodyAnnotationFnName, module);
        ReleaseAssert(icBodyAnnotationFn->getName().str() == icBodyAnnotationFnName);
        icBodyAnnotationFn->addFnAttr(Attribute::AttrKind::NoUnwind);

        replacement = CallInst::Create(icBodyAnnotationFn, icBodyArgs, "", origin /*insertBefore*/);
    }

    ReleaseAssert(replacement->getType() == origin->getType());
    if (!llvm_value_has_type<void>(origin))
    {
        origin->replaceAllUsesWith(replacement);
    }
    ReleaseAssert(origin->use_empty());

    // Remove all the API function calls as they have been processed by us and are no longer useful
    //
    {
        std::unordered_set<Instruction*> checkUnique;
        for (Instruction* i : instructionToRemove)
        {
            ReleaseAssert(!checkUnique.count(i));
            checkUnique.insert(i);
        }
        ReleaseAssert(checkUnique.count(origin));
    }

    for (Instruction* i : instructionToRemove)
    {
        ReleaseAssert(i->use_empty());
        i->eraseFromParent();
    }

    // Validate that the function is still valid
    // Note that if the user write weird logic that is disallowed by us (e.g., some API calls are executed conditionally),
    // then it is possible that our transformation generates ill-formed SSA. This validation will catch those cases.
    //
    ValidateLLVMFunction(originFn);
}

}   // anonymous namespace

void AstInlineCache::PreprocessModule(llvm::Module* module)
{
    using namespace llvm;
    std::vector<CallInst*> createIcApis;
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
                    if (callee != nullptr && IsCXXSymbol(callee->getName().str()))
                    {
                        std::string symName = DemangleCXXSymbol(callee->getName().str());
                        if (symName.find(" DeegenImpl_MakeIC_SetMainLambda<") != std::string::npos)
                        {
                            createIcApis.push_back(callInst);
                        }
                    }
                }
            }
        }
    }

    for (CallInst* origin: createIcApis)
    {
        PreprocessCreateIcApi(origin);
    }

    ValidateLLVMModule(module);

    DesugarAndSimplifyLLVMModule(module, DesugaringLevel::PerFunctionSimplifyOnly);

    // Scan through the module again, just to make sure that all the APIs we want to process has been processed
    //
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
                    if (callee != nullptr && IsCXXSymbol(callee->getName().str()))
                    {
                        std::string symName = DemangleCXXSymbol(callee->getName().str());
                        ReleaseAssert(symName.find(" DeegenImpl_MakeIC_SetMainLambda<") == std::string::npos);
                        ReleaseAssert(symName.find(" DeegenImpl_MakeIC_MarkEffect<") == std::string::npos);
                        ReleaseAssert(symName.find(" DeegenImpl_MakeIC_AddKey<") == std::string::npos);
                        ReleaseAssert(symName.find(" DeegenImpl_MakeIC_SetICKeyImpossibleValue<") == std::string::npos);
                        ReleaseAssert(symName != "MakeInlineCache()");
                    }
                }
            }
        }
    }
}

static AstInlineCache WARN_UNUSED AstInlineCacheParseOneUse(llvm::CallInst* origin)
{
    using namespace llvm;
    AstInlineCache r;
    ReleaseAssert(origin->arg_size() >= 4);
    ReleaseAssert(origin->getCalledFunction() != nullptr && origin->getCalledFunction()->getName().startswith(x_createIcBodyPlaceholderFunctionPrefix));
    Value* ic = origin->getArgOperand(0);
    CallInst* def = dyn_cast<CallInst>(ic);
    ReleaseAssert(def != nullptr);
    ReleaseAssert(def->getCalledFunction() != nullptr && def->getCalledFunction()->getName().startswith(x_createIcInitPlaceholderFunctionPrefix));
    r.m_icPtrOrigin = def;
    r.m_origin = origin;
    r.m_icKey = origin->getArgOperand(1);

    Value* icKeyImpossibleVal = origin->getArgOperand(2);
    if (isa<UndefValue>(icKeyImpossibleVal))
    {
        r.m_icKeyImpossibleValueMaybeNull = nullptr;
    }
    else
    {
        ReleaseAssert(isa<Constant>(icKeyImpossibleVal));
        r.m_icKeyImpossibleValueMaybeNull = cast<Constant>(icKeyImpossibleVal);
        ReleaseAssert(r.m_icKeyImpossibleValueMaybeNull->getType() == r.m_icKey->getType());
    }

    r.m_bodyFn = dyn_cast<Function>(origin->getArgOperand(3));
    ReleaseAssert(r.m_bodyFn != nullptr);

    r.m_bodyFnIcPtrArgOrd = static_cast<uint32_t>(-1);
    for (uint32_t i = 4; i < origin->arg_size(); i++)
    {
        Value* val = origin->getArgOperand(i);
        r.m_bodyFnArgs.push_back(val);
        if (val == ic)
        {
            ReleaseAssert(r.m_bodyFnIcPtrArgOrd == static_cast<uint32_t>(-1));
            r.m_bodyFnIcPtrArgOrd = i - 4;
        }
    }
    ReleaseAssert(r.m_bodyFnIcPtrArgOrd != static_cast<uint32_t>(-1));

    // Parse out all information about the effects
    //
    std::vector<Instruction*> instructionsToRemove;
    for (User* usr : ic->users())
    {
        ReleaseAssert(isa<CallInst>(usr));
        CallInst* ci = cast<CallInst>(usr);
        ReleaseAssert(ci->arg_size() > 0 && ci->getArgOperand(0) == ic);
        if (ci == origin)
        {
            continue;
        }
        instructionsToRemove.push_back(ci);
        ReleaseAssert(ci->getCalledFunction() != nullptr && ci->getCalledFunction()->getName().startswith(x_createIcRegisterEffectPlaceholderFunctionPrefix));
        AstInlineCache::Effect e;
        ReleaseAssert(ci->arg_size() >= 3);
        e.m_effectFnMain = dyn_cast<Function>(ci->getArgOperand(2));
        ReleaseAssert(e.m_effectFnMain != nullptr);
        for (uint32_t i = 3; i < ci->arg_size(); i++)
        {
            e.m_effectFnMainArgs.push_back(ci->getArgOperand(i));
        }
        Function* annotationFn = dyn_cast<Function>(ci->getArgOperand(1));
        ReleaseAssert(annotationFn != nullptr);
        ReleaseAssert(annotationFn->hasNUses(2));
        CallInst* target = nullptr;
        {
            Use& u1 = *annotationFn->use_begin();
            Use& u2 = *(++annotationFn->use_begin());
            User* usr1 = u1.getUser();
            User* usr2 = u2.getUser();
            ReleaseAssert(isa<CallInst>(usr1));
            ReleaseAssert(isa<CallInst>(usr2));
            CallInst* ci1 = cast<CallInst>(usr1);
            CallInst* ci2 = cast<CallInst>(usr2);
            if (ci1 == ci)
            {
                target = ci2;
            }
            else
            {
                target = ci1;
            }
        }
        ReleaseAssert(target != nullptr && target != ci);
        ReleaseAssert(target->getCalledFunction() == annotationFn);
        e.m_origin = target;

        ReleaseAssert(target->arg_size() >= 7);
        e.m_effectFnBodyCapture = dyn_cast<AllocaInst>(target->getArgOperand(1));
        ReleaseAssert(e.m_effectFnBodyCapture != nullptr);

        e.m_effectFnBody = dyn_cast<Function>(target->getArgOperand(2));
        ReleaseAssert(e.m_effectFnBody != nullptr);

        Function* effectWrapperFn = dyn_cast<Function>(target->getArgOperand(3));
        ReleaseAssert(effectWrapperFn != nullptr);
        ReleaseAssert(effectWrapperFn == e.m_effectFnMain);

        e.m_icStateDecoder = dyn_cast<Function>(target->getArgOperand(4));
        ReleaseAssert(e.m_icStateDecoder != nullptr);
        ReleaseAssert(e.m_icStateDecoder->getName().startswith(x_decodeICStateToEffectLambdaCaptureFunctionPrefix));

        e.m_icStateEncoder = dyn_cast<Function>(target->getArgOperand(5));
        ReleaseAssert(e.m_icStateEncoder != nullptr);
        ReleaseAssert(e.m_icStateEncoder->getName().startswith(x_encodeICStateFunctionPrefix));

        Value* numIcStateCapturesV = target->getArgOperand(6);
        ReleaseAssert(isa<ConstantInt>(numIcStateCapturesV));
        ReleaseAssert(llvm_value_has_type<uint64_t>(numIcStateCapturesV));
        uint64_t numIcStateCaptures = GetValueOfLLVMConstantInt<uint64_t>(numIcStateCapturesV);
        ReleaseAssert(target->arg_size() == 7 + numIcStateCaptures);
        for (uint32_t i = 0; i < numIcStateCaptures; i++)
        {
            Value* icStateCapture = target->getArgOperand(7 + i);
            e.m_icStateVals.push_back(icStateCapture);
        }

        ReleaseAssert(e.m_icStateEncoder->arg_size() == 1 + numIcStateCaptures);
        ReleaseAssert(e.m_icStateDecoder->arg_size() == 2 + numIcStateCaptures);
        for (uint32_t i = 0; i < numIcStateCaptures; i++)
        {
            ReleaseAssert(e.m_icStateEncoder->getArg(1 + i)->getType() == e.m_icStateVals[i]->getType());
            ReleaseAssert(llvm_value_has_type<uint64_t>(e.m_icStateDecoder->getArg(2 + i)));
        }

        ReleaseAssert(e.m_icStateDecoder->hasNUses(2));
        CallInst* decoderCall = nullptr;
        {
            Use& u1 = *e.m_icStateDecoder->use_begin();
            Use& u2 = *(++e.m_icStateDecoder->use_begin());
            User* usr1 = u1.getUser();
            User* usr2 = u2.getUser();
            ReleaseAssert(isa<CallInst>(usr1));
            ReleaseAssert(isa<CallInst>(usr2));
            CallInst* ci1 = cast<CallInst>(usr1);
            CallInst* ci2 = cast<CallInst>(usr2);
            if (ci1 == target)
            {
                decoderCall = ci2;
            }
            else
            {
                decoderCall = ci1;
            }
        }
        ReleaseAssert(decoderCall != nullptr && decoderCall != target);
        ReleaseAssert(decoderCall->getCalledFunction() == e.m_icStateDecoder);
        ReleaseAssert(decoderCall->arg_size() == 2 + numIcStateCaptures);
        for (uint32_t i = 0; i < numIcStateCaptures; i++)
        {
            Value* val = decoderCall->getArgOperand(2 + i);
            ReleaseAssert(isa<ConstantInt>(val));
            ReleaseAssert(llvm_value_has_type<uint64_t>(val));
            uint64_t ord = GetValueOfLLVMConstantInt<uint64_t>(val);
            e.m_icStateOrdInEffectCapture.push_back(SafeIntegerCast<uint32_t>(ord));
        }
        e.m_decoderCall = decoderCall;

        ReleaseAssert(e.m_icStateOrdInEffectCapture.size() == e.m_icStateVals.size());
        ReleaseAssert(e.m_effectFnMain->getReturnType() == r.m_bodyFn->getReturnType());
        ReleaseAssert(e.m_effectFnBody->getReturnType() == r.m_bodyFn->getReturnType());
        ReleaseAssert(e.m_origin->getType() == r.m_bodyFn->getReturnType());
        r.m_effects.push_back(e);
    }

    for (size_t i = 0; i < r.m_effects.size(); i++)
    {
        r.m_effects[i].m_effectOrdinal = i;
    }

    // Parse out all information about the other simple APIs
    //
    for (BasicBlock& bb : *r.m_bodyFn)
    {
        for (Instruction& inst : bb)
        {
            CallInst* callInst = dyn_cast<CallInst>(&inst);
            if (callInst != nullptr)
            {
                Function* callee = callInst->getCalledFunction();
                if (callee != nullptr && IsCXXSymbol(callee->getName().str()))
                {
                    std::string symName = DemangleCXXSymbol(callee->getName().str());
                    if (symName.find(" DeegenImpl_MakeIC_MarkEffectValue<") != std::string::npos)
                    {
                        ReleaseAssert(callInst->arg_size() == 2);
                        ReleaseAssert(callInst->getArgOperand(0) == r.m_bodyFn->getArg(r.m_bodyFnIcPtrArgOrd));
                        ReleaseAssert(callInst->getType() == r.m_bodyFn->getReturnType());
                        AstInlineCache::EffectValue e;
                        e.m_effectValue = callInst->getArgOperand(1);
                        ReleaseAssert(llvm_value_has_type<void*>(e.m_effectValue));
                        e.m_origin = callInst;
                        r.m_effectValues.push_back(e);
                    }
                    else if (symName.find("DeegenImpl_MakeIC_SetUncacheableForThisExecution(") != std::string::npos)
                    {
                        ReleaseAssert(callInst->arg_size() == 1);
                        ReleaseAssert(callInst->getArgOperand(0) == r.m_bodyFn->getArg(r.m_bodyFnIcPtrArgOrd));
                        r.m_setUncacheableApiCalls.push_back(callInst);
                    }
                }
            }
        }
    }

    {
        std::unordered_set<Instruction*> checkUnique;
        for (Instruction* i : instructionsToRemove)
        {
            ReleaseAssert(!checkUnique.count(i));
            checkUnique.insert(i);
        }
    }

    for (Instruction* i : instructionsToRemove)
    {
        ReleaseAssert(i->use_empty());
        i->eraseFromParent();
    }

    return r;
}

std::vector<AstInlineCache> WARN_UNUSED AstInlineCache::GetAllUseInFunction(llvm::Function* func)
{
    using namespace llvm;
    std::vector<CallInst*> allUses;
    for (BasicBlock& bb : *func)
    {
        for (Instruction& inst : bb)
        {
            CallInst* callInst = dyn_cast<CallInst>(&inst);
            if (callInst != nullptr)
            {
                Function* callee = callInst->getCalledFunction();
                if (callee != nullptr)
                {
                    if (callee->getName().str().starts_with(x_createIcBodyPlaceholderFunctionPrefix))
                    {
                        allUses.push_back(callInst);
                    }
                }
            }
        }
    }
    std::vector<AstInlineCache> res;
    for (CallInst* ci : allUses)
    {
        res.push_back(AstInlineCacheParseOneUse(ci));
    }
    return res;
}

}   // namespace dast
