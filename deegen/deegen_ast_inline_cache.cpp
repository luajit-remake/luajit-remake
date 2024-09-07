#include "deegen_ast_inline_cache.h"
#include "deegen_analyze_lambda_capture_pass.h"
#include "deegen_bytecode_operand.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_jit_slow_path_data.h"
#include "deegen_rewrite_closure_call.h"
#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_stencil_reserved_placeholder_ords.h"
#include "deegen_parse_asm_text.h"
#include "drt/baseline_jit_codegen_helper.h"
#include "invoke_clang_helper.h"
#include "llvm/Linker/Linker.h"

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
        uint64_t offset;
        if (capturedValueAddr == lambda->getArg(0))
        {
            offset = 0;
            ReleaseAssert(capturedValueOffsetToOrdinalMap.count(offset) && capturedValueOffsetToOrdinalMap[offset] == 0);
        }
        else
        {
            ReleaseAssert(isa<GetElementPtrInst>(capturedValueAddr));
            GetElementPtrInst* gep = cast<GetElementPtrInst>(capturedValueAddr);
            ReleaseAssert(gep->getPointerOperand() == lambda->getArg(0));
            APInt offsetAP(64 /*numBits*/, 0);
            ReleaseAssert(gep->accumulateConstantOffset(dataLayout, offsetAP /*out*/));
            offset = offsetAP.getZExtValue();
        }
        ReleaseAssert(capturedValueOffsetToOrdinalMap.count(offset));
        return capturedValueOffsetToOrdinalMap[offset];
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

    auto validateCapturedValueType = [&](Type* captureTy)
    {
        if (!captureTy->isIntOrPtrTy())
        {
            if (!captureTy->isStructTy())
            {
                fprintf(stderr, "[ERROR] Non-integer/pointer/word-sized struct type local capture in IC is currently unsupported!\n");
                abort();
            }
            StructType* sty = cast<StructType>(captureTy);
            size_t stySize = dataLayout.getStructLayout(sty)->getSizeInBytes();
            if (stySize > 8 || !is_power_of_2(stySize))
            {
                fprintf(stderr, "[ERROR] Non word-sized struct type local capture in IC is currently unsupported!\n");
                abort();
            }

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

            return;
        }
        else if (captureTy->isPointerTy())
        {
            return;
        }
        else
        {
            ReleaseAssert(captureTy->isIntegerTy());
            size_t bitWidth = captureTy->getIntegerBitWidth();
            ReleaseAssert(bitWidth % 8 == 0 && is_power_of_2(bitWidth));
            return;
        }
    };

    for (auto& captureIter : effectFnLocalCaptures)
    {
        size_t ord = captureIter.first;
        Value* valueInBodyFn = captureIter.second;

        Type* captureTy = valueInBodyFn->getType();
        validateCapturedValueType(captureTy);
        size_t typeSize = dataLayout.getTypeAllocSize(captureTy);
        ReleaseAssert(typeSize <= 8 && is_power_of_2(typeSize));
        bool requiresRangeAnnotation = (typeSize >= 4);

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

    std::unordered_map<uint64_t, CaptureValueRangeInfo> captureOrdToRangeInfoMap;
    for (CaptureValueRangeInfo& cvri : allCaptureValueRangeInfo)
    {
        ReleaseAssert(!captureOrdToRangeInfoMap.count(cvri.m_ordInCaptureStruct));
        captureOrdToRangeInfoMap[cvri.m_ordInCaptureStruct] = cvri;
    }

    annotationFnArgs.push_back(CreateLLVMConstantInt<uint64_t>(ctx, effectFnLocalCaptures.size()) /*numValuesInIcState*/);
    for (auto& it : effectFnLocalCaptures)
    {
        size_t captureOrd = it.first;
        Value* val = it.second;
        annotationFnArgs.push_back(val);

        Value* lb = nullptr;
        Value* ub = nullptr;
        if (captureOrdToRangeInfoMap.count(captureOrd))
        {
            CaptureValueRangeInfo& cvri = captureOrdToRangeInfoMap[captureOrd];
            lb = CreateLLVMConstantInt<int64_t>(ctx, cvri.m_lbInclusive);
            ub = CreateLLVMConstantInt<int64_t>(ctx, cvri.m_ubInclusive);
            captureOrdToRangeInfoMap.erase(captureOrdToRangeInfoMap.find(captureOrd));
        }
        else
        {
            size_t typeSize = dataLayout.getTypeAllocSize(val->getType());
            ReleaseAssert(typeSize < 4);
            ReleaseAssert(is_power_of_2(typeSize));
            lb = CreateLLVMConstantInt<int64_t>(ctx, 0);
            ub = CreateLLVMConstantInt<int64_t>(ctx, (static_cast<int64_t>(1) << (typeSize * 8)) - 1);
        }

        ReleaseAssert(lb != nullptr && ub != nullptr);
        annotationFnArgs.push_back(lb);
        annotationFnArgs.push_back(ub);
    }

    ReleaseAssert(captureOrdToRangeInfoMap.empty());

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

        ReleaseAssert(curArgOrd + numIcStateCaptures * 3 == target->arg_size());
        for (uint32_t i = 0; i < numIcStateCaptures; i++)
        {
            Value* icStateCapture = target->getArgOperand(curArgOrd);
            Value* icValueLb = target->getArgOperand(curArgOrd + 1);
            Value* icValueUb = target->getArgOperand(curArgOrd + 2);
            curArgOrd += 3;

            ReleaseAssert(isa<ConstantInt>(icValueLb) && llvm_value_has_type<int64_t>(icValueLb));
            ReleaseAssert(isa<ConstantInt>(icValueUb) && llvm_value_has_type<int64_t>(icValueUb));
            int64_t lb = GetValueOfLLVMConstantInt<int64_t>(icValueLb);
            int64_t ub = GetValueOfLLVMConstantInt<int64_t>(icValueUb);
            ReleaseAssert(lb <= ub);

            e.m_icStateVals.push_back({
                .m_valueInBodyFn = icStateCapture,
                .m_lbInclusive = lb,
                .m_ubInclusive = ub,
                .m_placeholderOrd = static_cast<size_t>(-1)
            });
        }

        ReleaseAssert(curArgOrd == target->arg_size());

        ReleaseAssert(e.m_icStateEncoder->arg_size() == 1 + numIcStateCaptures);
        ReleaseAssert(e.m_icStateDecoder->arg_size() == 1 + numIcStateCaptures);
        for (uint32_t i = 0; i < numIcStateCaptures; i++)
        {
            ReleaseAssert(e.m_icStateEncoder->getArg(1 + i)->getType() == e.m_icStateVals[i].m_valueInBodyFn->getType());
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

void LowerInterpreterGetBytecodePtrInternalAPI(DeegenBytecodeImplCreatorBase* ifi, llvm::Function* func)
{
    using namespace llvm;
    std::vector<CallInst*> allUses;
    DeegenEngineTier tier = ifi->GetTier();
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
        if (tier == DeegenEngineTier::Interpreter)
        {
            // For interpreter, the bytecodePtr is just the bytecodePtr
            //
            ci->replaceAllUsesWith(ifi->GetCurBytecode());
        }
        else if (tier == DeegenEngineTier::BaselineJIT)
        {
            // For baseline JIT, this should never be needed
            //
            ReleaseAssert(ci->use_empty());
        }
        else
        {
            ReleaseAssert(false && "unhandled");
        }
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
                Type* icStateType = e.m_icStateVals[i].m_valueInBodyFn->getType();
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
                allocas.push_back(new AllocaInst(e.m_icStateVals[i].m_valueInBodyFn->getType(), 0 /*addrSpace*/, "", bb));
            }

            for (uint32_t i = 0; i < numElementsInIcState; i++)
            {
                Type* icStateType = e.m_icStateVals[i].m_valueInBodyFn->getType();
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
                for (auto& icStateVal : e.m_icStateVals)
                {
                    args.push_back(icStateVal.m_valueInBodyFn);
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

void AstInlineCache::LowerIcPtrGetterFunctionForBaselineJit(BaselineJitImplCreator* ifi, llvm::Function* func)
{
    using namespace llvm;
    LLVMContext& ctx = func->getContext();

    std::vector<CallInst*> allUses;
    for (BasicBlock& bb : *func)
    {
        for (Instruction& inst : bb)
        {
            CallInst* ci = dyn_cast<CallInst>(&inst);
            if (ci != nullptr && ci->getCalledFunction() != nullptr && ci->getCalledFunction()->getName().startswith(x_createIcInitPlaceholderFunctionPrefix))
            {
                allUses.push_back(ci);
            }
        }
    }

    // For baseline JIT, the icPtr is always the SlowPathData
    //
    for (CallInst* origin : allUses)
    {
        ReleaseAssert(llvm_value_has_type<void*>(origin));
        ReleaseAssert(!ifi->IsJitSlowPath());
        Value* offset = ifi->GetSlowPathDataOffsetFromJitFastPath(origin, true /*useAliasOrdinal*/);
        Value* baselineJitCodeBlock = ifi->GetJitCodeBlock();
        ReleaseAssert(llvm_value_has_type<void*>(baselineJitCodeBlock));
        Value* replacement = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), baselineJitCodeBlock, { offset }, "", origin);
        origin->replaceAllUsesWith(replacement);
        origin->eraseFromParent();
    }
}

AstInlineCache::BaselineJitLLVMLoweringResult WARN_UNUSED AstInlineCache::DoLoweringForBaselineJit(BaselineJitImplCreator* ifi, size_t icUsageOrdInBytecode, size_t globalIcEffectTraitBaseOrd)
{
    using namespace llvm;
    LLVMContext& ctx = m_origin->getContext();
    ReleaseAssert(m_origin->getParent() != nullptr);
    Function* mainFn = m_origin->getParent()->getParent();
    ReleaseAssert(mainFn != nullptr);
    Module* module = mainFn->getParent();
    ReleaseAssert(module != nullptr);

    BaselineJitLLVMLoweringResult r;

    DataLayout dataLayout(module);

    AllocaInst* icResultAlloca = new AllocaInst(m_origin->getType(), 0 /*addrSpace*/, "", &mainFn->getEntryBlock().front() /*insertBefore*/);

    // Split block after instruction 'm_origin'
    //
    BasicBlock* icSplitBlock = m_origin->getParent();
    BasicBlock* joinBlock = SplitBlock(icSplitBlock, m_origin);
    ReleaseAssert(m_origin->getParent() == joinBlock);

    // After SplitBlock, the old block should have an unconditional branch instruction to the new block
    //
    ReleaseAssert(isa<BranchInst>(&icSplitBlock->getInstList().back()));

    std::unordered_map<size_t, BasicBlock*> effectOrdToBBMap;

    for (Effect& e : m_effects)
    {
        // Generate the logic in the main function for each specialization, which is simply calling the wrapped effect fn
        //
        for (size_t i = 0; i < e.m_effectFnMain.size(); i++)
        {
            BasicBlock* bb = BasicBlock::Create(ctx, "", mainFn);
            size_t effectOrd = e.m_effectStartOrdinal + i;
            ReleaseAssert(!effectOrdToBBMap.count(effectOrd));
            effectOrdToBBMap[effectOrd] = bb;
            Instruction* insertBefore = BranchInst::Create(joinBlock /*dest*/, bb /*insertAtEnd*/);

            // Insert a dummy unique magic assembly to prevent LLVM merging IC entry blocks with non-IC blocks
            //
            {
                size_t icEffectGlobalOrd = globalIcEffectTraitBaseOrd + e.m_effectStartOrdinal + i;
                std::string asmString = "movl $$" + std::to_string(icEffectGlobalOrd) + ", eax;";
                asmString = MagicAsm::WrapLLVMAsmPayload(asmString, MagicAsmKind::DummyAsmToPreventIcEntryBBMerge);
                FunctionType* fty = FunctionType::get(llvm_type_of<void>(ctx), { }, false);
                InlineAsm* ia = InlineAsm::get(fty, asmString, "" /*constraints*/, true /*hasSideEffects*/);
                CallInst* ci = CallInst::Create(ia, "", insertBefore);
                ci->addFnAttr(Attribute::NoUnwind);
            }

            std::vector<Value*> args;
            // In baseline JIT, the IC effect function does not need the IC state
            //
            args.push_back(ConstantPointerNull::get(PointerType::get(ctx, 0 /*addressSpace*/)));
            for (Value* val : e.m_effectFnMainArgs) { args.push_back(val); }
            Function* effectFn = e.m_effectFnMain[i];
            Value* effectResult = CallInst::Create(effectFn, args, "", insertBefore);
            new StoreInst(effectResult, icResultAlloca, insertBefore);

            ReleaseAssert(!effectFn->hasFnAttribute(Attribute::NoInline));
            effectFn->addFnAttr(Attribute::AlwaysInline);
            effectFn->setLinkage(GlobalValue::InternalLinkage);
        }

        // Generate the implementation of the decoder function, which should just populate values with placeholder values
        //
        {
            Function* decoderFn = e.m_icStateDecoder;
            ReleaseAssert(decoderFn->arg_size() == e.m_icStateVals.size() + 1);
            ReleaseAssert(decoderFn->empty());
            BasicBlock* decoderBB = BasicBlock::Create(ctx, "", decoderFn);
            for (size_t i = 0; i < e.m_icStateVals.size(); i++)
            {
                Effect::IcStateValueInfo& svi = e.m_icStateVals[i];
                ReleaseAssert(svi.m_placeholderOrd == static_cast<size_t>(-1));

                size_t ord = static_cast<size_t>(-1);
                Value* val = nullptr;
                bool isNonPrimitiveType = !svi.m_valueInBodyFn->getType()->isIntOrPtrTy();
                if (isNonPrimitiveType)
                {
                    size_t tySize = dataLayout.getTypeAllocSize(svi.m_valueInBodyFn->getType());
                    ReleaseAssert(is_power_of_2(tySize) && tySize <= 8);
                    Type* intTyForTySize = Type::getIntNTy(ctx, static_cast<uint32_t>(tySize * 8));
                    auto tmp = ifi->CreateGenericIcStateCapturePlaceholder(intTyForTySize, svi.m_lbInclusive, svi.m_ubInclusive, decoderBB);
                    ord = tmp.second;
                    CallInst* intVal = tmp.first;
                    AllocaInst* allocaInst1 = new AllocaInst(svi.m_valueInBodyFn->getType(), 0, "", &decoderBB->front());
                    AllocaInst* allocaInst2 = new AllocaInst(intTyForTySize, 0, "", &decoderBB->front());
                    new StoreInst(intVal, allocaInst2, decoderBB);
                    EmitLLVMIntrinsicMemcpy(module, allocaInst1 /*dst*/, allocaInst2 /*src*/, CreateLLVMConstantInt<uint64_t>(ctx, tySize), decoderBB);
                    val = new LoadInst(svi.m_valueInBodyFn->getType(), allocaInst1, "", decoderBB);
                }
                else
                {
                    auto tmp = ifi->CreateGenericIcStateCapturePlaceholder(svi.m_valueInBodyFn->getType(), svi.m_lbInclusive, svi.m_ubInclusive, decoderBB);
                    ord = tmp.second;
                    val = tmp.first;
                }
                ReleaseAssert(ord != static_cast<size_t>(-1) && val != nullptr);

                svi.m_placeholderOrd = ord;

                Value* dest = decoderFn->getArg(static_cast<uint32_t>(i + 1));
                ReleaseAssert(llvm_value_has_type<void*>(dest));
                new StoreInst(val, dest, decoderBB);
            }

            ReturnInst::Create(ctx, nullptr, decoderBB);

            ReleaseAssert(!decoderFn->hasFnAttribute(Attribute::NoInline));
            decoderFn->addFnAttr(Attribute::AlwaysInline);
            decoderFn->setLinkage(GlobalValue::InternalLinkage); 
        }
    }

    // Generate logic for the IC miss slow path
    //
    BasicBlock* icMissSlowPath = BasicBlock::Create(ctx, "", mainFn);

    {
        // Create the real m_bodyFn, which should not need the bytecodePtr
        //
        ReleaseAssertImp(m_shouldFuseICIntoInterpreterOpcode, m_bodyFnBytecodePtrArgOrd < m_bodyFn->arg_size());
        ReleaseAssertImp(m_shouldFuseICIntoInterpreterOpcode, m_bodyFn->getArg(m_bodyFnBytecodePtrArgOrd)->use_empty());
        ReleaseAssertImp(!m_shouldFuseICIntoInterpreterOpcode, m_bodyFnBytecodePtrArgOrd == static_cast<uint32_t>(-1));

        std::vector<Value*> bodyFnArgs;
        for (size_t i = 0; i < m_bodyFnArgs.size(); i++)
        {
            if (i != m_bodyFnBytecodePtrArgOrd)
            {
                bodyFnArgs.push_back(m_bodyFnArgs[i]);
            }
        }

        std::vector<Type*> bodyFnArgTys;
        for (Value* arg : bodyFnArgs)
        {
            bodyFnArgTys.push_back(arg->getType());
        }

        std::string bodyFnName = "__deegen_baseline_jit_" + ifi->GetBytecodeDef()->GetBytecodeIdName() + "_icbody_" + std::to_string(icUsageOrdInBytecode);
        ReleaseAssert(module->getNamedValue(bodyFnName) == nullptr);
        r.m_bodyFnName = bodyFnName;

        FunctionType* bodyFnWrapperFty = FunctionType::get(m_bodyFn->getReturnType(), bodyFnArgTys, false /*isVarArg*/);
        Function* bodyFnWrapper = Function::Create(bodyFnWrapperFty, GlobalValue::ExternalLinkage, bodyFnName, module);
        ReleaseAssert(bodyFnWrapper->getName() == bodyFnName);

        bodyFnWrapper->setDSOLocal(true);
        bodyFnWrapper->setCallingConv(CallingConv::PreserveMost);
        CopyFunctionAttributes(bodyFnWrapper /*dst*/, m_bodyFn);
        bodyFnWrapper->addFnAttr(Attribute::NoInline);
        bodyFnWrapper->addFnAttr(Attribute::NoUnwind);

        BasicBlock* bodyFnWrapperBB = BasicBlock::Create(ctx, "", bodyFnWrapper);
        std::vector<Value*> callArgs;
        {
            uint32_t idx = 0;
            for (size_t i = 0; i < m_bodyFnArgs.size(); i++)
            {
                if (i != m_bodyFnBytecodePtrArgOrd)
                {
                    ReleaseAssert(idx < bodyFnWrapper->arg_size());
                    callArgs.push_back(bodyFnWrapper->getArg(idx));
                    idx++;
                }
                else
                {
                    callArgs.push_back(UndefValue::get(llvm_type_of<void*>(ctx)));
                }
            }
            ReleaseAssert(idx == bodyFnWrapper->arg_size());
        }
        ReleaseAssert(callArgs.size() == m_bodyFn->arg_size());
        CallInst* callInst = CallInst::Create(m_bodyFn, callArgs, "", bodyFnWrapperBB);
        ReturnInst::Create(ctx, callInst, bodyFnWrapperBB);

        m_bodyFn->setLinkage(GlobalValue::InternalLinkage);
        ReleaseAssert(!m_bodyFn->hasFnAttribute(Attribute::NoInline));
        m_bodyFn->addFnAttr(Attribute::AlwaysInline);

        // Set up logic for IC miss slow path
        //
        CallInst* icMissSlowPathResult = CallInst::Create(bodyFnWrapper, bodyFnArgs, "", icMissSlowPath);
        icMissSlowPathResult->setCallingConv(CallingConv::PreserveMost);
        new StoreInst(icMissSlowPathResult, icResultAlloca, icMissSlowPath);
        BranchInst::Create(joinBlock /*dest*/, icMissSlowPath /*insertAtEnd*/);
    }

    // Generate logic for the join block
    //
    Value* icResult = new LoadInst(m_origin->getType(), icResultAlloca, "", m_origin);
    ReleaseAssert(icResult->getType() == m_origin->getType());
    m_origin->replaceAllUsesWith(icResult);
    m_origin->eraseFromParent();

    std::vector<BasicBlock*> effectImplBBs;
    ReleaseAssert(effectOrdToBBMap.size() == m_totalEffectKinds);
    for (size_t i = 0; i < m_totalEffectKinds; i++)
    {
        ReleaseAssert(effectOrdToBBMap.count(i));
        effectImplBBs.push_back(effectOrdToBBMap[i]);
    }

    // Generate CallBr magic for the IC entry point
    //
    ReleaseAssert(m_icKey->getType()->isIntOrPtrTy());

    {
        Instruction* insertBefore = icSplitBlock->getTerminator();
        Value* normalizedIcKey;
        if (m_icKey->getType()->isPointerTy())
        {
            normalizedIcKey = new PtrToIntInst(m_icKey, llvm_type_of<uint64_t>(ctx), "", insertBefore);
        }
        else
        {
            ReleaseAssert(m_icKey->getType()->isIntegerTy());
            size_t bitWidth = m_icKey->getType()->getIntegerBitWidth();
            if (bitWidth < 32)
            {
                normalizedIcKey = new ZExtInst(m_icKey, llvm_type_of<uint32_t>(ctx), "", insertBefore);
            }
            else
            {
                normalizedIcKey = m_icKey;
            }
        }

        ReleaseAssert(llvm_value_has_type<uint32_t>(normalizedIcKey) || llvm_value_has_type<uint64_t>(normalizedIcKey));

        // The first line of the assembly conveys the IC ordinal
        //
        std::string asmIdentStrPrefix = "movl $$" + std::to_string(icUsageOrdInBytecode) + ", eax;";

        // The second line means how many bytes of NOP padding the SMC region shall reserve.
        // It always starts with 0. We will compile things to ASM once to figure out how much padding we need,
        // then back-patch it at LLVM IR level, and compile it to ASM again with the right padding.
        //
        asmIdentStrPrefix += "movl $$0, eax;";

        FunctionType* iaFty = nullptr;
        InlineAsm* ia = nullptr;
        if (llvm_value_has_type<uint32_t>(normalizedIcKey))
        {
            std::string asmStr = "cmpl $1, $0;";
            for (size_t i = 0; i < effectImplBBs.size(); i++)
            {
                asmStr += "jne ${" + std::to_string(i + 2) + ":l};";
            }

            std::string constraintStr = "r,i,";
            for (size_t i = 0; i < effectImplBBs.size(); i++)
            {
                constraintStr += "!i,";
            }
            constraintStr += "~{cc},~{dirflag},~{fpsr},~{flags}";

            asmStr = asmIdentStrPrefix + asmStr;
            asmStr = MagicAsm::WrapLLVMAsmPayload(asmStr, MagicAsmKind::GenericIcEntry);

            iaFty = FunctionType::get(llvm_type_of<void>(ctx), { llvm_type_of<uint32_t>(ctx), llvm_type_of<void*>(ctx) }, false);
            ia = InlineAsm::get(iaFty, asmStr, constraintStr, true /*hasSideEffects*/);
        }
        else
        {
            std::string asmStr = "movabsq $2, $0;cmpq $0, $1;";
            for (size_t i = 0; i < effectImplBBs.size(); i++)
            {
                asmStr += "jne ${" + std::to_string(i + 3) + ":l};";
            }

            std::string constraintStr = "=&r,r,i,";
            for (size_t i = 0; i < effectImplBBs.size(); i++)
            {
                constraintStr += "!i,";
            }
            constraintStr += "~{cc},~{dirflag},~{fpsr},~{flags}";

            asmStr = asmIdentStrPrefix + asmStr;
            asmStr = MagicAsm::WrapLLVMAsmPayload(asmStr, MagicAsmKind::GenericIcEntry);

            iaFty = FunctionType::get(llvm_type_of<uint64_t>(ctx), { llvm_type_of<uint64_t>(ctx), llvm_type_of<void*>(ctx) }, false);
            ia = InlineAsm::get(iaFty, asmStr, constraintStr, true /*hasSideEffects*/);
        }
        ReleaseAssert(iaFty != nullptr && ia != nullptr);

        GlobalVariable* cpSym = DeegenInsertOrGetCopyAndPatchPlaceholderSymbol(module, CP_PLACEHOLDER_GENERIC_IC_KEY);
        ReleaseAssert(llvm_value_has_type<void*>(cpSym));

        CallBrInst* inst = CallBrInst::Create(iaFty,
                                              ia,
                                              icMissSlowPath /*fallthroughDest*/,
                                              effectImplBBs /*gotoDests*/,
                                              { normalizedIcKey, cpSym } /*args*/,
                                              "",
                                              icSplitBlock->getTerminator() /*insertBefore*/);
        inst->addFnAttr(Attribute::NoUnwind);
        inst->addFnAttr(Attribute::ReadNone);

        icSplitBlock->getTerminator()->eraseFromParent();
    }

    AllocaInst* isCacheableAlloca = new AllocaInst(llvm_type_of<uint8_t>(ctx), 0 /*addrSpace*/, "", &m_bodyFn->getEntryBlock().front() /*insertBefore*/);

    new StoreInst(CreateLLVMConstantInt<uint8_t>(ctx, 1), isCacheableAlloca, FindFirstNonAllocaInstInEntryBB(m_bodyFn));

    // Lower the SetUncacheable() APIs
    //
    for (CallInst* setUncacheableApiCall : m_setUncacheableApiCalls)
    {
        ReleaseAssert(llvm_value_has_type<void>(setUncacheableApiCall));
        new StoreInst(CreateLLVMConstantInt<uint8_t>(ctx, 0), isCacheableAlloca, setUncacheableApiCall /*insertBefore*/);
        setUncacheableApiCall->eraseFromParent();
    }

    // Lower the body function
    // Note that for baseline JIT, the icPtr in the body function is actually the SlowPathData
    //
    for (Effect& e : m_effects)
    {
        Value* slowPathData = e.m_icPtr;

        Value* icSite = nullptr;
        {
            size_t offset = ifi->GetBytecodeDef()->GetBaselineJitSlowPathDataLayout()->m_genericICs.GetOffsetForSite(icUsageOrdInBytecode);
            icSite = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathData,
                                                       { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", e.m_origin);
        }

        // Check if we should create a new IC
        // We should do it only if it is cacheable and the existing number of IC entries has not reached maximum
        //
        Instruction* createIcLogicInsertionPt = nullptr;
        BasicBlock* afterIcCreationBB = nullptr;
        {
            Value* isCacheableU8 = new LoadInst(llvm_type_of<uint8_t>(ctx), isCacheableAlloca, "", e.m_origin);
            Value* isCacheable = new TruncInst(isCacheableU8, llvm_type_of<bool>(ctx), "", e.m_origin);
            Value* numExistingIcAddr = GetElementPtrInst::CreateInBounds(
                llvm_type_of<uint8_t>(ctx), icSite,
                { CreateLLVMConstantInt<uint64_t>(ctx, offsetof_member_v<&JitGenericInlineCacheSite::m_numEntries>) }, "", e.m_origin);
            Value* numExistingIc = new LoadInst(llvm_type_of<uint8_t>(ctx), numExistingIcAddr, "", e.m_origin);
            Value* isLessThanMax = new ICmpInst(e.m_origin, ICmpInst::ICMP_ULT, numExistingIc, CreateLLVMConstantInt<uint8_t>(ctx, SafeIntegerCast<uint8_t>(x_maxJitGenericInlineCacheEntries)));
            Value* shouldCreateIc = BinaryOperator::Create(BinaryOperator::And, isCacheable, isLessThanMax, "", e.m_origin);

            createIcLogicInsertionPt = SplitBlockAndInsertIfThen(shouldCreateIc, e.m_origin /*splitBefore*/, false /*isUnreachable*/);
            ReleaseAssert(isa<BranchInst>(createIcLogicInsertionPt));
            ReleaseAssert(!cast<BranchInst>(createIcLogicInsertionPt)->isConditional());
            ReleaseAssert(cast<BranchInst>(createIcLogicInsertionPt)->getSuccessor(0) == e.m_origin->getParent());
            afterIcCreationBB = e.m_origin->getParent();
        }

        // Figure out the effect ordinal
        //
        Value* effectOrd;
        {
            Function* effectOrdinalGetterFn = CreateEffectOrdinalGetterFn(module, m_bodyFn /*functionToCopyAttrFrom*/, e.m_specializations, 0 /*startOrd*/);
            std::vector<Value*> args;
            for (auto& it : e.m_specializations)
            {
                args.push_back(it.m_valueInBodyFn);
            }
            effectOrd = CallInst::Create(effectOrdinalGetterFn, args, "", createIcLogicInsertionPt);
            ReleaseAssert(llvm_value_has_type<uint8_t>(effectOrd));
        }

        auto castIcStateCaptureToI64 = [&](Value* captureVal, Instruction* insertBefore) WARN_UNUSED -> Value*
        {
            Type* ty = captureVal->getType();
            bool isNonPrimitiveType = !ty->isIntOrPtrTy();
            if (isNonPrimitiveType)
            {
                size_t tySize = dataLayout.getTypeAllocSize(ty);
                ReleaseAssert(is_power_of_2(tySize) && tySize <= 8);
                Type* intTyForTySize = Type::getIntNTy(ctx, static_cast<uint32_t>(tySize * 8));
                AllocaInst* allocaInst1 = new AllocaInst(ty, 0, "", &m_bodyFn->getEntryBlock().front());
                AllocaInst* allocaInst2 = new AllocaInst(intTyForTySize, 0, "", &m_bodyFn->getEntryBlock().front());
                new StoreInst(captureVal, allocaInst1, insertBefore);
                EmitLLVMIntrinsicMemcpy(module, allocaInst2 /*dst*/, allocaInst1 /*src*/, CreateLLVMConstantInt<uint64_t>(ctx, tySize), insertBefore);
                Value* intVal = new LoadInst(intTyForTySize, allocaInst2, "", insertBefore);
                if (tySize < 8)
                {
                    return new ZExtInst(intVal, llvm_type_of<uint64_t>(ctx), "", insertBefore);
                }
                else
                {
                    ReleaseAssert(llvm_value_has_type<uint64_t>(intVal));
                    return intVal;
                }
            }
            else if (ty->isPointerTy())
            {
                return new PtrToIntInst(captureVal, llvm_type_of<uint64_t>(ctx), "", insertBefore);
            }
            else
            {
                ReleaseAssert(ty->isIntegerTy());
                uint32_t bitWidth = ty->getIntegerBitWidth();
                ReleaseAssert(bitWidth <= 64);
                if (bitWidth < 64)
                {
                    return new ZExtInst(captureVal, llvm_type_of<uint64_t>(ctx), "", insertBefore);
                }
                else
                {
                    ReleaseAssert(llvm_value_has_type<uint64_t>(captureVal));
                    return captureVal;
                }
            }
        };

        // Encode each captured value to the i64 expected by the code generation function
        //
        std::vector<Value*> icStateCaptureI64List;
        for (size_t i = 0; i < e.m_icStateVals.size(); i++)
        {
            Effect::IcStateValueInfo& svi = e.m_icStateVals[i];
            Value* i64Val = castIcStateCaptureToI64(svi.m_valueInBodyFn, createIcLogicInsertionPt);
            ReleaseAssert(llvm_value_has_type<uint64_t>(i64Val));
            icStateCaptureI64List.push_back(i64Val);
        }
        ReleaseAssert(icStateCaptureI64List.size() == e.m_icStateVals.size());
        for (size_t i = 1; i < e.m_icStateVals.size(); i++)
        {
            ReleaseAssert(e.m_icStateVals[i].m_placeholderOrd == e.m_icStateVals[0].m_placeholderOrd + i);
        }

        ReleaseAssert(m_bodyFnIcKeyArgOrd < m_bodyFn->arg_size());
        Value* icKeyValI64 = castIcStateCaptureToI64(m_bodyFn->getArg(m_bodyFnIcKeyArgOrd), createIcLogicInsertionPt);
        ReleaseAssert(llvm_value_has_type<uint64_t>(icKeyValI64));

        auto emitCallToCodegenFnImpl = [&](size_t icEffectGlobalOrd, bool isPopulatingInlineSlab, llvm::Value* jitDestAddr, Instruction* insertBefore)
        {
            ReleaseAssertIff(isPopulatingInlineSlab, jitDestAddr == nullptr);

            // Create the declaration for the codegen implementation function, and call it
            // The codegen implementation has the following prototype:
            //     void* dest: the address to populate JIT code
            //     void* slowPathData: the SlowPathData for this bytecode
            //     uint64_t icKey: the IC Key casted as i64
            // followed by all the values in the IC state in i64
            //
            std::vector<Value*> cgFnArgs;
            cgFnArgs.push_back(isPopulatingInlineSlab ? ConstantPointerNull::get(PointerType::get(ctx, 0 /*addrSpace*/)) : jitDestAddr);
            cgFnArgs.push_back(slowPathData);
            cgFnArgs.push_back(icKeyValI64);
            for (Value* val : icStateCaptureI64List)
            {
                cgFnArgs.push_back(val);
            }

            std::vector<Type*> cgFnArgTys;
            for (Value* val : cgFnArgs)
            {
                cgFnArgTys.push_back(val->getType());
            }

            std::string cgFnName = x_jit_codegen_ic_impl_placeholder_fn_prefix + std::to_string(icEffectGlobalOrd);
            if (isPopulatingInlineSlab)
            {
                cgFnName += "_inline_slab";
            }
            ReleaseAssert(module->getNamedValue(cgFnName) == nullptr);

            FunctionType* fty = FunctionType::get(llvm_type_of<void>(ctx), cgFnArgTys, false /*isVarArg*/);
            Function* cgFn = Function::Create(fty, GlobalValue::ExternalLinkage, cgFnName, module);
            ReleaseAssert(cgFn->getName() == cgFnName);
            cgFn->setDSOLocal(true);
            cgFn->addFnAttr(Attribute::NoUnwind);

            CallInst::Create(cgFn, cgFnArgs, "", insertBefore);
        };

        size_t icStateValStartOrd = (e.m_icStateVals.size() > 0) ? e.m_icStateVals[0].m_placeholderOrd : 0;

        // Build up a switch which calls each code generation implementation
        //
        BasicBlock* unreachableBlock = BasicBlock::Create(ctx, "", m_bodyFn);
        new UnreachableInst(ctx, unreachableBlock);

        SwitchInst* si = SwitchInst::Create(effectOrd,
                                            unreachableBlock /*default*/,
                                            static_cast<uint32_t>(e.m_effectFnMain.size()) /*numCasesHint*/,
                                            createIcLogicInsertionPt);

        for (size_t k = 0; k < e.m_effectFnMain.size(); k++)
        {
            BasicBlock* cgBB = BasicBlock::Create(ctx, "", m_bodyFn);            
            ConstantInt* caseInt = dyn_cast<ConstantInt>(CreateLLVMConstantInt<uint8_t>(ctx, SafeIntegerCast<uint8_t>(k)));
            ReleaseAssert(caseInt != nullptr);
            si->addCase(caseInt, cgBB);

            size_t icEffectGlobalOrd = globalIcEffectTraitBaseOrd + e.m_effectStartOrdinal + k;
            ReleaseAssert(icEffectGlobalOrd <= 65535);

            std::string isIcQualifyForInlineSlabFnName = x_jit_check_generic_ic_fits_in_inline_slab_placeholder_fn_prefix + std::to_string(icEffectGlobalOrd);
            ReleaseAssert(module->getNamedValue(isIcQualifyForInlineSlabFnName) == nullptr);
            FunctionType* isIcQualifyForInlineSlabFty = FunctionType::get(llvm_type_of<bool>(ctx), { llvm_type_of<void*>(ctx) }, false /*isVarArg*/);
            Function* isIcQualifyForInlineSlabFn = Function::Create(isIcQualifyForInlineSlabFty, GlobalValue::ExternalLinkage, isIcQualifyForInlineSlabFnName, module);
            ReleaseAssert(isIcQualifyForInlineSlabFn->getName() == isIcQualifyForInlineSlabFnName);
            isIcQualifyForInlineSlabFn->setDSOLocal(true);
            isIcQualifyForInlineSlabFn->addFnAttr(Attribute::NoUnwind);
            isIcQualifyForInlineSlabFn->addRetAttr(Attribute::ZExt);

            CallInst* shouldPopulateInlineSlab = CallInst::Create(isIcQualifyForInlineSlabFn, { icSite }, "", cgBB);
            shouldPopulateInlineSlab->addRetAttr(Attribute::ZExt);

            BasicBlock* inlineSlabBB = BasicBlock::Create(ctx, "", m_bodyFn);
            BasicBlock* outlineStubBB = BasicBlock::Create(ctx, "", m_bodyFn);

            BranchInst::Create(inlineSlabBB, outlineStubBB, shouldPopulateInlineSlab, cgBB);

            // Create logic for generating an outlined IC case
            //
            {
                Instruction* insPt = BranchInst::Create(afterIcCreationBB /*dest*/, outlineStubBB /*insertAtEnd*/);

                // Call GenericInlineCacheSite::Insert, which allocates space of the IC and do all the bookkeeping
                //
                Constant* icEffectGlobalOrdCst = CreateLLVMConstantInt<uint16_t>(ctx, static_cast<uint16_t>(icEffectGlobalOrd));
                Value* jitAddr = CreateCallToDeegenCommonSnippet(module, "CreateNewJitGenericIC", { icSite, icEffectGlobalOrdCst }, insPt);
                ReleaseAssert(llvm_value_has_type<void*>(jitAddr));

                emitCallToCodegenFnImpl(icEffectGlobalOrd, false /*isPopulatingInlineSlab*/, jitAddr, insPt);
            }

            // Create logic for generating the inline slab
            //
            {
                Instruction* insPt = BranchInst::Create(afterIcCreationBB /*dest*/, inlineSlabBB /*insertAtEnd*/);
                emitCallToCodegenFnImpl(icEffectGlobalOrd, true /*isPopulatingInlineSlab*/, nullptr /*jitAddr*/, insPt);
            }

            r.m_effectPlaceholderDesc.push_back({
                .m_globalOrd = icEffectGlobalOrd,
                .m_placeholderStart = icStateValStartOrd,
                .m_numPlaceholders = e.m_icStateVals.size()
            });
        }

        createIcLogicInsertionPt->eraseFromParent();
        createIcLogicInsertionPt = nullptr;

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

    ValidateLLVMModule(module);

    return r;
}

std::vector<AstInlineCache::BaselineJitAsmTransformResult> WARN_UNUSED AstInlineCache::DoAsmTransformForBaselineJit(X64AsmFile* file)
{
    std::unordered_map<uint64_t /*uniqOrd*/, BaselineJitAsmTransformResult> finalRes;

    auto processOne = [&](X64AsmBlock* block, size_t lineOrd)
    {
        ReleaseAssert(lineOrd < block->m_lines.size());
        ReleaseAssert(block->m_lines[lineOrd].IsMagicInstructionOfKind(MagicAsmKind::GenericIcEntry));
        AsmMagicPayload* payload = block->m_lines[lineOrd].m_magicPayload;
        // The IC magic has the following payload:
        //     movl $uniq_id, eax
        //     movl $padding_needed, eax
        //     check ic hit
        //     jne <every ic effect>...
        //
        // and fallthroughs to the IC miss block
        //

        // Decode a 'movl $XXX, eax' line to get XXX
        //
        auto getOperandValFromMagicPayloadLine = [&](X64AsmLine& line) WARN_UNUSED -> uint64_t
        {
            ReleaseAssert(line.NumWords() == 3 && line.GetWord(0) == "movl" && line.GetWord(2) == "eax");
            std::string s = line.GetWord(1);
            ReleaseAssert(s.starts_with("$") && s.ends_with(","));
            int val = StoiOrFail(s.substr(1, s.length() - 2));
            ReleaseAssert(val >= 0);
            return static_cast<uint64_t>(val);
        };

        ReleaseAssert(payload->m_lines.size() > 2);
        uint64_t icUniqueOrd = getOperandValFromMagicPayloadLine(payload->m_lines[0]);
        uint64_t smcRegionNopPaddingLen = getOperandValFromMagicPayloadLine(payload->m_lines[1]);

        size_t icEffectBeginLine = 2;
        std::vector<X64AsmLine> checkIcHitLogic;
        while (icEffectBeginLine < payload->m_lines.size())
        {
            if (payload->m_lines[icEffectBeginLine].IsConditionalJumpInst())
            {
                break;
            }
            checkIcHitLogic.push_back(payload->m_lines[icEffectBeginLine]);
            icEffectBeginLine++;
        }
        ReleaseAssert(checkIcHitLogic.size() == 1 || checkIcHitLogic.size() == 2);

        std::vector<std::string> effectEntryLabels;
        for (size_t i = icEffectBeginLine; i < payload->m_lines.size(); i++)
        {
            X64AsmLine& line = payload->m_lines[i];
            ReleaseAssert(line.IsConditionalJumpInst());
            ReleaseAssert(line.NumWords() == 2 && line.GetWord(0) == "jne");
            effectEntryLabels.push_back(line.GetWord(1));
        }

        // Figure out the slow path block
        //
        std::string icMissBlockLabel;
        ReleaseAssert(lineOrd + 1 < block->m_lines.size());
        if (lineOrd + 1 == block->m_lines.size() - 1)
        {
            // This means that the ASM is followed by a jump to the slow path block, no need to do anything
            //
            ReleaseAssert(block->m_lines.back().IsDirectUnconditionalJumpInst());
            ReleaseAssert(block->m_endsWithJmpToLocalLabel);
            icMissBlockLabel = block->m_terminalJmpTargetLabel;
        }
        else
        {
            // We shall split the block
            //
            X64AsmBlock* pred = nullptr;
            X64AsmBlock* slowPathBlock = nullptr;
            block->SplitAtLine(file, lineOrd + 1, pred /*out*/, slowPathBlock /*out*/);
            ReleaseAssert(pred != nullptr && slowPathBlock != nullptr);
            icMissBlockLabel = slowPathBlock->m_normalizedLabelName;

            // Insert the splitted blocks after 'block' and remove 'block'
            //
            file->InsertBlocksAfter({ pred, slowPathBlock }, block);
            file->RemoveBlock(block);
            block = pred;

            file->Validate();
        }

        ReleaseAssert(icMissBlockLabel != "");
        ReleaseAssert(lineOrd + 2 == block->m_lines.size());
        ReleaseAssert(block->m_lines[lineOrd].IsMagicInstructionOfKind(MagicAsmKind::GenericIcEntry));

        // If the ASM is not the first line in the block, we need to split the block before the ASM
        //
        if (lineOrd != 0)
        {
            X64AsmBlock* pred = nullptr;
            X64AsmBlock* succ = nullptr;
            block->SplitAtLine(file, lineOrd, pred /*out*/, succ /*out*/);
            ReleaseAssert(pred != nullptr && succ != nullptr);

            file->InsertBlocksAfter({ pred, succ }, block);
            file->RemoveBlock(block);
            block = succ;

            file->Validate();
        }

        // Now the ASM should be the only content in the block
        //
        ReleaseAssert(block->m_lines.size() == 2);
        ReleaseAssert(block->m_lines[0].IsMagicInstructionOfKind(MagicAsmKind::GenericIcEntry));
        lineOrd = 0;

        // Remove the ASM magic
        //
        block->m_lines[1].m_prefixingText = block->m_lines[0].m_prefixingText + block->m_lines[1].m_prefixingText;
        block->m_lines[0] = block->m_lines[1];
        block->m_lines.pop_back();
        ReleaseAssert(block->m_lines.size() == 1);

        std::string smcBlockLabel = block->m_normalizedLabelName;

        std::string uniqLabel = file->m_labelNormalizer.GetUniqueLabel();
        ReleaseAssert(block->m_trailingLabelLine.IsCommentOrEmptyLine());
        block->m_trailingLabelLine = X64AsmLine::Parse(uniqLabel + ":");
        ReleaseAssert(block->m_trailingLabelLine.IsLocalLabel());

        std::string smcRegionLengthMeasurementSym = file->EmitComputeLabelDistanceAsm(smcBlockLabel /*begin*/, uniqLabel /*end*/);

        if (smcRegionNopPaddingLen > 0)
        {
            std::vector<uint8_t> buf;
            buf.resize(smcRegionNopPaddingLen);
            FillAddressRangeWithX64MultiByteNOPs(buf.data(), smcRegionNopPaddingLen);
            std::string nopString = ".byte ";
            for (size_t i = 0; i < smcRegionNopPaddingLen; i++)
            {
                nopString += std::to_string(buf[i]);
                if (i + 1 < smcRegionNopPaddingLen)
                {
                    nopString += ",";
                }
            }
            nopString += "\n";
            ReleaseAssert(block->m_trailingLabelLine.m_prefixingText == "");
            block->m_trailingLabelLine.m_prefixingText = nopString;
        }

        checkIcHitLogic.push_back(X64AsmLine::Parse("\tjne\t__deegen_cp_placeholder_" + std::to_string(CP_PLACEHOLDER_IC_MISS_DEST)));

        // Figure out all the IC effect entry blocks
        // Must deduplicate, as it is possible that multiple IC effects have identical code and thus gets identical label
        //
        std::unordered_set<X64AsmBlock*> allIcEffectEntryBlocks;
        for (std::string& entryLabel : effectEntryLabels)
        {
            X64AsmBlock* b = file->FindBlockInFastPath(entryLabel);
            ReleaseAssert(b != nullptr);
            allIcEffectEntryBlocks.insert(b);
        }

        // For each IC effect, prepend the check IC hit logic
        //
        for (X64AsmBlock* b : allIcEffectEntryBlocks)
        {
            std::vector<X64AsmLine> newContents = checkIcHitLogic;
            newContents.insert(newContents.end(), b->m_lines.begin(), b->m_lines.end());
            b->m_lines = newContents;
        }

        file->Validate();

        ReleaseAssert(!finalRes.count(icUniqueOrd));
        finalRes[icUniqueOrd] = {
            .m_labelForSMCRegion = smcBlockLabel,
            .m_labelForEffects = effectEntryLabels,
            .m_labelForIcMissLogic = icMissBlockLabel,
            .m_uniqueOrd = icUniqueOrd,
            .m_symbolNameForSMCLabelOffset = "",
            .m_symbolNameForSMCRegionLength = smcRegionLengthMeasurementSym,
            .m_symbolNameForIcMissLogicLabelOffset = ""
        };
    };

    // Process one by one to avoid iterator invalidation problems
    //
    auto findAndProcessOne = [&]() WARN_UNUSED -> bool
    {
        for (X64AsmBlock* block : file->m_blocks)
        {
            for (size_t i = 0; i < block->m_lines.size(); i++)
            {
                if (block->m_lines[i].IsMagicInstructionOfKind(MagicAsmKind::GenericIcEntry))
                {
                    processOne(block, i);
                    return true;
                }
            }
        }
        return false;
    };

    while (findAndProcessOne()) { }

    std::vector<BaselineJitAsmTransformResult> r;

    size_t totalCnt = finalRes.size();
    for (size_t i = 0; i < totalCnt; i++)
    {
        ReleaseAssert(finalRes.count(i));
        r.push_back(finalRes[i]);
    }

    return r;
}

AstInlineCache::BaselineJitCodegenResult WARN_UNUSED AstInlineCache::CreateJitIcCodegenImplementation(BaselineJitImplCreator* ifi,
                                                                                                      BaselineJitLLVMLoweringResult::Item icInfo,
                                                                                                      DeegenStencil& stencil,
                                                                                                      InlineSlabInfo inlineSlabInfo,
                                                                                                      size_t icUsageOrdInBytecode,
                                                                                                      bool isCodegenForInlineSlab)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();

    ReleaseAssertImp(icInfo.m_numPlaceholders > 0, icInfo.m_placeholderStart < ifi->GetNumTotalGenericIcCaptures());
    ReleaseAssert(icInfo.m_placeholderStart + icInfo.m_numPlaceholders <= ifi->GetNumTotalGenericIcCaptures());

    BaselineJitSlowPathDataLayout* slowPathDataLayout = ifi->GetBytecodeDef()->GetBaselineJitSlowPathDataLayout();

    std::string cgFnName = x_jit_codegen_ic_impl_placeholder_fn_prefix + std::to_string(icInfo.m_globalOrd);
    if (isCodegenForInlineSlab)
    {
        cgFnName += "_inline_slab";
    }

    std::vector<size_t> extraPlaceholderOrds {
        CP_PLACEHOLDER_IC_MISS_DEST,
        CP_PLACEHOLDER_GENERIC_IC_KEY
    };
    if (ifi->GetBytecodeDef()->m_hasConditionalBranchTarget)
    {
        extraPlaceholderOrds.push_back(CP_PLACEHOLDER_BYTECODE_CONDBR_DEST);
    }
    DeegenStencilCodegenResult cgRes = stencil.PrintCodegenFunctions(false /*mayAttemptToEliminateJmpToFallthrough*/,
                                                                     ifi->GetBytecodeDef()->m_list.size(),
                                                                     ifi->GetNumTotalGenericIcCaptures(),
                                                                     ifi->GetStencilRcDefinitions(),
                                                                     extraPlaceholderOrds);

    ReleaseAssertImp(isCodegenForInlineSlab, cgRes.m_dataSecPreFixupCode.size() == 0 && cgRes.m_dataSecAlignment == 1);
    ReleaseAssertImp(isCodegenForInlineSlab, cgRes.m_icPathPreFixupCode.size() == inlineSlabInfo.m_smcRegionLength);

    // For simplicity, the IC logic currently always put its private data section right after the code section
    //
    size_t dataSectionOffset;
    {
        dataSectionOffset = cgRes.m_icPathPreFixupCode.size();
        size_t alignment = cgRes.m_dataSecAlignment;
        ReleaseAssert(alignment > 0);
        ReleaseAssert(alignment <= x_baselineJitMaxPossibleDataSectionAlignment);
        // Our codegen allocator allocates 16-byte-aligned memory, so the data section alignment must not exceed that
        //
        ReleaseAssert(alignment <= 16);
        dataSectionOffset = (dataSectionOffset + alignment - 1) / alignment * alignment;
    }

    std::vector<uint8_t> codeAndData;
    {
        codeAndData = cgRes.m_icPathPreFixupCode;
        ReleaseAssert(dataSectionOffset >= codeAndData.size());
        while (codeAndData.size() < dataSectionOffset)
        {
            codeAndData.push_back(0);
        }
        ReleaseAssert(codeAndData.size() == dataSectionOffset);
        codeAndData.insert(codeAndData.end(), cgRes.m_dataSecPreFixupCode.begin(), cgRes.m_dataSecPreFixupCode.end());
    }

    std::unique_ptr<Module> module = cgRes.GenerateCodegenLogicLLVMModule(ifi->GetModule());

    Function* cgCodeFn = module->getFunction(cgRes.x_icPathCodegenFuncName);
    ReleaseAssert(cgCodeFn != nullptr);
    cgCodeFn->addFnAttr(Attribute::AlwaysInline);
    cgCodeFn->setLinkage(GlobalValue::InternalLinkage);

    Function* cgDataFn = module->getFunction(cgRes.x_dataSecCodegenFuncName);
    ReleaseAssert(cgDataFn != nullptr);
    cgDataFn->addFnAttr(Attribute::AlwaysInline);
    cgDataFn->setLinkage(GlobalValue::InternalLinkage);

    // Set up the codegen function prototype, must agree with DoLoweringForBaselineJit
    //
    std::vector<Type*> cgFnArgTys;
    cgFnArgTys.push_back(llvm_type_of<void*>(ctx));     // jitAddr
    cgFnArgTys.push_back(llvm_type_of<void*>(ctx));     // slowPathData
    cgFnArgTys.push_back(llvm_type_of<uint64_t>(ctx));  // icKey
    for (size_t i = 0; i < icInfo.m_numPlaceholders; i++)
    {
        cgFnArgTys.push_back(llvm_type_of<uint64_t>(ctx));
    }

    FunctionType* cgFTy = FunctionType::get(llvm_type_of<void>(ctx), cgFnArgTys, false /*isVarArg*/);
    Function* cgFn = Function::Create(cgFTy, GlobalValue::ExternalLinkage, cgFnName, module.get());
    ReleaseAssert(cgFn->getName() == cgFnName);
    cgFn->setDSOLocal(true);
    cgFn->addFnAttr(Attribute::NoUnwind);
    CopyFunctionAttributes(cgFn /*dst*/, cgCodeFn /*src*/);

    // For codegen inline slab logic, the passed in 'jitAddr' is always nullptr
    //
    if (!isCodegenForInlineSlab)
    {
        cgFn->addParamAttr(0, Attribute::NoAlias);
    }

    cgFn->addParamAttr(1, Attribute::NoAlias);

    BasicBlock* bb = BasicBlock::Create(ctx, "", cgFn);

    Value* slowPathData = cgFn->getArg(1);

    Value* mainLogicFastPath = slowPathDataLayout->m_jitAddr.EmitGetValueLogic(slowPathData, bb);

    // For normal generation, 'destJitAddr' is passed in as first param
    // For inline slab generation, 'destJitAddr' is implicit (always the start address of the SMC region).
    // We still retain the same function prototype for simplicity, but the first param is not used.
    //
    Value* destJitAddr = nullptr;
    if (!isCodegenForInlineSlab)
    {
        destJitAddr = cgFn->getArg(0);
    }
    else
    {
        destJitAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), mainLogicFastPath,
                                                        { CreateLLVMConstantInt<uint64_t>(ctx, inlineSlabInfo.m_smcRegionOffset) }, "", bb);
    }

    Value* icKeyI64 = cgFn->getArg(2);
    std::vector<Value*> inputPlaceholders;
    for (size_t i = 3; i < cgFn->arg_size(); i++)
    {
        inputPlaceholders.push_back(cgFn->getArg(static_cast<uint32_t>(i)));
    }
    ReleaseAssert(inputPlaceholders.size() == icInfo.m_numPlaceholders);

    // Set up the bytecode operands argument list
    //
    // TODO: The logic below asserts that SlowPathDataOffset and BaselineCodeBlock are unused.
    // However, this assert might not hold, as the IC logic might use BaseineCodeBlock32 and SlowPathDataOffset if it tail-duplicated the logic
    // So to support all cases, we need to pass BaselineCodeBlock to IC body
    //
    std::vector<Value*> bytecodeValList = DeegenStencilCodegenResult::BuildBytecodeOperandVectorFromSlowPathData(DeegenEngineTier::BaselineJIT,
                                                                                                                 ifi->GetBytecodeDef(),
                                                                                                                 slowPathData,
                                                                                                                 nullptr /*slowPathDataOffset, must unused*/,
                                                                                                                 nullptr /*baselineCodeBlock32, must unused*/,
                                                                                                                 bb /*insertAtEnd*/);

    // Set up the placeholder argument list
    // The codegen logic should only need [icInfo.m_placeholderStart, icInfo.m_placeholderStart + icInfo.m_numPlaceholders),
    // everything else is nullptr
    //
    std::vector<Value*> placeholderList;
    for (size_t i = 0; i < ifi->GetNumTotalGenericIcCaptures(); i++)
    {
        if (icInfo.m_placeholderStart <= i && i < icInfo.m_placeholderStart + icInfo.m_numPlaceholders)
        {
            placeholderList.push_back(inputPlaceholders[i - icInfo.m_placeholderStart]);
        }
        else
        {
            placeholderList.push_back(nullptr);
        }
    }

    // Set up the common header arguments:
    //     uint64_t fastPathAddr
    //     uint64_t slowPathAddr
    //     uint64_t icPathAddr
    //     uint64_t icDataSecAddr
    //     uint64_t dataSecAddr
    //
    std::vector<Value*> headerArgsList;
    headerArgsList.push_back(new PtrToIntInst(mainLogicFastPath, llvm_type_of<uint64_t>(ctx), "", bb));
    Value* mainLogicSlowPath = slowPathDataLayout->m_jitSlowPathAddr.EmitGetValueLogic(slowPathData, bb);
    headerArgsList.push_back(new PtrToIntInst(mainLogicSlowPath, llvm_type_of<uint64_t>(ctx), "", bb));
    headerArgsList.push_back(new PtrToIntInst(destJitAddr, llvm_type_of<uint64_t>(ctx), "", bb));
    Value* destJitDataSecAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), destJitAddr,
                                                                  { CreateLLVMConstantInt<uint64_t>(ctx, dataSectionOffset) }, "", bb);
    headerArgsList.push_back(new PtrToIntInst(destJitDataSecAddr, llvm_type_of<uint64_t>(ctx), "", bb));
    Value* mainLogicDataSec = slowPathDataLayout->m_jitDataSecAddr.EmitGetValueLogic(slowPathData, bb);
    headerArgsList.push_back(new PtrToIntInst(mainLogicDataSec, llvm_type_of<uint64_t>(ctx), "", bb));

    ReleaseAssert(inlineSlabInfo.m_smcRegionLength >= 5);
    Value* patchableJmpEndAddr = nullptr;

    // We must special-check for 'isCodegenForInlineSlab', because we are called after 'm_isInlineSlabUsed' has been set to true
    //
    if (isCodegenForInlineSlab)
    {
        // We are generating the inline slab, so the SMC region is in the initial jmp + nop form
        //
        ReleaseAssert(inlineSlabInfo.m_hasInlineSlab);
        patchableJmpEndAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), mainLogicFastPath,
                                                                { CreateLLVMConstantInt<uint64_t>(ctx, inlineSlabInfo.m_smcRegionOffset + 5) }, "", bb);
    }
    else if (!inlineSlabInfo.m_hasInlineSlab)
    {
        // No inline slab is possible for this IC, no need to check anything. The SMC region is in the initial jmp + nop form
        //
        patchableJmpEndAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), mainLogicFastPath,
                                                                { CreateLLVMConstantInt<uint64_t>(ctx, inlineSlabInfo.m_smcRegionOffset + 5) }, "", bb);
    }
    else
    {
        // Inline slab is possible, and we are generating an outlined IC case.
        // We need to check if an inline slab exists
        //
        Value* isInlineSlabUsed = nullptr;
        {
            size_t offset = ifi->GetBytecodeDef()->GetBaselineJitSlowPathDataLayout()->m_genericICs.GetOffsetForSite(icUsageOrdInBytecode);
            Value* icSite = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathData,
                                                              { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", bb);

            Value* isInlineSlabUsedAddr = GetElementPtrInst::CreateInBounds(
                llvm_type_of<uint8_t>(ctx), icSite,
                {
                    CreateLLVMConstantInt<uint64_t>(ctx, offsetof_member_v<&JitGenericInlineCacheSite::m_isInlineSlabUsed>)
                }, "", bb);
            static_assert(std::is_same_v<uint8_t, typeof_member_t<&JitGenericInlineCacheSite::m_isInlineSlabUsed>>);
            Value* isInlineSlabUsedU8 = new LoadInst(llvm_type_of<uint8_t>(ctx), isInlineSlabUsedAddr, "", false /*isVolatile*/, Align(1), bb);
            isInlineSlabUsed = new ICmpInst(*bb, ICmpInst::ICMP_NE, isInlineSlabUsedU8, CreateLLVMConstantInt<uint8_t>(ctx, 0));
        }

        ReleaseAssert(llvm_value_has_type<bool>(isInlineSlabUsed));

        Value* patchableJumpEndOffset = SelectInst::Create(
            isInlineSlabUsed,
            CreateLLVMConstantInt<uint64_t>(ctx, inlineSlabInfo.m_inlineSlabPatchableJumpEndOffsetInFastPath),
            CreateLLVMConstantInt<uint64_t>(ctx, inlineSlabInfo.m_smcRegionOffset + 5),
            "",
            bb);

        patchableJmpEndAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), mainLogicFastPath,
                                                                { patchableJumpEndOffset }, "", bb);
    }
    ReleaseAssert(patchableJmpEndAddr != nullptr && llvm_value_has_type<void*>(patchableJmpEndAddr));

    Value* missDestForThisIc = X64PatchableJumpUtil::GetDest(patchableJmpEndAddr, bb);
    Value* missDestForThisIcI64 = new PtrToIntInst(missDestForThisIc, llvm_type_of<uint64_t>(ctx), "", bb);

    Value* condBrDest = nullptr;
    if (ifi->GetBytecodeDef()->m_hasConditionalBranchTarget)
    {
        condBrDest = slowPathDataLayout->m_condBrJitAddr.EmitGetValueLogic(slowPathData, bb);
        ReleaseAssert(condBrDest != nullptr);
    }

    // Set up the extra placeholder arguments
    //
    auto getValueForExtraPlacehoder = [&](size_t extraPlaceholderOrd) WARN_UNUSED -> Value*
    {
        switch (extraPlaceholderOrd)
        {
        case CP_PLACEHOLDER_IC_MISS_DEST:
        {
            return missDestForThisIcI64;
        }
        case CP_PLACEHOLDER_GENERIC_IC_KEY:
        {
            return icKeyI64;
        }
        case CP_PLACEHOLDER_BYTECODE_CONDBR_DEST:
        {
            ReleaseAssert(condBrDest != nullptr);
            return condBrDest;
        }
        default:
        {
            ReleaseAssert(false);
        }
        }   /* switch extraPlaceholderOrd */
    };

    std::vector<Value*> extraPlaceholderArgsList;
    for (size_t extraPlaceholderOrd : extraPlaceholderOrds)
    {
        extraPlaceholderArgsList.push_back(getValueForExtraPlacehoder(extraPlaceholderOrd));
    }

    // Piece together everything
    //
    std::vector<Value*> allArgsExceptFirst;
    allArgsExceptFirst = headerArgsList;
    allArgsExceptFirst.insert(allArgsExceptFirst.end(), bytecodeValList.begin(), bytecodeValList.end());
    allArgsExceptFirst.insert(allArgsExceptFirst.end(), placeholderList.begin(), placeholderList.end());
    allArgsExceptFirst.insert(allArgsExceptFirst.end(), extraPlaceholderArgsList.begin(), extraPlaceholderArgsList.end());

    {
        auto validateArgs = [&](Function* target)
        {
            ReleaseAssert(allArgsExceptFirst.size() + 1 == target->arg_size());
            for (size_t i = 0; i < allArgsExceptFirst.size(); i++)
            {
                Value* arg = allArgsExceptFirst[i];
                Argument* expectArg = target->getArg(static_cast<uint32_t>(i + 1));
                if (arg == nullptr)
                {
                    if (!expectArg->use_empty())
                    {
                        cgCodeFn->print(dbgs());
                        std::cout << std::endl << std::endl << (i + 2) << std::endl << std::endl;
                    }
                    ReleaseAssert(expectArg->use_empty());
                }
                else
                {
                    ReleaseAssert(arg->getType() == expectArg->getType());
                }
            }
        };

        validateArgs(cgCodeFn);
        validateArgs(cgDataFn);

        for (size_t i = 0; i < allArgsExceptFirst.size(); i++)
        {
            if (allArgsExceptFirst[i] == nullptr)
            {
                allArgsExceptFirst[i] = UndefValue::get(llvm_type_of<uint64_t>(ctx));
            }
        }

        validateArgs(cgCodeFn);
        validateArgs(cgDataFn);
    }

    ReleaseAssertImp(isCodegenForInlineSlab, codeAndData.size() == inlineSlabInfo.m_smcRegionLength);

    // Memcpy the unrelocated code and data
    //
    {
        // If we are generating the inline slab, we must not clobber anything after the range since they contain valid code.
        // If we are generating an outlined IC case, since the allocated region is always 16-byte aligned, it's safe to up-align to 8-byte
        //
        bool mustBeExact = isCodegenForInlineSlab;
        EmitCopyLogicForBaselineJitCodeGen(module.get(), codeAndData, destJitAddr, "deegen_ic_pre_fixup_code_and_data", bb, mustBeExact);
    }

    // Create calls to patch the code section and data section
    //
    auto emitPatchFnCall = [&](Function* target, Value* dstAddr)
    {
        ReleaseAssert(llvm_value_has_type<void*>(dstAddr));
        std::vector<Value*> args;
        args.push_back(dstAddr);
        args.insert(args.end(), allArgsExceptFirst.begin(), allArgsExceptFirst.end());
        CallInst::Create(target, args, "", bb);
    };

    emitPatchFnCall(cgCodeFn, destJitAddr);
    emitPatchFnCall(cgDataFn, destJitDataSecAddr);

    // Update SMC region to jump to the newly created IC case
    // We only (and must only) do this for the outlined IC case. If we are generating the inline slab, the patchable jump is already overwritten.
    //
    if (!isCodegenForInlineSlab)
    {
        X64PatchableJumpUtil::SetDest(patchableJmpEndAddr, destJitAddr, bb);
    }

    ReturnInst::Create(ctx, nullptr, bb);

    RunLLVMOptimizePass(module.get());
    ReleaseAssert(module->getFunction(cgFnName) != nullptr);

    for (Function& fn : *module.get())
    {
        if (!fn.empty())
        {
            ReleaseAssert(fn.getName() == cgFnName);
        }
    }

    std::string disasmForAudit;
    {
        disasmForAudit = DumpStencilDisassemblyForAuditPurpose(
            stencil.m_triple, false /*isDataSection*/, cgRes.m_icPathPreFixupCode, cgRes.m_icPathRelocMarker, "# " /*linePrefix*/);

        if (cgRes.m_dataSecPreFixupCode.size() > 0)
        {
            disasmForAudit += std::string("#\n# Data Section:\n");
            disasmForAudit += DumpStencilDisassemblyForAuditPurpose(
                stencil.m_triple, true /*isDataSection*/, cgRes.m_dataSecPreFixupCode, cgRes.m_dataSecRelocMarker, "# " /*linePrefix*/);
        }
        disasmForAudit += "\n";
    }

    return {
        .m_module = std::move(module),
        .m_resultFnName = cgFnName,
        .m_icSize = codeAndData.size(),
        .m_disasmForAudit = disasmForAudit
    };
}

