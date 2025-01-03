#include "deegen_type_based_hcs_helper.h"
#include "deegen_register_pinning_scheme.h"
#include "tvalue_typecheck_optimization.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_dfg_jit_impl_creator.h"

namespace dast {

std::unordered_map<uint64_t /*operandOrd*/, uint64_t /*argOrd*/> TypeBasedHCSHelper::GetQuickeningSlowPathAdditionalArgs(BytecodeVariantDefinition* bytecodeDef)
{
    ReleaseAssert(bytecodeDef->HasQuickeningSlowPath());

    // We can only use the unused registers in InterpreterFunctionInterface, so unfortunately this is
    // currently tightly coupled with our value-passing convention of InterpreterFunctionInterface..
    //
    // The already-decoded args will be passed to the continuation using registers in the following order.
    //
    std::vector<uint64_t> gprList = RegisterPinningScheme::GetAvaiableGPRListForBytecodeSlowPath();
    std::vector<uint64_t> fprList = RegisterPinningScheme::GetAvaiableFPRListForBytecodeSlowPath();

    // We will use pop_back for simplicity, so reverse the vector now
    //
    std::reverse(gprList.begin(), gprList.end());
    std::reverse(fprList.begin(), fprList.end());

    std::unordered_map<uint64_t /*operandOrd*/, uint64_t /*argOrd*/> res;
    for (auto& it : bytecodeDef->m_quickening)
    {
        TypeMaskTy mask = it.m_speculatedMask;
        // This is the only case that we want to (and can) use FPR to hold the TValue directly
        //
        bool shouldUseFPR = (mask == x_typeMaskFor<tDoubleNotNaN>);
        size_t operandOrd = it.m_operandOrd;

        if (shouldUseFPR)
        {
            if (fprList.empty())
            {
                continue;
            }
            size_t argOrd = fprList.back();
            fprList.pop_back();
            res[operandOrd] = argOrd;
        }
        else
        {
            if (gprList.empty())
            {
                continue;
            }
            size_t argOrd = gprList.back();
            gprList.pop_back();
            res[operandOrd] = argOrd;
        }
    }
    return res;
}

llvm::Value* WARN_UNUSED TypeBasedHCSHelper::GetBytecodeOperandUsageValueFromAlreadyDecodedArgs(llvm::Function* interfaceFn, uint64_t argOrd, llvm::BasicBlock* bb)
{
    return RegisterPinningScheme::GetArgumentAsInt64Value(interfaceFn, argOrd, bb);
}

void TypeBasedHCSHelper::GenerateCheckConditionLogic(DeegenBytecodeImplCreatorBase* ifi,
                                                     std::vector<llvm::Value*> bytecodeOperandUsageValueList,
                                                     llvm::BasicBlock*& bb /*inout*/)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();

    ReleaseAssert(ifi->IsMainComponent());

    Function* wrapper = bb->getParent();
    ReleaseAssert(wrapper != nullptr);

    bool shouldBranchToSaveRegStub = false;
    // If we are DFG JIT and reg alloc is enabled, we need to branch to the SaveRegStub to save the regs
    //
    if (ifi->IsDfgJIT() && !ifi->AsDfgJIT()->IsRegAllocDisabled())
    {
        shouldBranchToSaveRegStub = true;
    }

