#include "deegen_call_inline_cache.h"
#include "deegen_bytecode_operand.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_jit_slow_path_data.h"
#include "tvalue_typecheck_optimization.h"
#include "deegen_stencil_runtime_constant_insertion_pass.h"
#include "deegen_baseline_jit_codegen_logic_creator.h"
#include "deegen_magic_asm_helper.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Linker/Linker.h"
#include "deegen_parse_asm_text.h"
#include "invoke_clang_helper.h"
#include "drt/baseline_jit_codegen_helper.h"
#include "deegen_stencil_reserved_placeholder_ords.h"
#include "deegen_stencil_fixup_cross_reference_helper.h"

namespace dast {

void DeegenCallIcLogicCreator::EmitGenericGetCallTargetLogic(DeegenBytecodeImplCreatorBase* ifi,
                                                             llvm::Value* functionObject,
                                                             llvm::Value*& calleeCb /*out*/,
                                                             llvm::Value*& codePointer /*out*/,
                                                             llvm::Instruction* insertBefore)
{
    using namespace llvm;
    Value* functionObjectPtr = new IntToPtrInst(functionObject, llvm_type_of<void*>(functionObject->getContext()), "", insertBefore);
    Value* codeBlockAndEntryPoint = ifi->CallDeegenCommonSnippet("GetCalleeEntryPoint", { functionObjectPtr }, insertBefore);
    ReleaseAssert(codeBlockAndEntryPoint->getType()->isAggregateType());

    calleeCb = ExtractValueInst::Create(codeBlockAndEntryPoint, { 0 /*idx*/ }, "", insertBefore);
    codePointer = ExtractValueInst::Create(codeBlockAndEntryPoint, { 1 /*idx*/ }, "", insertBefore);
    ReleaseAssert(llvm_value_has_type<void*>(calleeCb));
    ReleaseAssert(llvm_value_has_type<void*>(codePointer));
}

static void EmitInterpreterCallIcCacheMissPopulateIcSlowPath(InterpreterBytecodeImplCreator* ifi,
                                                             llvm::Value* functionObject, // HEAPPTR
                                                             llvm::Value*& calleeCb /*out*/,
                                                             llvm::Value*& codePointer /*out*/,
                                                             llvm::Instruction* insertBefore)
{
    using namespace llvm;
    calleeCb = nullptr;
    codePointer = nullptr;
    DeegenCallIcLogicCreator::EmitGenericGetCallTargetLogic(ifi, functionObject, calleeCb /*out*/, codePointer /*out*/, insertBefore /*insertBefore*/);
    ReleaseAssert(calleeCb != nullptr);
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

    // The doubly link is only needed for tiering up (and only exists when tiering up is enabled)
    //
    if (x_allow_interpreter_tier_up_to_baseline_jit)
    {
        Value* doublyLinkAddr = ic.GetDoublyLink()->EmitGetAddress(ifi->GetModule(), ifi->GetBytecodeMetadataPtr(), insertBefore);
        ifi->CallDeegenCommonSnippet("UpdateInterpreterCallIcDoublyLink", { calleeCb, doublyLinkAddr }, insertBefore);
    }
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
                                                  llvm::Value*& calleeCb /*out*/,
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

    Value* icHitCalleeCb = CreateCallToDeegenCommonSnippet(ifi->GetModule(), "GetCbFromTValueFuncObj", { tv }, icHitBB);
    ReleaseAssert(llvm_value_has_type<void*>(icHitCalleeCb));
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
    Value* icMissCalleeCb = nullptr;
    Value* icMissCodePtr = nullptr;
    EmitInterpreterCallIcCacheMissPopulateIcSlowPath(ifi, tv, icMissCalleeCb /*out*/, icMissCodePtr /*out*/, updateIcBBEnd /*insertBefore*/);
    ReleaseAssert(icMissCalleeCb != nullptr);
    ReleaseAssert(icMissCodePtr != nullptr);

    // Set up the join block logic, which should simply be some PHI instructions that joins the ic-hit path and ic-miss path
    //
    ReleaseAssert(!bb->empty());
    Instruction* phiInsertionPt = bb->getFirstNonPHI();
    PHINode* joinCalleeCb = PHINode::Create(llvm_type_of<void*>(ctx), 2 /*reserveInDeg*/, "", phiInsertionPt);
    joinCalleeCb->addIncoming(icHitCalleeCb, icHitBB);
    joinCalleeCb->addIncoming(icMissCalleeCb, updateIc);

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

    calleeCb = joinCalleeCb;
    codePointer = joinCodePtr;
}

void DeegenCallIcLogicCreator::EmitForInterpreter(InterpreterBytecodeImplCreator* ifi,
                                                  llvm::Value* functionObject,
                                                  llvm::Value*& calleeCb /*out*/,
                                                  llvm::Value*& codePointer /*out*/,
                                                  llvm::Instruction* insertBefore)
{
    using namespace llvm;

    if (!ifi->GetBytecodeDef()->HasInterpreterCallIC())
    {
        // Inline cache is not available for whatever reason, just honestly emit the slow path
        //
        EmitGenericGetCallTargetLogic(ifi, functionObject, calleeCb /*out*/, codePointer /*out*/, insertBefore);
        return;
    }

    ifi->GetBytecodeDef()->m_isInterpreterCallIcEverUsed = true;

    // Currently, the interpreter call inline cache is (probably?) not too beneficial for performance unless when the IC
    // check can be hoisted to eliminate a prior 'target.Is<tFunction>()' check (which should happen in the important
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
            EmitInterpreterCallIcWithHoistedCheck(ifi, tv, brInst, calleeCb /*out*/, codePointer /*out*/, insertBefore);
            return;
        }
    }

    // When we reach here, we cannot hoist the IC check. So just generate the slow path and update IC
    //
    EmitInterpreterCallIcCacheMissPopulateIcSlowPath(ifi, functionObject, calleeCb /*out*/, codePointer /*out*/, insertBefore);
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

    GlobalVariable* cpSym = DeegenInsertOrGetCopyAndPatchPlaceholderSymbol(module, CP_PLACEHOLDER_CALL_IC_DIRECT_CALL_TVALUE);
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

    GlobalVariable* cpSym = DeegenInsertOrGetCopyAndPatchPlaceholderSymbol(module, CP_PLACEHOLDER_CALL_IC_CALLEE_CB32);
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