void AstInlineCache::AttemptIrRewriteToManuallyTailDuplicateSimpleIcCases(llvm::Function* func, std::string fallthroughPlaceholderName)
{
    using namespace llvm;

    std::vector<BasicBlock*> icEntryBBList;
    for (BasicBlock& bb : *func)
    {
        for (Instruction& inst : bb)
        {
            if (MagicAsm::IsMagicOfKind(&inst, MagicAsmKind::DummyAsmToPreventIcEntryBBMerge))
            {
                icEntryBBList.push_back(&bb);
                break;
            }
        }
    }

    std::vector<BasicBlock*> rewriteTargetBBList;
    for (BasicBlock* targetBB : icEntryBBList)
    {
        Instruction* terminatorInst = targetBB->getTerminator();
        if (!isa<BranchInst>(terminatorInst))
        {
            continue;
        }

        BranchInst* brInst = cast<BranchInst>(terminatorInst);
        if (brInst->isConditional())
        {
            continue;
        }

        // Now we know 'targetBB' ends with an unconditional branch to 'bb'.
        //
        BasicBlock* bb = brInst->getSuccessor(0);

        // Do not tail duplicate if 'bb' is too large
        //
        if (bb->getInstList().size() > 12)
        {
            continue;
        }

        // Check if its destination block is a terminal BB that dispatches to the next bytecode
        //
        CallInst* ci = bb->getTerminatingMustTailCall();
        if (ci == nullptr)
        {
            continue;
        }

        Value* calledOp = ci->getCalledOperand();
        if (!isa<GlobalValue>(calledOp))
        {
            continue;
        }
        GlobalValue* gv = cast<GlobalValue>(calledOp);
        if (gv->getName() == fallthroughPlaceholderName)
        {
            rewriteTargetBBList.push_back(targetBB);
        }
    }

    size_t dummyOrd = 1000000;

    for (BasicBlock* targetBB : rewriteTargetBBList)
    {
        BranchInst* term = dyn_cast<BranchInst>(targetBB->getTerminator());
        ReleaseAssert(!term->isConditional());
        BasicBlock* destBB = term->getSuccessor(0);

        std::unordered_map<Value*, Value*> remap;
        for (Instruction& inst : *destBB)
        {
            if (isa<PHINode>(&inst))
            {
                PHINode* phi = cast<PHINode>(&inst);
                int phiBlockIdx = phi->getBasicBlockIndex(targetBB);
                ReleaseAssert(phiBlockIdx != -1);
                Value* incomingValue = phi->getIncomingValue(static_cast<uint32_t>(phiBlockIdx));
                // Must not delete PHI even if empty, otherwise the remapping won't work!
                //
                phi->removeIncomingValue(targetBB, false /*deletePhiIfEmpty*/);

                ReleaseAssert(!remap.count(phi));
                remap[phi] = incomingValue;
            }
            else
            {
                Instruction* clonedInst = inst.clone();
                for (auto& op : clonedInst->operands())
                {
                    Value* opVal = op.get();
                    if (remap.count(opVal))
                    {
                        op.set(remap[opVal]);
                    }
                }

                ReleaseAssert(!remap.count(&inst));
                remap[&inst] = clonedInst;
                clonedInst->insertBefore(term);
            }
        }

        term->eraseFromParent();

        CallInst* ci = destBB->getTerminatingMustTailCall();
        ReleaseAssert(ci != nullptr);

        // Prevent LLVM from merging the blocks at machine codegen level again
        //
        {
            std::string asmString = "movl $$" + std::to_string(dummyOrd) + ", eax;";
            dummyOrd++;
            asmString = MagicAsm::WrapLLVMAsmPayload(asmString, MagicAsmKind::DummyAsmToPreventIcEntryBBMerge);
            FunctionType* fty = FunctionType::get(llvm_type_of<void>(ci->getContext()), { }, false);
            InlineAsm* ia = InlineAsm::get(fty, asmString, "" /*constraints*/, true /*hasSideEffects*/);
            CallInst* inst = CallInst::Create(ia, "", ci /*insertBefore*/);
            inst->addFnAttr(Attribute::NoUnwind);
        }
    }

    ValidateLLVMFunction(func);

    EliminateUnreachableBlocks(*func);
    ValidateLLVMFunction(func);
}