    // First create the basic block that fallbacks to the slow path
    //
    BasicBlock* slowpathBB;
    {
        std::string slowpathName = BytecodeIrInfo::GetQuickeningSlowPathFuncName(ifi->GetBytecodeDef());
        if (shouldBranchToSaveRegStub)
        {
            slowpathName += "_save_registers";
        }

        Function* slowpathFn = RegisterPinningScheme::CreateFunction(ifi->GetModule(), slowpathName);
        slowpathFn->addFnAttr(Attribute::NoInline);
        ReleaseAssert(slowpathFn->getName() == slowpathName);
        slowpathBB = BasicBlock::Create(ctx, "slowpath", wrapper);
        Instruction* tmp = new UnreachableInst(ctx, slowpathBB);
        Value* curBytecode = nullptr;
        Value* slowPathData = nullptr;
        Value* cbForTier = nullptr;
        if (ifi->IsInterpreter())
        {
            // The interpreter needs to pass curBytecode to slowpath.
            //
            InterpreterBytecodeImplCreator* ibc = assert_cast<InterpreterBytecodeImplCreator*>(ifi);
            curBytecode = ibc->GetCurBytecode();
            cbForTier = ibc->GetInterpreterCodeBlock();
        }
        else if (ifi->IsBaselineJIT() || ifi->IsDfgJIT())
        {
            // The baseline JIT / DFG JIT needs to pass the SlowPathData pointer
            // The logic to get the slow path data is the same regardless of whether the CodeBlock is BaselineCodeBlock or DfgCodeBlock
            //
            JitImplCreatorBase* j = assert_cast<JitImplCreatorBase*>(ifi);
            ReleaseAssert(!j->IsJitSlowPath());
            Value* offset = j->GetSlowPathDataOffsetFromJitFastPath(tmp /*insertBefore*/);
            Value* jitCodeBlock = j->GetJitCodeBlock();
            ReleaseAssert(llvm_value_has_type<void*>(jitCodeBlock));
            slowPathData = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), jitCodeBlock, { offset }, "", tmp /*insertBefore*/);
            cbForTier = jitCodeBlock;
        }
        else
        {
            ReleaseAssert(false);
        }

        CallInst* callInst;
        if (ifi->IsInterpreter())
        {
            callInst = ifi->GetExecFnContext()->PrepareDispatch<InterpreterInterface>()
                           .Set<RPV_StackBase>(ifi->GetStackBase())
                           .Set<RPV_CodeBlock>(cbForTier)
                           .Set<RPV_CurBytecode>(curBytecode)
                           .Dispatch(slowpathFn, tmp /*insertBefore*/);
        }
        else
        {
            ReleaseAssert(ifi->IsBaselineJIT() || ifi->IsDfgJIT());
            if (shouldBranchToSaveRegStub)
            {
                callInst = ifi->GetExecFnContext()->PrepareDispatch<JitAOTSlowPathSaveRegStubInterface>()
                               .Set<RPV_StackBase>(ifi->GetStackBase())
                               .Set<RPV_CodeBlock>(cbForTier)
                               .Set<RPV_JitSlowPathDataForSaveRegStub>(slowPathData)
                               .Dispatch(slowpathFn, tmp /*insertBefore*/);
            }
            else
            {
                callInst = ifi->GetExecFnContext()->PrepareDispatch<JitAOTSlowPathInterface>()
                               .Set<RPV_StackBase>(ifi->GetStackBase())
                               .Set<RPV_CodeBlock>(cbForTier)
                               .Set<RPV_JitSlowPathData>(slowPathData)
                               .Dispatch(slowpathFn, tmp /*insertBefore*/);
            }
        }
        tmp->eraseFromParent();

        // Try to pass already-decoded args to slow path. This is not possible in DFG due to reg alloc
        //
        if (ifi->IsInterpreter() || ifi->IsBaselineJIT())
        {
            std::unordered_map<uint64_t /*operandOrd*/, uint64_t /*argOrd*/> extraArgs = GetQuickeningSlowPathAdditionalArgs(ifi->GetBytecodeDef());
            for (auto& it : extraArgs)
            {
                uint64_t operandOrd = it.first;
                ReleaseAssert(operandOrd < bytecodeOperandUsageValueList.size());
                uint32_t argOrd = SafeIntegerCast<uint32_t>(it.second);
                ReleaseAssert(argOrd < slowpathFn->arg_size());
                Type* desiredArgTy = slowpathFn->getArg(argOrd)->getType();
                Value* srcValue = bytecodeOperandUsageValueList[operandOrd];
                ReleaseAssert(llvm_value_has_type<uint64_t>(srcValue));
                Value* argValue;
                if (llvm_type_has_type<double>(desiredArgTy))
                {
                    argValue = new BitCastInst(srcValue, llvm_type_of<double>(ctx), "", callInst /*insertBefore*/);
                }
                else if (llvm_type_has_type<void*>(desiredArgTy))
                {
                    argValue = new IntToPtrInst(srcValue, llvm_type_of<void*>(ctx), "", callInst /*insertBefore*/);
                }
                else
                {
                    ReleaseAssert(llvm_type_has_type<uint64_t>(desiredArgTy));
                    argValue = srcValue;
                }
                ReleaseAssert(argValue->getType() == desiredArgTy);
                RegisterPinningScheme::SetExtraDispatchArgument(callInst, argOrd, argValue);
            }
        }

        // For DFG, if reg alloc is enabled, we need to pass all registers to slow path as well
        //
        ReleaseAssertIff(shouldBranchToSaveRegStub, ifi->IsDfgJIT() && !ifi->AsDfgJIT()->IsRegAllocDisabled());
        if (shouldBranchToSaveRegStub)
        {
            DfgJitImplCreator* j = ifi->AsDfgJIT();
            ForEachDfgRegAllocRegister(
                [&](X64Reg reg) {
                    PhyRegUseKind purpose = j->GetRegisterPurpose(reg).Kind();
                    if (purpose == PhyRegUseKind::ScratchUse)
                    {
                        return;
                    }
                    Value* regVal = RegisterPinningScheme::GetRegisterValueAtEntry(wrapper, reg);
                    RegisterPinningScheme::SetExtraDispatchArgument(callInst, reg, regVal);
                });
        }
    }

    // Now, create each check of the quickening condition
    //
    Function* expectIntrin = Intrinsic::getDeclaration(ifi->GetModule(), Intrinsic::expect, { Type::getInt1Ty(ctx) });
    TypeCheckFunctionSelector tcFnSelector(ifi->GetModule());

    for (auto& it : ifi->GetBytecodeDef()->m_quickening)
    {
        size_t operandOrd = it.m_operandOrd;
        ReleaseAssert(operandOrd < bytecodeOperandUsageValueList.size());
        TypeMaskTy mask = it.m_speculatedMask;
        // For interpreter/baseline JIT, there is no speculation, so the precondition is always tTop
        // For DFG JIT, the precondition is the speculated type mask if it exists
        //
        TypeMaskTy preconditionMask = x_typeMaskFor<tTop>;
        if (ifi->IsDfgJIT())
        {
            ReleaseAssert(ifi->GetBytecodeDef()->m_isDfgVariant);
            ReleaseAssert(operandOrd < ifi->GetBytecodeDef()->m_list.size());
            BcOperand* op = ifi->GetBytecodeDef()->m_list[operandOrd].get();
            ReleaseAssert(op->GetKind() == BcOperandKind::Slot);
            BcOpSlot* opSlot = assert_cast<BcOpSlot*>(op);
            if (opSlot->HasDfgSpeculation())
            {
                preconditionMask = opSlot->GetDfgSpecMask();
            }
        }
        TypeCheckFunctionSelector::QueryResult res = tcFnSelector.Query(mask, preconditionMask);
        ReleaseAssert(res.m_opKind == TypeCheckFunctionSelector::QueryResult::CallFunction);
        Function* callee = res.m_func;
        ReleaseAssert(callee != nullptr && callee->arg_size() == 1 && llvm_value_has_type<uint64_t>(callee->getArg(0)) && llvm_type_has_type<bool>(callee->getReturnType()));
        CallInst* checkPassed = CallInst::Create(callee, { bytecodeOperandUsageValueList[operandOrd] }, "", bb);
        ReleaseAssert(llvm_value_has_type<bool>(checkPassed));
        checkPassed->addRetAttr(Attribute::ZExt);
        checkPassed = CallInst::Create(expectIntrin, { checkPassed, CreateLLVMConstantInt<bool>(ctx, true) }, "", bb);

        BasicBlock* newBB = BasicBlock::Create(ctx, "", wrapper);
        BranchInst::Create(newBB /*ifTrue*/, slowpathBB /*ifFalse*/, checkPassed /*cond*/, bb);
        bb = newBB;
    }
}

}   // namespace dast
