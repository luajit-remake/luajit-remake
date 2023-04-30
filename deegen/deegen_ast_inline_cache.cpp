#include "deegen_ast_inline_cache.h"
#include "deegen_analyze_lambda_capture_pass.h"
#include "deegen_bytecode_operand.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_rewrite_closure_call.h"

namespace dast {

namespace {

constexpr const char* x_createIcInitPlaceholderFunctionPrefix = "__DeegenInternal_AstIC_GetICPtr_IdentificationFunc_";
constexpr const char* x_createIcBodyPlaceholderFunctionPrefix = "__DeegenInternal_AstIC_Body_IdentificationFunc_";
constexpr const char* x_createIcRegisterEffectPlaceholderFunctionPrefix = "__DeegenInternal_AstIC_RegisterEffect_IdentificationFunc_";
constexpr const char* x_icEffectPlaceholderFunctionPrefix = "__DeegenInternal_AstICEffect_IdentificationFunc_";
constexpr const char* x_icBodyClosureWrapperFunctionPrefix = "__deegen_inline_cache_body_";
constexpr const char* x_icEffectClosureWrapperFunctionPrefix = "__deegen_inline_cache_effect_";
constexpr const char* x_decodeICStateToEffectLambdaCaptureFunctionPrefix = "__deegen_inline_cache_decode_ic_state_";
constexpr const char* x_encodeICStateFunctionPrefix = "__deegen_inline_cache_encode_ic_state_";

struct PreprocessIcEffectApiResult
{
    llvm::CallInst* m_annotationInst;
    llvm::Function* m_annotationFn;
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
    Function* lambda = DeegenAnalyzeLambdaCapturePass::GetLambdaFunctionFromPtrPtr(lambdaPtrPtr);

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

    // Now, parse out the specialization info of the effect lambda
    //
    struct EffectSpecializationInfo
    {
        CallInst* m_origin;
        size_t m_ordInCaptureStruct;
        bool m_isFullCoverage;
        std::vector<Constant*> m_specializationValues;
        Value* m_valueInBodyFn;
    };

    ReleaseAssert(isa<StructType>(capture->getAllocatedType()));
    StructType* captureST = cast<StructType>(capture->getAllocatedType());

    std::vector<size_t> byteOffsetOfCapturedValueInCaptureStruct;
    {
        DataLayout dataLayout(module);
        const StructLayout* captureSTSL = dataLayout.getStructLayout(captureST);
        for (uint32_t i = 0; i < captureST->getNumElements(); i++)
        {
            uint64_t offset = captureSTSL->getElementOffset(i);
            byteOffsetOfCapturedValueInCaptureStruct.push_back(offset);
        }
    }

    std::unordered_map<size_t /*offset*/, size_t /*ord*/> capturedValueOffsetToOrdinalMap;
    for (size_t i = 0; i < byteOffsetOfCapturedValueInCaptureStruct.size(); i++)
    {
        size_t offset = byteOffsetOfCapturedValueInCaptureStruct[i];
        // I don't know if this might actually happen in LLVM (maybe it will only happen with no_unique_address attr?)..
        // But let's just assert this for sanity.
        //
        if (capturedValueOffsetToOrdinalMap.count(offset))
        {
            fprintf(stderr, "[ERROR] IC lambda capture contains two elements at the same offset %u!", static_cast<unsigned int>(offset));
            abort();
        }
        capturedValueOffsetToOrdinalMap[offset] = i;
    }

    DataLayout dataLayout(module);

    auto getOrdinalInCaptureStruct = [&](Value* capturedValueAddr) WARN_UNUSED -> size_t
    {
        ReleaseAssert(lambda->arg_size() == 1 && llvm_value_has_type<void*>(lambda->getArg(0)));
        ReleaseAssert(llvm_value_has_type<void*>(capturedValueAddr));
        if (capturedValueAddr == lambda->getArg(0))
        {
            return 0;
        }
        else
        {
            ReleaseAssert(isa<GetElementPtrInst>(capturedValueAddr));
            GetElementPtrInst* gep = cast<GetElementPtrInst>(capturedValueAddr);
            ReleaseAssert(gep->getPointerOperand() == lambda->getArg(0));
            APInt offsetAP(64 /*numBits*/, 0);
            ReleaseAssert(gep->accumulateConstantOffset(dataLayout, offsetAP /*out*/));
            uint64_t offset = offsetAP.getZExtValue();
            ReleaseAssert(capturedValueOffsetToOrdinalMap.count(offset));
            return capturedValueOffsetToOrdinalMap[offset];
        }
    };

    struct CaptureValueRangeInfo
    {
        CallInst* m_origin;
        size_t m_ordInCaptureStruct;
        int64_t m_lbInclusive;
        int64_t m_ubInclusive;
        Value* m_valueInBodyFn;
    };

    std::vector<EffectSpecializationInfo> allEffectSpecializationInfo;
    std::vector<CaptureValueRangeInfo> allCaptureValueRangeInfo;
    ReleaseAssert(lambda->arg_size() == 1);
    for (BasicBlock& bb : *lambda)
    {
        for (Instruction& inst : bb)
        {
            CallInst* ci = dyn_cast<CallInst>(&inst);
            if (ci != nullptr)
            {
                Function* callee = ci->getCalledFunction();
                if (callee != nullptr && IsCXXSymbol(callee->getName().str()))
                {
                    std::string demangledCxxSymbolName = DemangleCXXSymbol(callee->getName().str());
                    if (demangledCxxSymbolName.find(" DeegenImpl_MakeIC_SpecializeIcEffect<") != std::string::npos)
                    {
                        EffectSpecializationInfo esi;
                        ReleaseAssert(ci->arg_size() > 2);
                        esi.m_origin = ci;
                        esi.m_isFullCoverage = GetValueOfLLVMConstantInt<bool>(ci->getArgOperand(0));
                        Value* capturedValueAddr = ci->getArgOperand(1);
                        ReleaseAssert(llvm_value_has_type<void*>(capturedValueAddr));
                        size_t ordinalInCaptureStruct = getOrdinalInCaptureStruct(capturedValueAddr);
                        esi.m_ordInCaptureStruct = ordinalInCaptureStruct;

                        std::unordered_set<Constant*> checkUnique;
                        for (uint32_t i = 2; i < ci->arg_size(); i++)
                        {
                            Value* arg = ci->getArgOperand(i);
                            if (!isa<Constant>(arg))
                            {
                                fprintf(stderr, "[ERROR] IC effect specialization list contains non-constant value!\n");
                                abort();
                            }
                            Constant* cst = cast<Constant>(arg);
                            // In LLVM constants are hash-consed, so pointer comparison is fine
                            //
                            if (checkUnique.count(cst))
                            {
                                fprintf(stderr, "[ERROR] IC effect specialization list contains duplicated elements!\n");
                                abort();
                            }
                            checkUnique.insert(cst);
                            esi.m_specializationValues.push_back(cst);
                            ReleaseAssert(cst->getType() == esi.m_specializationValues[0]->getType());
                        }
                        ReleaseAssert(esi.m_specializationValues.size() > 0);

                        if (!effectFnLocalCaptures.count(ordinalInCaptureStruct))
                        {
                            fprintf(stderr, "[ERROR] IC effect specialization must specialize on a IC state constant (a variable defined in IC body), not a fresh value!\n");
                            abort();
                        }

                        esi.m_valueInBodyFn = effectFnLocalCaptures[ordinalInCaptureStruct];
                        if (!llvm_value_has_type<bool>(esi.m_specializationValues[0]))
                        {
                            ReleaseAssert(esi.m_valueInBodyFn->getType() == esi.m_specializationValues[0]->getType());
                        }
                        else
                        {
                            ReleaseAssert(llvm_value_has_type<uint8_t>(esi.m_valueInBodyFn));
                            // For boolean, the only specialization that makes sense is SpecializeFullCoverage(false, true)
                            //
                            if (esi.m_specializationValues.size() != 2)
                            {
                                fprintf(stderr, "[ERROR] If you specialize a boolean in IC effect, the only sensible specialization is to specialize both false and true!\n");
                                abort();
                            }
                            if (!esi.m_isFullCoverage)
                            {
                                fprintf(stderr, "[ERROR] If you specialize a boolean in IC effect, the only sensible specialization is to specialize full coverage!\n");
                                abort();
                            }
                        }
                        allEffectSpecializationInfo.push_back(esi);
                    }

                    if (demangledCxxSymbolName.find(" DeegenImpl_MakeIC_SpecifyIcCaptureValueRange<") != std::string::npos)
                    {
                        CaptureValueRangeInfo cvi;
                        ReleaseAssert(ci->arg_size() == 3);
                        cvi.m_origin = ci;
                        ReleaseAssert(llvm_value_has_type<int64_t>(ci->getArgOperand(1)));
                        if (!isa<ConstantInt>(ci->getArgOperand(1)))
                        {
                            fprintf(stderr, "[ERROR] Range value (lower bound) passed to SpecifyIcCaptureValueRange is not a constant!\n");
                            abort();
                        }
                        cvi.m_lbInclusive = GetValueOfLLVMConstantInt<int64_t>(ci->getArgOperand(1));

                        ReleaseAssert(llvm_value_has_type<int64_t>(ci->getArgOperand(2)));
                        if (!isa<ConstantInt>(ci->getArgOperand(2)))
                        {
                            fprintf(stderr, "[ERROR] Range value (upper bound) passed to SpecifyIcCaptureValueRange is not a constant!\n");
                            abort();
                        }
                        cvi.m_ubInclusive = GetValueOfLLVMConstantInt<int64_t>(ci->getArgOperand(2));

                        if (cvi.m_lbInclusive > cvi.m_ubInclusive)
                        {
                            fprintf(stderr, "[ERROR] Range value passed to SpecifyIcCaptureValueRange is an empty interval!\n");
                            abort();
                        }

                        Value* capturedValueAddr = ci->getArgOperand(0);
                        ReleaseAssert(llvm_value_has_type<void*>(capturedValueAddr));
                        size_t ordinalInCaptureStruct = getOrdinalInCaptureStruct(capturedValueAddr);
                        cvi.m_ordInCaptureStruct = ordinalInCaptureStruct;

                        if (!effectFnLocalCaptures.count(ordinalInCaptureStruct))
                        {
                            fprintf(stderr, "[ERROR] SpecifyIcCaptureValueRange must specify on a IC state constant (a variable defined in IC body), not a fresh value!\n");
                            abort();
                        }

                        cvi.m_valueInBodyFn = effectFnLocalCaptures[ordinalInCaptureStruct];

                        Type* captureTy = cvi.m_valueInBodyFn->getType();
                        if (captureTy->isIntegerTy() && captureTy->getIntegerBitWidth() < 32)
                        {
                            fprintf(stderr, "[ERROR] No need to use SpecifyIcCaptureValueRange on values < 32 bit!\n");
                            abort();
                        }

                        if (captureTy->isStructTy() && dataLayout.getStructLayout(cast<StructType>(captureTy))->getSizeInBytes() < 4)
                        {
                            fprintf(stderr, "[ERROR] No need to use SpecifyIcCaptureValueRange on values < 32 bit!\n");
                            abort();
                        }

                        allCaptureValueRangeInfo.push_back(cvi);
                    }
                }
            }
        }
    }

