#include "deegen_call_inline_cache.h"
#include "deegen_bytecode_operand.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "tvalue_typecheck_optimization.h"

namespace dast {

static void EmitInterpreterCallIcSlowPath(InterpreterBytecodeImplCreator* ifi,
                                          llvm::Value* functionObject,
                                          llvm::Value*& calleeCbHeapPtr /*out*/,
                                          llvm::Value*& codePointer /*out*/,
                                          llvm::Instruction* insertBefore)
{
    using namespace llvm;
    Value* codeBlockAndEntryPoint = ifi->CallDeegenCommonSnippet("GetCalleeEntryPoint", { functionObject }, insertBefore);
    ReleaseAssert(codeBlockAndEntryPoint->getType()->isAggregateType());

    calleeCbHeapPtr = ExtractValueInst::Create(codeBlockAndEntryPoint, { 0 /*idx*/ }, "", insertBefore);
    codePointer = ExtractValueInst::Create(codeBlockAndEntryPoint, { 1 /*idx*/ }, "", insertBefore);
    ReleaseAssert(llvm_value_has_type<HeapPtr<void>>(calleeCbHeapPtr));
    ReleaseAssert(llvm_value_has_type<void*>(codePointer));
}

static void EmitInterpreterCallIcCacheMissPopulateIcSlowPath(InterpreterBytecodeImplCreator* ifi,
                                                             llvm::Value* functionObject,
                                                             llvm::Value*& calleeCbHeapPtr /*out*/,
                                                             llvm::Value*& codePointer /*out*/,
                                                             llvm::Instruction* insertBefore)
{
    using namespace llvm;
    calleeCbHeapPtr = nullptr;
    codePointer = nullptr;
    EmitInterpreterCallIcSlowPath(ifi, functionObject, calleeCbHeapPtr /*out*/, codePointer /*out*/, insertBefore /*insertBefore*/);
    ReleaseAssert(calleeCbHeapPtr != nullptr);
    ReleaseAssert(codePointer != nullptr);

    InterpreterCallIcMetadata& ic = ifi->GetBytecodeDef()->GetInterpreterCallIc();

    Value* cachedIcTvAddr = ic.GetCachedTValue()->EmitGetAddress(ifi->GetModule(), ifi->GetBytecodeMetadataPtr(), insertBefore);
    ReleaseAssert(ic.GetCachedTValue()->GetSize() == 8);
    Value* tv = ifi->CallDeegenCommonSnippet("BoxFunctionObjectToTValue", { functionObject }, insertBefore);
    ReleaseAssert(llvm_value_has_type<uint64_t>(tv));
    new StoreInst(tv, cachedIcTvAddr, false /*isVolatile*/, Align(ic.GetCachedTValue()->GetAlignment()), insertBefore);

    Value* cachedIcCodePtrAddr = ic.GetCachedCodePointer()->EmitGetAddress(ifi->GetModule(), ifi->GetBytecodeMetadataPtr(), insertBefore);
    ReleaseAssert(ic.GetCachedCodePointer()->GetSize() == 8);
    ReleaseAssert(llvm_value_has_type<void*>(codePointer));
    new StoreInst(codePointer, cachedIcCodePtrAddr, false /*isVolatile*/, Align(ic.GetCachedCodePointer()->GetAlignment()), insertBefore);
}

static bool WARN_UNUSED CheckCanHoistInterpreterCallIcCheck(llvm::Value* functionObject,
                                                            llvm::BasicBlock* bb,
                                                            llvm::Value*& tv /*out*/,
                                                            llvm::BranchInst*& condBrInst /*out*/)
{
    using namespace llvm;
    ReleaseAssert(llvm_value_has_type<uint64_t>(functionObject));
    PtrToIntInst* ptrToInt = dyn_cast<PtrToIntInst>(functionObject);
    if (ptrToInt == nullptr)
    {
        return false;
    }

    Value* ptrOperand = ptrToInt->getPointerOperand();
    CallInst* ci = dyn_cast<CallInst>(ptrOperand);
    if (ci == nullptr)
    {
        return false;
    }

    {
        Function* callee = ci->getCalledFunction();
        if (callee == nullptr || !IsTValueDecodeAPIFunction(callee))
        {
            return false;
        }
    }

    ReleaseAssert(ci->arg_size() == 1);
    tv = ci->getArgOperand(0);
    ReleaseAssert(llvm_value_has_type<uint64_t>(tv));

    BasicBlock* pred = bb->getSinglePredecessor();
    if (pred == nullptr)
    {
        return false;
    }

    Instruction* term = pred->getTerminator();
    condBrInst = dyn_cast<BranchInst>(term);
    if (condBrInst == nullptr)
    {
        return false;
    }

    if (!condBrInst->isConditional())
    {
        return false;
    }

    Value* cond = condBrInst->getCondition();
    CallInst* condCi = dyn_cast<CallInst>(cond);
    if (condCi == nullptr)
    {
        return false;
    }

    Function* callee = condCi->getCalledFunction();
    if (callee == nullptr)
    {
        return false;
    }

    if (!IsTValueTypeCheckAPIFunction(callee) && !IsTValueTypeCheckStrengthReductionFunction(callee))
    {
        return false;
    }

    TypeSpeculationMask checkedMask = GetCheckedMaskOfTValueTypecheckFunction(callee);
    if ((checkedMask & x_typeSpeculationMaskFor<tFunction>) != x_typeSpeculationMaskFor<tFunction>)
    {
        // I'm not sure if this is even possible, since our MakeCall API always accept a 'target' that is a tFunction
        //
        ReleaseAssert(false && "unexpected");
    }

    return true;
}

static void EmitInterpreterCallIcWithHoistedCheck(InterpreterBytecodeImplCreator* ifi,
                                                  llvm::Value* tv,
                                                  llvm::BranchInst* term,
                                                  llvm::Value*& calleeCbHeapPtr /*out*/,
                                                  llvm::Value*& codePointer /*out*/,
                                                  llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();
    BasicBlock* bb = insertBefore->getParent();
    ReleaseAssert(bb != nullptr);
    Function* fn = bb->getParent();
    ReleaseAssert(fn != nullptr);

    // The precondition of this function is that we have the following IR:
    //
    // pred: (unique predecessor of bb)
    //     ...
    //     %0 = TValue::Is<tFunction>(%tv)
    //     br %0, bb, ..
    // bb:
    //     %fnObj = TValue::As<tFunction>(%tv)
    //     ...
    //     MakeCall(%fnObj, ...)
    //
    // In this case, the IC check can be hoisted above the tFunction check, that is, we can rewrite it to:
    //
    // pred:
    //     ...
    //     %0 = TValue::Is<tFunction>(%tv)
    //     %icHit = cmp eq %tv, %cached_tv
    //     br %icHit, icHit, icMiss
    // icHit:
    //     decode ic...
    //     br bb
    // icMiss:
    //     br %0, createIc, ..
    // createIc:
    //     ...
    //     br bb
    // bb:
    //     %fnObj = TValue::As<tFunction>(%tv)
    //     %codePtr = phi [ icHit, cached_codePtr ], [ createIc, codePtr ]
    //
    // The code below performs the above rewrite.
    //
    BasicBlock* updateIc = BasicBlock::Create(ctx, "", fn, bb /*insertBefore*/);
    BranchInst* updateIcBBEnd = BranchInst::Create(bb, updateIc);

    BasicBlock* pred = term->getParent();
    ReleaseAssert(pred != nullptr);
    ReleaseAssert(pred->getTerminator() == term);

    // Now, set up the IC check logic, which should be inserted before 'term'
    //
    InterpreterCallIcMetadata& ic = ifi->GetBytecodeDef()->GetInterpreterCallIc();

    Value* cachedIcTvAddr = ic.GetCachedTValue()->EmitGetAddress(ifi->GetModule(), ifi->GetBytecodeMetadataPtr(), term);
    ReleaseAssert(ic.GetCachedTValue()->GetSize() == 8);
    Value* cachedIcTv = new LoadInst(llvm_type_of<uint64_t>(ctx), cachedIcTvAddr, "", false /*isVolatile*/, Align(ic.GetCachedTValue()->GetAlignment()), term);

    Value* icHit = new ICmpInst(term, CmpInst::Predicate::ICMP_EQ, cachedIcTv, tv);
    Function* expectIntrin = Intrinsic::getDeclaration(ifi->GetModule(), Intrinsic::expect, { Type::getInt1Ty(ctx) });
    icHit = CallInst::Create(expectIntrin, { icHit, CreateLLVMConstantInt<bool>(ctx, true) }, "", term);

    // Split 'pred' before 'term', the true branch (IC hit path) should branch to 'bb', the false branch (IC miss path) should branch to 'term'.
    //
    Instruction* unreachableInst = SplitBlockAndInsertIfThen(icHit, term /*splitBefore*/, true /*createUnreachableInThenBlock*/);
    ReleaseAssert(isa<UnreachableInst>(unreachableInst));
    BasicBlock* icHitBB = unreachableInst->getParent();
    unreachableInst->eraseFromParent();

    // The icHitBlock should decode the IC and branch to the join block 'bb'
    //
    Value* cachedIcCodePtrAddr = ic.GetCachedCodePointer()->EmitGetAddress(ifi->GetModule(), ifi->GetBytecodeMetadataPtr(), icHitBB);
    ReleaseAssert(ic.GetCachedCodePointer()->GetSize() == 8);
    Value* icHitCodePtr = new LoadInst(llvm_type_of<void*>(ctx), cachedIcCodePtrAddr, "", false /*isVolatile*/, Align(ic.GetCachedCodePointer()->GetAlignment()), icHitBB);

    Value* icHitCalleeCbHeapPtr = CreateCallToDeegenCommonSnippet(ifi->GetModule(), "GetCbHeapPtrFromTValueFuncObj", { tv }, icHitBB);
    ReleaseAssert(llvm_value_has_type<HeapPtr<void>>(icHitCalleeCbHeapPtr));
    BranchInst::Create(bb, icHitBB);

    // The IC miss path should continue the original check on 'tv'
    // But if the check succeeds, instead of directly branching to 'bb', it needs to update the IC first. So branch to the 'updateIC' block instead,
    //
    BasicBlock* icMissBB = term->getParent();
    ReleaseAssert(icMissBB != nullptr);
    ReleaseAssert(icMissBB != pred);
    ReleaseAssert(term->isConditional());
    if (term->getSuccessor(0) == bb)
    {
        term->setSuccessor(0, updateIc);
        ReleaseAssert(term->getSuccessor(1) != bb);
    }
    else
    {
        ReleaseAssert(term->getSuccessor(1) == bb);
        term->setSuccessor(1, updateIc);
    }

    // Set up the logic in IC miss path ('updateIc' basic block), which should run the slow path and then populate IC
    //
    Value* icMissCalleeCbHeapPtr = nullptr;
    Value* icMissCodePtr = nullptr;
    Value* fo64 = ifi->CallDeegenCommonSnippet("GetFuncObjAsU64FromTValue", { tv }, updateIcBBEnd /*insertBefore*/);
    ReleaseAssert(llvm_value_has_type<uint64_t>(fo64));
    EmitInterpreterCallIcCacheMissPopulateIcSlowPath(ifi, fo64, icMissCalleeCbHeapPtr /*out*/, icMissCodePtr /*out*/, updateIcBBEnd /*insertBefore*/);
    ReleaseAssert(icMissCalleeCbHeapPtr != nullptr);
    ReleaseAssert(icMissCodePtr != nullptr);

    // Set up the join block logic, which should simply be some PHI instructions that joins the ic-hit path and ic-miss path
    //
    ReleaseAssert(!bb->empty());
    Instruction* phiInsertionPt = bb->getFirstNonPHI();
    PHINode* joinCalleeCbHeapPtr = PHINode::Create(llvm_type_of<HeapPtr<void>>(ctx), 2 /*reserveInDeg*/, "", phiInsertionPt);
    joinCalleeCbHeapPtr->addIncoming(icHitCalleeCbHeapPtr, icHitBB);
    joinCalleeCbHeapPtr->addIncoming(icMissCalleeCbHeapPtr, updateIc);

    PHINode* joinCodePtr = PHINode::Create(llvm_type_of<void*>(ctx), 2 /*reserveInDeg*/, "", phiInsertionPt);
    joinCodePtr->addIncoming(icHitCodePtr, icHitBB);
    joinCodePtr->addIncoming(icMissCodePtr, updateIc);

    // Unfortunately if we do not manually sink the Is<tFunction> typecheck, LLVM could generate somewhat bad code.. so do this rewrite ourselves.
    // Basically the idea is the following: before we have the following IR:
    //
    // pred:
    //     %res = typecheck...
    //     %icHit = testIc ...
    //     br %icHit, %icHitBB, %icMissBB
    // icHitBB:
    //     ...
    //     br joinBB
    // icMissBB:
    //     ...
    //     br %res, joinBB, ...
    // joinBB:
    //     ...
    //
    // If the typecheck is not used in 'pred' block, then we can sink it to 'icHitBB' and 'icMissBB'.
    // For the icHitBB, due to the design of IC, we know it must be a tFunction, so it must be true.
    // For the icMissBB, we honestly execute the check.
    // That is, the transformed IR looks like the following:
    //
    // pred:
    //     %icHit = testIc ...
    //     br %icHit, %icHitBB, %icMissBB
    // icHitBB:
    //     %res.icHit = true
    //     ...
    //     br joinBB
    // icMissBB:
    //     %res.icMiss = typecheck ...
    //     ...
    //     br %res.icMiss, joinBB, ...
    // joinBB:
    //     %res = phi [ icHitBB, %res.icHit ], [ icMissBB, %res.icMiss ]
    //
    // Due to the nature of this transform, after the transform, every user of the original '%res' must be
    // dominated by either 'icMissBB' or 'joinBB'.
    // We can then rewrite all the users to use the corresponding version of '%res' depending on which basic block dominates the user.
    //
    {
        CallInst* typechk = dyn_cast<CallInst>(term->getCondition());
        ReleaseAssert(typechk != nullptr);
        ReleaseAssert(typechk->getParent() == pred);
        Function* callee = typechk->getCalledFunction();
        ReleaseAssert(callee != nullptr && (IsTValueTypeCheckAPIFunction(callee) || IsTValueTypeCheckStrengthReductionFunction(callee)));
        ReleaseAssert(typechk->arg_size() == 1 && typechk->getArgOperand(0) == tv);
        ReleaseAssert((GetCheckedMaskOfTValueTypecheckFunction(callee) & x_typeSpeculationMaskFor<tFunction>) == x_typeSpeculationMaskFor<tFunction>);

        DominatorTree dt(*fn);
        bool isUsedInPredBlock = false;
        for (Use& u : typechk->uses())
        {
            Instruction* inst = dyn_cast<Instruction>(u.getUser());
            ReleaseAssert(inst != nullptr);
            BasicBlock* instBB = inst->getParent();
            ReleaseAssert(instBB != nullptr);
            ReleaseAssert(dt.dominates(pred, u));
            if (instBB == pred)
            {
                isUsedInPredBlock = true;
            }
        }

        if (!isUsedInPredBlock)
        {
            Constant* tcIcHit = CreateLLVMConstantInt<bool>(ctx, true);
            Instruction* tcIcMiss = typechk->clone();
            icMissBB->getInstList().push_front(tcIcMiss);
            PHINode* tcJoin = PHINode::Create(llvm_type_of<bool>(ctx), 2 /*reserveInDeg*/, "", phiInsertionPt);
            tcJoin->addIncoming(tcIcHit, icHitBB);
            tcJoin->addIncoming(tcIcMiss, updateIc);

            std::unordered_map<Use*, Value*> useReplacementMap;
            for (Use& u : typechk->uses())
            {
                Instruction* inst = dyn_cast<Instruction>(u.getUser());
                ReleaseAssert(inst != nullptr);
                BasicBlock* instBB = inst->getParent();
                ReleaseAssert(instBB != nullptr);
                ReleaseAssert(dt.dominates(pred, instBB));
                ReleaseAssert(dt.isReachableFromEntry(instBB));
                bool dominatedByJoinBlock = dt.dominates(bb, instBB);
                bool dominatedByIcMissBlock = dt.dominates(icMissBB, instBB);
                if (dominatedByJoinBlock)
                {
                    ReleaseAssert(!dominatedByIcMissBlock);
                    ReleaseAssert(!useReplacementMap.count(&u));
                    useReplacementMap[&u] = tcJoin;
                }
                else
                {
                    ReleaseAssert(dominatedByIcMissBlock);
                    ReleaseAssert(!useReplacementMap.count(&u));
                    useReplacementMap[&u] = tcIcMiss;
                }
            }
            for (auto& it : useReplacementMap)
            {
                Use* u = it.first;
                Value* replace = it.second;
                ReleaseAssert(u->get()->getType() == replace->getType());
                u->set(replace);
            }
            ReleaseAssert(typechk->use_empty());
            typechk->eraseFromParent();
        }
    }

    calleeCbHeapPtr = joinCalleeCbHeapPtr;
    codePointer = joinCodePtr;
}

void DeegenCallIcLogicCreator::EmitForInterpreter(InterpreterBytecodeImplCreator* ifi,
                                                  llvm::Value* functionObject,
                                                  llvm::Value*& calleeCbHeapPtr /*out*/,
                                                  llvm::Value*& codePointer /*out*/,
                                                  llvm::Instruction* insertBefore)
{
    using namespace llvm;

    if (!ifi->GetBytecodeDef()->HasInterpreterCallIC())
    {
        // Inline cache is not available for whatever reason, just honestly emit the slow path
        //
        EmitInterpreterCallIcSlowPath(ifi, functionObject, calleeCbHeapPtr /*out*/, codePointer /*out*/, insertBefore);
        return;
    }

    ifi->GetBytecodeDef()->m_isInterpreterCallIcEverUsed = true;

    // Currently, the call inline cache is (probably?) not too beneficial for performance unless when the IC check
    // can be hoisted to eliminate a prior 'target.Is<tFunction>()' check (which should happen in the important
    // cases, but note that even if that cannot happen, we still want the IC since it collects important information
    // on the callee).
    //
    // So the current strategy is to try to hoist the IC check first. If that is successful then we are all good. But
    // if it is not successful, we will not emit the "fastpath" that checks the IC, but always execute the slow path
    // and then update the IC.
    //
    // Currently the attempt to hoist is kind of naive: we hoist only if  'functionObject' is created by a TValue::As
    // API (from some value 'tv'), the current bb has only one predecessor, and the terminator of the unique predecessor
    // is a conditional branch conditioned on a TValue tFunction (or any of its superset) typecheck of 'tv'. That is, we
    // try to identify the following pattern:
    //
    // pred: (unique predecessor of bb)
    //     ...
    //     %0 = TValue::Is<tFunction>(%tv)
    //     br %0, bb, ..
    // bb:
    //     %fnObj = TValue::As<tFunction>(%tv)
    //     ...
    //     MakeCall(%fnObj, ...)
    //
    // See comments in 'EmitInterpreterCallIcWithHoistedCheck' for details of the hoisting.
    //
    {
        Value* tv = nullptr;
        BranchInst* brInst = nullptr;
        BasicBlock* bb = insertBefore->getParent();
        ReleaseAssert(bb != nullptr);
        bool canHoist = CheckCanHoistInterpreterCallIcCheck(functionObject, bb, tv /*out*/, brInst /*out*/);
        if (canHoist)
        {
            ReleaseAssert(tv != nullptr);
            ReleaseAssert(brInst != nullptr);
            EmitInterpreterCallIcWithHoistedCheck(ifi, tv, brInst, calleeCbHeapPtr /*out*/, codePointer /*out*/, insertBefore);
            return;
        }
    }

    // When we reach here, we cannot hoist the IC check. So just generate the slow path and update IC
    //
    EmitInterpreterCallIcCacheMissPopulateIcSlowPath(ifi, functionObject, calleeCbHeapPtr /*out*/, codePointer /*out*/, insertBefore);
}

}   // namespace dast
