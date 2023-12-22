#include "deegen_dfg_jit_process_call_inlining.h"
#include "llvm_naive_reachability_analysis.h"
#include "deegen_ast_make_call.h"
#include "deegen_osr_exit_placeholder.h"
#include "llvm_check_function_effectful.h"
#include "deegen_analyze_read_locations.h"
#include "deegen_ast_return.h"
#include "deegen_ast_return_value_accessor.h"

namespace dast {

// Convert basic blocks that cannot reach the target call site to OSR exit for the speculated call inlining prologue
//
static void PruneBasicBlocksForSpeculatedCallPrologue(llvm::Function* func, llvm::BasicBlock* callSite)
{
    using namespace llvm;
    ReleaseAssert(callSite->getTerminator()->getNumSuccessors() == 0);

    LLVMNaiveReachabilityAnalysis ra(func);
    std::vector<BasicBlock*> bbToOsrExit;
    for (BasicBlock& bb : *func)
    {
        if (!ra.IsReachable(&bb, callSite))
        {
            ReleaseAssert(&bb != callSite);
            ReleaseAssert(&bb != &func->getEntryBlock());
            bbToOsrExit.push_back(&bb);
        }
    }

    for (BasicBlock* bb : bbToOsrExit)
    {
        std::vector<Instruction*> instToRemove;
        for (Instruction& inst : *bb)
        {
            instToRemove.push_back(&inst);
        }
        for (Instruction* inst : instToRemove)
        {
            if (!llvm_value_has_type<void>(inst))
            {
                inst->replaceAllUsesWith(UndefValue::get(inst->getType()));
            }
            ReleaseAssert(inst->use_empty());
            inst->eraseFromParent();
        }

        Function* osrExitPlaceholder = GetOsrExitFunctionPlaceholder(func->getParent());
        CallInst::Create(osrExitPlaceholder, "", bb);
        new UnreachableInst(func->getContext(), bb);
    }

    ValidateLLVMFunction(func);
    EliminateUnreachableBlocks(*func);
    ValidateLLVMFunction(func);
}

bool WARN_UNUSED DeegenOptJitSpeculativeInliningInfo::TryGenerateInfo()
{
    using namespace llvm;
    std::unique_ptr<Module> module = CloneModule(*m_component->m_module.get());
    Function* func = module->getFunction(m_component->m_impl->getName().str());
    ReleaseAssert(func != nullptr);
    ReleaseAssert(!m_success);

    AstMakeCall callSite;
    {
        std::vector<AstMakeCall> callSiteList = AstMakeCall::GetAllUseInFunctionInDeterministicOrder(func);
        ReleaseAssert(m_callSiteOrdinal < callSiteList.size());
        callSite = callSiteList[m_callSiteOrdinal];
    }

    BasicBlock* callSiteBB = callSite.m_origin->getParent();
    PruneBasicBlocksForSpeculatedCallPrologue(func, callSiteBB);

    // Give up if the residue function is effectful
    //
    {
        auto externFnChecker = [&](Function* fnToCheck, const std::vector<bool>&) -> bool
        {
            if (fnToCheck == callSite.m_origin->getCalledFunction())
            {
                return false;
            }
            return true;
        };
        bool isEffectful = DetermineIfLLVMFunctionMightBeEffectful(func, {} /*excludedPtrArgs*/, externFnChecker);
        if (isEffectful)
        {
            return false;
        }
    }

    // Attempt to figure out the read info of the residue function
    //
    {
        std::unique_ptr<Module> tmpModule = CloneModule(*module.get());
        DesugarAndSimplifyLLVMModule(tmpModule.get(), DesugaringLevel::Top);

        Function* clonedFn = tmpModule->getFunction(func->getName());
        ReleaseAssert(clonedFn != nullptr);

        Function* clonedRetContFn;
        AstMakeCall clonedFnCall;
        {
            std::vector<AstMakeCall> allCalls = AstMakeCall::GetAllUseInFunctionInDeterministicOrder(clonedFn);
            ReleaseAssert(allCalls.size() == 1);
            clonedFnCall = allCalls[0];
            CallInst::Create(GetOsrExitFunctionPlaceholder(tmpModule.get()), "", clonedFnCall.m_origin);
            clonedRetContFn = clonedFnCall.m_continuation;
        }

        auto mainFnArgToBcOperandMapper = [&](Argument* arg) -> BcOperand*
        {
            ReleaseAssert(arg->getParent() == clonedFn);
            size_t ord = arg->getArgNo();
            ReleaseAssert(ord < m_bytecodeDef->m_list.size());
            return m_bytecodeDef->m_list[ord].get();
        };

        // Figure out information about the call arguments
        //
        m_hasRangedArgs = false;
        m_numSingletonArgs = 0;
        for (size_t argDescOrd  = 0; argDescOrd < clonedFnCall.m_args.size(); argDescOrd++)
        {
            AstMakeCall::Arg& arg = clonedFnCall.m_args[argDescOrd];
            if (arg.IsArgRange())
            {
                // We currently only support one arg range in MakeCall API, so we should not see more than one here
                //
                ReleaseAssert(!m_hasRangedArgs);
                m_hasRangedArgs = true;
                m_rangedArgLocInArguments = argDescOrd;

                // Try figure out the range information
                //
                LLVMValueToOperandExprMapper oem;
                oem.Run(clonedFn, mainFnArgToBcOperandMapper);
                if (oem.m_analysisFailed)
                {
                    return false;
                }

                m_rangeArgBase = oem.TryGetUniqueExpr(arg.GetArgStart());
                if (m_rangeArgBase == nullptr)
                {
                    return false;
                }
                m_numRangeArgs = oem.TryGetUniqueExpr(arg.GetArgNum());
                if (m_numRangeArgs == nullptr)
                {
                    return false;
                }
            }
            else
            {
                m_numSingletonArgs++;
            }
        }

        ReleaseAssert(m_numSingletonArgs + (m_hasRangedArgs ? 1 : 0) == clonedFnCall.m_args.size());

        // Convert the MakeCall API to a dummy instruction, to avoid confuse the analyzer below
        //
        clonedFnCall.m_origin->eraseFromParent();

        // Attempt to figure out the read info of the residue function
        //
        {
            ReleaseAssert(clonedFn->arg_size() == m_bytecodeDef->m_list.size());

            bool success = true;
            std::unordered_set<SimpleOperandExprNode*> allReads;
            for (size_t i = 0; i < m_bytecodeDef->m_list.size(); i++)
            {
                if (m_bytecodeDef->m_list[i]->GetKind() == BcOperandKind::BytecodeRangeBase)
                {
                    Argument* arg = clonedFn->getArg(static_cast<uint32_t>(i));
                    ReleaseAssert(llvm_value_has_type<void*>(arg));
                    std::vector<SimpleOperandExprNode*> tmpRes;
                    success = TryAnalyzeBytecodeReadLocations(arg, mainFnArgToBcOperandMapper, tmpRes /*out*/);
                    if (!success)
                    {
                        break;
                    }
                    for (SimpleOperandExprNode* expr : tmpRes)
                    {
                        allReads.insert(expr);
                    }
                }
                else if (m_bytecodeDef->m_list[i]->GetKind() == BcOperandKind::Slot)
                {
                    Argument* arg = clonedFn->getArg(static_cast<uint32_t>(i));
                    ReleaseAssert(llvm_value_has_type<uint64_t>(arg));
                    if (!arg->use_empty())
                    {
                        m_prologueReadDirectInputs.push_back(i);
                    }
                }
            }

            m_canDeterminePrologueReads = success;
            if (success)
            {
                for (SimpleOperandExprNode* expr : allReads)
                {
                    m_prologueReadInfo.push_back(expr);
                }
            }
        }

        // Attempt to convert the return continuation to pure data flow
        //
        m_isRetContConvertibleToPureDataFlow = false;

        auto tryAnalyzeReturnContinuation = [&]()
        {
            // Currently we only identify the following simple pattern:
            // (1) the continuation is a call to StoreReturnValuesTo or StoreReturnValuesAsVariadicResult, followed by a Return
            // (2) the continuation is a call to GetReturnValueAtOrd followed by a Return that returns this value
            //
            ReleaseAssert(clonedRetContFn != nullptr);
            ReleaseAssert(!clonedRetContFn->empty());
            if (clonedRetContFn->getBasicBlockList().size() > 1)
            {
                return;
            }

            // Identify if the function ends with a Return() API call
            //
            AstBytecodeReturn retApiCall;
            {
                std::vector<AstBytecodeReturn> rs = AstBytecodeReturn::GetAllUseInFunction(clonedRetContFn);
                if (rs.size() != 1)
                {
                    return;
                }
                retApiCall = rs[0];
            }

            if (retApiCall.m_doesBranch)
            {
                return;
            }

            auto rcFnArgToBcOperandMapper = [&](Argument* arg) -> BcOperand*
            {
                ReleaseAssert(arg->getParent() == clonedRetContFn);
                size_t ord = arg->getArgNo();
                ReleaseAssert(ord < m_bytecodeDef->m_list.size());
                return m_bytecodeDef->m_list[ord].get();
            };

            if (retApiCall.HasValueOutput())
            {
                // Try to determine if this is the "return kth result" pattern
                //
                Value* val = retApiCall.ValueOperand();

                std::vector<AstReturnValueAccessor> rs = AstReturnValueAccessor::GetAllUseInFunction(clonedRetContFn);
                if (rs.size() != 1)
                {
                    return;
                }
                if (rs[0].m_kind != AstReturnValueAccessor::Kind::GetOneReturnValue)
                {
                    return;
                }
                CallInst* ci = rs[0].m_origin;
                if (ci != val)
                {
                    return;
                }

                ReleaseAssert(ci->arg_size() == 1);
                Value* arg = ci->getArgOperand(0);
                ReleaseAssert(llvm_value_has_type<uint64_t>(arg));

                LLVMValueToOperandExprMapper oem;
                oem.Run(clonedRetContFn, rcFnArgToBcOperandMapper);
                if (oem.m_analysisFailed)
                {
                    return;
                }

                SimpleOperandExprNode* expr = oem.TryGetUniqueExpr(arg);
                if (expr == nullptr)
                {
                    return;
                }

                // Check if the function becomes not effectful not considering the Return and GetReturnValueAtOrd API call
                // Note that the GetReturnValueAtOrd is already not effectful, so all we need to do is to get rid of the Return
                //
                CallInst::Create(GetOsrExitFunctionPlaceholder(clonedRetContFn->getParent()), "", retApiCall.m_origin);
                retApiCall.m_origin->eraseFromParent();

                if (DetermineIfLLVMFunctionMightBeEffectful(clonedRetContFn))
                {
                    return;
                }

                m_isRetContConvertibleToPureDataFlow = true;
                m_trivialRetContLogicKind = TrivialRetContLogicKind::ReturnOne;
                m_trivialRetContReturnOneOrd = expr;
                return;
            }
            else
            {
                // Try to determine if this is the "Store first k results to frame" or "Store results as VariadicResults" pattern
                //
                std::vector<AstReturnValueAccessor> rs = AstReturnValueAccessor::GetAllUseInFunction(clonedRetContFn);
                if (rs.size() != 1)
                {
                    return;
                }

                if (rs[0].m_kind == AstReturnValueAccessor::Kind::StoreFirstKFillNil)
                {
                    ReleaseAssert(rs[0].m_origin->arg_size() == 2);
                    Value* rangeOffsetVal = rs[0].m_origin->getArgOperand(0);
                    Value* numToStoreVal = rs[0].m_origin->getArgOperand(1);
                    ReleaseAssert(llvm_value_has_type<void*>(rangeOffsetVal));
                    ReleaseAssert(llvm_value_has_type<size_t>(numToStoreVal));

                    LLVMValueToOperandExprMapper oem;
                    oem.Run(clonedRetContFn, rcFnArgToBcOperandMapper);
                    if (oem.m_analysisFailed)
                    {
                        return;
                    }

                    SimpleOperandExprNode* rangeOffset = oem.TryGetUniqueExpr(rangeOffsetVal);
                    SimpleOperandExprNode* numToStore = oem.TryGetUniqueExpr(numToStoreVal);
                    if (rangeOffset == nullptr || numToStore == nullptr)
                    {
                        return;
                    }

                    // Remove the Return and Store and check if the function is not effectful
                    //
                    rs[0].m_origin->eraseFromParent();
                    CallInst::Create(GetOsrExitFunctionPlaceholder(clonedRetContFn->getParent()), "", retApiCall.m_origin);
                    retApiCall.m_origin->eraseFromParent();

                    if (DetermineIfLLVMFunctionMightBeEffectful(clonedRetContFn))
                    {
                        return;
                    }

                    m_isRetContConvertibleToPureDataFlow = true;
                    m_trivialRetContLogicKind = TrivialRetContLogicKind::StoreRange;
                    m_trivialRetContStoreRangeOffset = rangeOffset;
                    m_trivialRetContStoreRangeNum = numToStore;
                    return;
                }
                else if (rs[0].m_kind == AstReturnValueAccessor::Kind::StoreAsVariadicResults)
                {
                    ReleaseAssert(rs[0].m_origin->arg_size() == 0);

                    // Remove the Return and Store and check if the function is not effectful
                    //
                    rs[0].m_origin->eraseFromParent();
                    CallInst::Create(GetOsrExitFunctionPlaceholder(clonedRetContFn->getParent()), "", retApiCall.m_origin);
                    retApiCall.m_origin->eraseFromParent();

                    if (DetermineIfLLVMFunctionMightBeEffectful(clonedRetContFn))
                    {
                        return;
                    }

                    m_isRetContConvertibleToPureDataFlow = true;
                    m_trivialRetContLogicKind = TrivialRetContLogicKind::StoreAsVarRes;
                    return;
                }
                else
                {
                    return;
                }
            }
        };

        if (!clonedFnCall.m_isMustTailCall)
        {
            tryAnalyzeReturnContinuation();
        }
    }

#if 0
    if (func->getName().find("Call_0") != std::string::npos)
    {
        fprintf(stderr, "######## can determine prologue reads = %s\n", (m_canDeterminePrologueReads ? "true" : "false"));
        fprintf(stderr, "#read exprs = %llu\n", static_cast<unsigned long long>(m_prologueReadInfo.size()));
        SimpleOperandExprCppPrinter printer;
        for (SimpleOperandExprNode* expr : m_prologueReadInfo)
        {
            size_t varOrd = printer.Print(stderr, expr);
            fprintf(stderr, "#### tmp_%llu\n", static_cast<unsigned long long>(varOrd));
        }

        fprintf(stderr, "##### hasRangedArg = %s, numSingletonArgs = %d\n", (m_hasRangedArgs ? "true" : "false"), static_cast<int>(m_numSingletonArgs));
        if (m_hasRangedArgs)
        {
            fprintf(stderr, "##### rangedArgLoc = %d\n", static_cast<int>(m_rangedArgLocInArguments));
            size_t rangeArgBaseVarOrd = printer.Print(stderr, m_rangeArgBase);
            size_t rangeArgLenVarOrd = printer.Print(stderr, m_numRangeArgs);
            fprintf(stderr, "##### range: tmp_%d, len: tmp_%d\n", static_cast<int>(rangeArgBaseVarOrd), static_cast<int>(rangeArgLenVarOrd));
        }

        if (m_isRetContConvertibleToPureDataFlow)
        {
            if (m_trivialRetContLogicKind == TrivialRetContLogicKind::ReturnOne)
            {
                fprintf(stderr, "#### RC Kind: Return one\n");
                size_t rcVarOrd = printer.Print(stderr, m_trivialRetContReturnOneOrd);
                fprintf(stderr, "#### RC ord: tmp_%d\n", static_cast<int>(rcVarOrd));
            }
            else if (m_trivialRetContLogicKind == TrivialRetContLogicKind::StoreRange)
            {
                fprintf(stderr, "#### RC Kind: Store range\n");
                size_t rc1VarOrd = printer.Print(stderr, m_trivialRetContStoreRangeOffset);
                size_t rc2VarOrd = printer.Print(stderr, m_trivialRetContStoreRangeNum);
                fprintf(stderr, "#### RC range: tmp_%d tmp_%d\n", static_cast<int>(rc1VarOrd), static_cast<int>(rc2VarOrd));
            }
            else
            {
                fprintf(stderr, "#### RC Kind: Store as VariadicResults\n");
            }
        }
        else
        {
            fprintf(stderr, "#### RC cannot be converted to data flow\n");
        }
    }
#endif

    m_isMustTailCall = callSite.m_isMustTailCall;
    m_isInPlaceCall = callSite.m_isInPlaceCall;
    m_isCallPassingVarRes = callSite.m_passVariadicRes;

    m_success = true;
    return true;
}

std::string WARN_UNUSED DeegenOptJitSpeculativeInliningInfo::GetCppTraitObjectName()
{
    std::string traitObjName = "x_deegen_dfg_speculative_inlining_trait_" + m_bytecodeDef->GetBytecodeIdName() + "_callsite_" + std::to_string(m_callSiteOrdinal);
    return traitObjName;
}

void DeegenOptJitSpeculativeInliningInfo::PrintCppFile(FILE* file)
{
    ReleaseAssert(m_success);

    std::string traitObjName = GetCppTraitObjectName();

    fprintf(file, "constexpr BytecodeSpeculativeInliningTrait %s = {\n", traitObjName.c_str());

    fprintf(file, "    .m_isInPlaceCall = %s,\n", (m_isInPlaceCall ? "true" : "false"));
    fprintf(file, "    .m_isTailCall = %s,\n", (m_isMustTailCall ? "true" : "false"));
    fprintf(file, "    .m_appendsVariadicResultsToArgs = %s,\n", (m_isCallPassingVarRes ? "true" : "false"));
    fprintf(file, "    .m_hasReducedReadInfo = %s,\n", (m_canDeterminePrologueReads ? "true" : "false"));
    fprintf(file, "    .m_hasRangeArgs = %s,\n", (m_hasRangedArgs ? "true" : "false"));

    {
        std::string rcTrivialnessKind = "NotTrivial";
        if (m_isRetContConvertibleToPureDataFlow)
        {
            switch (m_trivialRetContLogicKind)
            {
            case TrivialRetContLogicKind::ReturnOne:
            {
                rcTrivialnessKind = "ReturnKthResult";
                break;
            }
            case TrivialRetContLogicKind::StoreRange:
            {
                rcTrivialnessKind = "StoreFirstKResults";
                break;
            }
            case TrivialRetContLogicKind::StoreAsVarRes:
            {
                rcTrivialnessKind = "StoreAllAsVariadicResults";
            }
            }   /*switch*/
        }
        fprintf(file, "    .m_rcTrivialness = BytecodeSpeculativeInliningTrait::TrivialRCKind::%s,\n", rcTrivialnessKind.c_str());
    }

    fprintf(file, "    .m_numExtraOutputs = %u,\n", static_cast<unsigned int>(m_numSingletonArgs));

    if (m_hasRangedArgs)
    {
        fprintf(file, "    .m_rangeLocationInArgs = %u,\n", static_cast<unsigned int>(m_rangedArgLocInArguments));
    }
    else
    {
        fprintf(file, "    .m_rangeLocationInArgs = static_cast<uint32_t>(-1),\n");
    }

    if (!m_hasRangedArgs)
    {
        fprintf(file, "    .m_rangeInfoGetter = nullptr,\n");
    }
    else
    {
        fprintf(file, "    .m_rangeInfoGetter = [](DeegenBytecodeBuilder::BytecodeDecoder* decoder, size_t bcPos) {\n");
        fprintf(file, "        [[maybe_unused]] auto ops = decoder->Decode%s_Variant<%u>(bcPos);\n", m_bytecodeDef->m_bytecodeName.c_str(), SafeIntegerCast<unsigned int>(m_bytecodeDef->m_variantOrd));

        SimpleOperandExprCppPrinter printer;
        size_t rangeStartVarOrd = printer.Print(file, m_rangeArgBase);
        size_t rangeLengthVarOrd = printer.Print(file, m_numRangeArgs);
        fprintf(file, "        TestAssert(tmp_%u %% sizeof(TValue) == 0);\n", static_cast<unsigned int>(rangeStartVarOrd));
        fprintf(file, "        return BytecodeSpeculativeInliningTrait::RangeArgInfo {\n");
        fprintf(file, "            .m_rangeStart = tmp_%u / sizeof(TValue),\n", static_cast<unsigned int>(rangeStartVarOrd));
        fprintf(file, "            .m_rangeLen = tmp_%u,\n", static_cast<unsigned int>(rangeLengthVarOrd));
        fprintf(file, "        };\n");
        fprintf(file, "    },\n");
    }

    if (!m_isRetContConvertibleToPureDataFlow || m_trivialRetContLogicKind == TrivialRetContLogicKind::StoreAsVarRes)
    {
        fprintf(file, "    .m_trivialRCInfoGetter = nullptr,\n");
    }
    else
    {
        fprintf(file, "    .m_trivialRCInfoGetter = [](DeegenBytecodeBuilder::BytecodeDecoder* decoder, size_t bcPos) {\n");
        fprintf(file, "        [[maybe_unused]] auto ops = decoder->Decode%s_Variant<%u>(bcPos);\n", m_bytecodeDef->m_bytecodeName.c_str(), SafeIntegerCast<unsigned int>(m_bytecodeDef->m_variantOrd));
        if (m_trivialRetContLogicKind == TrivialRetContLogicKind::ReturnOne)
        {
            SimpleOperandExprCppPrinter printer;
            size_t returnVarOrd = printer.Print(file, m_trivialRetContReturnOneOrd);
            fprintf(file, "        return BytecodeSpeculativeInliningTrait::TrivialRCInfo {\n");
            fprintf(file, "            .m_num = tmp_%u,\n", static_cast<unsigned int>(returnVarOrd));
            fprintf(file, "        };\n");
        }
        else
        {
            ReleaseAssert(m_trivialRetContLogicKind == TrivialRetContLogicKind::StoreRange);
            SimpleOperandExprCppPrinter printer;
            size_t rangeStartVarOrd = printer.Print(file, m_trivialRetContStoreRangeOffset);
            size_t numVarOrd = printer.Print(file, m_trivialRetContStoreRangeNum);
            fprintf(file, "        TestAssert(tmp_%u %% sizeof(TValue) == 0);\n", static_cast<unsigned int>(rangeStartVarOrd));
            fprintf(file, "        return BytecodeSpeculativeInliningTrait::TrivialRCInfo {\n");
            fprintf(file, "            .m_num = tmp_%u,\n", static_cast<unsigned int>(numVarOrd));
            fprintf(file, "            .m_rangeStart = tmp_%u / sizeof(TValue),\n", static_cast<unsigned int>(rangeStartVarOrd));
            fprintf(file, "        };\n");
        }
        fprintf(file, "    },\n");
    }

    if (!m_canDeterminePrologueReads)
    {
        fprintf(file, "    .m_prologueReadInfoGetter = nullptr\n");
    }
    else
    {
        fprintf(file, "    .m_prologueReadInfoGetter = [](DeegenBytecodeBuilder::BytecodeDecoder* decoder, size_t bcPos, TempBitVector& bv, TempVector<uint32_t>& result) {\n");
        fprintf(file, "        [[maybe_unused]] auto ops = decoder->Decode%s_Variant<%u>(bcPos);\n", m_bytecodeDef->m_bytecodeName.c_str(), SafeIntegerCast<unsigned int>(m_bytecodeDef->m_variantOrd));
        fprintf(file, "        BytecodeSpeculativeInliningTrait::AssertBitVectorIsAllZero(bv);\n");
        fprintf(file, "        result.clear();\n");
        SimpleOperandExprCppPrinter printer;
        for (SimpleOperandExprNode* expr : m_prologueReadInfo)
        {
            size_t varOrd = printer.Print(file, expr);
            fprintf(file, "        TestAssert(tmp_%u %% sizeof(TValue) == 0);\n", static_cast<unsigned int>(varOrd));
            fprintf(file, "        BytecodeSpeculativeInliningTrait::AddToListIfNotExist(result, bv, tmp_%u / sizeof(TValue));\n", static_cast<unsigned int>(varOrd));
        }
        for (size_t opOrd : m_prologueReadDirectInputs)
        {
            ReleaseAssert(opOrd < m_bytecodeDef->m_list.size());
            BcOperand* bcOp = m_bytecodeDef->m_list[opOrd].get();
            ReleaseAssert(bcOp->GetKind() == BcOperandKind::Slot);
            ReleaseAssert(opOrd < m_bytecodeDef->m_originalOperandTypes.size());
            if (m_bytecodeDef->m_originalOperandTypes[opOrd] == DeegenBytecodeOperandType::BytecodeSlotOrConstant)
            {
                fprintf(file, "        BytecodeSpeculativeInliningTrait::AddToListIfNotExist(result, bv, ops.%s.AsLocal().m_localOrd);\n", bcOp->OperandName().c_str());
            }
            else
            {
                ReleaseAssert(m_bytecodeDef->m_originalOperandTypes[opOrd] == DeegenBytecodeOperandType::BytecodeSlot);
                fprintf(file, "        BytecodeSpeculativeInliningTrait::AddToListIfNotExist(result, bv, ops.%s.m_localOrd);\n", bcOp->OperandName().c_str());
            }
        }
        fprintf(file, "        BytecodeSpeculativeInliningTrait::ResetBitVectorGivenList(bv, result);\n");
        fprintf(file, "    }\n");
    }

    fprintf(file, "};\n");
}

}   // namespace dast