static llvm::FunctionType* WARN_UNUSED GetBaselineJitCallIcSlowPathFnPrototype(llvm::LLVMContext& ctx)
{
    using namespace llvm;
    StructType* retTy = StructType::get(ctx, { llvm_type_of<void*>(ctx) /*calleeCb32*/, llvm_type_of<void*>(ctx) /*codePtr*/ });
    FunctionType* fty = FunctionType::get(
        retTy,
        {
            llvm_type_of<void*>(ctx),           // BaselineCodeBlock
            llvm_type_of<uint64_t>(ctx),        // slowPathDataOffset
            llvm_type_of<void*>(ctx),           // slowPathAddr for this stencil
            llvm_type_of<void*>(ctx),           // dataSecAddr for this stencil
            llvm_type_of<uint64_t>(ctx)         // target tvalue
        },
        false /*isVarArg*/);

    return fty;
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

        GlobalVariable* gv = DeegenInsertOrGetCopyAndPatchPlaceholderSymbol(ifi->GetModule(), CP_PLACEHOLDER_CALL_IC_CALLEE_CB32);
        Value* iGv = new PtrToIntInst(gv, llvm_type_of<uint32_t>(ctx), "", dcHitOrigin);
        Value* dcHitCalleeCb = ifi->CallDeegenCommonSnippet("GetCbFromU32", { iGv }, dcHitOrigin);
        Value* dcHitCodePtr = DeegenInsertOrGetCopyAndPatchPlaceholderSymbol(ifi->GetModule(), CP_PLACEHOLDER_CALL_IC_CALLEE_CODE_PTR);

        finalRes.push_back({
            .calleeCb = dcHitCalleeCb,
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
        // Kind of bad, we are hardcoding the assumption that the CalleeCb is just the zero-extension of calleeCbU32.. but stay simple for now..
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

        Value* calleeCb = ifi->CallDeegenCommonSnippet("GetCbFromU32", { calleeCbU32 }, ccHitOrigin);
        Value* codePtr = DeegenInsertOrGetCopyAndPatchPlaceholderSymbol(ifi->GetModule(), CP_PLACEHOLDER_CALL_IC_CALLEE_CODE_PTR);

        finalRes.push_back({
            .calleeCb = calleeCb,
            .codePointer = codePtr,
            .origin = ccHitOrigin
        });
    };

    // Emit logic for the IC miss block, and push the MakeCall instance to finalRes
    //
    // Basically, we just need to call the create IC functor, which creates the IC and returns the CalleeCb and CodePtr
    //
    // However, currently the create IC takes CodeBlock, execFuncPtr and funcObj, and returns nothing for simplicity
    // TODO: make it return CalleeCb and CodePtr so the slow path is slightly faster
    //
    auto emitIcMissBlock = [&](Instruction* icMissOrigin, Value* functionObjectTarget)
    {
        // Currently we only employ call IC in main component, and the slow path function name assumes that
        //
        ReleaseAssert(ifi->IsMainComponent());
        std::string icCreatorFnName = "__deegen_baseline_jit_codegen_" + ifi->GetBytecodeDef()->GetBytecodeIdName() + "_jit_call_ic_" + std::to_string(unique_ord);
        FunctionType* fty = GetBaselineJitCallIcSlowPathFnPrototype(ctx);
        ReleaseAssert(ifi->GetModule()->getNamedValue(icCreatorFnName) == nullptr);
        Function* icCreatorFn = Function::Create(fty, GlobalValue::ExternalLinkage, icCreatorFnName, ifi->GetModule());
        icCreatorFn->addFnAttr(Attribute::NoUnwind);
        icCreatorFn->addFnAttr(Attribute::NoInline);
        ReleaseAssert(icCreatorFn->getName() == icCreatorFnName);
        icCreatorFn->setCallingConv(CallingConv::PreserveMost);

        Value* slowPathDataOffset = ifi->GetSlowPathDataOffsetFromJitFastPath(icMissOrigin /*insertBefore*/);
        Value* slowPathAddrOfThisStencil = DeegenInsertOrGetCopyAndPatchPlaceholderSymbol(ifi->GetModule(), CP_PLACEHOLDER_STENCIL_SLOW_PATH_ADDR);
        Value* dataSecAddrOfThisStencil = DeegenInsertOrGetCopyAndPatchPlaceholderSymbol(ifi->GetModule(), CP_PLACEHOLDER_STENCIL_DATA_SEC_ADDR);
        Value* targetTV = ifi->CallDeegenCommonSnippet("BoxFunctionObjectToTValue", { functionObjectTarget }, icMissOrigin);
        ReleaseAssert(llvm_value_has_type<uint64_t>(targetTV));

        CallInst* codeBlockAndEntryPoint = CallInst::Create(
            icCreatorFn,
            {
                ifi->GetJitCodeBlock(),
                slowPathDataOffset,
                slowPathAddrOfThisStencil,
                dataSecAddrOfThisStencil,
                targetTV
            },
            "", icMissOrigin);
        codeBlockAndEntryPoint->setCallingConv(CallingConv::PreserveMost);

        ReleaseAssert(codeBlockAndEntryPoint->getType()->isStructTy());
        ReleaseAssert(dyn_cast<StructType>(codeBlockAndEntryPoint->getType())->getNumElements() == 2);

        Value* calleeCb = ExtractValueInst::Create(codeBlockAndEntryPoint, { 0 /*idx*/ }, "", icMissOrigin);
        Value* codePointer = ExtractValueInst::Create(codeBlockAndEntryPoint, { 1 /*idx*/ }, "", icMissOrigin);
        ReleaseAssert(llvm_value_has_type<void*>(calleeCb));
        ReleaseAssert(llvm_value_has_type<void*>(codePointer));

        finalRes.push_back({
            .calleeCb = calleeCb,
            .codePointer = codePointer,
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
// After IC extraction, the SMC region needs to be fixed up by calling BaselineJitAsmTransformResult::FixupSMCRegion,
// which simply make it unconditionally jump to 'ccIcMissEntry'.
//
std::vector<DeegenCallIcLogicCreator::BaselineJitAsmTransformResult> WARN_UNUSED DeegenCallIcLogicCreator::DoBaselineJitAsmTransform(X64AsmFile* file)
{
    std::vector<DeegenCallIcLogicCreator::BaselineJitAsmTransformResult> r;

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
            newList.back().GetWord(1) = "__deegen_cp_placeholder_" + std::to_string(CP_PLACEHOLDER_IC_MISS_DEST);

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
            newList.back().GetWord(1) = "__deegen_cp_placeholder_" + std::to_string(CP_PLACEHOLDER_IC_MISS_DEST);

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
void DeegenCallIcLogicCreator::BaselineJitAsmTransformResult::FixupSMCRegionAfterCFGAnalysis(X64AsmFile* file)
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

void DeegenCallIcLogicCreator::BaselineJitAsmTransformResult::EmitComputeLabelOffsetAndLengthSymbol(X64AsmFile* file)
{
    // Compute the offset and length of SMC region
    //
    {
        X64AsmBlock* smcBlock = file->FindBlockInFastPath(m_labelForSMCRegion);
        ReleaseAssert(smcBlock->m_trailingLabelLine.IsCommentOrEmptyLine());
        std::string uniqLabel = file->m_labelNormalizer.GetUniqueLabel();
        smcBlock->m_trailingLabelLine = X64AsmLine::Parse(uniqLabel + ":");
        ReleaseAssert(smcBlock->m_trailingLabelLine.IsLocalLabel());
        m_symbolNameForSMCRegionLength = file->EmitComputeLabelDistanceAsm(smcBlock->m_normalizedLabelName /*begin*/, uniqLabel /*end*/);
        m_symbolNameForSMCLabelOffset = file->EmitComputeLabelDistanceAsm(file->m_blocks[0]->m_normalizedLabelName /*begin*/, smcBlock->m_normalizedLabelName /*end*/);
    }

    // Compute the offset of m_labelForCcIcMissLogic in slow path
    //
    ReleaseAssert(file->FindBlockInSlowPath(m_labelForCcIcMissLogic) != nullptr);
    m_symbolNameForCcIcMissLogicLabelOffset = file->EmitComputeLabelDistanceAsm(file->m_slowpath[0]->m_normalizedLabelName, m_labelForCcIcMissLogic);

    // Compute the offset of m_labelForDcIcMissLogic in slow path
    //
    ReleaseAssert(file->FindBlockInSlowPath(m_labelForDcIcMissLogic) != nullptr);
    m_symbolNameForDcIcMissLogicLabelOffset = file->EmitComputeLabelDistanceAsm(file->m_slowpath[0]->m_normalizedLabelName, m_labelForDcIcMissLogic);
}

// Fix up cross reference to other stencils in the bytecode
// 'argForFastPathAddr' is the fast path addr of the owning stencil, not owning bytecode
//
static void FixupCrossReferenceToOtherStencilsInBytecodeForCallIcCodegen(llvm::Function* func,
                                                                         llvm::Instruction* stencilMainLogicFastPathAddr,
                                                                         size_t stencilBaseOffsetInFastPath,
                                                                         std::unordered_map<std::string, size_t> stencilToFastPathOffsetMap)
{
    using namespace llvm;
    Module* module = func->getParent();
    ReleaseAssert(module != nullptr);

    LLVMContext& ctx = module->getContext();

    for (auto& item : stencilToFastPathOffsetMap)
    {
        std::string gvName = item.first;
        uint64_t offsetRelativeToBytecodeEntry = item.second;
        uint64_t offsetRelativeToStencilEntry = offsetRelativeToBytecodeEntry - stencilBaseOffsetInFastPath;

        Function* gvToReplace = module->getFunction(gvName);
        if (gvToReplace != nullptr)
        {
            Instruction* insPt = stencilMainLogicFastPathAddr->getNextNode();
            ReleaseAssert(insPt != nullptr);
            Instruction* replacement = GetElementPtrInst::CreateInBounds(
                llvm_type_of<uint8_t>(ctx), stencilMainLogicFastPathAddr,
                { CreateLLVMConstantInt<uint64_t>(ctx, offsetRelativeToStencilEntry) }, "", insPt);

            DeegenStencilFixupCrossRefHelper::RunOnFunction(func, gvToReplace /*gvToReplace*/, replacement);

            if (!gvToReplace->use_empty())
            {
                fprintf(stderr, "[ERROR] Unexpected use of '%s' that is not handled correctly, a bug?\n", gvToReplace->getName().str().c_str());
                module->dump();
                abort();
            }
        }
        else
        {
            ReleaseAssert(module->getNamedValue(gvName) == nullptr);
        }
    }
}

struct CreateCodegenCallIcLogicImplResult
{
    std::unique_ptr<llvm::Module> m_module;
    std::string m_disasmForAudit;
    std::vector<std::pair<size_t /*offset*/, bool /*is64*/>> m_codePtrPatchRecords;
    size_t m_icSize;
    std::string m_resFnName;
};

// Produce a function that codegen one IC
//
// The function takes the following arguments:
//    0 void* addr: the address to codegen the IC
//    1 uint64_t slowPathDataOffset: the offset of slowPathData in SlowPathData stream
//    2 uint64_t codeBlock32: the owning function's codeblock32
//  3-5 uint64_t fastPathAddr/slowPathAddr/dataSecAddr: the owning stencil (not bytecode!)'s fast/slow/data section address
//    6 uint64_t icMissAddr: where to transfer control if IC misses
//  7-8 uint64_t cbHeapU32/codePtr: info of the cached call target
//    9 uint64_t tv: the TValue to call, caller shall pass in undef for closure-call case
//   10 uint64_t condBrDest: the condBr dest of the bytecode, caller shall pass in undef if not exists
//
// followed by the standard vector of bytecode operand info.
//
static CreateCodegenCallIcLogicImplResult WARN_UNUSED CreateCodegenCallIcLogicImpl(BaselineJitImplCreator* ifi,
                                                                                   size_t stencilBaseOffsetInFastPath,
                                                                                   std::unordered_map<std::string, size_t> stencilToFastPathOffsetMap,
                                                                                   const DeegenStencil& mainLogicStencil,
                                                                                   const std::string& asmFile,
                                                                                   bool isDirectCallIc)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();

    std::string objFile = CompileAssemblyFileToObjectFile(asmFile, " -fno-pic -fno-pie ");

    DeegenStencil stencil = DeegenStencil::ParseIcLogic(ctx, objFile, mainLogicStencil.m_sectionToPdoOffsetMap);

    // The main logic must be perfectly in sync, or we are going to have big trouble at runtime
    // This check is not a complete check because we didn't check relocations, but at least gives us some confidence..
    //
    // Unfortunately for m_fastPath we cannot assert byte equal, since it may contain SMC region that has already been updated by earlier transforms..
    //
    ReleaseAssert(stencil.m_fastPathCode.size() == mainLogicStencil.m_fastPathCode.size());
    ReleaseAssert(stencil.m_slowPathCode == mainLogicStencil.m_slowPathCode);

    std::vector<size_t> extraPlaceholderOrds {
        CP_PLACEHOLDER_CALL_IC_CALLEE_CB32,
        CP_PLACEHOLDER_CALL_IC_CALLEE_CODE_PTR,
        CP_PLACEHOLDER_IC_MISS_DEST
    };
    if (isDirectCallIc)
    {
        extraPlaceholderOrds.push_back(CP_PLACEHOLDER_CALL_IC_DIRECT_CALL_TVALUE);
    }
    if (ifi->GetBytecodeDef()->m_hasConditionalBranchTarget)
    {
        extraPlaceholderOrds.push_back(CP_PLACEHOLDER_BYTECODE_CONDBR_DEST);
    }
    DeegenStencilCodegenResult cgRes = stencil.PrintCodegenFunctions(false /*mayEliminateTailJump*/,
                                                                     ifi->GetBytecodeDef()->m_list.size() /*numBytecodeOperands*/,
                                                                     ifi->GetNumTotalGenericIcCaptures(),
                                                                     ifi->GetStencilRcDefinitions(),
                                                                     extraPlaceholderOrds);

    // For simplicity, the stencil currently always put its private data section right after the code section
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

    std::string disasmForAudit;
    {
        if (isDirectCallIc)
        {
            disasmForAudit = "# Direct Call IC:\n\n";
        }
        else
        {
            disasmForAudit = "# Closure Call IC:\n\n";
        }

        disasmForAudit += DumpStencilDisassemblyForAuditPurpose(
            stencil.m_triple, false /*isDataSection*/, cgRes.m_icPathPreFixupCode, cgRes.m_icPathRelocMarker, "# " /*linePrefix*/);

        if (cgRes.m_dataSecPreFixupCode.size() > 0)
        {
            disasmForAudit += std::string("#\n# Data Section:\n");
            disasmForAudit += DumpStencilDisassemblyForAuditPurpose(
                stencil.m_triple, true /*isDataSection*/, cgRes.m_dataSecPreFixupCode, cgRes.m_dataSecRelocMarker, "# " /*linePrefix*/);
        }
        disasmForAudit += "\n";
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

    // Create the codegen function implementation
    //
    std::vector<Type*> cgArgTys;
    cgArgTys.push_back(llvm_type_of<void*>(ctx));       // outputAddr
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // slowPathDataOffset
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // codeBlock32
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // fastPathAddr
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // slowPathAddr
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // dataSecAddr
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // icMissAddr
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // calleeCbU32
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // codePtr
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // tv
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // condBrDest

    size_t bytecodeValListStartOrd = cgArgTys.size();

    // The bytecode operand value vector
    //
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // ordinal 100
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // ordinal 101
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // ordinal 103
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // ordinal 104

    for (size_t i = 0; i < ifi->GetBytecodeDef()->m_list.size(); i++)
    {
        cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));
    }

    std::string resFnName = std::string("deegen_codegen_ic_") + (isDirectCallIc ? "dc" : "cc");

    FunctionType* fty = FunctionType::get(llvm_type_of<void>(ctx), cgArgTys, false /*isVarArg*/);
    Function* fn = Function::Create(fty, GlobalValue::ExternalLinkage, resFnName, module.get());

    fn->addFnAttr(Attribute::NoUnwind);
    CopyFunctionAttributes(fn, cgCodeFn);
    fn->setDSOLocal(true);

    fn->addParamAttr(0, Attribute::NoAlias);

    BasicBlock* entryBB = BasicBlock::Create(ctx, "", fn);

    Value* icCodeAddr = fn->getArg(0);
    // Value* slowPathDataOffset = fn->getArg(1);
    // Value* codeBlock32 = fn->getArg(2);

    Value* fastPathAddrI64 = fn->getArg(3);
    Value* slowPathAddrI64 = fn->getArg(4);
    Value* dataSecAddrI64 = fn->getArg(5);

    Value* icMissDest = fn->getArg(6);
    Value* calleeCbU32 = fn->getArg(7);
    Value* targetFnCodePtr = fn->getArg(8);

    Value* calledFnTValue = fn->getArg(9);
    Value* condBrDest = fn->getArg(10);

    auto getExtraPlaceholderValue = [&](size_t placeholderOrd) WARN_UNUSED -> Value*
    {
        switch (placeholderOrd)
        {
        case CP_PLACEHOLDER_CALL_IC_CALLEE_CB32:
        {
            return calleeCbU32;
        }
        case CP_PLACEHOLDER_CALL_IC_CALLEE_CODE_PTR:
        {
            return targetFnCodePtr;
        }
        case CP_PLACEHOLDER_IC_MISS_DEST:
        {
            return icMissDest;
        }
        case CP_PLACEHOLDER_CALL_IC_DIRECT_CALL_TVALUE:
        {
            ReleaseAssert(isDirectCallIc);
            return calledFnTValue;
        }
        case CP_PLACEHOLDER_BYTECODE_CONDBR_DEST:
        {
            ReleaseAssert(ifi->GetBytecodeDef()->m_hasConditionalBranchTarget);
            return condBrDest;
        }
        default:
        {
            ReleaseAssert(false);
        }
        }   /* switch placeholderOrd */
    };

    std::vector<Value*> extraPlaceholderValues;
    for (size_t placeholderOrd : extraPlaceholderOrds)
    {
        extraPlaceholderValues.push_back(getExtraPlaceholderValue(placeholderOrd));
    }

    Value* icCodeAddrI64 = new PtrToIntInst(icCodeAddr, llvm_type_of<uint64_t>(ctx), "", entryBB);
    Value* icDataAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), icCodeAddr,
                                                          { CreateLLVMConstantInt<uint64_t>(ctx, dataSectionOffset) }, "", entryBB);

    Value* icDataAddrI64 = new PtrToIntInst(icDataAddr, llvm_type_of<uint64_t>(ctx), "", entryBB);

    auto buildArgVector = [&](Function* target, Value* dstAddr) WARN_UNUSED -> std::vector<Value*>
    {
        std::vector<Value*> args;
        args.push_back(dstAddr);
        args.push_back(fastPathAddrI64);
        args.push_back(slowPathAddrI64);
        args.push_back(icCodeAddrI64);
        args.push_back(icDataAddrI64);
        args.push_back(dataSecAddrI64);

        for (size_t i = bytecodeValListStartOrd; i < fn->arg_size(); i++)
        {
            args.push_back(fn->getArg(static_cast<uint32_t>(i)));
        }

        for (size_t i = 0; i < ifi->GetNumTotalGenericIcCaptures(); i++)
        {
            ReleaseAssert(args.size() < target->arg_size());
            ReleaseAssert(target->getArg(static_cast<uint32_t>(args.size()))->use_empty());
            args.push_back(UndefValue::get(llvm_type_of<uint64_t>(ctx)));
        }

        for (Value* val : extraPlaceholderValues)
        {
            ReleaseAssert(llvm_value_has_type<uint64_t>(val));
            args.push_back(val);
        }

        ReleaseAssert(args.size() == target->arg_size());
        for (size_t i = 0; i < args.size(); i++)
        {
            ReleaseAssert(args[i] != nullptr);
            ReleaseAssert(args[i]->getType() == target->getArg(static_cast<uint32_t>(i))->getType());
        }

        return args;
    };

    EmitCopyLogicForBaselineJitCodeGen(module.get(), codeAndData, icCodeAddr, "deegen_ic_pre_fixup_code_and_data", entryBB);

    auto emitPatchFnCall = [&](Function* target, Value* dstAddr)
    {
        ReleaseAssert(llvm_value_has_type<void*>(dstAddr));
        std::vector<Value*> args = buildArgVector(target, dstAddr);
        CallInst::Create(target, args, "", entryBB);
    };

    emitPatchFnCall(cgCodeFn, icCodeAddr);
    emitPatchFnCall(cgDataFn, icDataAddr);

    ReturnInst::Create(ctx, nullptr, entryBB);

    ValidateLLVMModule(module.get());

    DesugarAndSimplifyLLVMModule(module.get(), DesugaringLevel::Top);

    ReleaseAssert(module->getFunction(resFnName) != nullptr);
    for (Function& func : *module)
    {
        if (!func.isDeclaration())
        {
            ReleaseAssert(func.getName() == resFnName);
        }
    }

    // Fix up cross reference to other stencils in the bytecode
    //
    {
        Instruction* insPt = FindFirstNonAllocaInstInEntryBB(fn);
        Instruction* stencilMainLogicFastPathAddr = new IntToPtrInst(fastPathAddrI64, llvm_type_of<void*>(ctx), "", insPt);
        FixupCrossReferenceToOtherStencilsInBytecodeForCallIcCodegen(fn, stencilMainLogicFastPathAddr, stencilBaseOffsetInFastPath, stencilToFastPathOffsetMap);
    }

    RunLLVMOptimizePass(module.get());

    std::vector<std::pair<size_t /*offset*/, bool /*is64*/>> codePtrPatchRecords;
    auto scanRelocationListForCodePtrPatches = [&](const std::vector<RelocationRecord>& rlist, size_t baseOffset)
    {
        for (const RelocationRecord& rr : rlist)
        {
            if (rr.m_symKind == RelocationRecord::SymKind::StencilHole)
            {
                if (rr.m_stencilHoleOrd == CP_PLACEHOLDER_CALL_IC_CALLEE_CODE_PTR)
                {
                    bool is64 = (rr.m_relocationType == ELF::R_X86_64_64);
                    codePtrPatchRecords.push_back(std::make_pair(baseOffset + rr.m_offset, is64));
                }
            }
        }
    };

    scanRelocationListForCodePtrPatches(stencil.m_icPathRelos, 0 /*baseOffset*/);
    scanRelocationListForCodePtrPatches(stencil.m_privateDataObject.m_relocations, dataSectionOffset /*baseOffset*/);

    std::sort(codePtrPatchRecords.begin(), codePtrPatchRecords.end());

    for (size_t i = 0; i + 1 < codePtrPatchRecords.size(); i++)
    {
        ReleaseAssert(codePtrPatchRecords[i].first + (codePtrPatchRecords[i].second ? 8 : 4) <= codePtrPatchRecords[i + 1].first);
    }
    ReleaseAssertImp(codePtrPatchRecords.size() > 0, codePtrPatchRecords.back().first + (codePtrPatchRecords.back().second ? 8 : 4) <= codeAndData.size());

    return {
        .m_module = std::move(module),
        .m_disasmForAudit = disasmForAudit,
        .m_codePtrPatchRecords = codePtrPatchRecords,
        .m_icSize = RoundUpToMultipleOf<8>(codeAndData.size()),
        .m_resFnName = resFnName
    };
}

struct CreateCodegenCallIcRepatchSmcRegionImplResult
{
    std::unique_ptr<llvm::Module> m_module;
    std::string m_disasmForAudit;
    std::string m_resFnName;
};

// Create the codegen logic that repatch the SMC region to closure call logic
//
// The function takes the following arguments:
//     void* fastPathAddr: the fastPathAddr of the owning stencil (not bytecode!)
//     uint64_t slowPathAddr / dataSecAddr: the slowPath/dataSec addr of the owning stencil (not bytecode!)
// followed by the standard bytecode operand value list.
//
static CreateCodegenCallIcRepatchSmcRegionImplResult WARN_UNUSED CreateRepatchCallIcSmcRegionToClosureModeImpl(BaselineJitImplCreator* ifi,
                                                                                                               size_t stencilBaseOffsetInFastPath,
                                                                                                               std::unordered_map<std::string, size_t> stencilToFastPathOffsetMap,
                                                                                                               const DeegenStencil& mainLogicStencil,
                                                                                                               size_t smcRegionOffset,
                                                                                                               size_t smcRegionSize)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();

    DeegenStencil stencil = mainLogicStencil;
    ReleaseAssert(!stencil.m_isForIcLogicExtraction);
    stencil.m_slowPathRelos.clear();
    stencil.m_privateDataObject.m_relocations.clear();

    // Filter out the relocations in the SMC region: we need and only need them
    //
    {
        std::vector<RelocationRecord> rlist;
        for (RelocationRecord& rr : stencil.m_fastPathRelos)
        {
            if (smcRegionOffset <= rr.m_offset && rr.m_offset < smcRegionOffset + smcRegionSize)
            {
                ReleaseAssert(rr.m_offset + (rr.m_relocationType == ELF::R_X86_64_64 ? 8 : 4) <= smcRegionOffset + smcRegionSize);
                rlist.push_back(rr);
            }
        }
        stencil.m_fastPathRelos = rlist;
    }

    // Generate patch logic to fix up the relocations in SMC region
    //
    DeegenStencilCodegenResult cgRes = stencil.PrintCodegenFunctions(false /*mayEliminateTailJump*/,
                                                                     ifi->GetBytecodeDef()->m_list.size() /*numBytecodeOperands*/,
                                                                     ifi->GetNumTotalGenericIcCaptures(),
                                                                     ifi->GetStencilRcDefinitions());

    ReleaseAssert(cgRes.m_fastPathPreFixupCode.size() >= smcRegionOffset + smcRegionSize);
    ReleaseAssert(cgRes.m_condBrFixupOffsetsInFastPath.empty());

    std::string disasmForAudit;
    disasmForAudit += "# SMC region:\n";
    disasmForAudit += "#     offset = " + std::to_string(smcRegionOffset) + " size = " + std::to_string(smcRegionSize) + "\n";
    disasmForAudit += "# SMC region logic for Closure Call:\n\n";
    {
        std::vector<uint8_t> codeSlice;
        std::vector<bool> isPartOfRelocSlice;
        for (size_t i = smcRegionOffset; i < smcRegionOffset + smcRegionSize; i++)
        {
            ReleaseAssert(i < cgRes.m_fastPathPreFixupCode.size() && i < cgRes.m_fastPathRelocMarker.size());
            codeSlice.push_back(cgRes.m_fastPathPreFixupCode[i]);
            isPartOfRelocSlice.push_back(cgRes.m_fastPathRelocMarker[i]);
        }

        disasmForAudit += DumpStencilDisassemblyForAuditPurpose(
            stencil.m_triple, false /*isDataSection*/, codeSlice, isPartOfRelocSlice, "# " /*linePrefix*/);

        disasmForAudit += "\n";
    }

    std::unique_ptr<Module> module = cgRes.GenerateCodegenLogicLLVMModule(ifi->GetModule());

    Function* cgFastPathFn = module->getFunction(cgRes.x_fastPathCodegenFuncName);
    ReleaseAssert(cgFastPathFn != nullptr);
    cgFastPathFn->addFnAttr(Attribute::AlwaysInline);
    cgFastPathFn->setLinkage(GlobalValue::InternalLinkage);

    // We don't need slow path & data sec codegen function, set them to internal so they get removed
    //
    {
        Function* fn = module->getFunction(cgRes.x_slowPathCodegenFuncName);
        ReleaseAssert(fn != nullptr);
        fn->addFnAttr(Attribute::AlwaysInline);
        fn->setLinkage(GlobalValue::InternalLinkage);

        fn = module->getFunction(cgRes.x_dataSecCodegenFuncName);
        ReleaseAssert(fn != nullptr);
        fn->addFnAttr(Attribute::AlwaysInline);
        fn->setLinkage(GlobalValue::InternalLinkage);
    }

    std::vector<Type*> cgArgTys;
    cgArgTys.push_back(llvm_type_of<void*>(ctx));       // outputAddr, also fastPathAddr
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // slowPathAddr
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // dataSecAddr

    size_t bytecodeValListStartOrd = cgArgTys.size();

    // The bytecode operand value vector
    //
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // ordinal 100
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // ordinal 101
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // ordinal 103
    cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));    // ordinal 104

    for (size_t i = 0; i < ifi->GetBytecodeDef()->m_list.size(); i++)
    {
        cgArgTys.push_back(llvm_type_of<uint64_t>(ctx));
    }

    constexpr const char* x_resFnName = "deegen_codegen_rewrite_smc";

    FunctionType* fty = FunctionType::get(llvm_type_of<void>(ctx), cgArgTys, false /*isVarArg*/);
    Function* fn = Function::Create(fty, GlobalValue::ExternalLinkage, x_resFnName, module.get());

    fn->addFnAttr(Attribute::NoUnwind);
    CopyFunctionAttributes(fn, cgFastPathFn);
    fn->setDSOLocal(true);

    fn->addParamAttr(0, Attribute::NoAlias);

    BasicBlock* entryBB = BasicBlock::Create(ctx, "", fn);

    Value* fastPathAddr = fn->getArg(0);
    Value* slowPathAddrI64 = fn->getArg(1);
    Value* dataSecAddrI64 = fn->getArg(2);

    Value* fastPathAddrI64 = new PtrToIntInst(fastPathAddr, llvm_type_of<uint64_t>(ctx), "", entryBB);

    // Copy pre-fixup code to SMC region
    //
    {
        std::vector<uint8_t> smcRegionPreFixupCode;
        for (size_t i = smcRegionOffset; i < smcRegionOffset + smcRegionSize; i++)
        {
            ReleaseAssert(i < cgRes.m_fastPathPreFixupCode.size());
            smcRegionPreFixupCode.push_back(cgRes.m_fastPathPreFixupCode[i]);
        }
        ReleaseAssert(smcRegionPreFixupCode.size() == smcRegionSize);

        Value* dstAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), fastPathAddr,
                                                           { CreateLLVMConstantInt<uint64_t>(ctx, smcRegionOffset) }, "", entryBB);

        // The memcpy must not clobber anything after, so mustBeExact is true
        //
        EmitCopyLogicForBaselineJitCodeGen(module.get(),
                                           smcRegionPreFixupCode,
                                           dstAddr,
                                           "deegen_ic_pre_fixup_smc_region_for_closure_call",
                                           entryBB,
                                           true /*mustBeExact*/);
    }

    auto buildArgVector = [&](Function* target) WARN_UNUSED -> std::vector<Value*>
    {
        std::vector<Value*> args;
        args.push_back(fastPathAddr);
        args.push_back(fastPathAddrI64);
        args.push_back(slowPathAddrI64);
        args.push_back(UndefValue::get(llvm_type_of<uint64_t>(ctx)));
        args.push_back(UndefValue::get(llvm_type_of<uint64_t>(ctx)));
        args.push_back(dataSecAddrI64);

        for (size_t i = bytecodeValListStartOrd; i < fn->arg_size(); i++)
        {
            args.push_back(fn->getArg(static_cast<uint32_t>(i)));
        }

        for (size_t i = 0; i < ifi->GetNumTotalGenericIcCaptures(); i++)
        {
            ReleaseAssert(args.size() < target->arg_size());
            ReleaseAssert(target->getArg(static_cast<uint32_t>(args.size()))->use_empty());
            args.push_back(UndefValue::get(llvm_type_of<uint64_t>(ctx)));
        }

        ReleaseAssert(args.size() == target->arg_size());
        for (size_t i = 0; i < args.size(); i++)
        {
            ReleaseAssert(args[i] != nullptr);
            ReleaseAssert(args[i]->getType() == target->getArg(static_cast<uint32_t>(i))->getType());
            ReleaseAssertImp(isa<UndefValue>(args[i]), target->getArg(static_cast<uint32_t>(i))->use_empty());
        }

        return args;
    };

    CallInst::Create(cgFastPathFn, buildArgVector(cgFastPathFn), "", entryBB);
    ReturnInst::Create(ctx, nullptr, entryBB);

    ValidateLLVMModule(module.get());

    DesugarAndSimplifyLLVMModule(module.get(), DesugaringLevel::Top);

    ReleaseAssert(module->getFunction(x_resFnName) != nullptr);
    for (Function& func : *module)
    {
        if (!func.isDeclaration())
        {
            ReleaseAssert(func.getName() == x_resFnName);
        }
    }

    // Fix up cross reference to other stencils in the bytecode
    //
    {
        Instruction* insPt = FindFirstNonAllocaInstInEntryBB(fn);
        // 'FixupCrossReferenceToOtherStencilsInBytecodeForCallIcCodegen' expects 'stencilMainLogicFastPathAddr' to be an instruction,
        // workaround by creating a no-op gep
        //
        Instruction* stencilMainLogicFastPathAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), fastPathAddr,
                                                                                      { CreateLLVMConstantInt<uint64_t>(ctx, 0) }, "", insPt);
        FixupCrossReferenceToOtherStencilsInBytecodeForCallIcCodegen(fn, stencilMainLogicFastPathAddr, stencilBaseOffsetInFastPath, stencilToFastPathOffsetMap);
    }

    RunLLVMOptimizePass(module.get());

    return {
        .m_module = std::move(module),
        .m_disasmForAudit = disasmForAudit,
        .m_resFnName = x_resFnName
    };
}

