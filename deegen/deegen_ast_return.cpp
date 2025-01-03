#include "deegen_ast_return.h"
#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_register_pinning_scheme.h"
#include "deegen_dfg_jit_impl_creator.h"
#include "deegen_bytecode_operand.h"
#include "deegen_options.h"

namespace dast {

std::vector<AstBytecodeReturn> WARN_UNUSED AstBytecodeReturn::GetAllUseInFunction(llvm::Function* func)
{
    using namespace llvm;
    std::vector<AstBytecodeReturn> result;
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
                    std::string calleeName = callee->getName().str();
                    if (calleeName == "DeegenImpl_ReturnValue" ||
                        calleeName == "DeegenImpl_ReturnNone" ||
                        calleeName == "DeegenImpl_ReturnValueAndBranch" ||
                        calleeName == "DeegenImpl_ReturnNoneAndBranch")
                    {
                        AstBytecodeReturn item;
                        item.m_origin = callInst;
                        if (calleeName == "DeegenImpl_ReturnValue" || calleeName == "DeegenImpl_ReturnValueAndBranch")
                        {
                            ReleaseAssert(callInst->arg_size() == 1);
                            item.m_valueOperand = callInst->getArgOperand(0);
                            ReleaseAssert(llvm_value_has_type<uint64_t>(item.m_valueOperand));
                        }
                        else
                        {
                            item.m_valueOperand = nullptr;
                        }
                        item.m_doesBranch = (calleeName == "DeegenImpl_ReturnValueAndBranch" || calleeName == "DeegenImpl_ReturnNoneAndBranch");
                        result.push_back(item);
                    }
                }
            }
        }
    }
    return result;
}

llvm::Value* GetInterpreterFunctionFromInterpreterOpcode(llvm::Module* module, llvm::Value* opcode, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    std::string dispatchTableSymbolName = x_deegen_interpreter_dispatch_table_symbol_name;

    LLVMContext& ctx = module->getContext();
    llvm::ArrayType* dispatchTableTy = llvm::ArrayType::get(llvm_type_of<void*>(ctx), 0 /*numElements*/);
    if (module->getNamedValue(dispatchTableSymbolName) == nullptr)
    {
        GlobalVariable* tmp = new GlobalVariable(*module,
                                                 dispatchTableTy /*valueType*/,
                                                 true /*isConstant*/,
                                                 GlobalValue::ExternalLinkage,
                                                 nullptr /*initializer*/,
                                                 dispatchTableSymbolName /*name*/);
        ReleaseAssert(tmp->getName().str() == dispatchTableSymbolName);
        tmp->setAlignment(MaybeAlign(8));
        tmp->setDSOLocal(true);
    }

    GlobalVariable* gv = module->getGlobalVariable(dispatchTableSymbolName);
    ReleaseAssert(gv != nullptr);
    ReleaseAssert(gv->isConstant());
    ReleaseAssert(!gv->hasInitializer());
    ReleaseAssert(gv->getValueType() == dispatchTableTy);

    GetElementPtrInst* gepInst = GetElementPtrInst::CreateInBounds(
        dispatchTableTy /*pointeeType*/, gv,
        { CreateLLVMConstantInt<uint64_t>(ctx, 0), opcode },
        "" /*name*/, insertBefore);

    LoadInst* result = new LoadInst(llvm_type_of<void*>(ctx), gepInst, "" /*name*/, false /*isVolatile*/, Align(8), insertBefore);
    return result;
}

void AstBytecodeReturn::EmitStoreOutputToStackLogic(DeegenBytecodeImplCreatorBase* ifi, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();

    ReleaseAssertIff(HasValueOutput(), ifi->GetBytecodeDef()->m_hasOutputValue);
    if (HasValueOutput())
    {
        Value* slot = ifi->GetOutputSlot();
        ReleaseAssert(llvm_value_has_type<uint64_t>(slot));
        GetElementPtrInst* bvPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), ifi->GetStackBase(), { slot }, "", insertBefore);
        ReleaseAssert(llvm_value_has_type<uint64_t>(m_valueOperand));
        std::ignore = new StoreInst(m_valueOperand, bvPtr, false /*isVolatile*/, Align(8), insertBefore);
    }
}