AstInlineCache::BaselineJitFinalLoweringResult WARN_UNUSED AstInlineCache::DoLoweringAfterAsmTransform(
    BytecodeIrInfo* bii,
    BaselineJitImplCreator* ifi,
    std::unique_ptr<llvm::Module> icBodyModule,
    std::vector<BaselineJitLLVMLoweringResult>& icLLRes,
    std::vector<BaselineJitAsmLoweringResult>& icAsmRes,
    DeegenStencil& mainStencil,
    const DeegenGlobalBytecodeTraitAccessor& gbta)
{
    using namespace llvm;

    LLVMContext& ctx = ifi->GetModule()->getContext();
    ReleaseAssert(icAsmRes.size() == icLLRes.size());
    ReleaseAssertIff(icLLRes.size() == 0, icBodyModule.get() == nullptr);

    BaselineJitFinalLoweringResult finalRes;
    if (icLLRes.size() == 0)
    {
        return finalRes;
    }

    bool hasFastPathReturnContinuation = false;
    for (auto& rc : bii->m_allRetConts)
    {
        if (!rc->IsReturnContinuationUsedBySlowPathOnly())
        {
            hasFastPathReturnContinuation = true;
        }
    }

    size_t fallthroughPlaceholderOrd = DeegenPlaceholderUtils::FindFallthroughPlaceholderOrd(ifi->GetStencilRcDefinitions());

    std::map<uint64_t /*globalOrd*/, uint64_t /*icSize*/> genericIcSizeMap;
    std::vector<std::string> genericIcAuditInfo;
    size_t globalIcTraitOrdBase = gbta.GetGenericIcEffectTraitBaseOrdinal(ifi->GetBytecodeDef()->GetBytecodeIdName());
    for (size_t icUsageOrd = 0; icUsageOrd < icAsmRes.size(); icUsageOrd++)
    {
        std::string auditInfo;
        BaselineJitAsmLoweringResult& slRes = icAsmRes[icUsageOrd];
        BaselineJitLLVMLoweringResult& llRes = icLLRes[icUsageOrd];
        ReleaseAssert(slRes.m_uniqueOrd == icUsageOrd);

        size_t smcRegionOffset = mainStencil.RetrieveLabelDistanceComputationResult(slRes.m_symbolNameForSMCLabelOffset);
        size_t smcRegionLen = mainStencil.RetrieveLabelDistanceComputationResult(slRes.m_symbolNameForSMCRegionLength);
        size_t icMissSlowPathOffset = mainStencil.RetrieveLabelDistanceComputationResult(slRes.m_symbolNameForIcMissLogicLabelOffset);

        ReleaseAssert(smcRegionLen == 5);

        // Figure out if the IC may qualify for inline slab optimization
        // For now, for simplicity, we only enable inline slab optimization if the SMC region is at the tail position,
        // and the bytecode has no fast path return continuations (in other words, the SMC region can fallthrough
        // directly to the next bytecode). This is only for simplicity because that's all the use case we have for now.
        //
        bool shouldConsiderInlineSlabOpt = !hasFastPathReturnContinuation;
        ReleaseAssert(smcRegionOffset < mainStencil.m_fastPathCode.size());
        ReleaseAssert(smcRegionOffset + smcRegionLen <= mainStencil.m_fastPathCode.size());
        if (smcRegionOffset + smcRegionLen != mainStencil.m_fastPathCode.size())
        {
            shouldConsiderInlineSlabOpt = false;
        }

        size_t ultimateInlineSlabSize = static_cast<size_t>(-1);
        size_t inlineSlabIcMissPatchableJumpEndOffsetInFastPath = static_cast<size_t>(-1);
        std::map<uint64_t /*ordInLLRes*/, DeegenStencil> allInlineSlabEffects;

        if (shouldConsiderInlineSlabOpt)
        {
            std::unordered_set<uint64_t /*ordInLLRes*/> effectOrdsWithTailJumpRemoved;
            for (size_t k = 0; k < llRes.m_effectPlaceholderDesc.size(); k++)
            {
                // TODO: We should attempt to transform the logic so that it fallthroughs to the next bytecode
                //
                std::string icLogicObjFile = CompileAssemblyFileToObjectFile(slRes.m_icLogicAsm[k], " -fno-pic -fno-pie ");
                DeegenStencil icStencil = DeegenStencil::ParseIcLogic(ctx, icLogicObjFile, mainStencil.m_sectionToPdoOffsetMap);

                if (icStencil.m_privateDataObject.m_bytes.size() > 0)
                {
                    // The stencil has a data section, don't consider it for inline slab
                    //
                    continue;
                }

                // Figure out if the stencil ends with a jump to the next bytecode, if yes, strip it
                //
                ReleaseAssert(icStencil.m_icPathCode.size() >= 5);
                if (fallthroughPlaceholderOrd != static_cast<size_t>(-1))
                {
                    bool found = false;
                    std::vector<uint8_t>& code = icStencil.m_icPathCode;
                    std::vector<RelocationRecord>& relos = icStencil.m_icPathRelos;
                    std::vector<RelocationRecord>::iterator relocToRemove = relos.end();
                    for (auto it = relos.begin(); it != relos.end(); it++)
                    {
                        RelocationRecord& rr = *it;
                        if (rr.m_offset == code.size() - 4 &&
                            (rr.m_relocationType == ELF::R_X86_64_PLT32 || rr.m_relocationType == ELF::R_X86_64_PC32) &&
                            rr.m_symKind == RelocationRecord::SymKind::StencilHole &&
                            rr.m_stencilHoleOrd == fallthroughPlaceholderOrd)
                        {
                            ReleaseAssert(rr.m_addend == -4);
                            ReleaseAssert(!found);
                            found = true;
                            relocToRemove = it;
                        }
                    }
                    if (found)
                    {
                        ReleaseAssert(relocToRemove != relos.end());
                        relos.erase(relocToRemove);
                        ReleaseAssert(code[code.size() - 5] == 0xe9 /*jmp*/);
                        code.resize(code.size() - 5);

                        ReleaseAssert(!effectOrdsWithTailJumpRemoved.count(k));
                        effectOrdsWithTailJumpRemoved.insert(k);
                    }
                }

                ReleaseAssert(!allInlineSlabEffects.count(k));
                allInlineSlabEffects[k] = std::move(icStencil);
            }

            // Determine whether we shall enable inline slab, and if yes, its size
            //
            // Currently the heuristic is as follows:
            // 1. At least 60% of the total # effects must fit in the inline slab.
            // 2. We find the smallest size that satisfies (1), say S.
            // 3. If S > maxS, give up. Currently maxS = 64.
            // 4. Set T = min(S + tolerance_factor, maxS), where currently tolerance_factor = 12.
            //    This allows IC slightly larger than the threshold to fit in the inline slab as well.
            // 5. Set final inline slab size to be the max IC size <= T.
            //
            const double x_inlineSlabMinCoverage = 0.6;
            const size_t x_maxAllowedInlineSlabSize = 64;
            const size_t x_inlineSlabExtraToleranceBytes = 12;

            size_t minEffectsToCover = static_cast<size_t>(static_cast<double>(llRes.m_effectPlaceholderDesc.size()) * x_inlineSlabMinCoverage);
            if (minEffectsToCover < 1) { minEffectsToCover = 1; }
            if (allInlineSlabEffects.size() < minEffectsToCover)
            {
                // The # of IC effects qualified for inline slab is not sufficient, give up
                //
                allInlineSlabEffects.clear();
            }
            else
            {
                std::vector<uint64_t> icSizeList;
                for (auto& it : allInlineSlabEffects)
                {
                    icSizeList.push_back(it.second.m_icPathCode.size());
                }
                std::sort(icSizeList.begin(), icSizeList.end());

                ReleaseAssert(icSizeList.size() >= minEffectsToCover);
                size_t inlineSlabSize = icSizeList[minEffectsToCover - 1];

                // The inline slab still must at least be able to accomodate a patchable jump
                // This should be trivially true, because the IC check/branch miss logic is already longer than that
                //
                ReleaseAssert(inlineSlabSize >= 5);

                if (inlineSlabSize > x_maxAllowedInlineSlabSize)
                {
                    // Give up
                    //
                    allInlineSlabEffects.clear();
                }
                else
                {
                    inlineSlabSize = std::min(inlineSlabSize + x_inlineSlabExtraToleranceBytes, x_maxAllowedInlineSlabSize);

                    size_t maxIcSizeInInlineSlab = 0;
                    for (size_t icSize : icSizeList)
                    {
                        if (icSize <= inlineSlabSize)
                        {
                            maxIcSizeInInlineSlab = std::max(maxIcSizeInInlineSlab, icSize);
                        }
                    }
                    inlineSlabSize = maxIcSizeInInlineSlab;

                    // Remove all IC that won't fit in the inline slab
                    //
                    {
                        std::vector<uint64_t> icIndexToRemove;
                        for (auto& it : allInlineSlabEffects)
                        {
                            if (it.second.m_icPathCode.size() > inlineSlabSize)
                            {
                                icIndexToRemove.push_back(it.first);
                            }
                        }

                        for (uint64_t indexToRemove : icIndexToRemove)
                        {
                            ReleaseAssert(allInlineSlabEffects.count(indexToRemove));
                            allInlineSlabEffects.erase(allInlineSlabEffects.find(indexToRemove));
                        }
                    }

                    // Sanity check the decision makes sense
                    //
                    ReleaseAssert(inlineSlabSize >= 5);
                    ReleaseAssert(inlineSlabSize <= x_maxAllowedInlineSlabSize);
                    ReleaseAssert(allInlineSlabEffects.size() >= minEffectsToCover);

                    // Pad nops to make the code length equal to the inline slab size
                    //
                    for (auto& it : allInlineSlabEffects)
                    {
                        std::vector<uint8_t>& code = it.second.m_icPathCode;

                        ReleaseAssert(code.size() <= inlineSlabSize);
                        size_t nopBegin = code.size();
                        size_t bytesToFill = inlineSlabSize - nopBegin;
                        code.resize(inlineSlabSize);

                        if (bytesToFill >= 16 && effectOrdsWithTailJumpRemoved.count(it.first))
                        {
                            // The NOP sequence will actually be executed as we removed the tail jump.
                            // Executing >= 16 bytes of NOP might not make sense (I forgot the exact source but I believe
                            // this is the threshold used by GCC). So in that case do a short jump directly.
                            //
                            uint8_t* buf = code.data() + nopBegin;
                            buf[0] = 0xeb;      // imm8 jmp
                            ReleaseAssert(2 <= bytesToFill && bytesToFill <= 129);
                            buf[1] = SafeIntegerCast<uint8_t>(bytesToFill - 2);
                            nopBegin += 2;
                            bytesToFill -= 2;
                        }

                        FillAddressRangeWithX64MultiByteNOPs(code.data() + nopBegin, bytesToFill);
                    }

                    // Figure out the location of the patchable jump for IC miss in the inline slab
                    // They are guaranteed to have the same offset in any IC effect due to how we generated the logic
                    //
                    size_t patchableJmpOperandLocInIcPath = static_cast<size_t>(-1);
                    for (auto& it : allInlineSlabEffects)
                    {
                        std::vector<RelocationRecord>& relocs = it.second.m_icPathRelos;
                        size_t offset = static_cast<size_t>(-1);
                        for (RelocationRecord& rr : relocs)
                        {
                            if (rr.m_symKind == RelocationRecord::SymKind::StencilHole &&
                                rr.m_stencilHoleOrd == CP_PLACEHOLDER_IC_MISS_DEST)
                            {
                                ReleaseAssert(rr.m_relocationType == ELF::R_X86_64_PLT32 || rr.m_relocationType == ELF::R_X86_64_PC32);
                                ReleaseAssert(rr.m_addend == -4);
                                ReleaseAssert(offset == static_cast<size_t>(-1));
                                offset = rr.m_offset;
                                ReleaseAssert(offset != static_cast<size_t>(-1));
                            }
                        }
                        ReleaseAssert(offset != static_cast<size_t>(-1));
                        if (patchableJmpOperandLocInIcPath == static_cast<size_t>(-1))
                        {
                            patchableJmpOperandLocInIcPath = offset;
                        }
                        else
                        {
                            ReleaseAssert(patchableJmpOperandLocInIcPath == offset);
                        }
                    }
                    ReleaseAssert(patchableJmpOperandLocInIcPath != static_cast<size_t>(-1));

                    // Pad NOPs for the main logic's SMC region
                    // Since we already asserted that the SMC region is at the end of the function,
                    // it's simply populating NOPs at the end, as it won't break any jump targets etc
                    //
                    {
                        std::vector<uint8_t>& code = mainStencil.m_fastPathCode;

                        ReleaseAssert(inlineSlabSize >= smcRegionLen);
                        ReleaseAssert(smcRegionOffset + smcRegionLen == code.size());

                        size_t numBytesToPad = inlineSlabSize - smcRegionLen;
                        size_t offsetStart = code.size();
                        code.resize(offsetStart + numBytesToPad);
                        FillAddressRangeWithX64MultiByteNOPs(code.data() + offsetStart, numBytesToPad);

                        smcRegionLen = inlineSlabSize;
                        ReleaseAssert(smcRegionOffset + smcRegionLen == code.size());
                    }

                    // Declare success
                    //
                    ultimateInlineSlabSize = inlineSlabSize;
                    inlineSlabIcMissPatchableJumpEndOffsetInFastPath = smcRegionOffset + patchableJmpOperandLocInIcPath + 4;

                    ReleaseAssert(smcRegionOffset < inlineSlabIcMissPatchableJumpEndOffsetInFastPath - 4);
                    ReleaseAssert(inlineSlabIcMissPatchableJumpEndOffsetInFastPath <= smcRegionOffset + smcRegionLen);
                }
            }
        }

        ReleaseAssert(llRes.m_effectPlaceholderDesc.size() == slRes.m_icLogicAsm.size());

        InlineSlabInfo inlineSlabInfo {
            .m_hasInlineSlab = (ultimateInlineSlabSize != static_cast<size_t>(-1)),
            .m_smcRegionOffset = smcRegionOffset,
            .m_smcRegionLength = smcRegionLen,
            .m_inlineSlabPatchableJumpEndOffsetInFastPath = inlineSlabIcMissPatchableJumpEndOffsetInFastPath,
            .m_icMissLogicOffsetInSlowPath = icMissSlowPathOffset
        };

        // Generate codegen logic for each inline slab
        //
        std::string inlineSlabAuditLog;
        ReleaseAssertIff(ultimateInlineSlabSize != static_cast<size_t>(-1), !allInlineSlabEffects.empty());
        for (size_t k = 0; k < llRes.m_effectPlaceholderDesc.size(); k++)
        {
            size_t icGlobalOrd = llRes.m_effectPlaceholderDesc[k].m_globalOrd;
            std::string isIcQualifyForInlineSlabFnName = x_jit_check_generic_ic_fits_in_inline_slab_placeholder_fn_prefix + std::to_string(icGlobalOrd);
            Function* isIcQualifyForInlineSlabFn = icBodyModule->getFunction(isIcQualifyForInlineSlabFnName);
            ReleaseAssert(isIcQualifyForInlineSlabFn != nullptr);
            ReleaseAssert(isIcQualifyForInlineSlabFn->empty());
            ReleaseAssert(llvm_type_has_type<bool>(isIcQualifyForInlineSlabFn->getReturnType()) && isIcQualifyForInlineSlabFn->arg_size() == 1);

            isIcQualifyForInlineSlabFn->setLinkage(GlobalValue::InternalLinkage);
            isIcQualifyForInlineSlabFn->addFnAttr(Attribute::AlwaysInline);

            if (allInlineSlabEffects.count(k))
            {
                DeegenStencil& icStencil = allInlineSlabEffects[k];

                ReleaseAssert(ultimateInlineSlabSize != static_cast<size_t>(-1) && inlineSlabIcMissPatchableJumpEndOffsetInFastPath != static_cast<size_t>(-1));

                // Create the implementation for isIcQualifyForInlineSlabFn
                // It should check if the inline slab is already used, and do updates correspondingly.
                //
                {
                    BasicBlock* bb = BasicBlock::Create(ctx, "", isIcQualifyForInlineSlabFn);
                    Value* icSite = isIcQualifyForInlineSlabFn->getArg(0);
                    ReleaseAssert(llvm_value_has_type<void*>(icSite));
                    Value* isInlineSlabUsedAddr = GetElementPtrInst::CreateInBounds(
                        llvm_type_of<uint8_t>(ctx), icSite,
                        {
                            CreateLLVMConstantInt<uint64_t>(ctx, offsetof_member_v<&JitGenericInlineCacheSite::m_isInlineSlabUsed>)
                        }, "", bb);
                    static_assert(std::is_same_v<uint8_t, typeof_member_t<&JitGenericInlineCacheSite::m_isInlineSlabUsed>>);
                    Value* isInlineSlabUsedU8 = new LoadInst(llvm_type_of<uint8_t>(ctx), isInlineSlabUsedAddr, "", false /*isVolatile*/, Align(1), bb);
                    Value* isInlineSlabUsed = new ICmpInst(*bb, ICmpInst::ICMP_NE, isInlineSlabUsedU8, CreateLLVMConstantInt<uint8_t>(ctx, 0));

                    BasicBlock* trueBB = BasicBlock::Create(ctx, "", isIcQualifyForInlineSlabFn);
                    BasicBlock* falseBB = BasicBlock::Create(ctx, "", isIcQualifyForInlineSlabFn);
                    BranchInst::Create(trueBB, falseBB, isInlineSlabUsed, bb);

                    // If the inline slab is already used, we can't do anything
                    //
                    ReturnInst::Create(ctx, CreateLLVMConstantInt<bool>(ctx, false), trueBB);

                    // Otherwise, we should set it to used, increment # of IC, and return true
                    //
                    new StoreInst(CreateLLVMConstantInt<uint8_t>(ctx, 1), isInlineSlabUsedAddr, falseBB);
                    Value* numExistingIcAddr = GetElementPtrInst::CreateInBounds(
                        llvm_type_of<uint8_t>(ctx), icSite,
                        {
                            CreateLLVMConstantInt<uint64_t>(ctx, offsetof_member_v<&JitGenericInlineCacheSite::m_numEntries>)
                        }, "", falseBB);
                    static_assert(std::is_same_v<uint8_t, typeof_member_t<&JitGenericInlineCacheSite::m_numEntries>>);
                    Value* numExistingIc = new LoadInst(llvm_type_of<uint8_t>(ctx), numExistingIcAddr, "", false /*isVolatile*/, Align(1), falseBB);
                    Value* newNumExistingIcVal = BinaryOperator::Create(BinaryOperator::Add, numExistingIc, CreateLLVMConstantInt<uint8_t>(ctx, 1), "", falseBB);
                    new StoreInst(newNumExistingIcVal, numExistingIcAddr, falseBB);

                    ReturnInst::Create(ctx, CreateLLVMConstantInt<bool>(ctx, true), falseBB);
                }

                {
                    // Create the implementation of the codegen function
                    //
                    BaselineJitCodegenResult cgRes = CreateJitIcCodegenImplementation(
                        ifi,
                        llRes.m_effectPlaceholderDesc[k] /*icInfo*/,
                        icStencil,
                        inlineSlabInfo,
                        icUsageOrd,
                        true /*isCodegenForInlineSlab*/);

                    ReleaseAssert(cgRes.m_icSize == smcRegionLen);

                    ReleaseAssert(icBodyModule.get() != nullptr);
                    ReleaseAssert(icBodyModule->getFunction(cgRes.m_resultFnName) != nullptr);
                    ReleaseAssert(icBodyModule->getFunction(cgRes.m_resultFnName)->empty());

                    // Link the codegen impl function into the bodyFn module
                    // Do not reuse linker because we are also modifying the module by ourselves
                    //
                    {
                        Linker linker(*icBodyModule.get());
                        ReleaseAssert(linker.linkInModule(std::move(cgRes.m_module)) == false);
                    }

                    // Dump audit log
                    //
                    {
                        ReleaseAssert(icGlobalOrd >= globalIcTraitOrdBase);
                        std::string disAsmForAuditPfx = "# IC Effect Kind #" + std::to_string(icGlobalOrd - globalIcTraitOrdBase);
                        disAsmForAuditPfx += " (inline slab version, global ord = " + std::to_string(icGlobalOrd) + ", code len = " + std::to_string(cgRes.m_icSize) + "):\n\n";
                        inlineSlabAuditLog += disAsmForAuditPfx + cgRes.m_disasmForAudit;
                    }
                }
            }
            else
            {
                // This IC effect does not qualify for inline slab optimization
                //
                BasicBlock* bb = BasicBlock::Create(ctx, "", isIcQualifyForInlineSlabFn);
                ReturnInst::Create(ctx, CreateLLVMConstantInt<bool>(ctx, false), bb);
            }
        }

        // Generate logic for each outlined IC case
        //
        ReleaseAssert(icBodyModule.get() != nullptr);
        Linker linker(*icBodyModule.get());

        for (size_t k = 0; k < llRes.m_effectPlaceholderDesc.size(); k++)
        {
            std::string icLogicObjFile = CompileAssemblyFileToObjectFile(slRes.m_icLogicAsm[k], " -fno-pic -fno-pie ");
            DeegenStencil icStencil = DeegenStencil::ParseIcLogic(ctx, icLogicObjFile, mainStencil.m_sectionToPdoOffsetMap);

            BaselineJitCodegenResult cgRes = CreateJitIcCodegenImplementation(
                ifi,
                llRes.m_effectPlaceholderDesc[k] /*icInfo*/,
                icStencil,
                inlineSlabInfo,
                icUsageOrd,
                false /*isCodegenForInlineSlab*/);

            ReleaseAssert(icBodyModule->getFunction(cgRes.m_resultFnName) != nullptr);
            ReleaseAssert(icBodyModule->getFunction(cgRes.m_resultFnName)->empty());
            ReleaseAssert(linker.linkInModule(std::move(cgRes.m_module)) == false);

            size_t icGlobalOrd = llRes.m_effectPlaceholderDesc[k].m_globalOrd;
            ReleaseAssert(icGlobalOrd >= globalIcTraitOrdBase);
            std::string disAsmForAuditPfx = "# IC Effect Kind #" + std::to_string(icGlobalOrd - globalIcTraitOrdBase);
            disAsmForAuditPfx += " (global ord = " + std::to_string(icGlobalOrd) + ", code len = " + std::to_string(cgRes.m_icSize) + "):\n\n";
            auditInfo += disAsmForAuditPfx + cgRes.m_disasmForAudit;

            ReleaseAssert(!genericIcSizeMap.count(icGlobalOrd));
            genericIcSizeMap[icGlobalOrd] = cgRes.m_icSize;
        }

        auditInfo += inlineSlabAuditLog;

        auditInfo += "# SMC region offset = " + std::to_string(smcRegionOffset) + ", length = " + std::to_string(smcRegionLen) + "\n";
        auditInfo += "# IC miss slow path offset = " + std::to_string(icMissSlowPathOffset) + "\n";
        auditInfo += std::string("# Has Inline Slab = ") + (inlineSlabInfo.m_hasInlineSlab ? "true" : "false");
        if (inlineSlabInfo.m_hasInlineSlab)
        {
            ReleaseAssert(inlineSlabInfo.m_inlineSlabPatchableJumpEndOffsetInFastPath > inlineSlabInfo.m_smcRegionOffset);
            auditInfo += ", Inline Slab Patchable Jump End Offset = " + std::to_string(inlineSlabInfo.m_inlineSlabPatchableJumpEndOffsetInFastPath - inlineSlabInfo.m_smcRegionOffset);
        }
        auditInfo += "\n\n";

        globalIcTraitOrdBase += llRes.m_effectPlaceholderDesc.size();
        genericIcAuditInfo.push_back(auditInfo);
    }
    ReleaseAssert(globalIcTraitOrdBase == gbta.GetGenericIcEffectTraitBaseOrdinal(ifi->GetBytecodeDef()->GetBytecodeIdName()) + gbta.GetNumTotalGenericIcEffectKinds(ifi->GetBytecodeDef()->GetBytecodeIdName()));

    for (auto& it : genericIcSizeMap)
    {
        finalRes.m_icTraitInfo.push_back({
            .m_ordInTraitTable = it.first,
            .m_allocationLength = it.second
        });
    }

    // Change all the IC codegen implementations to internal and always_inline
    //
    for (Function& fn : *icBodyModule.get())
    {
        if (fn.getName().startswith(x_jit_codegen_ic_impl_placeholder_fn_prefix) && !fn.empty())
        {
            ReleaseAssert(fn.hasExternalLinkage());
            ReleaseAssert(!fn.hasFnAttribute(Attribute::NoInline));
            fn.setLinkage(GlobalValue::InternalLinkage);
            fn.addFnAttr(Attribute::AlwaysInline);
        }
    }

    RunLLVMOptimizePass(icBodyModule.get());

    // Assert that all the IC codegen implementation functions are gone:
    // they should either be inlined, or never used and eliminated (for IC that does not qualify for inline slab)
    //
    // And assert that all the check-can-use-inline-slab functions are gone: they must have been inlined
    //
    for (Function& fn : *icBodyModule.get())
    {
        ReleaseAssert(!fn.getName().startswith(x_jit_codegen_ic_impl_placeholder_fn_prefix));
        ReleaseAssert(!fn.getName().startswith(x_jit_check_generic_ic_fits_in_inline_slab_placeholder_fn_prefix));
    }

    // Generate the final generic IC audit log
    //
    {
        std::string finalAuditLog;
        ReleaseAssert(genericIcAuditInfo.size() == icLLRes.size());
        for (size_t i = 0; i < genericIcAuditInfo.size(); i++)
        {
            if (genericIcAuditInfo.size() > 1)
            {
                finalAuditLog += "# IC Info (IC body = " + icLLRes[i].m_bodyFnName + ")\n\n";
            }
            finalAuditLog += genericIcAuditInfo[i];
        }
        {
            std::unique_ptr<Module> clonedModule = CloneModule(*icBodyModule.get());
            finalAuditLog += CompileLLVMModuleToAssemblyFile(clonedModule.get(), Reloc::Static, CodeModel::Small);
        }
        finalRes.m_disasmForAudit = finalAuditLog;
    }

    finalRes.m_icBodyModule = std::move(icBodyModule);
    return finalRes;
}

}   // namespace dast