// The main logic stencil's SMC region initially hold the logic for closure-call mode,
// because that's the longest instruction sequence and we need it to hold up space of the SMC region
// However, at runtime, the SMC region starts in direct-call mode, which simply contains a jump to slow path.
//
// This function fix up the initial contents in the SMC region to a jump + NOPs.
//
static void SetupCallIcSmcRegionInitialInstructions(DeegenStencil& mainLogicStencil /*inout*/,
                                                    size_t smcRegionOffset,
                                                    size_t smcRegionSize,
                                                    size_t dcIcMissDestOffsetInSlowPath)
{
    {
        ReleaseAssert(smcRegionSize >= 5);
        std::vector<uint8_t> byteSeq;
        byteSeq.resize(smcRegionSize, 0);
        byteSeq[0] = 0xe9;  // jmp
        FillAddressRangeWithX64MultiByteNOPs(byteSeq.data() + 5, byteSeq.size() - 5);

        for (size_t i = 0; i < smcRegionSize; i++)
        {
            ReleaseAssert(smcRegionOffset + i < mainLogicStencil.m_fastPathCode.size());
            mainLogicStencil.m_fastPathCode[smcRegionOffset + i] = byteSeq[i];
        }
    }

    std::vector<RelocationRecord> rlist;
    for (RelocationRecord& rr : mainLogicStencil.m_fastPathRelos)
    {
        if (smcRegionOffset <= rr.m_offset && rr.m_offset < smcRegionOffset + smcRegionSize)
        {
            ReleaseAssert(rr.m_offset + (rr.m_relocationType == llvm::ELF::R_X86_64_64 ? 8 : 4) <= smcRegionOffset + smcRegionSize);
            continue;
        }

        rlist.push_back(rr);
    }

    {
        RelocationRecord rr;
        rr.m_relocationType = llvm::ELF::R_X86_64_PC32;
        rr.m_symKind = RelocationRecord::SymKind::SlowPathAddr;
        rr.m_offset = smcRegionOffset + 1;
        // SlowPathAddr + dcIcMissDestOffsetInSlowPath - PC - 4
        //
        rr.m_addend = static_cast<int64_t>(dcIcMissDestOffsetInSlowPath - 4);
        rlist.push_back(rr);
    }

    // For sanity, sort everything by m_offset
    //
    {
        std::map<uint64_t, RelocationRecord> sortedList;
        for (RelocationRecord& rr : rlist)
        {
            ReleaseAssert(!sortedList.count(rr.m_offset));
            sortedList[rr.m_offset] = rr;
        }

        rlist.clear();
        for (auto& it : sortedList)
        {
            rlist.push_back(it.second);
        }
    }

    mainLogicStencil.m_fastPathRelos = rlist;
}