void AstBytecodeReturn::DoLoweringForInterpreter(InterpreterBytecodeImplCreator* ifi)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();

    // If the bytecode has an output, store it now
    //
    EmitStoreOutputToStackLogic(ifi, m_origin /*insertBefore*/);

    // Compute the next bytecode to execute
    //
    Value* bytecodeTarget;
    if (DoesBranch())
    {
        ReleaseAssert(ifi->GetBytecodeDef()->m_hasConditionalBranchTarget);
        Value* bytecodeOffset = ifi->GetCondBrDest();
        ReleaseAssert(llvm_value_has_type<int32_t>(bytecodeOffset));
        Instruction* offset64 = new SExtInst(bytecodeOffset, llvm_type_of<int64_t>(ctx), "", m_origin /*insertBefore*/);
        bytecodeTarget = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), ifi->GetCurBytecode(), { offset64 }, "", m_origin /*insertBefore*/);

        ifi->CallDeegenCommonSnippet("UpdateInterpreterTierUpCounterForBranch", { ifi->GetInterpreterCodeBlock(), ifi->GetCurBytecode(), bytecodeTarget }, m_origin /*insertBefore*/);
    }
    else
    {
        Value* offset64 = CreateLLVMConstantInt<uint64_t>(ctx, ifi->GetBytecodeDef()->GetBytecodeStructLength());
        bytecodeTarget = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), ifi->GetCurBytecode(), { offset64 }, "", m_origin /*insertBefore*/);
    }
    ReleaseAssert(llvm_value_has_type<void*>(bytecodeTarget));

    Value* opcode = BytecodeVariantDefinition::DecodeBytecodeOpcode(bytecodeTarget, m_origin /*insertBefore*/);
    ReleaseAssert(llvm_value_has_type<uint64_t>(opcode));

    Value* targetFunction = GetInterpreterFunctionFromInterpreterOpcode(ifi->GetModule(), opcode, m_origin /*insertBefore*/);
    ReleaseAssert(llvm_value_has_type<void*>(targetFunction));

    ifi->GetExecFnContext()->PrepareDispatch<InterpreterInterface>()
        .Set<RPV_StackBase>(ifi->GetStackBase())
        .Set<RPV_CodeBlock>(ifi->GetInterpreterCodeBlock())
        .Set<RPV_CurBytecode>(bytecodeTarget)
        .Dispatch(targetFunction, m_origin /*insertBefore*/);

    AssertInstructionIsFollowedByUnreachable(m_origin);
    Instruction* unreachableInst = m_origin->getNextNode();
    m_origin->eraseFromParent();
    unreachableInst->eraseFromParent();
    m_origin = nullptr;
}

void AstBytecodeReturn::DoLoweringForBaselineJIT(BaselineJitImplCreator* ifi)
{
    using namespace llvm;

    // If the bytecode has an output, store it now
    //
    EmitStoreOutputToStackLogic(ifi, m_origin /*insertBefore*/);

    // Jump to the correct destination in JIT'ed code
    //
    Value* target;
    if (DoesBranch())
    {
        ReleaseAssert(ifi->HasCondBrTarget());
        target = ifi->GetCondBrDest();
    }
    else
    {
        target = ifi->GetFallthroughDest();
    }
    ReleaseAssert(llvm_value_has_type<void*>(target));

    ifi->GetExecFnContext()->PrepareDispatch<JitGeneratedCodeInterface>()
        .Set<RPV_StackBase>(ifi->GetStackBase())
        .Set<RPV_CodeBlock>(ifi->GetJitCodeBlock())
        .Dispatch(target, m_origin /*insertBefore*/);

    AssertInstructionIsFollowedByUnreachable(m_origin);
    Instruction* unreachableInst = m_origin->getNextNode();
    m_origin->eraseFromParent();
    unreachableInst->eraseFromParent();
    m_origin = nullptr;
}

