#include "deegen_type_based_hcs_helper.h"
#include "deegen_interpreter_function_interface.h"
#include "tvalue_typecheck_optimization.h"
#include "deegen_baseline_jit_impl_creator.h"

namespace dast {

std::unordered_map<uint64_t /*operandOrd*/, uint64_t /*argOrd*/> TypeBasedHCSHelper::GetQuickeningSlowPathAdditionalArgs(BytecodeVariantDefinition* bytecodeDef)
{
    ReleaseAssert(bytecodeDef->HasQuickeningSlowPath());

    // We can only use the unused registers in InterpreterFunctionInterface, so unfortunately this is
    // currently tightly coupled with our value-passing convention of InterpreterFunctionInterface..
    //
    // The already-decoded args will be passed to the continuation using registers in the following order.
    //
    std::vector<uint64_t> gprList = InterpreterFunctionInterface::GetAvaiableGPRListForBytecodeSlowPath();
    std::vector<uint64_t> fprList = InterpreterFunctionInterface::GetAvaiableFPRListForBytecodeSlowPath();

    // We will use pop_back for simplicity, so reverse the vector now
    //
    std::reverse(gprList.begin(), gprList.end());
    std::reverse(fprList.begin(), fprList.end());

    std::unordered_map<uint64_t /*operandOrd*/, uint64_t /*argOrd*/> res;
    for (auto& it : bytecodeDef->m_quickening)
    {
        TypeSpeculationMask mask = it.m_speculatedMask;
        // This is the only case that we want to (and can) use FPR to hold the TValue directly
        //
        bool shouldUseFPR = (mask == x_typeSpeculationMaskFor<tDoubleNotNaN>);
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
    using namespace llvm;
    LLVMContext& ctx = interfaceFn->getContext();
    ReleaseAssert(interfaceFn->getFunctionType() == InterpreterFunctionInterface::GetType(ctx));
    ReleaseAssert(argOrd < interfaceFn->arg_size());
    Value* arg = interfaceFn->getArg(static_cast<uint32_t>(argOrd));
    if (llvm_value_has_type<double>(arg))
    {
        Instruction* dblToI64 = new BitCastInst(arg, llvm_type_of<uint64_t>(ctx), "", bb);
        return dblToI64;
    }
    else if (llvm_value_has_type<void*>(arg))
    {
        Instruction* ptrToI64 = new PtrToIntInst(arg, llvm_type_of<uint64_t>(ctx), "", bb);
        return ptrToI64;
    }
    else
    {
        ReleaseAssert(llvm_value_has_type<uint64_t>(arg));
        return arg;
    }
}

void TypeBasedHCSHelper::GenerateCheckConditionLogic(DeegenBytecodeImplCreatorBase* ifi,
                                                     std::vector<llvm::Value*> bytecodeOperandUsageValueList,
                                                     llvm::BasicBlock*& bb /*inout*/)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();

    Function* wrapper = bb->getParent();
    ReleaseAssert(wrapper != nullptr);

    // First create the basic block that fallbacks to the slow path
    //
    BasicBlock* slowpathBB;
    {
        std::string slowpathName = BytecodeIrInfo::GetQuickeningSlowPathFuncName(ifi->GetBytecodeDef());
        Function* slowpathFn = InterpreterFunctionInterface::CreateFunction(ifi->GetModule(), slowpathName);
        slowpathFn->addFnAttr(Attribute::AttrKind::NoReturn);
        slowpathFn->addFnAttr(Attribute::AttrKind::NoInline);
        ReleaseAssert(slowpathFn->getName() == slowpathName);
        slowpathBB = BasicBlock::Create(ctx, "slowpath", wrapper);
        Instruction* tmp = new UnreachableInst(ctx, slowpathBB);
        Value* callSiteInfo = nullptr;
        if (ifi->IsInterpreter())
        {
            // The interpreter needs to pass curBytecode to slowpath.
            //
            callSiteInfo = ifi->GetCurBytecode();
        }
        else
        {
            // The baseline JIT needs to pass the BaselineJITSlowPathData pointer
            //
            BaselineJitImplCreator* j = assert_cast<BaselineJitImplCreator*>(ifi);
            ReleaseAssert(!j->IsBaselineJitSlowPath());
            Value* offset = j->GetSlowPathDataOffsetFromJitFastPath(tmp /*insertBefore*/);
            Value* baselineJitCodeBlock = j->CallDeegenCommonSnippet("GetBaselineJitCodeBlockFromCodeBlock", { j->GetCodeBlock() }, tmp /*insertBefore*/);
            ReleaseAssert(llvm_value_has_type<void*>(baselineJitCodeBlock));
            callSiteInfo = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), baselineJitCodeBlock, { offset }, "", tmp /*insertBefore*/);
        }
        CallInst* callInst = InterpreterFunctionInterface::CreateDispatchToBytecodeSlowPath(slowpathFn, ifi->GetCoroutineCtx(), ifi->GetStackBase(), callSiteInfo, ifi->GetCodeBlock(), tmp /*insertBefore*/);
        tmp->eraseFromParent();

        std::unordered_map<uint64_t /*operandOrd*/, uint64_t /*argOrd*/> extraArgs = GetQuickeningSlowPathAdditionalArgs(ifi->GetBytecodeDef());
        for (auto& it : extraArgs)
        {
            uint64_t operandOrd = it.first;
            ReleaseAssert(operandOrd < bytecodeOperandUsageValueList.size());
            uint64_t argOrd = it.second;
            ReleaseAssert(argOrd < slowpathFn->arg_size());
            Type* desiredArgTy = slowpathFn->getArg(static_cast<uint32_t>(argOrd))->getType();
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
            ReleaseAssert(dyn_cast<UndefValue>(callInst->getArgOperand(static_cast<uint32_t>(argOrd))) != nullptr);
            callInst->setArgOperand(static_cast<uint32_t>(argOrd), argValue);
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
        TypeSpeculationMask mask = it.m_speculatedMask;
        TypeCheckFunctionSelector::QueryResult res = tcFnSelector.Query(mask, x_typeSpeculationMaskFor<tTop>);
        ReleaseAssert(res.m_opKind == TypeCheckFunctionSelector::QueryResult::CallFunction);
        Function* callee = res.m_func;
        ReleaseAssert(callee != nullptr && callee->arg_size() == 1 && llvm_value_has_type<uint64_t>(callee->getArg(0)) && llvm_type_has_type<bool>(callee->getReturnType()));
        CallInst* checkPassed = CallInst::Create(callee, { bytecodeOperandUsageValueList[operandOrd] }, "", bb);
        ReleaseAssert(llvm_value_has_type<bool>(checkPassed));
        checkPassed = CallInst::Create(expectIntrin, { checkPassed, CreateLLVMConstantInt<bool>(ctx, true) }, "", bb);

        BasicBlock* newBB = BasicBlock::Create(ctx, "", wrapper);
        BranchInst::Create(newBB /*ifTrue*/, slowpathBB /*ifFalse*/, checkPassed /*cond*/, bb);
        bb = newBB;
    }
}

}   // namespace dast