DeegenCallIcLogicCreator::BaselineJitCodegenResult WARN_UNUSED DeegenCallIcLogicCreator::CreateBaselineJitCallIcCreator(
    BaselineJitImplCreator* ifi,
    std::unordered_map<std::string, size_t> stencilToFastPathOffsetMap,
    DeegenStencil& mainLogicStencil /*inout*/,
    BaselineJitAsmLoweringResult& icInfo,
    const DeegenGlobalBytecodeTraitAccessor& bcTraitAccessor)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();

    BaselineJitSlowPathDataLayout* slowPathDataLayout = ifi->GetBytecodeDef()->GetBaselineJitSlowPathDataLayout();

    ReleaseAssert(stencilToFastPathOffsetMap.count(ifi->GetResultFunctionName()));
    size_t stencilBaseOffsetInFastPath = stencilToFastPathOffsetMap[ifi->GetResultFunctionName()];

    size_t smcRegionOffset = mainLogicStencil.RetrieveLabelDistanceComputationResult(icInfo.m_symbolNameForSMCLabelOffset);
    size_t smcRegionLength = mainLogicStencil.RetrieveLabelDistanceComputationResult(icInfo.m_symbolNameForSMCRegionLength);
    ReleaseAssert(smcRegionOffset < mainLogicStencil.m_fastPathCode.size());
    ReleaseAssert(smcRegionOffset + smcRegionLength <= mainLogicStencil.m_fastPathCode.size());
    // The SMC region should at least be long enough to hold a jmp instruction, check that for sanity
    //
    ReleaseAssert(smcRegionLength >= 5);

    size_t dcIcMissDestOffset = mainLogicStencil.RetrieveLabelDistanceComputationResult(icInfo.m_symbolNameForDcIcMissLogicLabelOffset);
    ReleaseAssert(dcIcMissDestOffset < mainLogicStencil.m_slowPathCode.size());

    size_t ccIcMissDestOffset = mainLogicStencil.RetrieveLabelDistanceComputationResult(icInfo.m_symbolNameForCcIcMissLogicLabelOffset);
    ReleaseAssert(ccIcMissDestOffset < mainLogicStencil.m_slowPathCode.size());

    CreateCodegenCallIcLogicImplResult dcRes = CreateCodegenCallIcLogicImpl(ifi,
                                                                            stencilBaseOffsetInFastPath,
                                                                            stencilToFastPathOffsetMap,
                                                                            mainLogicStencil,
                                                                            icInfo.m_directCallLogicAsm,
                                                                            true /*isDirectCallIc*/);

    CreateCodegenCallIcLogicImplResult ccRes = CreateCodegenCallIcLogicImpl(ifi,
                                                                            stencilBaseOffsetInFastPath,
                                                                            stencilToFastPathOffsetMap,
                                                                            mainLogicStencil,
                                                                            icInfo.m_closureCallLogicAsm,
                                                                            false /*isDirectCallIc*/);

    CreateCodegenCallIcRepatchSmcRegionImplResult smcRes = CreateRepatchCallIcSmcRegionToClosureModeImpl(ifi,
                                                                                                         stencilBaseOffsetInFastPath,
                                                                                                         stencilToFastPathOffsetMap,
                                                                                                         mainLogicStencil,
                                                                                                         smcRegionOffset,
                                                                                                         smcRegionLength);

    // Must be done last since the functions above expect the unmodified mainLogicStencil
    //
    SetupCallIcSmcRegionInitialInstructions(mainLogicStencil /*inout*/, smcRegionOffset, smcRegionLength, dcIcMissDestOffset);

    // Put everything together
    //
    std::unique_ptr<Module> module = std::move(dcRes.m_module);

    {
        Linker linker(*module.get());
        ReleaseAssert(linker.linkInModule(std::move(ccRes.m_module)) == false);
        ReleaseAssert(linker.linkInModule(std::move(smcRes.m_module)) == false);
    }

    auto getFnAndChangeToInternalAndInline = [&](const std::string& targetFnName) WARN_UNUSED -> Function*
    {
        Function* target = module->getFunction(targetFnName);
        ReleaseAssert(target != nullptr);
        ReleaseAssert(target->hasExternalLinkage());
        ReleaseAssert(!target->hasFnAttribute(Attribute::NoInline));
        target->setLinkage(GlobalValue::InternalLinkage);
        target->addFnAttr(Attribute::AlwaysInline);
        return target;
    };

    Function* dcCgFn = getFnAndChangeToInternalAndInline(dcRes.m_resFnName);
    Function* ccCgFn = getFnAndChangeToInternalAndInline(ccRes.m_resFnName);
    Function* smcCgFn = getFnAndChangeToInternalAndInline(smcRes.m_resFnName);

    // This is not the final function prototype
    // We do not directly generate the final function because teaching LLVM that the SlowPathData is 'noalias'
    // is *really* important as we heavily rely on LLVM code sinking pass to sink access to SlowPathData to
    // where the data is actually used. And the easiest way to achieve this is to use a parameter 'noalias' attribute.
    //
    StructType* retTy = StructType::get(ctx, { llvm_type_of<void*>(ctx) /*calleeCb32*/, llvm_type_of<void*>(ctx) /*codePtr*/ });
    FunctionType* fty = FunctionType::get(
        retTy,
        {
            // This is lame: in new design we no longer need CodeBlock, but I just don't want to change this code now..
            // It will go away after inlining anyway, so don't bother.
            //
            llvm_type_of<void*>(ctx),           // CodeBlock, unused
            llvm_type_of<void*>(ctx),           // BaselineCodeBlock
            llvm_type_of<uint64_t>(ctx),        // slowPathDataOffset
            llvm_type_of<void*>(ctx),           // slowPathAddr for this stencil
            llvm_type_of<void*>(ctx),           // dataSecAddr for this stencil
            llvm_type_of<uint64_t>(ctx)            // target pointer as u64
        },
        false /*isVarArg*/);

    std::string resultFuncName = std::string("__deegen_baseline_jit_codegen_") + ifi->GetBytecodeDef()->GetBytecodeIdName() + "_jit_call_ic_" + std::to_string(icInfo.m_uniqueOrd);
    Function* fn = Function::Create(fty, GlobalValue::ExternalLinkage, resultFuncName + "_impl", module.get());

    fn->addFnAttr(Attribute::NoUnwind);
    CopyFunctionAttributes(fn, dcCgFn);
    fn->setDSOLocal(true);

    fn->addParamAttr(1, Attribute::NoAlias);

    BasicBlock* entryBB = BasicBlock::Create(ctx, "", fn);
    AllocaInst* transitedToCCModeAlloca = new AllocaInst(llvm_type_of<uint8_t>(ctx), 0 /*addrSpace*/, "", entryBB);
    AllocaInst* jitAddrAlloca = new AllocaInst(llvm_type_of<void*>(ctx), 0 /*addrSpace*/, "", entryBB);

    new StoreInst(CreateLLVMConstantInt<uint8_t>(ctx, 0), transitedToCCModeAlloca, entryBB);

    Value* baselineCodeBlock = fn->getArg(1);
    Value* slowPathDataOffset = fn->getArg(2);
    Value* slowPathData = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), baselineCodeBlock, { slowPathDataOffset }, "", entryBB);

    Value* slowPathAddrOfOwningStencil = fn->getArg(3);
    Value* dataSecAddrOfOwningStencil = fn->getArg(4);
    Value* targetU64 = fn->getArg(5);

    Value* fastPathAddrOfOwningBytecode = slowPathDataLayout->m_jitAddr.EmitGetValueLogic(slowPathData, entryBB);
    Value* fastPathAddrOfOwningStencil = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), fastPathAddrOfOwningBytecode,
                                                                           { CreateLLVMConstantInt<uint64_t>(ctx, stencilBaseOffsetInFastPath) }, "", entryBB);

    Value* fastPathAddrI64 = new PtrToIntInst(fastPathAddrOfOwningStencil, llvm_type_of<uint64_t>(ctx), "", entryBB);
    Value* slowPathAddrI64 = new PtrToIntInst(slowPathAddrOfOwningStencil, llvm_type_of<uint64_t>(ctx), "", entryBB);
    Value* dataSecAddrI64 = new PtrToIntInst(dataSecAddrOfOwningStencil, llvm_type_of<uint64_t>(ctx), "", entryBB);

    Value* baselineCodeBlock32;
    {
        // Ugly: this relies on the fact that BaselineCodeBlock lives in the system heap, so we can truncate to 32 bit and get a 2GB pointer
        //
        Value* tmp = new PtrToIntInst(baselineCodeBlock, llvm_type_of<uint64_t>(ctx), "", entryBB);
        tmp = new TruncInst(tmp, llvm_type_of<uint32_t>(ctx), "", entryBB);
        baselineCodeBlock32 = new ZExtInst(tmp, llvm_type_of<uint64_t>(ctx), "", entryBB);
    }

    size_t icSiteOffsetInSlowPathData = slowPathDataLayout->m_callICs.GetOffsetForSite(icInfo.m_uniqueOrd);
    Value* icSite = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathData,
                                                      { CreateLLVMConstantInt<uint64_t>(ctx, icSiteOffsetInSlowPathData) }, "", entryBB);

    // Read icSite->m_numEntries
    //
    Value* numExistingICs;
    {
        static_assert(std::is_same_v<typeof_member_t<&JitCallInlineCacheSite::m_numEntries>, uint8_t>);
        Value* ptr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), icSite,
                                                       { CreateLLVMConstantInt<uint64_t>(ctx, offsetof_member_v<&JitCallInlineCacheSite::m_numEntries>) }, "", entryBB);
        numExistingICs = new LoadInst(llvm_type_of<uint8_t>(ctx), ptr, "", entryBB);
    }

    // If m_numEntries >= x_maxJitCallInlineCacheEntries, don't create more ICs. Just get the result and return.
    //
    Value* isIcCountReachedMaximum = new ICmpInst(*entryBB, ICmpInst::ICMP_UGE, numExistingICs, CreateLLVMConstantInt<uint8_t>(ctx, x_maxJitCallInlineCacheEntries));

    BasicBlock* skipIcCreationBB = BasicBlock::Create(ctx, "", fn);
    BasicBlock* prepareCreateIcBB = BasicBlock::Create(ctx, "", fn);

    BranchInst::Create(skipIcCreationBB /*trueBB*/, prepareCreateIcBB /*falseBB*/, isIcCountReachedMaximum, entryBB);

    auto createReturnLogic = [&](Value* cb, Value* entryPoint, BasicBlock* bb)
    {
        ReleaseAssert(llvm_value_has_type<void*>(cb));
        ReleaseAssert(llvm_value_has_type<void*>(entryPoint));
        Value* tmp = InsertValueInst::Create(UndefValue::get(retTy), cb, { 0 /*idx*/ }, "", bb);
        tmp = InsertValueInst::Create(tmp, entryPoint, { 1 /*idx*/ }, "", bb);
        ReturnInst::Create(ctx, tmp, bb);
    };

    {
        ReleaseAssert(llvm_value_has_type<uint64_t>(targetU64));
        Value* targetPtr = new IntToPtrInst(targetU64, llvm_type_of<void*>(ctx), "", skipIcCreationBB);
        Value* codeBlockAndEntryPoint = CreateCallToDeegenCommonSnippet(module.get(), "GetCalleeEntryPoint", { targetPtr }, skipIcCreationBB);

        Value* calleeCb = ExtractValueInst::Create(codeBlockAndEntryPoint, { 0 /*idx*/ }, "", skipIcCreationBB);
        Value* codePointer = ExtractValueInst::Create(codeBlockAndEntryPoint, { 1 /*idx*/ }, "", skipIcCreationBB);
        ReleaseAssert(llvm_value_has_type<void*>(calleeCb));
        ReleaseAssert(llvm_value_has_type<void*>(codePointer));

        createReturnLogic(calleeCb, codePointer, skipIcCreationBB);
    }

    // Read icSite->m_mode
    //
    Value* currentIcMode;
    {
        static_assert(std::is_same_v<std::underlying_type_t<typeof_member_t<&JitCallInlineCacheSite::m_mode>>, uint8_t>);
        Value* ptr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), icSite,
                                                       { CreateLLVMConstantInt<uint64_t>(ctx, offsetof_member_v<&JitCallInlineCacheSite::m_mode>) }, "", prepareCreateIcBB);
        currentIcMode = new LoadInst(llvm_type_of<uint8_t>(ctx), ptr, "", prepareCreateIcBB);
    }

    // Depending on if m_mode == DirectCall, we need to execute different logic
    //
    Value* isIcSiteInDirectCallMode = new ICmpInst(*prepareCreateIcBB, ICmpInst::ICMP_EQ, currentIcMode, CreateLLVMConstantInt<uint8_t>(ctx, static_cast<uint8_t>(JitCallInlineCacheSite::Mode::DirectCall)));

    BasicBlock* dcBB = BasicBlock::Create(ctx, "", fn);
    BasicBlock* ccBB = BasicBlock::Create(ctx, "", fn);

    BranchInst::Create(dcBB /*trueBB*/, ccBB /*falseBB*/, isIcSiteInDirectCallMode, prepareCreateIcBB);

    Value* dcIcTraitOrd;
    {
        size_t dcIcTraitOrdVal = bcTraitAccessor.GetJitCallIcTraitOrd(ifi->GetBytecodeDef()->GetBytecodeIdName(),
                                                                      icInfo.m_uniqueOrd /*icSiteOrd*/,
                                                                      true /*isDirectCall*/);
        ReleaseAssert(dcIcTraitOrdVal <= 65534);
        dcIcTraitOrd = CreateLLVMConstantInt<uint16_t>(ctx, static_cast<uint16_t>(dcIcTraitOrdVal));
    }

    auto getCondBrDestU64 = [&](BasicBlock* bb) WARN_UNUSED -> Value*
    {
        Value* condBrDest = nullptr;
        if (ifi->GetBytecodeDef()->m_hasConditionalBranchTarget)
        {
            condBrDest = slowPathDataLayout->m_condBrJitAddr.EmitGetValueLogic(slowPathData, bb);
            ReleaseAssert(llvm_value_has_type<void*>(condBrDest));
            condBrDest = new PtrToIntInst(condBrDest, llvm_type_of<uint64_t>(ctx), "", bb);
        }
        return condBrDest;
    };

    auto emitCallToIcCodegenFn = [&](Function* target,
                                     Value* outputAddr,
                                     Value* icMissAddr,
                                     Value* calleeCbU32,
                                     Value* codePtr,
                                     Value* condBrDestU64,
                                     std::vector<Value*> bytecodeOperandArgs,
                                     BasicBlock* bb)
    {
        std::vector<Value*> args;
        args.push_back(outputAddr);
        args.push_back(slowPathDataOffset);
        args.push_back(baselineCodeBlock32);
        args.push_back(fastPathAddrI64);
        args.push_back(slowPathAddrI64);
        args.push_back(dataSecAddrI64);
        args.push_back(icMissAddr);
        args.push_back(calleeCbU32);
        args.push_back(codePtr);
        args.push_back(targetU64);
        args.push_back(condBrDestU64);
        for (Value* val : bytecodeOperandArgs)
        {
            args.push_back(val);
        }

        ReleaseAssert(args.size() == target->arg_size());
        for (size_t i = 0; i < args.size(); i++)
        {
            if (args[i] == nullptr)
            {
                ReleaseAssert(target->getArg(static_cast<uint32_t>(i))->use_empty());
            }
            else
            {
                ReleaseAssert(args[i]->getType() == target->getArg(static_cast<uint32_t>(i))->getType());
            }
        }

        for (size_t i = 0; i < args.size(); i++)
        {
            if (args[i] == nullptr)
            {
                args[i] = UndefValue::get(target->getArg(static_cast<uint32_t>(i))->getType());
            }
        }

        CallInst::Create(target, args, "", bb);
    };

    // For direct-call mode, we want to call InsertInDirectCallMode
    //
    {
        Value* targetPtr = new IntToPtrInst(targetU64, llvm_type_of<void*>(ctx), "", dcBB);
        CallInst* dcJitAddr = CreateCallToDeegenCommonSnippet(module.get(), "CreateNewJitCallIcForDirectCallModeSite",
                                                              { icSite, dcIcTraitOrd, targetPtr, transitedToCCModeAlloca }, dcBB);
        ReleaseAssert(llvm_value_has_type<void*>(dcJitAddr));

        new StoreInst(dcJitAddr, jitAddrAlloca, dcBB);

        Value* transitedToCCModeU8 = new LoadInst(llvm_type_of<uint8_t>(ctx), transitedToCCModeAlloca, "", dcBB);
        Value* transitedToCCMode = new ICmpInst(*dcBB, ICmpInst::ICMP_NE, transitedToCCModeU8, CreateLLVMConstantInt<uint8_t>(ctx, 0));

        BasicBlock* insertIcDcModeBB = BasicBlock::Create(ctx, "", fn);

        BranchInst::Create(ccBB /*trueBB*/, insertIcDcModeBB /*falseBB*/, transitedToCCMode, dcBB);

        Value* patchableJmpEndAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), fastPathAddrOfOwningStencil,
                                                                       { CreateLLVMConstantInt<uint64_t>(ctx, smcRegionOffset + 5) }, "", insertIcDcModeBB);

        Value* icMissAddr = X64PatchableJumpUtil::GetDest(patchableJmpEndAddr, insertIcDcModeBB);
        Value* icMissAddrI64 = new PtrToIntInst(icMissAddr, llvm_type_of<uint64_t>(ctx), "", insertIcDcModeBB);

        X64PatchableJumpUtil::SetDest(patchableJmpEndAddr, dcJitAddr /*newDest*/, insertIcDcModeBB);

        Value* codeBlockAndEntryPoint = CreateCallToDeegenCommonSnippet(module.get(), "GetCalleeEntryPoint", { targetPtr }, insertIcDcModeBB);

        Value* calleeCb = ExtractValueInst::Create(codeBlockAndEntryPoint, { 0 /*idx*/ }, "", insertIcDcModeBB);
        Value* codePointer = ExtractValueInst::Create(codeBlockAndEntryPoint, { 1 /*idx*/ }, "", insertIcDcModeBB);
        ReleaseAssert(llvm_value_has_type<void*>(calleeCb));
        ReleaseAssert(llvm_value_has_type<void*>(codePointer));

        Value* calleeCbU32 = new PtrToIntInst(calleeCb, llvm_type_of<uint64_t>(ctx), "", insertIcDcModeBB);
        Value* codePtrU64 = new PtrToIntInst(codePointer, llvm_type_of<uint64_t>(ctx), "", insertIcDcModeBB);

        std::vector<Value*> bytecodeOperandList = DeegenStencilCodegenResult::BuildBytecodeOperandVectorFromSlowPathData(DeegenEngineTier::BaselineJIT,
                                                                                                                         ifi->GetBytecodeDef(),
                                                                                                                         slowPathData,
                                                                                                                         slowPathDataOffset,
                                                                                                                         baselineCodeBlock32,
                                                                                                                         insertIcDcModeBB /*insertAtEnd*/);
        Value* condBrDest = getCondBrDestU64(insertIcDcModeBB);
        emitCallToIcCodegenFn(dcCgFn,
                              dcJitAddr /*outputAddr*/,
                              icMissAddrI64,
                              calleeCbU32,
                              codePtrU64,
                              condBrDest,
                              bytecodeOperandList,
                              insertIcDcModeBB);

        createReturnLogic(calleeCb, codePointer, insertIcDcModeBB);
    }

    std::vector<Value*> bytecodeOperandList = DeegenStencilCodegenResult::BuildBytecodeOperandVectorFromSlowPathData(DeegenEngineTier::BaselineJIT,
                                                                                                                     ifi->GetBytecodeDef(),
                                                                                                                     slowPathData,
                                                                                                                     slowPathDataOffset,
                                                                                                                     baselineCodeBlock32,
                                                                                                                     ccBB /*insertAtEnd*/);
    Value* condBrDest = getCondBrDestU64(ccBB);

    // For closure-call mode, first check if we just transited from direct-call to closure-call.
    // If so, we need to first repatch the SMC region
    //
    BasicBlock* prepareInsertIcCcModeBB = BasicBlock::Create(ctx, "", fn);
    BasicBlock* insertIcCcModeBB = BasicBlock::Create(ctx, "", fn);

    {
        Value* transitedToCCModeU8 = new LoadInst(llvm_type_of<uint8_t>(ctx), transitedToCCModeAlloca, "", ccBB);
        Value* transitedToCCMode = new ICmpInst(*ccBB, ICmpInst::ICMP_NE, transitedToCCModeU8, CreateLLVMConstantInt<uint8_t>(ctx, 0));

        BasicBlock* repatchSmcBB = BasicBlock::Create(ctx, "", fn);

        BranchInst::Create(repatchSmcBB /*trueBB*/, prepareInsertIcCcModeBB /*falseBB*/, transitedToCCMode, ccBB);

        {
            std::vector<Value*> args;
            args.push_back(fastPathAddrOfOwningStencil /*outputAddr*/);
            args.push_back(slowPathAddrI64);
            args.push_back(dataSecAddrI64);
            for (Value* val : bytecodeOperandList)
            {
                args.push_back(val);
            }

            ReleaseAssert(args.size() == smcCgFn->arg_size());
            for (size_t i = 0; i < args.size(); i++)
            {
                if (args[i] == nullptr)
                {
                    ReleaseAssert(smcCgFn->getArg(static_cast<uint32_t>(i))->use_empty());
                    args[i] = UndefValue::get(smcCgFn->getArg(static_cast<uint32_t>(i))->getType());
                }
                else
                {
                    ReleaseAssert(args[i]->getType() == smcCgFn->getArg(static_cast<uint32_t>(i))->getType());
                }
            }

            CallInst::Create(smcCgFn, args, "", repatchSmcBB);
        }

        BranchInst::Create(insertIcCcModeBB /*destBB*/, repatchSmcBB /*insertAtEnd*/);
    }

    // When we reach 'prepareInsertIcCcModeBB', we know we start in clousure call mode, and hasn't inserted IC entry yet
    //
    {
        Value* targetPtr = new IntToPtrInst(targetU64, llvm_type_of<void*>(ctx), "", prepareInsertIcCcModeBB);
        CallInst* ccJitAddr = CreateCallToDeegenCommonSnippet(module.get(), "CreateNewJitCallIcForClosureCallModeSite",
                                                              { icSite, dcIcTraitOrd, targetPtr }, prepareInsertIcCcModeBB);
        ReleaseAssert(llvm_value_has_type<void*>(ccJitAddr));

        new StoreInst(ccJitAddr, jitAddrAlloca, prepareInsertIcCcModeBB);
        BranchInst::Create(insertIcCcModeBB /*destBB*/, prepareInsertIcCcModeBB /*insertAtEnd*/);
    }

    // Emit insertIcCcModeBB, which should actually generate the closure-call mode IC
    //
    {
        Value* jitAddr = new LoadInst(llvm_type_of<void*>(ctx), jitAddrAlloca, "", insertIcCcModeBB);

        Value* patchableJmpEndAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), fastPathAddrOfOwningStencil,
                                                                       { CreateLLVMConstantInt<uint64_t>(ctx, smcRegionOffset + smcRegionLength) }, "", insertIcCcModeBB);

        Value* icMissAddr = X64PatchableJumpUtil::GetDest(patchableJmpEndAddr, insertIcCcModeBB);
        Value* icMissAddrI64 = new PtrToIntInst(icMissAddr, llvm_type_of<uint64_t>(ctx), "", insertIcCcModeBB);

        X64PatchableJumpUtil::SetDest(patchableJmpEndAddr, jitAddr /*newDest*/, insertIcCcModeBB);

        Value* targetPtr = new IntToPtrInst(targetU64, llvm_type_of<void*>(ctx), "", insertIcCcModeBB);
        Value* codeBlockAndEntryPoint = CreateCallToDeegenCommonSnippet(module.get(), "GetCalleeEntryPoint", { targetPtr }, insertIcCcModeBB);

        Value* calleeCb = ExtractValueInst::Create(codeBlockAndEntryPoint, { 0 /*idx*/ }, "", insertIcCcModeBB);
        Value* codePointer = ExtractValueInst::Create(codeBlockAndEntryPoint, { 1 /*idx*/ }, "", insertIcCcModeBB);
        ReleaseAssert(llvm_value_has_type<void*>(calleeCb));
        ReleaseAssert(llvm_value_has_type<void*>(codePointer));

        Value* calleeCbU32 = new PtrToIntInst(calleeCb, llvm_type_of<uint64_t>(ctx), "", insertIcCcModeBB);
        Value* codePtrU64 = new PtrToIntInst(codePointer, llvm_type_of<uint64_t>(ctx), "", insertIcCcModeBB);

        emitCallToIcCodegenFn(ccCgFn,
                              jitAddr /*outputAddr*/,
                              icMissAddrI64,
                              calleeCbU32,
                              codePtrU64,
                              condBrDest,
                              bytecodeOperandList,
                              insertIcCcModeBB);

        createReturnLogic(calleeCb, codePointer, insertIcCcModeBB);
    }

    ValidateLLVMModule(module.get());
    RunLLVMOptimizePass(module.get());

    fn = module->getFunction(resultFuncName + "_impl");
    ReleaseAssert(fn != nullptr);
    fn->setLinkage(GlobalValue::InternalLinkage);
    fn->addFnAttr(Attribute::AlwaysInline);

    FunctionType* finalFTy = GetBaselineJitCallIcSlowPathFnPrototype(ctx);
    ReleaseAssert(finalFTy->getReturnType() == retTy);

    Function* finalFn = Function::Create(finalFTy, GlobalValue::ExternalLinkage, resultFuncName, module.get());
    ReleaseAssert(finalFn->getName() == resultFuncName);

    finalFn->addFnAttr(Attribute::NoInline);
    finalFn->addFnAttr(Attribute::NoUnwind);
    CopyFunctionAttributes(finalFn, fn);
    finalFn->setDSOLocal(true);

    // This is expected by our caller
    //
    finalFn->setCallingConv(CallingConv::PreserveMost);

    {
        BasicBlock* finalFnBB = BasicBlock::Create(ctx, "", finalFn);
        CallInst* finalFnRes = CallInst::Create(
            fn,
            {
                UndefValue::get(llvm_type_of<void*>(ctx)),      // the unused "CodeBlock" parameter
                finalFn->getArg(0),
                finalFn->getArg(1),
                finalFn->getArg(2),
                finalFn->getArg(3),
                finalFn->getArg(4)
            },
            "", finalFnBB);
        ReturnInst::Create(ctx, finalFnRes, finalFnBB);
    }

    ValidateLLVMModule(module.get());
    RunLLVMOptimizePass(module.get());

    ReleaseAssert(module->getFunction(resultFuncName) != nullptr);
    for (Function& func : *module)
    {
        if (!func.isDeclaration())
        {
            ReleaseAssert(func.getName() == resultFuncName);
        }
    }

    std::string disasmForAudit;
    {
        disasmForAudit = dcRes.m_disasmForAudit + ccRes.m_disasmForAudit;

        disasmForAudit += "# Initial DC miss dest offset (relative to stencil, not bytecode) = " + std::to_string(dcIcMissDestOffset) +
            ", CC miss dest offset = " + std::to_string(ccIcMissDestOffset) + "\n\n";

        disasmForAudit += smcRes.m_disasmForAudit;

        disasmForAudit += "# Direct-call IC code length = " + std::to_string(dcRes.m_icSize) + "\n";
        disasmForAudit += "# Direct-call IC CodePtr patch records:\n";
        for (auto& item : dcRes.m_codePtrPatchRecords)
        {
            disasmForAudit += std::string("#     offset = ") + std::to_string(item.first) + (item.second ? " (64-bit)\n" : " (32-bit)\n");
        }
        disasmForAudit += "\n";

        disasmForAudit += "# Closure-call IC code length = " + std::to_string(ccRes.m_icSize) + "\n";
        disasmForAudit += "# Closure-call IC CodePtr patch records:\n";
        for (auto& item : ccRes.m_codePtrPatchRecords)
        {
            disasmForAudit += std::string("#     offset = ") + std::to_string(item.first) + (item.second ? " (64-bit)\n" : " (32-bit)\n");
        }
        disasmForAudit += "\n";
    }

    return {
        .m_module = std::move(module),
        .m_resultFnName = resultFuncName,
        .m_dcIcCodePtrPatchRecords = dcRes.m_codePtrPatchRecords,
        .m_ccIcCodePtrPatchRecords = ccRes.m_codePtrPatchRecords,
        .m_dcIcSize = dcRes.m_icSize,
        .m_ccIcSize = ccRes.m_icSize,
        .m_disasmForAudit = disasmForAudit,
        .m_uniqueOrd = icInfo.m_uniqueOrd
    };
}

}   // namespace dast
