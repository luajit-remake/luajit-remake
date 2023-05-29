#include "deegen_ast_return.h"
#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_interpreter_function_interface.h"
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

void AstBytecodeReturn::DoLoweringForInterpreter(InterpreterBytecodeImplCreator* ifi)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();

    // If the bytecode has an output, store it now
    //
    ReleaseAssertIff(HasValueOutput(), ifi->GetBytecodeDef()->m_hasOutputValue);
    if (HasValueOutput())
    {
        Value* slot = ifi->GetOutputSlot();
        ReleaseAssert(llvm_value_has_type<uint64_t>(slot));
        GetElementPtrInst* bvPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), ifi->GetStackBase(), { slot }, "", m_origin /*insertBefore*/);
        ReleaseAssert(llvm_value_has_type<uint64_t>(m_valueOperand));
        std::ignore = new StoreInst(m_valueOperand, bvPtr, false /*isVolatile*/, Align(8), m_origin /*insertBefore*/);
    }

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

        ifi->CallDeegenCommonSnippet("UpdateInterpreterTierUpCounterForBranch", { ifi->GetCodeBlock(), ifi->GetCurBytecode(), bytecodeTarget }, m_origin /*insertBefore*/);
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

    InterpreterFunctionInterface::CreateDispatchToBytecode(
        targetFunction,
        ifi->GetCoroutineCtx(),
        ifi->GetStackBase(),
        bytecodeTarget,
        ifi->GetCodeBlock(),
        m_origin /*insertBefore*/);

    AssertInstructionIsFollowedByUnreachable(m_origin);
    Instruction* unreachableInst = m_origin->getNextNode();
    m_origin->eraseFromParent();
    unreachableInst->eraseFromParent();
    m_origin = nullptr;
}

void AstBytecodeReturn::DoLoweringForBaselineJIT(BaselineJitImplCreator* ifi)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();

    // If the bytecode has an output, store it now
    //
    ReleaseAssertIff(HasValueOutput(), ifi->GetBytecodeDef()->m_hasOutputValue);
    if (HasValueOutput())
    {
        Value* slot = ifi->GetOutputSlot();
        ReleaseAssert(llvm_value_has_type<uint64_t>(slot));
        GetElementPtrInst* bvPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), ifi->GetStackBase(), { slot }, "", m_origin /*insertBefore*/);
        ReleaseAssert(llvm_value_has_type<uint64_t>(m_valueOperand));
        std::ignore = new StoreInst(m_valueOperand, bvPtr, false /*isVolatile*/, Align(8), m_origin /*insertBefore*/);
    }

    // Jump to the correct destination in JIT'ed code
    //
    Value* target;
    if (DoesBranch())
    {
        ReleaseAssert(ifi->GetBytecodeDef()->m_hasConditionalBranchTarget);
        target = ifi->GetCondBrDest();
    }
    else
    {
        target = ifi->GetFallthroughDest();
    }
    ReleaseAssert(llvm_value_has_type<void*>(target));

    InterpreterFunctionInterface::CreateDispatchToBytecode(
        target,
        ifi->GetCoroutineCtx(),
        ifi->GetStackBase(),
        UndefValue::get(llvm_type_of<void*>(ctx)),
        ifi->GetCodeBlock(),
        m_origin /*insertBefore*/);

    AssertInstructionIsFollowedByUnreachable(m_origin);
    Instruction* unreachableInst = m_origin->getNextNode();
    m_origin->eraseFromParent();
    unreachableInst->eraseFromParent();
    m_origin = nullptr;
}

}   // namespace dast
