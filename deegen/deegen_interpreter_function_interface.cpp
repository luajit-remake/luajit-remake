#include "deegen_interpreter_function_interface.h"
#include "deegen_options.h"

namespace dast {

llvm::FunctionType* WARN_UNUSED InterpreterFunctionInterface::GetType(llvm::LLVMContext& ctx)
{
    using namespace llvm;
    // We use GHC calling convension so we can pass more info and allow less-overhead C runtime call.
    //
    // Speficially, all 6 callee-saved registers (under default cc calling convention)
    // are available as parameters in GHC. We use this as a register pinning mechanism to pin
    // the important states (coroutineCtx, stackBase, etc) into these registers
    // so that they are not clobbered by C calls.
    //
    // GHC passes integral parameters in the following order:
    //   R13 [CC/MSABI callee saved]
    //   RBP [CC/MSABI callee saved]
    //   R12 [CC/MSABI callee saved]
    //   RBX [CC/MSABI callee saved]
    //   R14 [CC/MSABI callee saved]
    //   RSI [MSABI callee saved]
    //   RDI [MSABI callee saved]
    //   R8
    //   R9
    //   R15 [CC/MSABI callee saved]
    //
    return FunctionType::get(
        llvm_type_of<void>(ctx) /*result*/,
        {
            // R13 [CC/MSABI callee saved]
            // CoroutineCtx
            //
            llvm_type_of<void*>(ctx),

            // RBP [CC/MSABI callee saved]
            // StackBase (for return continuation, this is the callee's stack base)
            //
            llvm_type_of<void*>(ctx),

            // R12 [CC/MSABI callee saved]
            // For bytecode function: the current bytecode
            // For return continuation: unused
            // For function entry: #args
            //
            llvm_type_of<void*>(ctx),

            // RBX [CC/MSABI callee saved]
            // For bytecode function: the current codeBlock
            // For return continuation: unused
            // For function entry: codeblockHeapPtr
            //
            llvm_type_of<void*>(ctx),

            // R14 [CC/MSABI callee saved]
            // Tag register 1
            //
            llvm_type_of<uint64_t>(ctx),

            // RSI [MSABI callee saved]
            // For return continuation: the start of the ret values
            // Otherwise unused
            //
            llvm_type_of<void*>(ctx),

            // RDI [MSABI callee saved]
            // For return continuation: the # of ret values
            // For function entry: isMustTail64
            // Otherwise unused
            //
            llvm_type_of<uint64_t>(ctx),

            // R8
            // unused
            //
            llvm_type_of<uint64_t>(ctx),

            // R9
            // unused
            //
            llvm_type_of<uint64_t>(ctx),

            // R15 [CC/MSABI callee saved]
            // Tag register 2
            //
            llvm_type_of<uint64_t>(ctx),

            // XMM1
            // unused
            //
            llvm_type_of<double>(ctx),

            // XMM2
            // unused
            //
            llvm_type_of<double>(ctx),

            // XMM3
            // unused
            //
            llvm_type_of<double>(ctx),

            // XMM4
            // unused
            //
            llvm_type_of<double>(ctx),

            // XMM5
            // unused
            //
            llvm_type_of<double>(ctx),

            // XMM6
            // unused
            //
            llvm_type_of<double>(ctx)
        } /*params*/,
        false /*isVarArg*/);
}

llvm::Function* WARN_UNUSED InterpreterFunctionInterface::CreateFunction(llvm::Module* module, const std::string& name)
{
    using namespace llvm;
    ReleaseAssert(module->getNamedValue(name) == nullptr);
    Function* func = Function::Create(GetType(module->getContext()), GlobalValue::ExternalLinkage, name, module);
    ReleaseAssert(func != nullptr && func->getName() == name);
    func->setCallingConv(CallingConv::GHC);
    func->setDSOLocal(true);
    return func;
}

std::vector<uint64_t> WARN_UNUSED InterpreterFunctionInterface::GetAvaiableGPRListForBytecodeSlowPath()
{
    // The order doesn't matter. But I chose the order of GPR list to stay away from the C calling conv registers,
    // in the hope that it can reduce the likelihood of register shuffling when making C calls.
    //
    return std::vector<uint64_t> { 8 /*R9*/, 7 /*R8*/, 5 /*RSI*/, 6 /*RDI*/ };
}

std::vector<uint64_t> WARN_UNUSED InterpreterFunctionInterface::GetAvaiableFPRListForBytecodeSlowPath()
{
    return std::vector<uint64_t> { 10 /*XMM1*/, 11 /*XMM2*/, 12 /*XMM3*/, 13 /*XMM4*/, 14 /*XMM5*/, 15 /*XMM6*/ };
}

static llvm::CallInst* InterpreterFunctionCreateDispatchToBytecodeImpl(llvm::Value* target, llvm::Value* coroutineCtx, llvm::Value* stackbase, llvm::Value* bytecodePtr, llvm::Value* codeBlock, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = target->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(target));
    ReleaseAssert(llvm_value_has_type<void*>(coroutineCtx));
    ReleaseAssert(llvm_value_has_type<void*>(stackbase));
    ReleaseAssert(llvm_value_has_type<void*>(bytecodePtr));
    ReleaseAssert(llvm_value_has_type<void*>(codeBlock));
    ReleaseAssert(insertBefore != nullptr);
    ReleaseAssert(insertBefore->getParent() != nullptr);
    Function* func = insertBefore->getParent()->getParent();
    ReleaseAssert(func != nullptr);

