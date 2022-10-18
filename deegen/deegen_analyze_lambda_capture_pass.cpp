#include "deegen_analyze_lambda_capture_pass.h"

namespace dast {

static void DeegenAddLambdaCaptureAnnotationImpl(llvm::Function* func)
{
    using namespace llvm;
    LLVMContext& ctx = func->getContext();
    Module* module = func->getParent();
    ReleaseAssert(func->arg_size() == 1);

    // Backtrack until we get the original definition of the lambda
    //
    AllocaInst* def = nullptr;
    CallInst* ci = nullptr;
    {
        uint32_t argOrd = 0;
        while (true)
        {
            ReleaseAssert(func->hasOneUse());
            User* usr = func->use_begin()->getUser();
            ReleaseAssert(isa<CallInst>(usr));
            ci = cast<CallInst>(usr);
            ReleaseAssert(ci->getCalledFunction() == func);
            ReleaseAssert(ci->arg_size() > argOrd);

            func = ci->getFunction();
            Value* val = ci->getArgOperand(argOrd);
            if (isa<AllocaInst>(val))
            {
                def = cast<AllocaInst>(val);
                break;
            }
            ReleaseAssert(isa<LoadInst>(val));
            LoadInst* li = cast<LoadInst>(val);
            Value* from = li->getPointerOperand();
            ReleaseAssert(isa<AllocaInst>(from));
            AllocaInst* fromAlloca = cast<AllocaInst>(from);
            bool foundStoreInst = false;
            StoreInst* si = nullptr;
            for (Use& u : fromAlloca->uses())
            {
                if (isa<StoreInst>(u.getUser()))
                {
                    ReleaseAssert(!foundStoreInst);
                    foundStoreInst = true;
                    si = cast<StoreInst>(u.getUser());
                }
            }
            ReleaseAssert(foundStoreInst);
            ReleaseAssert(si->getPointerOperand() == fromAlloca);
            ReleaseAssert(isa<Argument>(si->getValueOperand()));
            Argument* arg = cast<Argument>(si->getValueOperand());
            argOrd = arg->getArgNo();
        }
    }

    ReleaseAssert(def != nullptr && ci != nullptr);
    ReleaseAssert(def->getFunction() == func && ci->getFunction() == func);

    ReleaseAssert(def->getAllocatedType()->isStructTy());
    StructType* captureTy = dyn_cast<StructType>(def->getAllocatedType());
    size_t numCaptures = captureTy->getStructNumElements();

    using CaptureKind = DeegenAnalyzeLambdaCapturePass::CaptureKind;

    struct Item
    {
        Item() : m_populated(false) { }

        bool m_populated;
        CaptureKind m_captureKind;
        AllocaInst* m_localVarAlloca;
        size_t m_ordInParentCapture;
    };

    std::vector<Item> info;
    info.resize(numCaptures);

    // Assert that a GEP instruction is operating on the parent closure capture, and return the ordinal
    // That is, we try to assert that the GEP has the following pattern:
    // func fn(%0) # %0 is the parent closure
    //    %1 = alloca ptr
    //    store %0, %1
    //    %2 = load %0
    //    %gep = getelementptr %2, { 0, ord }
    //
    // and we return the 'ord' part in the gep
    //
    auto assertGepIsOnParentCaptureAndGetOrd = [func](GetElementPtrInst* gep) WARN_UNUSED -> size_t
    {
        ReleaseAssert(isa<LoadInst>(gep->getPointerOperand()));
        LoadInst* li = cast<LoadInst>(gep->getPointerOperand());
        ReleaseAssert(isa<AllocaInst>(li->getPointerOperand()));
        AllocaInst* ai = cast<AllocaInst>(li->getPointerOperand());
        StoreInst* onlyStore = nullptr;
        {
            bool foundStoreInst = false;
            for (Use& u : ai->uses())
            {
                if (isa<StoreInst>(u.getUser()))
                {
                    ReleaseAssert(!foundStoreInst);
                    foundStoreInst = true;
                    onlyStore = cast<StoreInst>(u.getUser());
                }
            }
            ReleaseAssert(foundStoreInst);
        }

        ReleaseAssert(func->arg_size() > 0);
        ReleaseAssert(onlyStore->getPointerOperand() == ai);
        ReleaseAssert(onlyStore->getValueOperand() == func->getArg(0));

        ReleaseAssert(gep->getNumIndices() == 2);
        std::vector<int64_t> indices;
        for (Value* idx : gep->indices())
        {
            ReleaseAssert(isa<ConstantInt>(idx));
            int64_t val = cast<ConstantInt>(idx)->getSExtValue();
            indices.push_back(val);
        }
        ReleaseAssert(indices.size() == 2);
        ReleaseAssert(indices[0] == 0);
        ReleaseAssert(0 <= indices[1]);
        return static_cast<size_t>(indices[1]);
    };

    for (Use& u : def->uses())
    {
        User* usr = u.getUser();
        ReleaseAssert(isa<Instruction>(usr));
        if (isa<CallInst>(usr))
        {
            continue;
        }
        ReleaseAssert(isa<GetElementPtrInst>(usr));
        GetElementPtrInst* gep = cast<GetElementPtrInst>(usr);
        ReleaseAssert(gep->getParent() == ci->getParent());
        ReleaseAssert(gep->getPointerOperand() == def);
        ReleaseAssert(u.getOperandNo() == gep->getPointerOperandIndex());
        ReleaseAssert(gep->getNumIndices() == 2);
        std::vector<int64_t> indices;
        for (Value* idx : gep->indices())
        {
            ReleaseAssert(isa<ConstantInt>(idx));
            int64_t val = cast<ConstantInt>(idx)->getSExtValue();
            indices.push_back(val);
        }
        ReleaseAssert(indices.size() == 2);
        ReleaseAssert(indices[0] == 0);
        ReleaseAssert(0 <= indices[1] && indices[1] < static_cast<int64_t>(numCaptures));
        size_t captureIdx = static_cast<size_t>(indices[1]);
        ReleaseAssert(!info[captureIdx].m_populated);
        ReleaseAssert(gep->hasOneUse());
        ReleaseAssert(isa<Instruction>(*gep->user_begin()));
        Instruction* inst = cast<Instruction>(*gep->user_begin());

        if (isa<StoreInst>(inst))
        {
            StoreInst* si = cast<StoreInst>(inst);
            ReleaseAssert(si->getPointerOperand() == gep);
            Value* vo = si->getValueOperand();
            if (isa<AllocaInst>(vo))
            {
                // The IR is storing the address of an alloca into the captured state
                // So this is a by-ref capture of a local variable
                //
                info[captureIdx].m_populated = true;
                info[captureIdx].m_captureKind = CaptureKind::ByRefCaptureOfLocalVar;
                info[captureIdx].m_localVarAlloca = cast<AllocaInst>(vo);
                continue;
            }

            if (isa<LoadInst>(vo))
            {
                LoadInst* li = cast<LoadInst>(vo);
                Value* from = li->getPointerOperand();

                if (isa<AllocaInst>(from))
                {
                    // The IR is loading from an alloca, and storing its value into the captures state
                    // So this is a by-value capture of a local variable
                    //
                    info[captureIdx].m_populated = true;
                    info[captureIdx].m_captureKind = CaptureKind::ByValueCaptureOfLocalVar;
                    info[captureIdx].m_localVarAlloca = cast<AllocaInst>(from);
                    continue;
                }

                if (isa<LoadInst>(from))
                {
                    LoadInst* li2 = cast<LoadInst>(from);
                    // This GEP must be a GEP of the parent closure.
                    //
                    ReleaseAssert(isa<GetElementPtrInst>(li2->getPointerOperand()));
                    GetElementPtrInst* gi = cast<GetElementPtrInst>(li2->getPointerOperand());
                    size_t ord = assertGepIsOnParentCaptureAndGetOrd(gi);

                    // The IR looks like the following:
                    //     %0 = getelementptr %parentCapture, 0, ord
                    //     %1 = load %0
                    //     %2 = load %1
                    //     store %2 into capture state
                    // This implies that the enclosing lambda captured some value by reference (stored as a pointer),
                    // and the current lambda is capturing this value by value (resulting in a copy).
                    //
                    info[captureIdx].m_populated = true;
                    info[captureIdx].m_captureKind = CaptureKind::ByValueCaptureOfByRef;
                    info[captureIdx].m_ordInParentCapture = ord;
                    continue;
                }

                ReleaseAssert(isa<GetElementPtrInst>(from));
                // This GEP must be a GEP of the parent closure.
                //
                GetElementPtrInst* gi = cast<GetElementPtrInst>(from);
                size_t ord = assertGepIsOnParentCaptureAndGetOrd(gi);

                // The IR is loading the value from the parent capture and storing it into the current capture
                // There are two possiblities: both parent and current lambda are capturing by reference,
                // or both parent and current lambda are capturing by value.
                // Note that if the type of the store operand is not ptr, then we can actually deduce that
                // both parent and current lambda must be capturing the value by value. But providing this more
                // accurate info is not going to do anything good, so we are not doing this.
                //
                info[captureIdx].m_populated = true;
                info[captureIdx].m_captureKind = CaptureKind::SameCaptureKindAsEnclosingLambda;
                info[captureIdx].m_ordInParentCapture = ord;
                continue;
            }

            ReleaseAssert(isa<GetElementPtrInst>(vo));
            // This GEP must be a GEP of the parent closure.
            //
            GetElementPtrInst* gi = cast<GetElementPtrInst>(vo);
            size_t ord = assertGepIsOnParentCaptureAndGetOrd(gi);

            // This lambda is storing a pointer to the enclosing lambda's capture.
            // The only possibility is that the parent captured some value by value,
            // and the current lambda captured it by reference.
            //
            info[captureIdx].m_populated = true;
            info[captureIdx].m_captureKind = CaptureKind::ByRefCaptureOfByValue;
            info[captureIdx].m_ordInParentCapture = ord;
        }
        else
        {
            ReleaseAssert(isa<CallInst>(inst));
            CallInst* callInst = cast<CallInst>(inst);
            Function* calledFn = callInst->getCalledFunction();
            ReleaseAssert(calledFn != nullptr);
            ReleaseAssert(calledFn->isIntrinsic());
            ReleaseAssert(calledFn->getIntrinsicID() == Intrinsic::memcpy);
            ReleaseAssert(calledFn->arg_size() == 4);
            ReleaseAssert(callInst->getArgOperand(0) == gep);
            Value* vo = callInst->getArgOperand(1);
            ReleaseAssert(llvm_value_has_type<void*>(vo));

            if (isa<AllocaInst>(vo))
            {
                // The IR did a memcpy to copy the contents of an alloca into the capture
                // So this is a by-value capture of a local variable
                //
                info[captureIdx].m_populated = true;
                info[captureIdx].m_captureKind = CaptureKind::ByValueCaptureOfLocalVar;
                info[captureIdx].m_localVarAlloca = cast<AllocaInst>(vo);
                continue;
            }

            if (isa<LoadInst>(vo))
            {
                LoadInst* li = cast<LoadInst>(vo);
                ReleaseAssert(isa<GetElementPtrInst>(li->getPointerOperand()));
                // This GEP must be a GEP of the parent closure.
                //
                GetElementPtrInst* gi = cast<GetElementPtrInst>(li->getPointerOperand());
                size_t ord = assertGepIsOnParentCaptureAndGetOrd(gi);

                // The IR load a pointer from the enclosing lambda's capture,
                // and did a memcpy to copy some contents pointed by the pointer to the capture
                // This means the enclosing lambda is capturing some value by reference,
                // and the current lambda is capturing that value by value.
                //
                info[captureIdx].m_populated = true;
                info[captureIdx].m_captureKind = CaptureKind::ByValueCaptureOfByRef;
                info[captureIdx].m_ordInParentCapture = ord;
                continue;
            }

            ReleaseAssert(isa<GetElementPtrInst>(vo));
            // This GEP must be a GEP of the parent closure.
            //
            GetElementPtrInst* gi = cast<GetElementPtrInst>(vo);
            size_t ord = assertGepIsOnParentCaptureAndGetOrd(gi);

            // The IR is copying some contents of the enclosing lambda's capture into the current capture
            // I don't think Clang frontend would generate a memcpy to copy a pointer, but for now
            // let's stay conservative and assign CaptureKind::SameCaptureKindAsEnclosingLambda, since
            // there isn't any downside of doing that.
            //
            info[captureIdx].m_populated = true;
            info[captureIdx].m_captureKind = CaptureKind::SameCaptureKindAsEnclosingLambda;
            info[captureIdx].m_ordInParentCapture = ord;
        }
    }

    for (size_t i = 0; i < numCaptures; i++)
    {
        if (!info[i].m_populated)
        {
            Type* eleTy = captureTy->getElementType(static_cast<uint32_t>(i));
            if (isa<ArrayType>(eleTy) && cast<ArrayType>(eleTy)->getElementType() == llvm_type_of<uint8_t>(ctx))
            {
                // This is probably a padding, don't bother with it.
                // We probably want to assert stronger that it is actually a padding, but let's just go with it for now
                //
                continue;
            }

            fprintf(stderr, "Failed to parse out a lambda capture (ord %d of the %d captures) in function %s. Did you captured an empty class by value?\n",
                    static_cast<int>(i), static_cast<int>(numCaptures), func->getName().str().c_str());
            abort();
        }
    }

    std::string annotationFnName;
    size_t suffixOrd = 0;
    while (true)
    {
        annotationFnName = DeegenAnalyzeLambdaCapturePass::x_annotationFnPrefix + std::to_string(suffixOrd);
        if (module->getNamedValue(annotationFnName) == nullptr)
        {
            break;
        }
        suffixOrd++;
    }

    std::vector<Value*> annotationFnArgs;
    annotationFnArgs.push_back(def);
    for (size_t i = 0; i < numCaptures; i++)
    {
        if (!info[i].m_populated)
        {
            continue;
        }
        CaptureKind captureKind = info[i].m_captureKind;
        annotationFnArgs.push_back(CreateLLVMConstantInt<uint64_t>(ctx, i));
        annotationFnArgs.push_back(CreateLLVMConstantInt<uint64_t>(ctx, static_cast<uint64_t>(captureKind)));
        if (captureKind == CaptureKind::ByRefCaptureOfLocalVar || captureKind == CaptureKind::ByValueCaptureOfLocalVar)
        {
            annotationFnArgs.push_back(info[i].m_localVarAlloca);
        }
        else
        {
            annotationFnArgs.push_back(CreateLLVMConstantInt<uint64_t>(ctx, info[i].m_ordInParentCapture));
        }
    }

    std::vector<Type*> annotationFnArgTys;
    for (Value* val : annotationFnArgs) { annotationFnArgTys.push_back(val->getType()); }

    FunctionType* fty = FunctionType::get(llvm_type_of<void>(ctx) /*result*/, annotationFnArgTys, false /*isVarArgs*/);
    Function* annotationFn = Function::Create(fty, GlobalValue::LinkageTypes::ExternalLinkage, annotationFnName, module);
    ReleaseAssert(annotationFn->getName().str() == annotationFnName);
    annotationFn->addFnAttr(Attribute::AttrKind::NoUnwind);

    CallInst::Create(annotationFn, annotationFnArgs, "", ci /*insertBefore*/);

    ValidateLLVMFunction(func);
}

void DeegenAnalyzeLambdaCapturePass::AddAnnotations(llvm::Module* module)
{
    using namespace llvm;
    for (Function& func : *module)
    {
        std::string fnName = func.getName().str();
        if (IsCXXSymbol(fnName) && DemangleCXXSymbol(fnName).starts_with("void const* DeegenGetLambdaClosureAddr<"))
        {
            DeegenAddLambdaCaptureAnnotationImpl(&func);
        }
    }
    ValidateLLVMModule(module);
}

}   // namespace dast
