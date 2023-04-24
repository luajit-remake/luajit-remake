#include "deegen_call_inline_cache.h"
#include "deegen_bytecode_operand.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "tvalue_typecheck_optimization.h"
#include "deegen_stencil_runtime_constant_insertion_pass.h"
#include "deegen_baseline_jit_codegen_logic_creator.h"
#include "deegen_magic_asm_helper.h"
#include "llvm/IR/InlineAsm.h"
#include "deegen_parse_asm_text.h"

namespace dast {

void DeegenCallIcLogicCreator::EmitGenericGetCallTargetLogic(DeegenBytecodeImplCreatorBase* ifi,
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
    DeegenCallIcLogicCreator::EmitGenericGetCallTargetLogic(ifi, functionObject, calleeCbHeapPtr /*out*/, codePointer /*out*/, insertBefore /*insertBefore*/);
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

static bool WARN_UNUSED CheckCanHoistCallIcCheck(llvm::Value* functionObject,
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
        EmitGenericGetCallTargetLogic(ifi, functionObject, calleeCbHeapPtr /*out*/, codePointer /*out*/, insertBefore);
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
        bool canHoist = CheckCanHoistCallIcCheck(functionObject, bb, tv /*out*/, brInst /*out*/);
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

// Emit ASM magic for the CallIC direct-call case (i.e., cache on a fixed FunctionObject)
// We can then identify the ASM magic in the assembly output and do proper transformation
//
static void InsertBaselineJitCallIcMagicAsmForDirectCall(llvm::Module* module,
                                                         llvm::Value* tv,
                                                         llvm::BasicBlock* icHit,
                                                         llvm::BasicBlock* icMiss,
                                                         llvm::BasicBlock* icSlowPath,
                                                         uint64_t unique_ord,
                                                         llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();

    // The LLVM IR is reproduced from the following GCC-style inline ASM
    //
    // asm goto (
    //     "movabsq %[cached_tv], %[t_r0];"
    //     "cmpq %[t_r0], %[i_tv];"
    //     "jne %l[ic_miss];"
    //     "jne %l[ic_slowpath];"
    //         :
    //     [t_r0] "=&r"(tmp_i64) /*scratch*/
    //         :
    //     [i_tv] "r"(tv) /*in*/,
    //     [cached_tv] "i"(&ext_sym) /*in*/
    //         :
    //     "cc" /*clobber*/
    //         :
    //     ic_miss /*goto*/,
    //     ic_slowpath /*goto*/);
    //
    // If you want to change the ASM logic, you'd better modify the above GCC-style ASM, compile it using LLVM and
    // copy whatever LLVM produces, instead of directly modifying the LLVM-style ASM strings below...
    //
    // args: [i64 tv, ptr cached_tv] returns: i64
    //
    // You may ask: why does the above ASM needs both ic_miss and ic_slowpath destination?
    // This is actually REQUIRED FOR CORRECTNESS!
    //
    // If the ASM does not take the ic_slowpath destination, this also would allow LLVM (and Deegen's TValue typecheck
    // optimization, which is in fact the one that causes problems) to assume that the control flow must be
    //      direct-call-check => closure-call-check => ic-slow-path
    // and therefore make assumptions on the preconditions of ic-slow-path based on the check we did in closure-call-check.
    //
    // However, at runtime in reality, if the direct call IC misses, we will directly jump to slow path, and
    // the closure call IC check path doesn't even exist, so the assumption actually does not hold.
    //
    // Therefore, we must teach LLVM that the direct-call check may directly branch to IC slow path by putting it in the GOTO list as well.
    //
    std::string asmText = "movabsq $2, $0;cmpq $0, $1;jne ${3:l};jne ${4:l};";
    std::string constraintText = "=&r,r,i,!i,!i,~{cc},~{dirflag},~{fpsr},~{flags}";

    ReleaseAssert(unique_ord <= 1000000000);
    asmText = "movl $$" + std::to_string(unique_ord) + ", eax;" + asmText;

    asmText = MagicAsm::WrapLLVMAsmPayload(asmText, MagicAsmKind::CallIcDirectCall);

    ReleaseAssert(llvm_value_has_type<uint64_t>(tv));

    GlobalVariable* cpSym = DeegenInsertOrGetCopyAndPatchPlaceholderSymbol(module, 10000 /*CallIcDirectCallCachedValue*/);
    ReleaseAssert(llvm_value_has_type<void*>(cpSym));

    FunctionType* fty = FunctionType::get(llvm_type_of<uint64_t>(ctx), { llvm_type_of<uint64_t>(ctx), llvm_type_of<void*>(ctx) }, false);
    InlineAsm* ia = InlineAsm::get(fty,
                                   asmText,
                                   constraintText,
                                   true /*hasSideEffects*/);
    CallBrInst* inst = CallBrInst::Create(fty, ia, icHit /*fallthroughDest*/, { icMiss, icSlowPath } /*gotoDests*/, { tv, cpSym } /*args*/, "", insertBefore);
    inst->addFnAttr(Attribute::NoUnwind);
    inst->addFnAttr(Attribute::ReadNone);
}

// Emit ASM magic for the CallIC closure-call case (i.e., cache on a fixed CodeBlock)
// We can then identify the ASM magic in the assembly output and do proper transformation
//
static void InsertBaselineJitCallIcMagicAsmForClosureCall(llvm::Module* module,
                                                          llvm::Value* codeBlockSysHeapPtrVal,
                                                          llvm::BasicBlock* icHit,
                                                          llvm::BasicBlock* icMiss,
                                                          uint64_t unique_ord,
                                                          llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();

    // The LLVM IR is reproduced from the following GCC-style inline ASM
    //
    // asm goto (
    //     "cmpl %[cached_cb32], %[i_cb32];"
    //     "jne %l[ic_miss];"
    //     :
    //         /*no output reg or scratch reg*/
    //     :
    //         [i_cb32] "r"(cb32) /*in*/,
    //         [cached_cb32] "i"(&ext_sym) /*in*/
    //     :
    //         "cc" /*clobber*/
    //     :
    //         ic_miss /*goto*/);
    //
    // If you want to change the ASM logic, you'd better modify the above GCC-style ASM, compile it using LLVM and
    // copy whatever LLVM produces, instead of directly modifying the LLVM-style ASM strings below...
    //
    // args: [i32 cb32, ptr cached_cb32] returns: void
    //
    std::string asmText = "cmpl $1, $0;jne ${2:l};";
    std::string constraintText = "r,i,!i,~{cc},~{dirflag},~{fpsr},~{flags}";

    ReleaseAssert(unique_ord <= 1000000000);
    asmText = "movl $$" + std::to_string(unique_ord) + ", eax;" + asmText;

    asmText = MagicAsm::WrapLLVMAsmPayload(asmText, MagicAsmKind::CallIcClosureCall);

    ReleaseAssert(llvm_value_has_type<uint32_t>(codeBlockSysHeapPtrVal));

    GlobalVariable* cpSym = DeegenInsertOrGetCopyAndPatchPlaceholderSymbol(module, 10001 /*CallIcClosureCallCachedValue*/);
    ReleaseAssert(llvm_value_has_type<void*>(cpSym));

    FunctionType* fty = FunctionType::get(llvm_type_of<void>(ctx), { llvm_type_of<uint32_t>(ctx), llvm_type_of<void*>(ctx) }, false);
    InlineAsm* ia = InlineAsm::get(fty,
                                   asmText,
                                   constraintText,
                                   true /*hasSideEffects*/);
    CallBrInst* inst = CallBrInst::Create(fty, ia, icHit /*fallthroughDest*/, { icMiss } /*gotoDests*/, { codeBlockSysHeapPtrVal, cpSym } /*args*/, "", insertBefore);
    // not sure why LLVM doesn't add Attribute::ReadNone for this one, but let's just do what LLVM does...
    //
    inst->addFnAttr(Attribute::NoUnwind);
}

// Clone the basic block containing instruction 'origin', return the cloned instruction in the new BB (and the new BB is just the instruction's parent)
// This function assumes that:
// (1) The basic block has only one predecessor, so no PHI node shall occur in the block
// (2) The basic block is a terminal one, that is, it cannot branch to anyone else (note that this part is not checked in this function!)
//
static llvm::Instruction* WARN_UNUSED CloneBasicBlockContainingInstruction(llvm::Instruction* origin)
{
    using namespace llvm;
    LLVMContext& ctx = origin->getContext();
    BasicBlock* bb = origin->getParent();
    ReleaseAssert(bb != nullptr);
    Function* fn = bb->getParent();
    ReleaseAssert(fn != nullptr);

    BasicBlock* newBB = BasicBlock::Create(ctx, "", fn, bb /*insertBefore*/);
    ReleaseAssert(newBB->getParent() == fn);

    std::unordered_map<Value*, Value*> remap;

    Instruction* result = nullptr;
    for (Instruction& inst : *bb)
    {
        // This function only works on BBs without PHI nodes
        //
        ReleaseAssert(!isa<PHINode>(&inst));
        Instruction* newInst = inst.clone();
        newBB->getInstList().push_back(newInst);
        if (&inst == origin)
        {
            ReleaseAssert(result == nullptr);
            result = newInst;
        }

        for (auto it = newInst->op_begin(); it != newInst->op_end(); it++)
        {
            Value* val = it->get();
            ReleaseAssert(val != &inst);
            if (remap.count(val))
            {
                it->set(remap[val]);
            }
        }

        ReleaseAssert(!remap.count(&inst));
        remap[&inst] = newInst;
    }
    ReleaseAssert(result != nullptr);
    ReleaseAssert(result->getParent() == newBB);
    return result;
}

std::string WARN_UNUSED EmitComputeLabelOffsetAsm(const std::string& fnName, const std::string& labelName)
{
    ReleaseAssert(labelName.length() > 0 && labelName[0] != '.');
    ReleaseAssert(fnName.length() > 0);
    ReleaseAssert(labelName.find("XYZyZYX") == std::string::npos);
    ReleaseAssert(labelName.find(" ")== std::string::npos);
    ReleaseAssert(fnName.find(" ")== std::string::npos);
    std::string varName = "offset_of_label_XYZyZYX_" + labelName + "_XYZyZYX_in_function_" + fnName;
    std::string res = "";
    res += "\n\n\t.type\t" + varName + ",@object\n";
    res += "\t.section\t.rodata." + varName + ",\"a\",@progbits\n";
    res += "\t.globl\t" + varName + "\n";
    res += "\t.p2align\t3\n";
    res += varName + ":\n";
    res += "\t.quad\t." + labelName + "-" + fnName + "\n";
    res += ".size\t" + varName + ", 8\n\n";
    return res;
}

std::vector<DeegenCallIcLogicCreator::BaselineJitLLVMLoweringResult> WARN_UNUSED DeegenCallIcLogicCreator::EmitForBaselineJIT(
    BaselineJitImplCreator* ifi,
    llvm::Value* functionObject,
    uint64_t unique_ord,
    llvm::Instruction* origin)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();

    std::vector<DeegenCallIcLogicCreator::BaselineJitLLVMLoweringResult> finalRes;

    ReleaseAssert(origin->getParent() != nullptr);
    Function* func = origin->getParent()->getParent();
    ReleaseAssert(func != nullptr);
    ReleaseAssert(llvm_value_has_type<uint64_t>(functionObject));

    // Emit the direct call IC hit block, and push the MakeCall instance to finalRes
    //
    //     %calleeCb = <cached>     <--- inserted
    //     %codePtr = <cached>      <--- inserted
    //     MakeCall ....            <--- origin
    //
    auto emitIcDirectCallHitBlock = [&](Instruction* dcHitOrigin)
    {
        ReleaseAssert(isa<CallInst>(dcHitOrigin));

        GlobalVariable* gv = DeegenInsertOrGetCopyAndPatchPlaceholderSymbol(ifi->GetModule(), 10001 /*CallIcClosureCallCachedValue*/);
        Value* dcHitCalleeCb = new AddrSpaceCastInst(gv, llvm_type_of<HeapPtr<void>>(ctx), "", dcHitOrigin);
        Value* dcHitCodePtr = DeegenInsertOrGetCopyAndPatchPlaceholderSymbol(ifi->GetModule(), 10002 /*CallIcCachedCodePtr*/);

        finalRes.push_back({
            .calleeCbHeapPtr = dcHitCalleeCb,
            .codePointer = dcHitCodePtr,
            .origin = dcHitOrigin
        });
    };

    // Emit the direct call IC miss block:
    //
    //     %calleeCb = getCalleeCb(%tv)
    //     <closure-call IC check> => closure-call IC hit/miss
    //
    // Returns calleeCbU32
    //
    auto emitIcClosureCallCheckBlock = [&](BasicBlock* dcMissBB, Value* tv, BasicBlock* ccHitBB, BasicBlock* ccMissBB) WARN_UNUSED -> Value*
    {
        ReleaseAssert(dcMissBB->empty());
        UnreachableInst* dummy = new UnreachableInst(ctx, dcMissBB);
        // Kind of bad, we are hardcoding the assumption that the CalleeCbHeapPtr is just the zero-extension of calleeCbU32.. but stay simple for now..
        //
        Value* calleeCbU32 = ifi->CallDeegenCommonSnippet("GetCalleeCbU32FromTValue", { tv }, dummy /*insertBefore*/);
        InsertBaselineJitCallIcMagicAsmForClosureCall(ifi->GetModule(), calleeCbU32, ccHitBB, ccMissBB, unique_ord, dummy /*insertBefore*/);
        dummy->eraseFromParent();
        ReleaseAssert(llvm_value_has_type<uint32_t>(calleeCbU32));
        return calleeCbU32;
    };

    // Emit the closure call IC hit block, and push the MakeCall instance to finalRes
    //
    //     %codePtr = <cached>      <--- inserted
    //     MakeCall ....            <--- origin
    //
    auto emitIcClosureCallHitBlock = [&](Instruction* ccHitOrigin, Value* calleeCbU32)
    {
        ReleaseAssert(isa<CallInst>(ccHitOrigin));
        ReleaseAssert(llvm_value_has_type<uint32_t>(calleeCbU32));

        Value* calleeCbU64 = new ZExtInst(calleeCbU32, llvm_type_of<uint64_t>(ctx), "", ccHitOrigin);
        Value* calleeCbHeapPtr = new IntToPtrInst(calleeCbU64, llvm_type_of<HeapPtr<void>>(ctx), "", ccHitOrigin);
        Value* codePtr = DeegenInsertOrGetCopyAndPatchPlaceholderSymbol(ifi->GetModule(), 10002 /*CallIcCachedCodePtr*/);

        finalRes.push_back({
            .calleeCbHeapPtr = calleeCbHeapPtr,
            .codePointer = codePtr,
            .origin = ccHitOrigin
        });
    };

    // Emit logic for the IC miss block, and push the MakeCall instance to finalRes
    //
    // Basically, we just need to call the create IC functor, which creates the IC and returns the CalleeCbHeapPtr and CodePtr
    //
    // However, currently the create IC takes CodeBlock, execFuncPtr and funcObj, and returns nothing for simplicity
    // TODO: make it return CalleeCbHeapPtr and CodePtr so the slow path is slightly faster
    //
    auto emitIcMissBlock = [&](Instruction* icMissOrigin, Value* functionObjectTarget)
    {
#if 0
        std::string icCreatorFnName = "__deegen_baseline_jit_codegen_" + ifi->GetBytecodeDef()->GetBytecodeIdName() + "_call_ic_" + std::to_string(unique_ord);
        FunctionType* fty = FunctionType::get(llvm_type_of<void>(ctx), { llvm_type_of<void*>(ctx), llvm_type_of<void*>(ctx), llvm_type_of<uint64_t>(ctx) }, false);

        ReleaseAssert(ifi->GetModule()->getNamedValue(icCreatorFnName) == nullptr);
        Function* icCreatorFn = Function::Create(fty, GlobalValue::ExternalLinkage, icCreatorFnName, ifi->GetModule());
        icCreatorFn->addFnAttr(Attribute::NoUnwind);
        ReleaseAssert(icCreatorFn->getName() == icCreatorFnName);
        icCreatorFn->setCallingConv(CallingConv::PreserveMost);

        CallInst* ci = CallInst::Create(icCreatorFn, { ifi->GetCodeBlock(), func, functionObjectTarget }, "", icMissOrigin);
        ci->setCallingConv(CallingConv::PreserveMost);
#endif

        Value* calleeCbHeapPtr = nullptr;
        Value* codePtr = nullptr;
        EmitGenericGetCallTargetLogic(ifi, functionObjectTarget, calleeCbHeapPtr /*out*/, codePtr /*out*/, icMissOrigin);
        ReleaseAssert(calleeCbHeapPtr != nullptr && codePtr != nullptr);

        finalRes.push_back({
            .calleeCbHeapPtr = calleeCbHeapPtr,
            .codePointer = codePtr,
            .origin = icMissOrigin
        });
    };

    {
        Value* tv = nullptr;
        BranchInst* brInst = nullptr;
        BasicBlock* originBB = origin->getParent();
        ReleaseAssert(originBB != nullptr);
        bool canHoist = CheckCanHoistCallIcCheck(functionObject, originBB, tv /*out*/, brInst /*out*/);
        if (canHoist)
        {
            ReleaseAssert(tv != nullptr);
            ReleaseAssert(brInst != nullptr);
            BasicBlock* predBB = brInst->getParent();

            // Precondition:
            //
            // pred: (unique predecessor of originBB)
            //     ...
            //     %0 = TValue::Is<tFunction>(%tv)
            //     ...
            //     br %0, originBB, <not_function>
            //
            // originBB:
            //     %fnObj = TValue::As<tFunction>(%tv)
            //     ...
            //     MakeCall(%fnObj, ...)
            //
            // In this case, we hoist the IC check above the tFunction check, and rewrite the logic to:
            //
            // pred:
            //     ...
            //     %0 = TValue::Is<tFunction>(%tv)
            //     ...
            //     br ic_entry
            //
            // ic_entry:
            //     <direct-call IC check> => direct-call IC hit/miss
            //
            // direct-call IC hit:
            //     %calleeCb = <cached>
            //     %codePtr = <cached>
            //     <clone of originBB>
            //
            // direct-call IC miss:
            //     # This is important: we cannot decode hidden class unless we know the TValue is a pointer!
            //     %1 = TValue::IsPointer(%tv)
            //     br %1, closure_call_ic_entry, <not_function>
            //
            // closure_call_ic_entry:
            //     %calleeCb = getCalleeCb(%tv)
            //     <closure-call IC check> => closure-call IC hit/miss
            //
            // closure-call IC hit:
            //     %codePtr = <cached>
            //     <clone of originBB>
            //
            // closure-call IC miss:
            //      br %0, originBB, <not_function>
            //
            // originBB:
            //     <create IC>
            //     ... rest of the original originBB ...
            //

            // Create and emit the direct-call IC hit block
            //
            Instruction* dcHitOrigin = CloneBasicBlockContainingInstruction(origin);
            BasicBlock* dcHitBB = dcHitOrigin->getParent();
            emitIcDirectCallHitBlock(dcHitOrigin);

            BasicBlock* icEntryBB = BasicBlock::Create(ctx, "", func, originBB /*insertBefore*/);
            BasicBlock* dcMissBB = BasicBlock::Create(ctx, "", func, originBB /*insertBefore*/);

            BranchInst::Create(icEntryBB, brInst);

            BasicBlock* ccEntryBB = BasicBlock::Create(ctx, "", func, originBB /*insertBefore*/);

            Instruction* ccHitOrigin = CloneBasicBlockContainingInstruction(origin);
            BasicBlock* ccHitBB = ccHitOrigin->getParent();

            BasicBlock* ccMissBB = BasicBlock::Create(ctx, "", func, originBB /*insertBefore*/);

            // Emit the direct-call IC check
            //
            {
                UnreachableInst* dummy = new UnreachableInst(ctx, icEntryBB);
                InsertBaselineJitCallIcMagicAsmForDirectCall(ifi->GetModule(), tv, dcHitBB, dcMissBB, ccMissBB, unique_ord, dummy /*insertBefore*/);
                dummy->eraseFromParent();
            }

            // Emit the direct-call IC miss block
            //
            {
                UnreachableInst* dummy = new UnreachableInst(ctx, dcMissBB);
                Value* isHeapEntity = ifi->CallDeegenCommonSnippet("IsTValueHeapEntity", { tv }, dummy);
                ReleaseAssert(llvm_value_has_type<bool>(isHeapEntity));

                // Double-check that our transformation is OK, just to be extra certain
                //
                ReleaseAssert(brInst->isConditional());
                Value* originalCond = brInst->getCondition();
                ReleaseAssert(isa<CallInst>(originalCond));
                Function* originalCondFn = cast<CallInst>(originalCond)->getCalledFunction();
                ReleaseAssert(originalCondFn != nullptr);
                ReleaseAssert(IsTValueTypeCheckAPIFunction(originalCondFn) || IsTValueTypeCheckStrengthReductionFunction(originalCondFn));
                TypeSpeculationMask checkedMask = GetCheckedMaskOfTValueTypecheckFunction(originalCondFn);
                // Having passed this assert, we know brInst is checking a typemask at least as strict as tHeapEntity
                // (which should always be true since tHeapEntity is the only thing between tFunction and tTop, and we have
                // checked earlier that the condition covers tFunction), so we are good
                //
                ReleaseAssert((checkedMask & x_typeSpeculationMaskFor<tHeapEntity>) == checkedMask);

                // Add likely_true intrinsic for isHeapEntity to avoid breaking hot-cold splitting
                //
                Function* expectIntrin = Intrinsic::getDeclaration(func->getParent(), Intrinsic::expect, { Type::getInt1Ty(ctx) });
                isHeapEntity = CallInst::Create(expectIntrin, { isHeapEntity, CreateLLVMConstantInt<bool>(ctx, true) }, "", dummy);

                BranchInst::Create(ccEntryBB /*ifTrue*/, brInst->getSuccessor(1 /*falseBranch*/), isHeapEntity, dummy);
                dummy->eraseFromParent();
            }

            // Emit the direct-call IC miss block and closure-call IC hit block
            //
            {
                Value* calleeCbU32 = emitIcClosureCallCheckBlock(ccEntryBB, tv, ccHitBB, ccMissBB);
                ReleaseAssert(llvm_value_has_type<uint32_t>(calleeCbU32));
                emitIcClosureCallHitBlock(ccHitOrigin, calleeCbU32);
            }

            // Emit the closure-call IC miss block
            //
            {
                ReleaseAssert(brInst->getParent() != nullptr);
                brInst->removeFromParent();
                ccMissBB->getInstList().push_back(brInst);
            }

            // Emit the IC miss block, which is also the original 'bb' block
            //
            emitIcMissBlock(origin /*icMissOrigin*/, functionObject);

            ValidateLLVMFunction(func);

            // Sink the TValue::Is<tFunction> check (see comments in EmitInterpreterCallIcWithHoistedCheck)
            //
            {
                CallInst* typechk = dyn_cast<CallInst>(brInst->getCondition());
                ReleaseAssert(typechk != nullptr);
                ReleaseAssert(typechk->getParent() == predBB);
                Function* callee = typechk->getCalledFunction();
                ReleaseAssert(callee != nullptr && (IsTValueTypeCheckAPIFunction(callee) || IsTValueTypeCheckStrengthReductionFunction(callee)));
                ReleaseAssert(typechk->arg_size() == 1 && typechk->getArgOperand(0) == tv);
                ReleaseAssert((GetCheckedMaskOfTValueTypecheckFunction(callee) & x_typeSpeculationMaskFor<tFunction>) == x_typeSpeculationMaskFor<tFunction>);

                DominatorTree dt(*func);
                bool isUsedInPredBlock = false;
                for (Use& u : typechk->uses())
                {
                    Instruction* inst = dyn_cast<Instruction>(u.getUser());
                    ReleaseAssert(inst != nullptr);
                    BasicBlock* instBB = inst->getParent();
                    ReleaseAssert(instBB != nullptr);
                    ReleaseAssert(dt.dominates(predBB, u));
                    if (instBB == predBB)
                    {
                        isUsedInPredBlock = true;
                    }
                }

                if (!isUsedInPredBlock)
                {
                    std::unordered_map<Use*, Value*> useReplacementMap;
                    for (Use& u : typechk->uses())
                    {
                        Instruction* inst = dyn_cast<Instruction>(u.getUser());
                        ReleaseAssert(inst != nullptr);
                        BasicBlock* instBB = inst->getParent();
                        ReleaseAssert(instBB != nullptr);
                        ReleaseAssert(dt.dominates(predBB, instBB));
                        ReleaseAssert(dt.isReachableFromEntry(instBB));
                        bool dominatedByIcMissBlock = dt.dominates(ccMissBB, instBB);
                        if (dominatedByIcMissBlock)
                        {
                            continue;
                        }
                        ReleaseAssert(dt.dominates(ccHitBB, instBB) || dt.dominates(dcHitBB, instBB));
                        ReleaseAssert(!useReplacementMap.count(&u));
                        useReplacementMap[&u] = CreateLLVMConstantInt<bool>(ctx, true);
                    }
                    for (auto& it : useReplacementMap)
                    {
                        Use* u = it.first;
                        Value* replace = it.second;
                        ReleaseAssert(u->get()->getType() == replace->getType());
                        u->set(replace);
                    }
                    ReleaseAssert(typechk->getParent() != nullptr);
                    typechk->removeFromParent();
                    ccMissBB->getInstList().push_front(typechk);
                }
            }

            ValidateLLVMFunction(func);
            return finalRes;
        }
    }

    // We want to rewrite
    //
    //     MakeCall(%fnObj, ...)
    //
    // to the following:
    //
    //     # Our IC direct call case currently always caches TValue, not function object.
    //     # It seems like a questionable design for me, but in our boxing scheme, the TValue::Create is a no-op,
    //     # so let's just go with it for now and think about what's the best design when we actually
    //     # need to support other boxing schemes...
    //     #
    //     %tv = TValue::Create(%fnObj)
    //     <direct-call IC check> => direct-call IC hit/miss
    //
    // direct-call IC hit:
    //     %calleeCb = <cached>
    //     %codePtr = <cached>
    //     ... do call ...
    //
    // direct-call IC miss:
    //     %calleeCb = getCalleeCb(%tv)
    //     <closure-call IC check> => closure-call IC hit/IC miss
    //
    // closure-call IC hit:
    //     %codePtr = <cached>
    //     ... do call ...
    //
    // IC miss:
    //     <create IC>
    //     ... do call ...
    //
    {
        Value* tv = ifi->CallDeegenCommonSnippet("BoxFunctionObjectToTValue", { functionObject }, origin);
        ReleaseAssert(llvm_value_has_type<uint64_t>(tv));

        AssertInstructionIsFollowedByUnreachable(origin);

        BasicBlock* icMissBB = BasicBlock::Create(ctx, "", func);
        BasicBlock* dcHitBB = BasicBlock::Create(ctx, "", func, icMissBB /*insertBefore*/);
        BasicBlock* dcMissBB = BasicBlock::Create(ctx, "", func, icMissBB /*insertBefore*/);
        BasicBlock* ccHitBB = BasicBlock::Create(ctx, "", func, icMissBB /*insertBefore*/);

        InsertBaselineJitCallIcMagicAsmForDirectCall(ifi->GetModule(), tv, dcHitBB, dcMissBB, icMissBB, unique_ord, origin /*insertBefore*/);

        Value* calleeCbU32 = emitIcClosureCallCheckBlock(dcMissBB, tv, ccHitBB, icMissBB);

        Instruction* ccHitOrigin = origin->clone();
        ccHitBB->getInstList().push_back(ccHitOrigin);
        new UnreachableInst(ctx, ccHitBB);

        emitIcClosureCallHitBlock(ccHitOrigin, calleeCbU32);

        Instruction* dcHitOrigin = origin->clone();
        dcHitBB->getInstList().push_back(dcHitOrigin);
        new UnreachableInst(ctx, dcHitBB);

        emitIcDirectCallHitBlock(dcHitOrigin);

        Instruction* unreachableAfterOrigin = origin->getNextNode();
        ReleaseAssert(isa<UnreachableInst>(unreachableAfterOrigin));
        origin->removeFromParent();
        unreachableAfterOrigin->removeFromParent();

        icMissBB->getInstList().push_back(origin);
        icMissBB->getInstList().push_back(unreachableAfterOrigin);

        emitIcMissBlock(origin, functionObject);

        ValidateLLVMFunction(func);
        return finalRes;
    }
}

// The input ASM looks like the following:
//
//      ASM Block A                    ASM Block B                        ASM Block C
// ....  main func  ....
// [       magic       ]  ~~~~~~>  [ load hidden class ]
// [   DirectCall IC   ]           [       magic       ]  ~~~~~~~~~~~> [ IC miss slowpath ]
//                                 [   ClosureCall IC  ]
//
// and we want to rewrite it to the below three pieces:
//
// Main function piece:
//       ....  main func ....
//   smc_region_start:            ------------------
//      ....                        Self-Modifying
//      ....                         Code Region
//   smc_region_end:              ------------------
//
// At runtime, the SMC region starts with
//       jmp ic_miss_slowpath
//       nop * N
//
// When in DirectCall IC mode, the SMC region would be rewritten to:
//       jmp first_direct_call_ic
//       nop * N
//
// When in ClosureCall IC mode, the SMC region would be rewritten to:
//       [ load hidden class ]
//       jmp first_closure_call_ic
//
// (this is why we must reserve code space for the load hidden class logic in the main function)
//
// For the DirectCall and ClosureCall IC piece, we can mostly extract directly without any special handling,
// except that we must change the IC miss branch target to a stencil hole.
//
// Note that, the SMC region returned after this function is not final.
// In order for IC and main function to be extracted correctly, we must correctly preserve the asm CFG
// (so that our ASM CFG analysis can work correctly), including hidden control flow edges that only exists at runtime.
//
// Therefore, the SMC region returned after this function contains unnecessary jumps (only so that our
// CFG analysis can see those control flow edges). Specifically, it looks like the following:
//
//     [ load HC ]
//     jp ccIcMissEntry     // use a fake conditional jump only to tell our CFG analysis the control flow, same below
//     jp dcIcMissEntry
//     jmp __fake_dest
//
// After IC extraction, the SMC region needs to be fixed up by calling BaselineJitAsmLoweringResult::FixupSMCRegion,
// which simply make it unconditionally jump to 'ccIcMissEntry'.
//
std::vector<DeegenCallIcLogicCreator::BaselineJitAsmLoweringResult> WARN_UNUSED DeegenCallIcLogicCreator::DoBaselineJitAsmLowering(X64AsmFile* file)
{
    std::vector<DeegenCallIcLogicCreator::BaselineJitAsmLoweringResult> r;

    // Process one IC use, identified by a DirectCallIC asm magic (from this magic we can find everything we need)
    //
    auto processOne = [&](X64AsmBlock* dcBlock, size_t lineOrd)
    {
        ReleaseAssert(lineOrd < dcBlock->m_lines.size());
        ReleaseAssert(dcBlock->m_lines[lineOrd].IsMagicInstructionOfKind(MagicAsmKind::CallIcDirectCall));
        AsmMagicPayload* dcPayload = dcBlock->m_lines[lineOrd].m_magicPayload;
        // The DirectCall IC magic has the following payload:
        //     movl $uniq_id, eax
        //     movabsq $cached_val, someReg
        //     cmp someReg, tv
        //     jne closure_call_ic_entry
        //     jne dc_ic_miss_slowpath
        //
        ReleaseAssert(dcPayload->m_lines.size() == 5);

        // Decode the movl $uniq_id, eax line to get uniq_id
        //
        auto getIdFromMagicPayloadLine = [&](X64AsmLine& line) WARN_UNUSED -> uint64_t
        {
            ReleaseAssert(line.NumWords() == 3 && line.GetWord(0) == "movl" && line.GetWord(2) == "eax");
            std::string s = line.GetWord(1);
            ReleaseAssert(s.starts_with("$") && s.ends_with(","));
            int val = StoiOrFail(s.substr(1, s.length() - 2));
            ReleaseAssert(val >= 0);
            return static_cast<uint64_t>(val);
        };

        uint64_t icUniqueOrd = getIdFromMagicPayloadLine(dcPayload->m_lines[0]);

        ReleaseAssert(dcPayload->m_lines[3].IsConditionalJumpInst());
        std::string ccBlockLabel = dcPayload->m_lines[3].GetWord(1);
        ReleaseAssert(file->m_labelNormalizer.QueryLabelExists(ccBlockLabel));
        ccBlockLabel = file->m_labelNormalizer.GetNormalizedLabel(ccBlockLabel);

        ReleaseAssert(dcPayload->m_lines[4].IsConditionalJumpInst());
        std::string dcIcMissSlowPathLabel = dcPayload->m_lines[4].GetWord(1);
        dcIcMissSlowPathLabel = file->m_labelNormalizer.GetNormalizedLabel(dcIcMissSlowPathLabel);

        X64AsmBlock* ccBlock = nullptr;
        for (X64AsmBlock* block : file->m_blocks)
        {
            if (block->m_normalizedLabelName == ccBlockLabel)
            {
                ccBlock = block;
                break;
            }
        }
        ReleaseAssert(ccBlock != nullptr);
        // No reason to have a bug here, but still check it to fail early instead of getting weird bugs when splitting blocks...
        //
        ReleaseAssert(ccBlock != dcBlock);

        // Locate the ClosureCall IC magic in ccBlock
        // We are assuming that loading the hidden class would not introduce control flow. But this should be true for any sane VM design.
        //
        size_t ccMagicAsmOrd = static_cast<size_t>(-1);
        for (size_t i = 0; i < ccBlock->m_lines.size(); i++)
        {
            if (ccBlock->m_lines[i].IsMagicInstructionOfKind(MagicAsmKind::CallIcClosureCall))
            {
                ccMagicAsmOrd = i;
                break;
            }
        }
        ReleaseAssert(ccMagicAsmOrd != static_cast<size_t>(-1));

        AsmMagicPayload* ccPayload = ccBlock->m_lines[ccMagicAsmOrd].m_magicPayload;
        // The ClosureCall IC magic has the following payload:
        //     movl $uniq_id, eax
        //     cmp $cached_val, hidden_class
        //     jne cc_ic_miss_slowpath
        //
        ReleaseAssert(ccPayload->m_lines.size() == 3);
        ReleaseAssert(getIdFromMagicPayloadLine(ccPayload->m_lines[0]) == icUniqueOrd);

        ReleaseAssert(ccPayload->m_lines[2].IsConditionalJumpInst());
        std::string ccIcMissSlowPathLabel = ccPayload->m_lines[2].GetWord(1);
        ReleaseAssert(file->m_labelNormalizer.QueryLabelExists(ccIcMissSlowPathLabel));
        ccIcMissSlowPathLabel = file->m_labelNormalizer.GetNormalizedLabel(ccIcMissSlowPathLabel);

        ReleaseAssert(dcIcMissSlowPathLabel != ccBlockLabel);
        ReleaseAssert(ccIcMissSlowPathLabel != ccBlockLabel);
        ReleaseAssert(dcIcMissSlowPathLabel != dcBlock->m_normalizedLabelName);
        ReleaseAssert(ccIcMissSlowPathLabel != dcBlock->m_normalizedLabelName);

        // Now, perform the transform
        // Split 'block' after line 'lineOrd' (the line for DirectCall IC magic)
        //
        X64AsmBlock* pred = nullptr;
        X64AsmBlock* dcEntry = nullptr;
        dcBlock->SplitAtLine(file, lineOrd + 1, pred /*out*/, dcEntry /*out*/);
        ReleaseAssert(pred != nullptr && dcEntry != nullptr);

        // Set up 'dcEntry' block
        //
        {
            // Currently dcEntry is
            //     [ impl  ]
            //
            // We want
            //     movabsq $cached_val, someReg     # dcMagic line 1
            //     cmp someReg, tv                  # dcMagic line 2
            //     jne placeholder_call_ic_miss     # dcMagic line 3 with operand changed
            //     [ impl  ]
            //
            std::vector<X64AsmLine> newList;
            newList.push_back(dcPayload->m_lines[1]);
            newList.push_back(dcPayload->m_lines[2]);
            newList.push_back(dcPayload->m_lines[3]);
            ReleaseAssert(newList.back().IsConditionalJumpInst());
            newList.back().GetWord(1) = "__deegen_cp_placeholder_10003" /*ic_miss*/;

            for (size_t i = 0; i < dcEntry->m_lines.size(); i++)
            {
                newList.push_back(std::move(dcEntry->m_lines[i]));
            }
            dcEntry->m_lines = std::move(newList);
        }

        // Split 'ccBlock' after line 'ccMagicAsmOrd'
        //
        X64AsmBlock* smc = nullptr;
        X64AsmBlock* ccEntry = nullptr;
        ccBlock->SplitAtLine(file, ccMagicAsmOrd + 1, smc /*out*/, ccEntry /*out*/);
        ReleaseAssert(smc != nullptr && ccEntry != nullptr);

        // Set up 'ccEntry' block
        //
        {
            // Currently ccEntry is
            //     [ impl ]
            //
            // We want
            //     cmp $cached_val, hidden_class    # ccMagic line 1
            //     jne placeholder_call_ic_miss     # ccMagic line 2 with operand changed
            //     [ impl ]
            //
            std::vector<X64AsmLine> newList;
            newList.push_back(ccPayload->m_lines[1]);
            newList.push_back(ccPayload->m_lines[2]);
            ReleaseAssert(newList.back().IsConditionalJumpInst());
            newList.back().GetWord(1) = "__deegen_cp_placeholder_10003" /*ic_miss*/;

            for (size_t i = 0; i < ccEntry->m_lines.size(); i++)
            {
                newList.push_back(std::move(ccEntry->m_lines[i]));
            }
            ccEntry->m_lines = std::move(newList);
        }

        // Currently pred is
        //     [    ....     ]
        //     [    magic    ]
        //     [ jmp dcEntry ]
        //
        // Remove the 'magic' line and make it jump to 'smc'
        //
        {
            ReleaseAssert(pred->m_lines.size() >= 2);
            ReleaseAssert(pred->m_lines[pred->m_lines.size() - 2].IsMagicInstructionOfKind(MagicAsmKind::CallIcDirectCall));
            pred->m_lines[pred->m_lines.size() - 2] = pred->m_lines.back();
            pred->m_lines.pop_back();

            ReleaseAssert(pred->m_lines.back().IsDirectUnconditionalJumpInst());
            ReleaseAssert(pred->m_lines.back().GetWord(1) == dcEntry->m_normalizedLabelName);

            pred->m_lines.back().GetWord(1) = smc->m_normalizedLabelName;
            ReleaseAssert(pred->m_endsWithJmpToLocalLabel);
            pred->m_terminalJmpTargetLabel = smc->m_normalizedLabelName;
        }

        // Currently 'smc' is
        //     [ load HC ]
        //     [ magic   ]
        //     jmp ccEntry
        //
        // Remove the magic line and append fake jump instructions to demonstrate the control flow edges
        // (see comments before this function)
        //
        {
            ReleaseAssert(smc->m_lines.size() >= 2);
            ReleaseAssert(smc->m_lines[smc->m_lines.size() - 2].IsMagicInstructionOfKind(MagicAsmKind::CallIcClosureCall));

            X64AsmLine termJmp = smc->m_lines.back();
            smc->m_lines.pop_back();
            smc->m_lines.pop_back();

            ReleaseAssert(termJmp.IsDirectUnconditionalJumpInst() && termJmp.GetWord(1) == ccEntry->m_normalizedLabelName);
            termJmp.GetWord(1) = "__deegen_fake_jmp_dest";

            smc->m_lines.push_back(X64AsmLine::Parse("\tjp\t" + ccIcMissSlowPathLabel));
            smc->m_lines.push_back(X64AsmLine::Parse("\tjp\t" + dcIcMissSlowPathLabel));
            smc->m_lines.push_back(termJmp);

            ReleaseAssert(smc->m_endsWithJmpToLocalLabel);
            smc->m_endsWithJmpToLocalLabel = false;
            smc->m_terminalJmpTargetLabel = "";
        }

        // Now the control flow looks like this (detached = no longer reachable from function entry):
        //
        // pred:                            # not in block list
        //     [ .... ]
        //     jmp smc
        //
        // smc:                             # not in block list
        //     [ Load HC ]
        //     jmp slow
        //
        // slow:                            # in block list
        //     ....
        //
        // ccEntry:                         # detached, not in block list
        //     check closure call IC hit
        //     jne placeholder_ic_miss
        //     ....
        //
        // dcEntry:                         # detached, not in block list
        //     check direct call IC hit
        //     jne placeholder_ic_miss
        //     ....
        //
        // junk blocks in block list: dcBlock, ccBlock
        //
        // To preserve the existing code layout by LLVM as much as possible, we should insert [pred, smc, dcEntry] after dcBlock,
        // and [ccEntry] after ccBlock, then remove dcBlock and ccBlock
        //
        // Note that the block list is temporarily in inconsistent state after inserting blocks as there are duplicate labels
        // so make sure to remove block and restore to consistent state immediately after
        //
        file->InsertBlocksAfter({ pred, smc, dcEntry }, dcBlock /*insertAfter*/);
        file->RemoveBlock(dcBlock);

        file->InsertBlocksAfter({ ccEntry }, ccBlock /*insertAfter*/);
        file->RemoveBlock(ccBlock);

        file->Validate();

        r.push_back({
            .m_labelForSMCRegion = smc->m_normalizedLabelName,
            .m_labelForDirectCallIc = dcEntry->m_normalizedLabelName,
            .m_labelForClosureCallIc = ccEntry->m_normalizedLabelName,
            .m_labelForCcIcMissLogic = ccIcMissSlowPathLabel,
            .m_labelForDcIcMissLogic = dcIcMissSlowPathLabel,
            .m_uniqueOrd = icUniqueOrd
        });
    };

    // Process one by one to avoid iterator invalidation problems
    //
    auto findAndProcessOne = [&]() WARN_UNUSED -> bool
    {
        for (X64AsmBlock* block : file->m_blocks)
        {
            for (size_t i = 0; i < block->m_lines.size(); i++)
            {
                if (block->m_lines[i].IsMagicInstructionOfKind(MagicAsmKind::CallIcDirectCall))
                {
                    processOne(block, i);
                    return true;
                }
            }
        }
        return false;
    };

    while (findAndProcessOne()) { }

    // Sanity check the unique ordinals are indeed unique
    //
    {
        std::unordered_set<uint64_t> checkUnique;
        for (auto& item : r)
        {
            ReleaseAssert(!checkUnique.count(item.m_uniqueOrd));
            checkUnique.insert(item.m_uniqueOrd);
        }
    }

    return r;
}

// See comments of the above function
//
// Note that the final SMC region in the (pre-processed) ASM code is the logic for closure-call mode
// because it must be the longest (the SMC region for direct-call mode is simply one jump without any setup logic)
// This is slightly tricky, but should be fine for now...
//
void DeegenCallIcLogicCreator::BaselineJitAsmLoweringResult::FixupSMCRegionAfterCFGAnalysis(X64AsmFile* file)
{
    X64AsmBlock* block = nullptr;
    for (X64AsmBlock* b : file->m_blocks)
    {
        if (b->m_normalizedLabelName == m_labelForSMCRegion)
        {
            ReleaseAssert(block == nullptr);
            block = b;
        }
    }
    ReleaseAssert(block != nullptr);

    ReleaseAssert(block->m_lines.size() >= 3);
    X64AsmLine termJmp = block->m_lines.back();
    ReleaseAssert(termJmp.IsDirectUnconditionalJumpInst() && termJmp.GetWord(1) == "__deegen_fake_jmp_dest");
    block->m_lines.pop_back();

    ReleaseAssert(block->m_lines.back().NumWords() == 2 &&
                  block->m_lines.back().GetWord(0) == "jp" &&
                  block->m_lines.back().GetWord(1) == m_labelForDcIcMissLogic);
    block->m_lines.pop_back();

    ReleaseAssert(block->m_lines.back().NumWords() == 2 &&
                  block->m_lines.back().GetWord(0) == "jp" &&
                  block->m_lines.back().GetWord(1) == m_labelForCcIcMissLogic);
    block->m_lines.pop_back();

    termJmp.GetWord(1) = m_labelForCcIcMissLogic;
    block->m_lines.push_back(termJmp);

    ReleaseAssert(!block->m_endsWithJmpToLocalLabel);
    block->m_endsWithJmpToLocalLabel = true;
    block->m_terminalJmpTargetLabel = m_labelForCcIcMissLogic;
}

}   // namespace dast