    {
        std::unordered_set<size_t> checkUnique;
        for (auto& it : allEffectSpecializationInfo)
        {
            if (checkUnique.count(it.m_ordInCaptureStruct))
            {
                fprintf(stderr, "[ERROR] Cannot use IC effect specialization API on the same capture twice!\n");
                abort();
            }
            checkUnique.insert(it.m_ordInCaptureStruct);
        }
    }

    {
        std::unordered_set<size_t> checkUnique;
        for (auto& it : allCaptureValueRangeInfo)
        {
            if (checkUnique.count(it.m_ordInCaptureStruct))
            {
                fprintf(stderr, "[ERROR] Cannot use SpecifyIcCaptureValueRange API on the same capture twice!\n");
                abort();
            }
            checkUnique.insert(it.m_ordInCaptureStruct);
        }
    }

    for (auto& captureIter : effectFnLocalCaptures)
    {
        size_t ord = captureIter.first;
        Value* valueInBodyFn = captureIter.second;

        bool requiresRangeAnnotation;
        Type* captureTy = valueInBodyFn->getType();
        if (!captureTy->isIntOrPtrTy())
        {
            if (!captureTy->isStructTy())
            {
                fprintf(stderr, "[ERROR] Non-integer/pointer/word-sized struct type local capture in IC is currently unsupported!\n");
                abort();
            }
            StructType* sty = cast<StructType>(captureTy);
            size_t stySize = dataLayout.getStructLayout(sty)->getSizeInBytes();
            if (stySize > 8)
            {
                fprintf(stderr, "[ERROR] Non word-sized struct type local capture in IC is currently unsupported!\n");
                abort();
            }

            requiresRangeAnnotation = (stySize >= 4);

            bool isSingletonType = false;
            while (true)
            {
                if (sty->getNumElements() != 1)
                {
                    break;
                }
                Type* elementTy = sty->getElementType(0);
                if (!elementTy->isStructTy())
                {
                    if (!elementTy->isIntOrPtrTy())
                    {
                        fprintf(stderr, "[ERROR] Non-integer/pointer/word-sized struct type local capture in IC is currently unsupported!\n");
                        abort();
                    }
                    size_t bitWidth;
                    if (elementTy->isPointerTy())
                    {
                        bitWidth = 64;
                    }
                    else
                    {
                        ReleaseAssert(elementTy->isIntegerTy());
                        bitWidth = elementTy->getIntegerBitWidth();
                    }
                    ReleaseAssert(stySize * 8 == bitWidth);
                    isSingletonType = true;
                    break;
                }
                else
                {
                    sty = cast<StructType>(elementTy);
                }
            }

            if (!isSingletonType)
            {
                fprintf(stderr, "[WARNING] You captured a complex struct type in IC. You likely will get terrible code!\n");
            }
        }
        else if (captureTy->isPointerTy())
        {
            requiresRangeAnnotation = true;
        }
        else
        {
            ReleaseAssert(captureTy->isIntegerTy());
            requiresRangeAnnotation = (captureTy->getIntegerBitWidth() >= 32);
        }

        if (requiresRangeAnnotation)
        {
            bool found = false;
            for (auto& it : allEffectSpecializationInfo)
            {
                if (it.m_ordInCaptureStruct == ord && it.m_isFullCoverage)
                {
                    found = true;
                    break;
                }
            }
            for (auto& it : allCaptureValueRangeInfo)
            {
                if (it.m_ordInCaptureStruct == ord)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                fprintf(stderr, "[ERROR] All >=32 bit integral IC captures must have their value range explicitly specified!\n");
                abort();
            }
        }
    }

    for (auto& it : allEffectSpecializationInfo)
    {
        CallInst* ci = it.m_origin;
        ReleaseAssert(ci->use_empty());
        ci->eraseFromParent();
        it.m_origin = nullptr;
    }

    for (auto& it : allCaptureValueRangeInfo)
    {
        CallInst* ci = it.m_origin;
        ReleaseAssert(ci->use_empty());
        ci->eraseFromParent();
        it.m_origin = nullptr;
    }

    // If the effect specialization has full coverage (i.e., the value of the capture is guaranteed to be among
    // the specialization list), then we do not need to store the value into the IC state.
    //
    // Otherwise, since the value needs to be stored into the IC state for the generic fallback case (i.e., not in
    // the specialization list) anyway, we will simply unconditionally store it into the IC state (which is also
    // faster than doing a branch check and conditionally store it, since it is only a simple store instruction).
    //
    // The IC state decoder logic will also unconditionally decode the state. However, in the IC effect wrapper,
    // if the value is specialized to a constant, we will manually do a constant store afterwards. This is sufficient
    // to let LLVM know that the previous decoding load is a dead load and optimize it out.
    //
    for (auto& it : allEffectSpecializationInfo)
    {
        ReleaseAssert(effectFnLocalCaptures.count(it.m_ordInCaptureStruct));
        if (it.m_isFullCoverage)
        {
            effectFnLocalCaptures.erase(effectFnLocalCaptures.find(it.m_ordInCaptureStruct));
        }
    }

    size_t numSpecializations = 1;
    for (auto& it : allEffectSpecializationInfo)
    {
        size_t cnt = it.m_specializationValues.size() + (it.m_isFullCoverage ? 0 : 1);
        numSpecializations *= cnt;
    }
    // Just a sanity check, the limit is kind of arbitrary, but having 50 specializations seems crazy enough already..
    //
    if (numSpecializations > 50)
    {
        fprintf(stderr, "[ERROR] IC effect has too many (%u) specializations!\n", static_cast<unsigned int>(numSpecializations));
        abort();
    }

    // Figure out the function prototype for the effect wrappers.
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

    // Create the function prototype for the decoder function
    // Future lowering logic will lower this call to concrete logic
    //
    Function* decoderFn = nullptr;
    {
        std::vector<Type*> decoderFnArgTys;
        // First arg: icState
        //
        decoderFnArgTys.push_back(llvm_type_of<void*>(ctx));
        // Followed by a void* for each local capture: the destination address to fill
        //
        for (auto& it : effectFnLocalCaptures)
        {
            decoderFnArgTys.push_back(llvm_type_of<void*>(ctx));
            std::ignore = it;
        }
        FunctionType* decoderFnTy = FunctionType::get(llvm_type_of<void>(ctx) /*result*/, decoderFnArgTys, false /*isVarArg*/);
        std::string decoderFnName = GetFirstAvailableFunctionNameWithPrefix(module, x_decodeICStateToEffectLambdaCaptureFunctionPrefix);
        decoderFn = Function::Create(decoderFnTy, GlobalValue::ExternalLinkage, decoderFnName, module);
        ReleaseAssert(decoderFn->getName().str() == decoderFnName);
        decoderFn->addFnAttr(Attribute::AttrKind::NoUnwind);
    }
    ReleaseAssert(decoderFn != nullptr);

