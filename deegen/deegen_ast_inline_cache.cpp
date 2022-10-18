#include "deegen_ast_inline_cache.h"

namespace dast {

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

constexpr const char* x_createIcInitPlaceholderFunctionPrefix = "__DeegenInternal_AstGetInlineCachePtr_IdentificationFunc_";
constexpr const char* x_createIcBodyPlaceholderFunctionPrefix = "__DeegenInternal_AstCreateIC_IdentificationFunc_";
constexpr const char* x_icEffectPlaceholderFunctionPrefix = "__DeegenInternal_AstICEffect_IdentificationFunc_";
constexpr const char* x_icBodyClosureWrapperFunctionPrefix = "__deegen_inline_cache_body_";
// constexpr const char* x_icEffectClosureWrapperFunctionPrefix = "__deegen_inline_cache_effect_";

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

static llvm::Function* WARN_UNUSED CreatePlaceholderFunctionForCreateICInit(llvm::Module* module)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    std::string decidedName = GetFirstAvailableFunctionNameWithPrefix(module, x_createIcInitPlaceholderFunctionPrefix);
    FunctionType* fty = FunctionType::get(llvm_type_of<void*>(ctx) /*result*/, { } /*args*/, false /*isVarArg*/);
    Function* func = Function::Create(fty, GlobalValue::LinkageTypes::ExternalLinkage, decidedName, module);
    ReleaseAssert(func->getName().str() == decidedName);
    func->addFnAttr(Attribute::AttrKind::NoUnwind);
    return func;
}

static llvm::Function* WARN_UNUSED CreatePlaceholderFunctionForCreateICBody(llvm::Module* module, llvm::Type* keyType, std::vector<llvm::Type*> closureArgTypes, llvm::Type* resType)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    std::string decidedName = GetFirstAvailableFunctionNameWithPrefix(module, x_createIcBodyPlaceholderFunctionPrefix);

    std::vector<Type*> argTypes;
    argTypes.push_back(keyType);
    argTypes.push_back(keyType);
    argTypes.push_back(llvm_type_of<void*>(ctx));
    for (Type* t : closureArgTypes) { argTypes.push_back(t); }

    FunctionType* fty = FunctionType::get(resType /*result*/, argTypes, false /*isVarArg*/);
    Function* func = Function::Create(fty, GlobalValue::LinkageTypes::ExternalLinkage, decidedName, module);
    ReleaseAssert(func->getName().str() == decidedName);
    func->addFnAttr(Attribute::AttrKind::NoUnwind);
    return func;
}

static llvm::Function* WARN_UNUSED CreatePlaceholderFunctionForICEffect(llvm::Module* module, llvm::Type* resType)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    std::string decidedName = GetFirstAvailableFunctionNameWithPrefix(module, x_icEffectPlaceholderFunctionPrefix);

    std::vector<Type*> argTypes;
    argTypes.push_back(llvm_type_of<void*>(ctx));
    argTypes.push_back(llvm_type_of<void*>(ctx));
    argTypes.push_back(llvm_type_of<void*>(ctx));

    FunctionType* fty = FunctionType::get(resType /*result*/, argTypes, false /*isVarArg*/);
    Function* func = Function::Create(fty, GlobalValue::LinkageTypes::ExternalLinkage, decidedName, module);
    ReleaseAssert(func->getName().str() == decidedName);
    func->addFnAttr(Attribute::AttrKind::NoUnwind);
    return func;
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
        Function* uniqueInitFn = CreatePlaceholderFunctionForCreateICInit(module);
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

            // We always pass 'ic' as the first parameter in all APIs, so we can simply assert it's the first param.
            //
            ReleaseAssert(&u == &icApiCall->getOperandUse(0));
            Function* apiFn = icApiCall->getCalledFunction();
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

    std::string wrapperFnName = GetFirstAvailableFunctionNameWithPrefix(module, x_icBodyClosureWrapperFunctionPrefix);
    RewriteClosureToFunctionCallResult rr = RewriteClosureToFunctionCall(lambdaFunc, closureAlloca, wrapperFnName);

    std::vector<Type*> closureArgTys;
    for (Value* val : rr.m_args) { closureArgTys.push_back(val->getType()); }

    Function* closureWrapper = rr.m_newFunc;
    Type* resType = origin->getType();
    ReleaseAssert(resType == lambdaFunc->getReturnType());
    ReleaseAssert(resType == closureWrapper->getReturnType());
    Function* idFn = CreatePlaceholderFunctionForCreateICBody(module, icKey->getType(), closureArgTys, resType);
    CallInst* replacement;
    {
        std::vector<Value*> icBodyArgs;
        icBodyArgs.push_back(icKey);
        icBodyArgs.push_back(icKeyImpossibleValue);
        icBodyArgs.push_back(closureWrapper);
        for (Value* val : rr.m_args) { icBodyArgs.push_back(val); }
        replacement = CallInst::Create(idFn, icBodyArgs, "", origin /*insertBefore*/);
    }

    ReleaseAssert(replacement->getType() == origin->getType());
    if (!llvm_value_has_type<void>(origin))
    {
        origin->replaceAllUsesWith(replacement);
    }
    else
    {
        ReleaseAssert(origin->use_empty());
    }

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

static void PreprocessIcEffectApi(llvm::Module* module, llvm::CallInst* origin)
{
    using namespace llvm;
    ReleaseAssert(origin->arg_size() == 3);
    Value* ic = origin->getArgOperand(0);
    ReleaseAssert(llvm_value_has_type<void*>(ic));
    Value* closurePtr = origin->getArgOperand(1);
    ReleaseAssert(llvm_value_has_type<void*>(closurePtr));
    Value* lambdaPtrPtr = origin->getArgOperand(2);
    ReleaseAssert(llvm_value_has_type<void*>(lambdaPtrPtr));
    Function* lambda = GetLambdaFunctionFromPtrPtr(lambdaPtrPtr);
    Function* idFn = CreatePlaceholderFunctionForICEffect(module, origin->getType());
    CallInst* replacement = CallInst::Create(idFn, { ic, closurePtr, lambda}, "", origin /*insertBefore*/);
    ReleaseAssert(replacement->getType() == origin->getType());
    if (!llvm_value_has_type<void>(replacement))
    {
        origin->replaceAllUsesWith(replacement);
    }
    else
    {
        ReleaseAssert(origin->use_empty());
    }
    origin->eraseFromParent();
}

void AstInlineCache::PreprocessModule(llvm::Module* module)
{
    using namespace llvm;
    std::vector<CallInst*> createIcApis;
    std::vector<CallInst*> icEffectApis;
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
                        else if (symName.find(" DeegenImpl_MakeIC_MarkEffect<") != std::string::npos)
                        {
                            icEffectApis.push_back(callInst);
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

    for (CallInst* origin: icEffectApis)
    {
        PreprocessIcEffectApi(module, origin);
    }

    ValidateLLVMModule(module);

    DesugarAndSimplifyLLVMModule(module, DesugaringLevel::PerFunctionSimplifyOnly);
}

#if 0
AstInlineCache WARN_UNUSED AstInlineCacheParseOneUse(llvm::CallInst* origin)
{
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
        res.push_back()
}
#endif
}   // namespace dast