    CallInst* callInst = CallInst::Create(
        InterpreterFunctionInterface::GetType(ctx),
        target,
        {
            /*R13*/ coroutineCtx,
            /*RBP*/ stackbase,
            /*R12*/ bytecodePtr,
            /*RBX*/ codeBlock,
            /*R14*/ func->getArg(4),
            /*RSI*/ UndefValue::get(llvm_type_of<void*>(ctx)),
            /*RDI*/ UndefValue::get(llvm_type_of<uint64_t>(ctx)),
            /*R8 */ UndefValue::get(llvm_type_of<uint64_t>(ctx)),
            /*R9 */ UndefValue::get(llvm_type_of<uint64_t>(ctx)),
            /*R15*/ func->getArg(9),
            /*XMM 1-6*/
            UndefValue::get(llvm_type_of<double>(ctx)),
            UndefValue::get(llvm_type_of<double>(ctx)),
            UndefValue::get(llvm_type_of<double>(ctx)),
            UndefValue::get(llvm_type_of<double>(ctx)),
            UndefValue::get(llvm_type_of<double>(ctx)),
            UndefValue::get(llvm_type_of<double>(ctx))
        },
        "" /*name*/,
        insertBefore);
    callInst->setTailCallKind(CallInst::TailCallKind::TCK_MustTail);
    callInst->setCallingConv(CallingConv::GHC);
    ReleaseAssert(llvm_value_has_type<void>(callInst));

    std::ignore = ReturnInst::Create(ctx, nullptr /*retVal*/, insertBefore);

    return callInst;
}

llvm::CallInst* InterpreterFunctionInterface::CreateDispatchToBytecode(llvm::Value* target, llvm::Value* coroutineCtx, llvm::Value* stackbase, llvm::Value* bytecodePtr, llvm::Value* codeBlock, llvm::Instruction* insertBefore)
{
    return InterpreterFunctionCreateDispatchToBytecodeImpl(target, coroutineCtx, stackbase, bytecodePtr, codeBlock, insertBefore);
}

llvm::CallInst* InterpreterFunctionInterface::CreateDispatchToBytecodeSlowPath(llvm::Value* target, llvm::Value* coroutineCtx, llvm::Value* stackbase, llvm::Value* bytecodePtr, llvm::Value* codeBlock, llvm::Instruction* insertBefore)
{
    return InterpreterFunctionCreateDispatchToBytecodeImpl(target, coroutineCtx, stackbase, bytecodePtr, codeBlock, insertBefore);
}