void AstBytecodeReturn::DoLoweringForDfgJIT(DfgJitImplCreator* ifi, llvm::Function* func)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();

    // We use 0 to indicate a branch is not taken, and any non-zero value will work to indicate that the branch is taken.
    // Of course 1 works fine, but if we use a tag register, the instruction is shorter (movl 1, %reg is 5~6 bytes, mov %r15, %reg is only 3 bytes)
    //
    auto getBranchTakenDecisionValue = [&]() -> uint64_t
    {
        ReleaseAssert(TValue::x_mivTag != 0);
        return TValue::x_mivTag;
    };

    auto emitStoreCondBrDecisionToStackLogic = [&](Instruction* insertBefore)
    {
        ReleaseAssert(ifi->HasBranchDecisionOutput() && !ifi->IsBranchDecisionRegAllocated());
        uint64_t branchDecision = (DoesBranch() ? getBranchTakenDecisionValue() : 0);
        Value* brDecisionSlot = ifi->GetCondBrDecisionSlot();
        ReleaseAssert(llvm_value_has_type<uint64_t>(brDecisionSlot));
        GetElementPtrInst* bdPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), ifi->GetStackBase(), { brDecisionSlot }, "", insertBefore);
        std::ignore = new StoreInst(CreateLLVMConstantInt<uint64_t>(ctx, branchDecision), bdPtr, false /*isVolatile*/, Align(8), insertBefore);
    };

    if (!ifi->IsMainComponent())
    {
        ReleaseAssert(ifi->IsRegAllocDisabled());

        EmitStoreOutputToStackLogic(ifi, m_origin /*insertBefore*/);

        // If the bytecode makes a branch decision, we must output the branch decision here
        //
        if (ifi->HasBranchDecisionOutput())
        {
            emitStoreCondBrDecisionToStackLogic(m_origin /*insertBefore*/);
        }

        std::map<size_t /*seqOrd*/, Value*> spilledRegs;

        // For slow path, if register allocation is enabled for the fast path, we must load all the registers from the spill area
        // (Note that those register values are spilled when we enter the slow path from fast path)
        //
        if (!ifi->IsFastPathRegAllocAlwaysDisabled())
        {
            // Note that we must not be JIT code, since if we were JIT code (which means we are a fast path return continuation),
            // then reg alloc would have been always disabled (since there exists a fast path guest language call).
            // Therefore, we must be the AOT slow path and the registers are spilled on the stack.
            //
            ReleaseAssert(ifi->IsJitSlowPath());

            Value* offset = ifi->CallDeegenCommonSnippet("GetStackRegSpillRegionOffsetFromDfgCodeBlock", ifi->GetJitCodeBlock(), m_origin);
            ReleaseAssert(llvm_value_has_type<uint64_t>(offset));
            Value* regionStart = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), ifi->GetStackBase(), { offset }, "", m_origin);

            ForEachDfgRegAllocRegister([&](X64Reg reg) {
                size_t seqOrd = GetDfgRegAllocSequenceOrdForReg(reg);
                GetElementPtrInst* ptr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), regionStart, { CreateLLVMConstantInt<uint64_t>(ctx, seqOrd) }, "", m_origin);
                LoadInst* regVal = new LoadInst(llvm_type_of<uint64_t>(ctx), ptr, "" /*name*/, false /*isVolatile*/, Align(8), m_origin);
                ReleaseAssert(!spilledRegs.count(seqOrd));
                spilledRegs[seqOrd] = regVal;
            });
        }

        CallInst* ci = ifi->GetExecFnContext()->PrepareDispatch<JitGeneratedCodeInterface>()
            .Set<RPV_StackBase>(ifi->GetStackBase())
            .Set<RPV_CodeBlock>(ifi->GetJitCodeBlock())
            .Dispatch(ifi->GetFallthroughDest(), m_origin /*insertBefore*/);

        // The dispatch created above puts undef to all the register arguments, we need to replace them with actual values here
        //
        if (!ifi->IsFastPathRegAllocAlwaysDisabled())
        {
            ForEachDfgRegAllocRegister([&](X64Reg reg) {
                size_t seqOrd = GetDfgRegAllocSequenceOrdForReg(reg);
                ReleaseAssert(spilledRegs.count(seqOrd));
                Value* regVal = spilledRegs[seqOrd];
                ReleaseAssert(llvm_value_has_type<uint64_t>(regVal));
                uint32_t argOrd = RegisterPinningScheme::GetArgumentOrdinalForRegister(reg);
                ReleaseAssert(argOrd < ci->arg_size());
                Value* castedVal = RegisterPinningScheme::EmitCastI64ToArgumentType(regVal, argOrd, ci /*insertBefore*/);
                RegisterPinningScheme::SetExtraDispatchArgument(ci, argOrd, castedVal);
            });
        }
    }
    else
    {
        // The registers needed to be passed to the continuation
        //
        std::unordered_map<X64Reg, Value*> regValue;

        if (!ifi->IsRegAllocDisabled())
        {
            ForEachDfgRegAllocRegister([&](X64Reg reg) {
                PhyRegUseKind purpose = ifi->GetRegisterPurpose(reg).Kind();
                if (purpose == PhyRegUseKind::ScratchUse)
                {
                    // This is a scratch register, its value can be safely garbaged and not need to pass to continuation
                    //
                    return;
                }
                Value* regVal = RegisterPinningScheme::GetRegisterValueAtEntry(func, reg);
                ReleaseAssert(!regValue.count(reg));
                regValue[reg] = regVal;
            });
        }

        if (HasValueOutput())
        {
            if (!ifi->IsOutputRegAllocated())
            {
                EmitStoreOutputToStackLogic(ifi, m_origin /*insertBefore*/);
            }
            else
            {
                X64Reg reg = ifi->GetRegisterForOutput();
                uint32_t argOrd = RegisterPinningScheme::GetArgumentOrdinalForRegister(reg);
                ReleaseAssert(llvm_value_has_type<uint64_t>(m_valueOperand));
                Value* castedVal = RegisterPinningScheme::EmitCastI64ToArgumentType(m_valueOperand, argOrd, m_origin);
                ReleaseAssert(regValue.count(reg));
                regValue[reg] = castedVal;
            }
        }

        if (ifi->HasBranchDecisionOutput())
        {
            if (!ifi->IsBranchDecisionRegAllocated())
            {
                emitStoreCondBrDecisionToStackLogic(m_origin /*insertBefore*/);
            }
            else
            {
                X64Reg reg = ifi->GetRegisterForBranchDecision();
                uint32_t argOrd = RegisterPinningScheme::GetArgumentOrdinalForRegister(reg);
                Value* brDecision = CreateLLVMConstantInt<uint64_t>(ctx, (DoesBranch() ? getBranchTakenDecisionValue() : 0));
                Value* castedVal = RegisterPinningScheme::EmitCastI64ToArgumentType(brDecision, argOrd, m_origin);
                ReleaseAssert(regValue.count(reg));
                regValue[reg] = castedVal;
            }
        }

        CallInst* ci = ifi->GetExecFnContext()->PrepareDispatch<JitGeneratedCodeInterface>()
            .Set<RPV_StackBase>(ifi->GetStackBase())
            .Set<RPV_CodeBlock>(ifi->GetJitCodeBlock())
            .Dispatch(ifi->GetFallthroughDest(), m_origin /*insertBefore*/);

        if (!ifi->IsRegAllocDisabled())
        {
            for (auto& it : regValue)
            {
                X64Reg reg = it.first;
                Value* regVal = it.second;
                RegisterPinningScheme::SetExtraDispatchArgument(ci, reg, regVal);
            }
        }
    }

    AssertInstructionIsFollowedByUnreachable(m_origin);
    Instruction* unreachableInst = m_origin->getNextNode();
    m_origin->eraseFromParent();
    unreachableInst->eraseFromParent();
    m_origin = nullptr;
}

}   // namespace dast