    // Create the IC effect wrapper function for each specialization
    // (however, note that they all share the same IC state decoder)
    //
    std::vector<Function*> allEffectSpecializationFns;
    for (size_t specializationOrd = 0; specializationOrd < numSpecializations; specializationOrd++)
    {
        // A vector of the same length as 'allEffectSpecializationInfo'
        // If a value is nullptr, it means the specialization for this capture is the fallback generic case
        //
        std::vector<Constant*> specializationValue;
        {
            // DEVNOTE: this mapping computation from ordinal to specialization configuration below
            // must agree with the computation in CreateEffectOrdinalGetterFn
            //
            size_t tmp = specializationOrd;
            for (auto& it : allEffectSpecializationInfo)
            {
                size_t cnt = it.m_specializationValues.size() + (it.m_isFullCoverage ? 0 : 1);
                size_t option = tmp % cnt;
                tmp /= cnt;
                if (option < it.m_specializationValues.size())
                {
                    specializationValue.push_back(it.m_specializationValues[option]);
                }
                else
                {
                    ReleaseAssert(!it.m_isFullCoverage);
                    specializationValue.push_back(nullptr);
                }
            }
        }

        std::string effectWrapperFnName = GetFirstAvailableFunctionNameWithPrefix(module, x_icEffectClosureWrapperFunctionPrefix);
        Function* effectWrapperFn = Function::Create(effectWrapperFnTy, GlobalValue::InternalLinkage, effectWrapperFnName, module);
        ReleaseAssert(effectWrapperFn->getName().str() == effectWrapperFnName);
        effectWrapperFn->addFnAttr(Attribute::AttrKind::NoUnwind);
        CopyFunctionAttributes(effectWrapperFn, lambda);

        // Create the body of the effect wrapper
        // Basically what we need to do is to create the closure capture expected by the effect lambda, then call the effect lambda
        //
        BasicBlock* entryBlock = BasicBlock::Create(ctx, "", effectWrapperFn);
        AllocaInst* ai = new AllocaInst(captureST, 0 /*addrSpace*/, "", entryBlock);

        // Call the decode function placeholder to populate the IC state into the capture
        //
        std::vector<Value*> decoderFnArgs;
        decoderFnArgs.push_back(effectWrapperFn->getArg(0 /*icState*/));
        for (auto& it : effectFnLocalCaptures)
        {
            size_t ordInCapture = it.first;
            GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(
                captureST, ai,
                {
                    CreateLLVMConstantInt<uint64_t>(ctx, 0),
                    CreateLLVMConstantInt<uint32_t>(ctx, SafeIntegerCast<uint32_t>(ordInCapture))
                },
                "", entryBlock);
            decoderFnArgs.push_back(gep);
        }
        CallInst::Create(decoderFn, decoderFnArgs, "", entryBlock);

        // Now, populate every value from the main function into the lambda capture
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

        // Finally, populate the specialized constant into the capture
        // This is sufficient to let LLVM know that the previous loads and stores in decoderFn are dead and optimize them out
        //
        ReleaseAssert(specializationValue.size() == allEffectSpecializationInfo.size());
        for (size_t i = 0; i < specializationValue.size(); i++)
        {
            if (specializationValue[i] == nullptr)
            {
                // This capture is not specialized in this specialization, decoderFn has filled the correct value for us.
                //
                continue;
            }
            GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(
                captureST, ai,
                {
                    CreateLLVMConstantInt<uint64_t>(ctx, 0),
                    CreateLLVMConstantInt<uint32_t>(ctx, static_cast<uint32_t>(allEffectSpecializationInfo[i].m_ordInCaptureStruct))
                },
                "", entryBlock);

            Value* valToStore = specializationValue[i];
            if (llvm_value_has_type<bool>(specializationValue[i]))
            {
                valToStore = new ZExtInst(valToStore, llvm_type_of<uint8_t>(ctx), "", entryBlock);
            }
            new StoreInst(valToStore, gep, entryBlock);
        }

        // At this stage we have the capture expected by the effect lambda, so we can call it now
        //
        ReleaseAssert(lambda->arg_size() == 1);
        ReleaseAssert(llvm_value_has_type<void*>(lambda->getArg(0)));
        ReleaseAssert(lambda->getReturnType() == effectWrapperFn->getReturnType());
        CallInst* ci = CallInst::Create(lambda, { ai }, "", entryBlock);
        if (llvm_value_has_type<void>(ci))
        {
            ReturnInst::Create(ctx, nullptr /*returnVoid*/, entryBlock);
        }
        else
        {
            ReturnInst::Create(ctx, ci, entryBlock);
        }

        ValidateLLVMFunction(effectWrapperFn);

        allEffectSpecializationFns.push_back(effectWrapperFn);
    }

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
    annotationFnArgs.push_back(CreateLLVMConstantInt<uint64_t>(ctx, allEffectSpecializationInfo.size()));
    for (auto& it : allEffectSpecializationInfo)
    {
        annotationFnArgs.push_back(CreateLLVMConstantInt<bool>(ctx, it.m_isFullCoverage));
        annotationFnArgs.push_back(it.m_valueInBodyFn);
        annotationFnArgs.push_back(CreateLLVMConstantInt<uint64_t>(ctx, it.m_specializationValues.size()));
        for (Constant* cst : it.m_specializationValues)
        {
            annotationFnArgs.push_back(cst);
        }
    }
    annotationFnArgs.push_back(CreateLLVMConstantInt<uint64_t>(ctx, allEffectSpecializationFns.size()));
    for (Function* effectWrapperFn : allEffectSpecializationFns)
    {
        annotationFnArgs.push_back(effectWrapperFn);
    }
    annotationFnArgs.push_back(decoderFn);
    annotationFnArgs.push_back(encoderFn);
    annotationFnArgs.push_back(CreateLLVMConstantInt<uint64_t>(ctx, effectFnLocalCaptures.size()) /*numValuesInIcState*/);
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
    for (auto& it : capturedValuesInMainFn)
    {
        res.m_mainFunctionCapturedValues.push_back(it.second);
    }
    return res;
}

static llvm::Function* CreateGetBytecodePtrPlaceholderFnDeclaration(llvm::Module* module)
{
    using namespace llvm;
    std::string fnName = x_get_bytecode_ptr_placeholder_fn_name;
    Function* f = module->getFunction(fnName);
    if (f != nullptr)
    {
        ReleaseAssert(f->empty() && f->arg_size() == 0 && llvm_type_has_type<void*>(f->getReturnType()));
        return f;
    }
    ReleaseAssert(module->getNamedValue(fnName) == nullptr);
    FunctionType* fty = FunctionType::get(llvm_type_of<void*>(module->getContext()), { }, false /*isVarArg*/);
    f = Function::Create(fty, GlobalValue::ExternalLinkage, fnName, module);
    ReleaseAssert(f->getName() == fnName);
    return f;
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
    Function* lambdaFunc = DeegenAnalyzeLambdaCapturePass::GetLambdaFunctionFromPtrPtr(lambdaPtrPtr);
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
    bool shouldFuseIcIntoInterpreterOpcode = false;
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
            else if (symName.find("DeegenImpl_MakeIC_SetShouldFuseICIntoInterpreterOpcode(") != std::string::npos)
            {
                if (shouldFuseIcIntoInterpreterOpcode)
                {
                    fprintf(stderr, "[ERROR] FuseICIntoInterpreterOpcode() inline cache API should not be called multiple times!\n");
                    abort();
                }
                shouldFuseIcIntoInterpreterOpcode = true;
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
    // Unfortunately this wrapper alone is not enough, because we must additionally
    // make sure that the icKey is passed to the function (even if the user logic does not use it,
    // we need to use it to generate the IC entry). This is why the function name right now has an additional "_tmp"
    //
    RewriteClosureToFunctionCall::Result rr = RewriteClosureToFunctionCall::Run(lambdaFunc, closureAlloca, wrapperFnName + "_tmp");

    Function* closureWrapperImpl = rr.m_newFunc;
    rr.m_newFunc = nullptr;
    Type* resType = origin->getType();
    ReleaseAssert(resType == lambdaFunc->getReturnType());
    ReleaseAssert(resType == closureWrapperImpl->getReturnType());

    bool isIcKeyAlreadyPassedToBody = false;
    for (Value* val : rr.m_args)
    {
        if (val == icKey)
        {
            isIcKeyAlreadyPassedToBody = true;
        }
    }

    uint32_t bytecodePtrArgumentOrdinal = static_cast<uint32_t>(-1);
    Function* closureWrapper;
    {
        std::vector<Type*> tys;
        for (Value* val : rr.m_args) { tys.push_back(val->getType()); }
        if (!isIcKeyAlreadyPassedToBody)
        {
            // If the IC key hasn't been passed to the body, we need to pass it as it's needed for lowering
            //
            tys.push_back(icKey->getType());
        }
        if (shouldFuseIcIntoInterpreterOpcode)
        {
            // If the IC kind is to be fused into the opcode, we need to pass in the current bytecode ptr
            //
            tys.push_back(llvm_type_of<void*>(ctx));
            bytecodePtrArgumentOrdinal = static_cast<uint32_t>(tys.size()) - 1;
        }
        FunctionType* closureWrapperFty = FunctionType::get(resType, tys, false /*isVarArg*/);
        closureWrapper = Function::Create(closureWrapperFty, GlobalValue::InternalLinkage, wrapperFnName, module);
        ReleaseAssert(closureWrapper->getName() == wrapperFnName);
        CopyFunctionAttributes(closureWrapper, closureWrapperImpl);
        closureWrapper->addFnAttr(Attribute::AttrKind::NoUnwind);
        closureWrapper->setDSOLocal(true);

        BasicBlock* bb = BasicBlock::Create(ctx, "", closureWrapper);
        std::vector<Value*> args;
        for (uint32_t i = 0; i < rr.m_args.size(); i++)
        {
            args.push_back(closureWrapper->getArg(i));
        }
        CallInst* callInst = CallInst::Create(closureWrapperImpl, args, "", bb);
        if (llvm_value_has_type<void>(callInst))
        {
            ReturnInst::Create(ctx, nullptr /*returnVoid*/, bb);
        }
        else
        {
            ReturnInst::Create(ctx, callInst, bb);
        }
        if (closureWrapperImpl->hasFnAttribute(Attribute::AttrKind::NoInline))
        {
            closureWrapperImpl->removeFnAttr(Attribute::AttrKind::NoInline);
            closureWrapper->addFnAttr(Attribute::AttrKind::NoInline);
        }
        closureWrapperImpl->addFnAttr(Attribute::AttrKind::AlwaysInline);
        ValidateLLVMFunction(closureWrapper);
    }

    if (!isIcKeyAlreadyPassedToBody)
    {
        rr.m_args.push_back(icKey);
    }
    if (shouldFuseIcIntoInterpreterOpcode)
    {
        Function* getBytecodePtrFn = CreateGetBytecodePtrPlaceholderFnDeclaration(module);
        CallInst* bytecodePtr = CallInst::Create(getBytecodePtrFn, { }, "", origin /*insertBefore*/);
        rr.m_args.push_back(bytecodePtr);
    }
    ReleaseAssert(closureWrapper->arg_size() == rr.m_args.size());

    std::vector<Type*> closureArgTys;
    for (Value* val : rr.m_args) { closureArgTys.push_back(val->getType()); }
    CallInst* replacement;
    {
        std::vector<Value*> icBodyArgs;
        icBodyArgs.push_back(ic);
        icBodyArgs.push_back(icKey);
        icBodyArgs.push_back(icKeyImpossibleValue);
        icBodyArgs.push_back(closureWrapper);
        icBodyArgs.push_back(CreateLLVMConstantInt<bool>(ctx, shouldFuseIcIntoInterpreterOpcode));
        icBodyArgs.push_back(CreateLLVMConstantInt<uint32_t>(ctx, bytecodePtrArgumentOrdinal));
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

bool WARN_UNUSED AstInlineCache::IsIcPtrGetterFunctionCall(llvm::CallInst* inst)
{
    using namespace llvm;
    Function* fn = inst->getCalledFunction();
    if (fn == nullptr) { return false; }
    return fn->getName().startswith(x_createIcInitPlaceholderFunctionPrefix);
}

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
                        ReleaseAssert(symName.find("DeegenImpl_MakeIC_SetShouldFuseICIntoInterpreterOpcode(") == std::string::npos);
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
    constexpr uint32_t x_icBodyDescriptorNumFixedAttributeArgs = 6;
    ReleaseAssert(origin->arg_size() >= x_icBodyDescriptorNumFixedAttributeArgs);
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
        ReleaseAssert(isa<ConstantInt>(icKeyImpossibleVal));
        r.m_icKeyImpossibleValueMaybeNull = cast<ConstantInt>(icKeyImpossibleVal);
        ReleaseAssert(r.m_icKeyImpossibleValueMaybeNull->getType() == r.m_icKey->getType());
    }

    r.m_bodyFn = dyn_cast<Function>(origin->getArgOperand(3));
    ReleaseAssert(r.m_bodyFn != nullptr);

    ReleaseAssert(llvm_value_has_type<bool>(origin->getArgOperand(4)));
    r.m_shouldFuseICIntoInterpreterOpcode = GetValueOfLLVMConstantInt<bool>(origin->getArgOperand(4));

    ReleaseAssert(llvm_value_has_type<uint32_t>(origin->getArgOperand(5)));
    r.m_bodyFnBytecodePtrArgOrd = GetValueOfLLVMConstantInt<uint32_t>(origin->getArgOperand(5));
    ReleaseAssertIff(!r.m_shouldFuseICIntoInterpreterOpcode, r.m_bodyFnBytecodePtrArgOrd == static_cast<uint32_t>(-1));
    ReleaseAssertImp(r.m_shouldFuseICIntoInterpreterOpcode, r.m_bodyFnBytecodePtrArgOrd < r.m_bodyFn->arg_size());
    ReleaseAssertImp(r.m_shouldFuseICIntoInterpreterOpcode, llvm_value_has_type<void*>(r.m_bodyFn->getArg(r.m_bodyFnBytecodePtrArgOrd)));

    r.m_bodyFnIcPtrArgOrd = static_cast<uint32_t>(-1);
    r.m_bodyFnIcKeyArgOrd = static_cast<uint32_t>(-1);
    for (uint32_t i = x_icBodyDescriptorNumFixedAttributeArgs; i < origin->arg_size(); i++)
    {
        Value* val = origin->getArgOperand(i);
        r.m_bodyFnArgs.push_back(val);
        if (val == ic)
        {
            ReleaseAssert(r.m_bodyFnIcPtrArgOrd == static_cast<uint32_t>(-1));
            r.m_bodyFnIcPtrArgOrd = i - x_icBodyDescriptorNumFixedAttributeArgs;
        }
        if (val == r.m_icKey && r.m_bodyFnIcKeyArgOrd == static_cast<uint32_t>(-1))
        {
            r.m_bodyFnIcKeyArgOrd = i - x_icBodyDescriptorNumFixedAttributeArgs;
        }
    }
    ReleaseAssert(r.m_bodyFnIcPtrArgOrd != static_cast<uint32_t>(-1));
    ReleaseAssert(r.m_bodyFnIcKeyArgOrd != static_cast<uint32_t>(-1));

    // Parse out all information about the effects
    //
    std::vector<CallInst*> allIcApiCallsInMainFn;
    {
        std::unordered_set<User*> uniqueIcUsers;
        for (User* usr : ic->users())
        {
            uniqueIcUsers.insert(usr);
        }
        // Always sort the calls by function name, so that our processing result is deterministic
        // All the function names should be different because we have uniqued them in the preprocessing step
        //
        std::map<std::string /*fnName*/, CallInst*> sortedCIs;
        for (User* usr : uniqueIcUsers)
        {
            ReleaseAssert(isa<CallInst>(usr));
            CallInst* ci = cast<CallInst>(usr);
            Function* callee = ci->getCalledFunction();
            ReleaseAssert(callee != nullptr);
            std::string calleeName = callee->getName().str();
            ReleaseAssert(!sortedCIs.count(calleeName));
            sortedCIs[calleeName] = ci;
        }
        for (auto& it : sortedCIs)
        {
            allIcApiCallsInMainFn.push_back(it.second);
        }
    }

    std::vector<Instruction*> instructionsToRemove;
    for (CallInst* ci : allIcApiCallsInMainFn)
    {
        ReleaseAssert(ci->arg_size() > 0 && ci->getArgOperand(0) == ic);
        if (ci == origin)
        {
            continue;
        }
        instructionsToRemove.push_back(ci);
        ReleaseAssert(ci->getCalledFunction() != nullptr && ci->getCalledFunction()->getName().startswith(x_createIcRegisterEffectPlaceholderFunctionPrefix));
        AstInlineCache::Effect e;
        ReleaseAssert(ci->arg_size() >= 2);
        for (uint32_t i = 2; i < ci->arg_size(); i++)
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

        ReleaseAssert(target->arg_size() >= 9);
        e.m_icPtr = target->getArgOperand(0);
        ReleaseAssert(llvm_value_has_type<void*>(e.m_icPtr));
        ReleaseAssert(isa<Argument>(e.m_icPtr));
        ReleaseAssert(cast<Argument>(e.m_icPtr)->getArgNo() == r.m_bodyFnIcPtrArgOrd);

        e.m_effectFnBodyCapture = dyn_cast<AllocaInst>(target->getArgOperand(1));
        ReleaseAssert(e.m_effectFnBodyCapture != nullptr);

        e.m_effectFnBody = dyn_cast<Function>(target->getArgOperand(2));
        ReleaseAssert(e.m_effectFnBody != nullptr);

        size_t numSpecializationInfo = GetValueOfLLVMConstantInt<uint64_t>(target->getArgOperand(3));
        uint32_t curArgOrd = 4;
        size_t expectedNumEffectFns = 1;
        for (size_t k = 0; k < numSpecializationInfo; k++)
        {
            ReleaseAssert(target->arg_size() > curArgOrd + 2);
            AstInlineCache::Effect::SpecializationInfo spInfo;
            spInfo.m_isFullCoverage = GetValueOfLLVMConstantInt<bool>(target->getArgOperand(curArgOrd));
            curArgOrd++;
            spInfo.m_valueInBodyFn = target->getArgOperand(curArgOrd);
            curArgOrd++;
            size_t numSpecializations = GetValueOfLLVMConstantInt<uint64_t>(target->getArgOperand(curArgOrd));
            curArgOrd++;
            ReleaseAssert(target->arg_size() >= curArgOrd + numSpecializations);
            for (size_t i = 0; i < numSpecializations; i++)
            {
                Value* val = target->getArgOperand(curArgOrd);
                curArgOrd++;
                ReleaseAssert(isa<Constant>(val));
                spInfo.m_specializations.push_back(cast<Constant>(val));
            }

            ReleaseAssert(spInfo.m_specializations.size() > 0);
            for (Constant* cst : spInfo.m_specializations)
            {
                if (!llvm_value_has_type<bool>(cst))
                {
                    ReleaseAssert(cst->getType() == spInfo.m_valueInBodyFn->getType());
                }
                else
                {
                    ReleaseAssert(llvm_value_has_type<uint8_t>(spInfo.m_valueInBodyFn));
                }
            }
            expectedNumEffectFns *= spInfo.m_specializations.size() + (spInfo.m_isFullCoverage ? 0 : 1);
            e.m_specializations.push_back(spInfo);
        }

        ReleaseAssert(curArgOrd < target->arg_size());
        size_t numEffectFns = GetValueOfLLVMConstantInt<uint64_t>(target->getArgOperand(curArgOrd));
        curArgOrd++;
        ReleaseAssert(curArgOrd + numEffectFns <= target->arg_size());
        ReleaseAssert(numEffectFns == expectedNumEffectFns);
        ReleaseAssert(numEffectFns > 0);
        for (size_t i = 0; i < numEffectFns; i++)
        {
            Value* val = target->getArgOperand(curArgOrd);
            curArgOrd++;
            ReleaseAssert(isa<Function>(val));
            Function* effectFn = cast<Function>(val);
            e.m_effectFnMain.push_back(effectFn);
        }

        ReleaseAssert(curArgOrd + 2 < target->arg_size());
        e.m_icStateDecoder = dyn_cast<Function>(target->getArgOperand(curArgOrd));
        curArgOrd++;
        ReleaseAssert(e.m_icStateDecoder != nullptr);
        ReleaseAssert(e.m_icStateDecoder->getName().startswith(x_decodeICStateToEffectLambdaCaptureFunctionPrefix));

        e.m_icStateEncoder = dyn_cast<Function>(target->getArgOperand(curArgOrd));
        curArgOrd++;
        ReleaseAssert(e.m_icStateEncoder != nullptr);
        ReleaseAssert(e.m_icStateEncoder->getName().startswith(x_encodeICStateFunctionPrefix));

        Value* numIcStateCapturesV = target->getArgOperand(curArgOrd);
        curArgOrd++;
        ReleaseAssert(isa<ConstantInt>(numIcStateCapturesV));
        ReleaseAssert(llvm_value_has_type<uint64_t>(numIcStateCapturesV));
        uint64_t numIcStateCaptures = GetValueOfLLVMConstantInt<uint64_t>(numIcStateCapturesV);

        ReleaseAssert(curArgOrd + numIcStateCaptures == target->arg_size());
        for (uint32_t i = 0; i < numIcStateCaptures; i++)
        {
            Value* icStateCapture = target->getArgOperand(curArgOrd);
            curArgOrd++;
            e.m_icStateVals.push_back(icStateCapture);
        }

        ReleaseAssert(curArgOrd == target->arg_size());

        ReleaseAssert(e.m_icStateEncoder->arg_size() == 1 + numIcStateCaptures);
        ReleaseAssert(e.m_icStateDecoder->arg_size() == 1 + numIcStateCaptures);
        for (uint32_t i = 0; i < numIcStateCaptures; i++)
        {
            ReleaseAssert(e.m_icStateEncoder->getArg(1 + i)->getType() == e.m_icStateVals[i]->getType());
            ReleaseAssert(llvm_value_has_type<void*>(e.m_icStateDecoder->getArg(1 + i)));
        }

        for (Function* effectFn : e.m_effectFnMain)
        {
            ReleaseAssert(effectFn->getReturnType() == r.m_bodyFn->getReturnType());
        }
        ReleaseAssert(e.m_effectFnBody->getReturnType() == r.m_bodyFn->getReturnType());
        ReleaseAssert(e.m_origin->getType() == r.m_bodyFn->getReturnType());
        r.m_effects.push_back(e);
    }

    size_t totalEffectFns = 0;
    for (auto& it : r.m_effects)
    {
        it.m_effectStartOrdinal = totalEffectFns;
        totalEffectFns += it.m_effectFnMain.size();
    }
    ReleaseAssert(totalEffectFns < 250);
    r.m_totalEffectKinds = totalEffectFns;

    for (size_t i = 0; i < r.m_effects.size(); i++)
    {
        r.m_effects[i].m_ordinalInEffectArray = i;
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
                    if (symName.find("DeegenImpl_MakeIC_SetUncacheableForThisExecution(") != std::string::npos)
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

static llvm::Function* WARN_UNUSED CreateEffectOrdinalGetterFn(
    llvm::Module* module,
    llvm::Function* fnToCopyAttributeFrom,
    const std::vector<AstInlineCache::Effect::SpecializationInfo>& spList,
    size_t startOrd)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    std::string fnName = GetFirstAvailableFunctionNameWithPrefix(module, "__deegen_compute_ic_effect_ord_");
    std::vector<Type*> argTys;
    for (const auto& it : spList)
    {
        argTys.push_back(it.m_valueInBodyFn->getType());
    }
    FunctionType* fty = FunctionType::get(llvm_type_of<uint8_t>(ctx) /*result*/, argTys, false /*isVarArg*/);
    Function* fn = Function::Create(fty, GlobalValue::InternalLinkage, fnName, module);
    ReleaseAssert(fn->getName().str() == fnName);
    fn->addFnAttr(Attribute::AttrKind::AlwaysInline);
    fn->addFnAttr(Attribute::AttrKind::NoUnwind);
    CopyFunctionAttributes(fn, fnToCopyAttributeFrom);

    BasicBlock* allocaBlock = BasicBlock::Create(ctx, "", fn);
    BasicBlock* curBlock = BasicBlock::Create(ctx, "", fn);
    Instruction* allocaInsertionPoint = BranchInst::Create(curBlock, allocaBlock);

    std::vector<AllocaInst*> spOrdValues;
    for (uint32_t i = 0; i < spList.size(); i++)
    {
        AllocaInst* spOrd = new AllocaInst(llvm_type_of<uint8_t>(ctx), 0 /*addrSpace*/, "", allocaInsertionPoint);
        spOrdValues.push_back(spOrd);

        ReleaseAssert(curBlock->empty());

        BasicBlock* joinBlock = BasicBlock::Create(ctx, "", fn);
        BasicBlock* defaultBlock = BasicBlock::Create(ctx, "", fn);
        if (spList[i].m_isFullCoverage)
        {
            new UnreachableInst(ctx, defaultBlock);
        }
        else
        {
            new StoreInst(CreateLLVMConstantInt<uint8_t>(ctx, SafeIntegerCast<uint8_t>(spList[i].m_specializations.size())), spOrd, defaultBlock);
            BranchInst::Create(joinBlock, defaultBlock);
        }

        Value* switchValue = fn->getArg(i);
        ReleaseAssert(spList[i].m_specializations.size() > 0);
        if (llvm_value_has_type<bool>(spList[i].m_specializations[0]))
        {
            ReleaseAssert(llvm_value_has_type<uint8_t>(switchValue));
            switchValue = new TruncInst(switchValue, llvm_type_of<bool>(ctx), "", curBlock);
        }
        SwitchInst* si = SwitchInst::Create(switchValue, defaultBlock, static_cast<uint32_t>(spList[i].m_specializations.size()) /*numCasesHint*/, curBlock);
        for (size_t k = 0; k < spList[i].m_specializations.size(); k++)
        {
            BasicBlock* dst = BasicBlock::Create(ctx, "", fn, defaultBlock /*insertBefore*/);
            new StoreInst(CreateLLVMConstantInt<uint8_t>(ctx, SafeIntegerCast<uint8_t>(k)), spOrd, dst);
            BranchInst::Create(joinBlock, dst);

            ReleaseAssert(spList[i].m_specializations[k]->getType() == switchValue->getType());
            ConstantInt* csi = dyn_cast<ConstantInt>(spList[i].m_specializations[k]);
            ReleaseAssert(csi != nullptr);
            si->addCase(csi, dst);
        }

        curBlock = joinBlock;
    }

    std::vector<size_t> spOrdWeights;
    size_t curWeight = 1;
    for (size_t i = 0; i < spList.size(); i++)
    {
        size_t cnt = spList[i].m_specializations.size() + (spList[i].m_isFullCoverage ? 0 : 1);
        spOrdWeights.push_back(curWeight);
        curWeight *= cnt;
    }

    ReleaseAssert(curBlock->empty());
    UnreachableInst* dummy = new UnreachableInst(ctx, curBlock);
    Value* sum = CreateLLVMConstantInt<uint8_t>(ctx, SafeIntegerCast<uint8_t>(startOrd));

    ReleaseAssert(spOrdWeights.size() == spList.size());
    ReleaseAssert(spOrdValues.size() == spList.size());
    for (size_t i = 0; i < spList.size(); i++)
    {
        Value* val = new LoadInst(llvm_type_of<uint8_t>(ctx), spOrdValues[i], "", dummy);
        uint8_t weight = SafeIntegerCast<uint8_t>(spOrdWeights[i]);
        Value* thisTerm = CreateUnsignedMulNoOverflow(val, CreateLLVMConstantInt<uint8_t>(ctx, weight), dummy);
        sum = CreateUnsignedAddNoOverflow(sum, thisTerm, dummy);
    }
    ReturnInst::Create(ctx, sum, dummy);
    dummy->eraseFromParent();

    ValidateLLVMFunction(fn);
    return fn;
}

void LowerInterpreterGetBytecodePtrInternalAPI(InterpreterBytecodeImplCreator* ifi, llvm::Function* func)
{
    using namespace llvm;
    std::vector<CallInst*> allUses;
    for (BasicBlock& bb : *func)
    {
        for (Instruction& inst : bb)
        {
            CallInst* ci = dyn_cast<CallInst>(&inst);
            if (ci != nullptr && ci->getCalledFunction() != nullptr && ci->getCalledFunction()->getName() == x_get_bytecode_ptr_placeholder_fn_name)
            {
                allUses.push_back(ci);
            }
        }
    }
    for (CallInst* ci : allUses)
    {
        ReleaseAssert(llvm_value_has_type<void*>(ci));
        ci->replaceAllUsesWith(ifi->GetCurBytecode());
        ci->eraseFromParent();
    }
}

void AstInlineCache::DoLoweringForInterpreter()
{
    using namespace llvm;
    LLVMContext& ctx = m_origin->getContext();
    ReleaseAssert(m_origin->getParent() != nullptr);
    Function* mainFn = m_origin->getParent()->getParent();
    ReleaseAssert(mainFn != nullptr);
    Module* module = mainFn->getParent();
    ReleaseAssert(module != nullptr);

    // The metadata header:
    //     KeyType icValue
    //     uint8_t icEffectKind
    //
    // icEffectKind:
    //     1. When no impossible value is given, an invalid icEffectKind indicates no IC entry
    //     2. When there is only one possible effect kind, and the impossible value is given (so effect kind is not
    //     used to distinguish a nonexistent IC), we can omit it from the IC
    //
    // The main function logic basically looks like the following:
    // if (key == icValue) {
    //     switch (icEffectKind) {
    //     ...
    //     }
    // } else {
    //     RunIcBody(...)
    // }
    //

    // Create the 'isCacheable' variable, which should default to true, but can be set to false by 'SetUncacheable() API call
    //
    AllocaInst* isCacheableAlloca = new AllocaInst(llvm_type_of<uint8_t>(ctx), 0 /*addrSpace*/, "", &m_bodyFn->getEntryBlock().front() /*insertBefore*/);

    {
        // We should insert our init code after the first alloca, so that allocas come first and LLVM is happy.
        // Find first non-alloca instruction in the entry block.
        //
        Instruction* insertionPt = nullptr;
        for (Instruction& inst : m_bodyFn->getEntryBlock())
        {
            if (!isa<AllocaInst>(&inst))
            {
                insertionPt = &inst;
                break;
            }
        }
        ReleaseAssert(insertionPt != nullptr);
        new StoreInst(CreateLLVMConstantInt<uint8_t>(ctx, 1), isCacheableAlloca, insertionPt);
    }

    std::unique_ptr<BytecodeMetadataStruct> ms = std::make_unique<BytecodeMetadataStruct>();

    Value* icPtr = m_icPtrOrigin;
    ReleaseAssert(m_icPtrOrigin->getFunction() == mainFn);
    bool hasImpossibleValue = (m_icKeyImpossibleValueMaybeNull != nullptr);
    ReleaseAssert(m_icKey->getType()->isIntegerTy());
    size_t icKeyBytes = m_icKey->getType()->getIntegerBitWidth() / 8;

    ReleaseAssert(m_totalEffectKinds > 0 && m_totalEffectKinds < 254);

    // Note that if 'm_shouldFuseICIntoInterpreterOpcode' is true, we do not need effectKind to determine the IC effect to call,
    // but still we should not elide it, because C++ code needs this value in order to interpret the metadata struct
    //
    bool canElideEffectKind = (m_totalEffectKinds == 1) && hasImpossibleValue;

    // If 'm_shouldFuseICIntoInterpreterOpcode' is true but we can also elide the effect kind (i.e., there's no branch to
    // check the effect kind already), lock it down since in this case 'FuseICIntoInterpreterOpcode()' will only harm
    // performance (it does not eliminate any branch, but unnecessarily creates an interpreter function).
    // This also reduces some edge case discussion.
    //
    if (canElideEffectKind && m_shouldFuseICIntoInterpreterOpcode)
    {
        fprintf(stderr, "[ERROR] It is not beneficial to specify FuseICIntoInterpreterOpcode() if there is only one possible IC effect!\n");
        abort();
    }

    BytecodeMetadataElement* ms_cachedIcVal = ms->AddElement(1 /*alignment*/, icKeyBytes);
    if (hasImpossibleValue)
    {
        ms_cachedIcVal->SetInitValueCI(m_icKeyImpossibleValueMaybeNull);
    }

    // Load the IC entry header and compare 'key == cachedIcKey'
    //
    Value* icValEqual;
    {
        Value* addr = ms_cachedIcVal->EmitGetAddress(module, icPtr, m_origin);
        Value* cachedIcVal = new LoadInst(m_icKey->getType(), addr, "", false /*isVolatile*/, Align(1), m_origin);
        icValEqual = new ICmpInst(m_origin, CmpInst::Predicate::ICMP_EQ, cachedIcVal, m_icKey);
    }

    BytecodeMetadataElement* ms_effectKind = nullptr;
    std::vector<BytecodeMetadataStruct*> ms_effects;
    if (!canElideEffectKind)
    {
        BytecodeMetadataTaggedUnion* tu = ms->AddTaggedUnion();
        ms_effectKind = tu->GetTag();
        for (size_t i = 0; i < m_effects.size(); i++)
        {
            auto [caseOrd, caseStruct] = tu->AddNewCase();
            ReleaseAssert(caseOrd == i);
            ms_effects.push_back(caseStruct);
        }
    }
    else
    {
        ReleaseAssert(m_effects.size() == 1);
        ms_effects.push_back(ms->AddStruct());
    }
    ReleaseAssert(ms_effects.size() == m_effects.size());

    Value* icEffectKind = nullptr;
    if (!canElideEffectKind)
    {
        Value* icEffectKindAddr = ms_effectKind->EmitGetAddress(module, icPtr, m_origin);
        icEffectKind = new LoadInst(llvm_type_of<uint8_t>(ctx), icEffectKindAddr, "", false /*isVolatile*/, Align(1), m_origin);
    }

    // If the IC effect should be fused into the interpreter opcode, we need to later further specialize this IC for different effect kinds.
    // Specifically, for the initial implementation, we know the IC doesn't exist yet, so there is no need to compare.
    // Therefore, we pipe 'icValEqual' through a dummy function which implementation will be populated later to adapt the behavior.
    //
    if (m_shouldFuseICIntoInterpreterOpcode)
    {
        ReleaseAssert(module->getNamedValue(x_adapt_ic_hit_check_behavior_placeholder_fn) == nullptr);
        FunctionType* fty = FunctionType::get(llvm_type_of<bool>(ctx), { llvm_type_of<bool>(ctx) }, false /*isVarArg*/);
        Function* dummyFn = Function::Create(fty, GlobalValue::ExternalLinkage, x_adapt_ic_hit_check_behavior_placeholder_fn, module);
        ReleaseAssert(dummyFn->getName() == x_adapt_ic_hit_check_behavior_placeholder_fn);
        icValEqual = CallInst::Create(dummyFn, { icValEqual }, "", m_origin);
    }

    // Generate the if-then-else branch, the then-branch is the fast path (IC hit), the else-branch is the slow path (IC miss)
    //
    Function* expectIntrin = Intrinsic::getDeclaration(module, Intrinsic::expect, { Type::getInt1Ty(ctx) });
    icValEqual = CallInst::Create(expectIntrin, { icValEqual, CreateLLVMConstantInt<bool>(ctx, true) }, "", m_origin);

    Instruction* thenBlockTerminator = nullptr;
    Instruction* elseBlockTerminator = nullptr;
    SplitBlockAndInsertIfThenElse(icValEqual, m_origin /*splitBefore*/, &thenBlockTerminator /*out*/, &elseBlockTerminator /*out*/);
    ReleaseAssert(thenBlockTerminator != nullptr && elseBlockTerminator != nullptr);
    BasicBlock* icCacheHitBlock = thenBlockTerminator->getParent();
    BasicBlock* callIcBodyBlock = elseBlockTerminator->getParent();
    BasicBlock* joinBlock = m_origin->getParent();

    std::vector<BasicBlock*> effectCaseBBList;
    if (!canElideEffectKind)
    {
        // For the then-block, we need to create a switch case that calls each effect based on the effect kind
        //
        thenBlockTerminator->eraseFromParent();
        thenBlockTerminator = nullptr;

        // If no impossible value is specified, the default switch case clause should point to the slowpath since an invalid
        // effect kind indicates that the IC entry does not exist.
        // Otherwise, since the IC key will be set to the impossible value to indicate that the IC entry doesn't exist,
        // when we reach here, the icEffectKind must indicate a valid IC effect kind, so the default clause should be unreachable.
        //
        BasicBlock* defaultClause;
        if (hasImpossibleValue)
        {
            defaultClause = BasicBlock::Create(ctx, "", mainFn);
            new UnreachableInst(ctx, defaultClause);
        }
        else
        {
            defaultClause = callIcBodyBlock;
        }

        // Similar to above, if the IC effect should be fused into the interpreter opcode, we need to later further specialize this IC
        // for different effect kinds. Specifically, in the specialized implementation we already know the IC effect kind, so there is
        // no need to compare.
        // Therefore, we pipe 'icEffectKind' through a dummy function which implementation will be populated later to adapt the behavior.
        //
        if (m_shouldFuseICIntoInterpreterOpcode)
        {
            ReleaseAssert(module->getNamedValue(x_adapt_get_ic_effect_ord_behavior_placeholder_fn) == nullptr);
            FunctionType* fty = FunctionType::get(llvm_type_of<uint8_t>(ctx), { llvm_type_of<uint8_t>(ctx) }, false /*isVarArg*/);
            Function* dummyFn = Function::Create(fty, GlobalValue::ExternalLinkage, x_adapt_get_ic_effect_ord_behavior_placeholder_fn, module);
            ReleaseAssert(dummyFn->getName() == x_adapt_get_ic_effect_ord_behavior_placeholder_fn);
            icEffectKind = CallInst::Create(dummyFn, { icEffectKind }, "", icCacheHitBlock);
        }

        SwitchInst* switchInst = SwitchInst::Create(icEffectKind, defaultClause, static_cast<uint32_t>(m_totalEffectKinds) /*caseToReserve*/, icCacheHitBlock);
        for (size_t i = 0; i < m_totalEffectKinds; i++)
        {
            BasicBlock* bb = BasicBlock::Create(ctx, "", mainFn, joinBlock /*insertBefore*/);
            BranchInst::Create(joinBlock, bb);
            effectCaseBBList.push_back(bb);
            switchInst->addCase(CreateLLVMConstantInt<uint8_t>(ctx, SafeIntegerCast<uint8_t>(i)), bb);
        }
    }
    else
    {
        ReleaseAssert(m_totalEffectKinds == 1);
        effectCaseBBList.push_back(icCacheHitBlock);
    }
    ReleaseAssert(effectCaseBBList.size() == m_totalEffectKinds);

    // All we conceptually want to do is to create a PHI in the join BB that joins the results of all the cases together.
    // However, the result type can be an aggregate type, and it turns out that LLVM generates miserable code for PHI node with aggregate type
    // as for some reason, the optimizer is unable to decompose the aggregate-typed PHI into scalar-valued PHIs.
    // After some more investigation, it turns out that the right thing to do is to simply create an alloca, and let LLVM's SROA pass to
    // decompose the alloca and create decomposed PHI nodes for us.
    //
    AllocaInst* icResultAlloca = new AllocaInst(m_origin->getType(), 0 /*addrSpace*/, "", &mainFn->getEntryBlock().front() /*insertBefore*/);

    // Now, generate logic for each effect kind
    //
    for (Effect& e : m_effects)
    {
        // Generate the logic in the main function for each specialization, which is simply calling the wrapped effect fn
        //
        for (size_t i = 0; i < e.m_effectFnMain.size(); i++)
        {
            size_t effectOrd = e.m_effectStartOrdinal + i;
            ReleaseAssert(effectOrd < effectCaseBBList.size());
            BasicBlock* bb = effectCaseBBList[effectOrd];
            ReleaseAssert(bb->getInstList().size() == 1);
            Instruction* insertBefore = bb->getTerminator();
            std::vector<Value*> args;
            args.push_back(m_icPtrOrigin);
            for (Value* val : e.m_effectFnMainArgs) { args.push_back(val); }
            Value* effectResult = CallInst::Create(e.m_effectFnMain[i], args, "", insertBefore);
            new StoreInst(effectResult, icResultAlloca, insertBefore);
        }

        // Generate the implementation of the IC state encoder and decoder
        // For now, the IC state is simply a packed struct (i.e., ignores alignment requirement) of all the captured members
        // and we encode/decode by generating memcpy to copy the values in/out and let LLVM handle all the optimizations.
        //
        size_t numElementsInIcState = e.m_icStateVals.size();
        std::vector<BytecodeMetadataElement*> ms_icStates;

        {
            // Set up the struct to hold the captured state of this effect
            //
            DataLayout dataLayout(module);
            BytecodeMetadataStruct* bms = ms_effects[e.m_ordinalInEffectArray];
            for (size_t i = 0; i < numElementsInIcState; i++)
            {
                Type* icStateType = e.m_icStateVals[i]->getType();
                // Calling 'getTypeStoreSize' is likely incorrect, because the store size does not consider the tail padding,
                // but the Clang frontend is already implicitly generating memcpy that copies the tail padding.
                //
                size_t typeSize = dataLayout.getTypeAllocSize(icStateType);
                ms_icStates.push_back(bms->AddElement(1 /*alignment*/, typeSize));
            }
            ReleaseAssert(ms_icStates.size() == numElementsInIcState);
        }

        {
            // Generate the implementation of the IC state encoder
            // The prototype of the encoder is 'void* dst' followed by all the values of the IC state.
            //
            Function* target = e.m_icStateEncoder;
            ReleaseAssert(target->arg_size() == 1 + numElementsInIcState);
            ReleaseAssert(target->empty());
            target->setLinkage(GlobalValue::InternalLinkage);
            CopyFunctionAttributes(target, mainFn);
            target->addFnAttr(Attribute::AttrKind::AlwaysInline);
            BasicBlock* bb = BasicBlock::Create(ctx, "", target);

            Value* encoderIcPtr = target->getArg(0);

            std::vector<AllocaInst*> allocas;
            for (size_t i = 0; i < numElementsInIcState; i++)
            {
                allocas.push_back(new AllocaInst(e.m_icStateVals[i]->getType(), 0 /*addrSpace*/, "", bb));
            }

            for (uint32_t i = 0; i < numElementsInIcState; i++)
            {
                Type* icStateType = e.m_icStateVals[i]->getType();
                Value* src = target->getArg(1 + i);
                ReleaseAssert(src->getType() == icStateType);
                new StoreInst(src, allocas[i], bb);
            }

            for (size_t i = 0; i < numElementsInIcState; i++)
            {
                Value* addr = ms_icStates[i]->EmitGetAddress(module, encoderIcPtr, bb);
                EmitLLVMIntrinsicMemcpy(module, addr /*dst*/, allocas[i] /*src*/, CreateLLVMConstantInt<uint64_t>(ctx, ms_icStates[i]->GetSize()), bb);
            }
            ReturnInst::Create(ctx, nullptr /*returnVoid*/, bb);
            ValidateLLVMFunction(target);
        }

        {
            // Generate the implementation of the IC state decoder
            // The prototype of the decoder is 'void* src' followed by a list of void* pointers,
            // which are the destinations to decode each member into.
            //
            Function* target = e.m_icStateDecoder;
            ReleaseAssert(target->arg_size() == 1 + numElementsInIcState);
            ReleaseAssert(target->empty());
            target->setLinkage(GlobalValue::InternalLinkage);
            CopyFunctionAttributes(target, mainFn);
            target->addFnAttr(Attribute::AttrKind::AlwaysInline);
            BasicBlock* bb = BasicBlock::Create(ctx, "", target);

            Value* decoderIcPtr = target->getArg(0);

            for (uint32_t i = 0; i < numElementsInIcState; i++)
            {
                Value* dst = target->getArg(1 + i);
                ReleaseAssert(llvm_value_has_type<void*>(dst));
                Value* addr = ms_icStates[i]->EmitGetAddress(module, decoderIcPtr, bb);
                EmitLLVMIntrinsicMemcpy(module, dst, addr /*src*/, CreateLLVMConstantInt<uint64_t>(ctx, ms_icStates[i]->GetSize()), bb);
            }
            ReturnInst::Create(ctx, nullptr /*returnVoid*/, bb);
            ValidateLLVMFunction(target);
        }

        {
            // Generate the logic in the IC body that creates the IC state
            // Basically we need to create the IC state, then invoke the IC effect lambda
            //
            Value* bodyIcPtr = e.m_icPtr;
            // For the interpreter, since by design we only have space for one IC entry,
            // currently the policy is that the new IC entry will always overwrite the old IC entry.
            //
            // Check if the IC has been marked uncacheable by SetUncacheable() API. If that is the case, we must not create the IC.
            //
            Instruction* updateIcLogicInsertionPt = nullptr;
            {
                Value* isCacheableU8 = new LoadInst(llvm_type_of<uint8_t>(ctx), isCacheableAlloca, "", e.m_origin /*insertBefore*/);
                Value* isCacheable = new TruncInst(isCacheableU8,llvm_type_of<bool>(ctx), "", e.m_origin /*insertBefore*/);
                updateIcLogicInsertionPt = SplitBlockAndInsertIfThen(isCacheable, e.m_origin /*splitBefore*/, false /*isUnreachable*/);
                ReleaseAssert(isa<BranchInst>(updateIcLogicInsertionPt));
                ReleaseAssert(!cast<BranchInst>(updateIcLogicInsertionPt)->isConditional() &&
                              cast<BranchInst>(updateIcLogicInsertionPt)->getSuccessor(0) == e.m_origin->getParent());
            }
            ReleaseAssert(updateIcLogicInsertionPt != nullptr);
            // Write the IC key
            //
            {
                ReleaseAssert(m_bodyFnIcKeyArgOrd < m_bodyFn->arg_size());
                Value* icKeyToWrite = m_bodyFn->getArg(m_bodyFnIcKeyArgOrd);
                ReleaseAssert(icKeyToWrite->getType() == m_icKey->getType());
                Value* addr = ms_cachedIcVal->EmitGetAddress(module, bodyIcPtr, updateIcLogicInsertionPt);
                new StoreInst(icKeyToWrite, addr /*dst*/, false /*isVolatile*/, Align(1), updateIcLogicInsertionPt);
            }
            // Write the effect ordinal (if it is not elided)
            //
            if (!canElideEffectKind)
            {
                Function* effectOrdinalGetterFn = CreateEffectOrdinalGetterFn(module, m_bodyFn /*functionToCopyAttrFrom*/, e.m_specializations, e.m_effectStartOrdinal);
                std::vector<Value*> args;
                for (auto& it : e.m_specializations)
                {
                    args.push_back(it.m_valueInBodyFn);
                }
                Value* effectKindToWrite = CallInst::Create(effectOrdinalGetterFn, args, "", updateIcLogicInsertionPt);
                ReleaseAssert(llvm_value_has_type<uint8_t>(effectKindToWrite));
                Value* addr = ms_effectKind->EmitGetAddress(module, bodyIcPtr, updateIcLogicInsertionPt);
                Value* oldEffectKind = nullptr;
                if (m_shouldFuseICIntoInterpreterOpcode)
                {
                    oldEffectKind = new LoadInst(llvm_type_of<uint8_t>(ctx), addr, "", false /*isVolatile*/, Align(1), updateIcLogicInsertionPt);
                }

                new StoreInst(effectKindToWrite, addr /*dst*/, false /*isVolatile*/, Align(1), updateIcLogicInsertionPt);

                // If the IC effect ordinal is to be fused into the opcode, update it now
                // Note that the caller side is responsible for resetting the opcode back to the initial one before calling us
                //
                if (m_shouldFuseICIntoInterpreterOpcode)
                {
                    ReleaseAssert(m_bodyFnBytecodePtrArgOrd != static_cast<uint32_t>(-1));
                    ReleaseAssert(m_bodyFnBytecodePtrArgOrd < m_bodyFn->arg_size());
                    Value* bytecodePtr = m_bodyFn->getArg(m_bodyFnBytecodePtrArgOrd);
                    ReleaseAssert(llvm_value_has_type<void*>(bytecodePtr));
                    Type* opcodeTy = Type::getIntNTy(ctx, static_cast<uint32_t>(BytecodeVariantDefinition::x_opcodeSizeBytes * 8));
                    ReleaseAssert(opcodeTy->getIntegerBitWidth() == BytecodeVariantDefinition::x_opcodeSizeBytes * 8);

                    // Note that here we are taking advantage of the fact that an invalid IC always have effectKind == -1,
                    // the valid IC effects start at effectKind == 0, and the opcodes are laid out in the same order.
                    //
                    // Ideally we should just update the opcode by writing the absolute opcode value, which is both more robust
                    // and faster, but under the current design we do not know the opcode value at this point..
                    // This can be fixed but requires some work, so let's leave it to the future..
                    //
                    Value* valToSubtract = new SExtInst(oldEffectKind, opcodeTy, "", updateIcLogicInsertionPt);
                    Value* valToAdd = new ZExtInst(effectKindToWrite, opcodeTy, "", updateIcLogicInsertionPt);
                    Value* originalOpcode = new LoadInst(opcodeTy, bytecodePtr, "", false /*isVolatile*/, Align(1), updateIcLogicInsertionPt);
                    Instruction* tmp = CreateSub(originalOpcode, valToSubtract);
                    tmp->insertBefore(updateIcLogicInsertionPt);
                    Instruction* newOpcode = CreateAdd(tmp, valToAdd);
                    newOpcode->insertBefore(updateIcLogicInsertionPt);
                    new StoreInst(newOpcode, bytecodePtr, false /*isVolatile*/, Align(1), updateIcLogicInsertionPt);
                }
            }
            else
            {
                ReleaseAssert(!m_shouldFuseICIntoInterpreterOpcode);
            }
            // Write the IC state
            {
                std::vector<Value*> args;
                args.push_back(bodyIcPtr);
                for (Value* val : e.m_icStateVals)
                {
                    args.push_back(val);
                }
                CallInst* writeIcStateInst = CallInst::Create(e.m_icStateEncoder, args, "", updateIcLogicInsertionPt);
                ReleaseAssert(llvm_value_has_type<void>(writeIcStateInst));
            }
            // Replace the annotation dummy call by the actual call that executes the IC effect logic
            //
            ReleaseAssert(e.m_effectFnBody->arg_size() == 1);
            CallInst* replacement = CallInst::Create(e.m_effectFnBody, { e.m_effectFnBodyCapture }, "", e.m_origin /*insertBefore*/);
            ReleaseAssert(replacement->getType() == e.m_origin->getType());
            if (!llvm_value_has_type<void>(replacement))
            {
                e.m_origin->replaceAllUsesWith(replacement);
            }
            ReleaseAssert(e.m_origin->use_empty());
            e.m_origin->eraseFromParent();
        }

        // Set up the always_inline attribute for the effect functions
        //
        ReleaseAssert(!e.m_effectFnBody->hasFnAttribute(Attribute::AttrKind::NoInline));
        e.m_effectFnBody->addFnAttr(Attribute::AttrKind::AlwaysInline);
        e.m_effectFnBody->setLinkage(GlobalValue::InternalLinkage);

        for (Function* effectFn : e.m_effectFnMain)
        {
            ReleaseAssert(!effectFn->hasFnAttribute(Attribute::AttrKind::NoInline));
            effectFn->addFnAttr(Attribute::AttrKind::AlwaysInline);
            effectFn->setLinkage(GlobalValue::InternalLinkage);
        }
    }

    // Lower the SetUncacheable() APIs
    //
    for (CallInst* setUncacheableApiCall : m_setUncacheableApiCalls)
    {
        ReleaseAssert(llvm_value_has_type<void>(setUncacheableApiCall));
        new StoreInst(CreateLLVMConstantInt<uint8_t>(ctx, 0), isCacheableAlloca, setUncacheableApiCall /*insertBefore*/);
        setUncacheableApiCall->eraseFromParent();
    }

    // Create the slow path logic, which should simply call the IC body
    //
    {
        ReleaseAssert(callIcBodyBlock->getInstList().size() == 1);
        Instruction* insertBefore = callIcBodyBlock->getTerminator();
        Value* slowPathRes = CallInst::Create(m_bodyFn, m_bodyFnArgs, "", insertBefore);
        new StoreInst(slowPathRes, icResultAlloca, insertBefore);
    }

    // Create the join BB logic: load from 'icResultAlloca' to get the final result
    //
    {
        Value* icResult = new LoadInst(m_origin->getType(), icResultAlloca, "", m_origin);
        if (!llvm_value_has_type<void>(icResult))
        {
            m_origin->replaceAllUsesWith(icResult);
        }
        ReleaseAssert(m_origin->use_empty());
        m_origin->eraseFromParent();
    }

    m_icStruct = std::move(ms);

    ValidateLLVMFunction(mainFn);
    ValidateLLVMFunction(m_bodyFn);
}

void AstInlineCache::DoTrivialLowering()
{
    using namespace llvm;

    // Lower each Effect API in the IC body
    // No IC will be created, so it's sufficient to simply call the effect function
    //
    for (Effect& e : m_effects)
    {
        ReleaseAssert(e.m_effectFnBody->arg_size() == 1);
        CallInst* replacement = CallInst::Create(e.m_effectFnBody, { e.m_effectFnBodyCapture }, "", e.m_origin /*insertBefore*/);
        ReleaseAssert(replacement->getType() == e.m_origin->getType());
        if (!llvm_value_has_type<void>(replacement))
        {
            e.m_origin->replaceAllUsesWith(replacement);
        }
        ReleaseAssert(e.m_origin->use_empty());
        e.m_origin->eraseFromParent();

        ReleaseAssert(!e.m_effectFnBody->hasFnAttribute(Attribute::NoInline));
        e.m_effectFnBody->addFnAttr(Attribute::AlwaysInline);
        ReleaseAssert(e.m_effectFnBody->getLinkage() == GlobalValue::InternalLinkage);
    }

    // Lower m_origin to simply call m_bodyFn
    //
    Value* icResult = CallInst::Create(m_bodyFn, m_bodyFnArgs, "", m_origin);
    ReleaseAssert(icResult->getType() == m_origin->getType());
    m_origin->replaceAllUsesWith(icResult);
    m_origin->eraseFromParent();

    // Make the IC body function always_inline since there is no reason to not inline it now
    //
    ReleaseAssert(!m_bodyFn->hasFnAttribute(Attribute::NoInline));
    m_bodyFn->addFnAttr(Attribute::AlwaysInline);
    m_bodyFn->setLinkage(GlobalValue::InternalLinkage);
}

void AstInlineCache::TriviallyLowerAllInlineCaches(llvm::Function* func)
{
    using namespace llvm;

    std::vector<AstInlineCache> icList = AstInlineCache::GetAllUseInFunction(func);
    for (AstInlineCache& ic : icList)
    {
        ic.DoTrivialLowering();
    }

    DesugarAndSimplifyLLVMModule(func->getParent(), DesugaringLevel::PerFunctionSimplifyOnly);

    // Remove all use of MakeInlineCache() calls
    //
    for (AstInlineCache& ic : icList)
    {
        ReleaseAssert(ic.m_icPtrOrigin->use_empty());
        ic.m_icPtrOrigin->eraseFromParent();
    }

    // Remove all use of the GetInterpreterBytecodePtrPlaceholder() placeholders
    //
    {
        std::vector<CallInst*> allUses;
        for (BasicBlock& bb : *func)
        {
            for (Instruction& inst : bb)
            {
                CallInst* ci = dyn_cast<CallInst>(&inst);
                if (ci != nullptr && ci->getCalledFunction() != nullptr && ci->getCalledFunction()->getName() == x_get_bytecode_ptr_placeholder_fn_name)
                {
                    allUses.push_back(ci);
                }
            }
        }

        for (CallInst* ci : allUses)
        {
            ReleaseAssert(ci->use_empty());
            ci->eraseFromParent();
        }
    }

    ValidateLLVMFunction(func);

    RunLLVMDeadGlobalElimination(func->getParent());
}

}   // namespace dast