llvm::CallInst* InterpreterFunctionInterface::CreateDispatchToReturnContinuation(llvm::Value* target, llvm::Value* coroutineCtx, llvm::Value* stackbase, llvm::Value* retStart, llvm::Value* numRets, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = target->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(target));
    ReleaseAssert(llvm_value_has_type<void*>(coroutineCtx));
    ReleaseAssert(llvm_value_has_type<void*>(stackbase));
    ReleaseAssert(llvm_value_has_type<void*>(retStart));
    ReleaseAssert(llvm_value_has_type<uint64_t>(numRets));
    ReleaseAssert(insertBefore != nullptr);
    ReleaseAssert(insertBefore->getParent() != nullptr);
    Function* func = insertBefore->getParent()->getParent();
    ReleaseAssert(func != nullptr);

    CallInst* callInst = CallInst::Create(
        GetType(ctx),
        target,
        {
            /*R13*/ coroutineCtx,
            /*RBP*/ stackbase,
            /*R12*/ UndefValue::get(llvm_type_of<void*>(ctx)),
            /*RBX*/ UndefValue::get(llvm_type_of<void*>(ctx)),
            /*R14*/ func->getArg(4),
            /*RSI*/ retStart,
            /*RDI*/ numRets,
            /*R8 */ UndefValue::get(llvm_type_of<uint64_t>(ctx)),
            /*R9 */ UndefValue::get(llvm_type_of<uint64_t>(ctx)),
            /*R15*/ func->getArg(9),
            /*XMM 1-6*/
            UndefValue::get(llvm_type_of<double>(ctx)),
            UndefValue::get(llvm_type_of<double>(ctx)),
            UndefValue::get(llvm_type_of<double>(ctx)),
            UndefValue::get(llvm_type_of<double>(ctx)),
            UndefValue::get(llvm_type_of<double>(ctx)),
            UndefValue::get(llvm_type_of<double>(ctx))
        },
        "" /*name*/,
        insertBefore);
    callInst->setTailCallKind(CallInst::TailCallKind::TCK_MustTail);
    callInst->setCallingConv(CallingConv::GHC);
    ReleaseAssert(llvm_value_has_type<void>(callInst));

    std::ignore = ReturnInst::Create(ctx, nullptr /*retVal*/, insertBefore);

    return callInst;
}

llvm::CallInst* InterpreterFunctionInterface::CreateDispatchToCallee(llvm::Value* codePointer, llvm::Value* coroutineCtx, llvm::Value* preFixupStackBase, llvm::Value* calleeCodeBlockHeapPtr, llvm::Value* numArgs, llvm::Value* isMustTail64, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = codePointer->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(codePointer));
    ReleaseAssert(llvm_value_has_type<void*>(coroutineCtx));
    ReleaseAssert(llvm_value_has_type<void*>(preFixupStackBase));
    ReleaseAssert(llvm_value_has_type<HeapPtr<void>>(calleeCodeBlockHeapPtr));
    ReleaseAssert(llvm_value_has_type<uint64_t>(numArgs));
    ReleaseAssert(llvm_value_has_type<uint64_t>(isMustTail64));
    ReleaseAssert(insertBefore != nullptr);
    ReleaseAssert(insertBefore->getParent() != nullptr);
    Function* func = insertBefore->getParent()->getParent();
    ReleaseAssert(func != nullptr);

    IntToPtrInst* numArgsAsPtr = new IntToPtrInst(numArgs, llvm_type_of<void*>(ctx), "", insertBefore);
    // We need this cast only because LLVM's rule of musttail which requires identical prototype..
    // Callee will cast it back to HeapPtr
    //
    Value* fakeCodeBlockPtr = new AddrSpaceCastInst(calleeCodeBlockHeapPtr, llvm_type_of<void*>(ctx), "", insertBefore);

    CallInst* callInst = CallInst::Create(
        GetType(ctx),
        codePointer,
        {
            /*R13*/ coroutineCtx,
            /*RBP*/ preFixupStackBase,
            /*R12*/ numArgsAsPtr,
            /*RBX*/ fakeCodeBlockPtr,
            /*R14*/ func->getArg(4),
            /*RSI*/ UndefValue::get(llvm_type_of<void*>(ctx)),
            /*RDI*/ isMustTail64,
            /*R8 */ UndefValue::get(llvm_type_of<uint64_t>(ctx)),
            /*R9 */ UndefValue::get(llvm_type_of<uint64_t>(ctx)),
            /*R15*/ func->getArg(9),
            /*XMM 1-6*/
            UndefValue::get(llvm_type_of<double>(ctx)),
            UndefValue::get(llvm_type_of<double>(ctx)),
            UndefValue::get(llvm_type_of<double>(ctx)),
            UndefValue::get(llvm_type_of<double>(ctx)),
            UndefValue::get(llvm_type_of<double>(ctx)),
            UndefValue::get(llvm_type_of<double>(ctx))
        },
        "" /*name*/,
        insertBefore);
    callInst->setTailCallKind(CallInst::TailCallKind::TCK_MustTail);
    callInst->setCallingConv(CallingConv::GHC);
    ReleaseAssert(llvm_value_has_type<void>(callInst));

    std::ignore = ReturnInst::Create(ctx, nullptr /*retVal*/, insertBefore);

    return callInst;
}

}   // namespace dast
