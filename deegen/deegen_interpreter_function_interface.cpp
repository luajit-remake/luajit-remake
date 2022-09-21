#include "deegen_interpreter_function_interface.h"

namespace dast {

llvm::FunctionType* WARN_UNUSED InterpreterFunctionInterface::GetType(llvm::LLVMContext& ctx)
{
    using namespace llvm;
    // TODO: we should make it use GHC calling convension so we can pass more info
    // and allow less-overhead C runtime call.
    // Speficially, all 6 callee-saved registers (under default cc calling convention)
    // are available as parameters in GHC. We should use them as a register pinning
    // mechanism to pin the important states (coroutineCtx, stackBase, etc) into these registers
    // so that they are not clobbered by C calls.
    //
    return FunctionType::get(
        llvm_type_of<void>(ctx) /*result*/,
        {
            llvm_type_of<void*>(ctx) /*coroutineCtx*/,
            llvm_type_of<void*>(ctx) /*stackBase*/,
            /* Arg meaning if the function is:  bytecodeImpl    return cont     func entry        */
            llvm_type_of<void*>(ctx)         /* bytecode        RetStart        numArgs           */,
            llvm_type_of<void*>(ctx)         /* codeblock       numRets         codeblockHeapPtr  */,
            llvm_type_of<uint64_t>(ctx),     /* unused          unused          isMustTail        */
        } /*params*/,
        false /*isVarArg*/);
}

void InterpreterFunctionInterface::CreateDispatchToBytecode(llvm::Value* target, llvm::Value* coroutineCtx, llvm::Value* stackbase, llvm::Value* bytecodePtr, llvm::Value* codeBlock, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = target->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(target));
    ReleaseAssert(llvm_value_has_type<void*>(coroutineCtx));
    ReleaseAssert(llvm_value_has_type<void*>(stackbase));
    ReleaseAssert(llvm_value_has_type<void*>(bytecodePtr));
    ReleaseAssert(llvm_value_has_type<void*>(codeBlock));

    CallInst* callInst = CallInst::Create(
        GetType(ctx),
        target,
        {
            coroutineCtx, stackbase, bytecodePtr, codeBlock, UndefValue::get(llvm_type_of<uint64_t>(ctx))
        },
        "" /*name*/,
        insertBefore);
    callInst->setTailCallKind(CallInst::TailCallKind::TCK_MustTail);
    ReleaseAssert(llvm_value_has_type<void>(callInst));

    std::ignore = ReturnInst::Create(ctx, nullptr /*retVal*/, insertBefore);
}

void InterpreterFunctionInterface::CreateDispatchToReturnContinuation(llvm::Value* target, llvm::Value* coroutineCtx, llvm::Value* stackbase, llvm::Value* retStart, llvm::Value* numRets, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = target->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(target));
    ReleaseAssert(llvm_value_has_type<void*>(coroutineCtx));
    ReleaseAssert(llvm_value_has_type<void*>(stackbase));
    ReleaseAssert(llvm_value_has_type<void*>(retStart));
    ReleaseAssert(llvm_value_has_type<uint64_t>(numRets));

    IntToPtrInst* numRetAsPtr = new IntToPtrInst(numRets, llvm_type_of<void*>(ctx), "", insertBefore);
    CallInst* callInst = CallInst::Create(
        GetType(ctx),
        target,
        {
            coroutineCtx, stackbase, retStart, numRetAsPtr, UndefValue::get(llvm_type_of<uint64_t>(ctx))
        },
        "" /*name*/,
        insertBefore);
    callInst->setTailCallKind(CallInst::TailCallKind::TCK_MustTail);
    ReleaseAssert(llvm_value_has_type<void>(callInst));

    std::ignore = ReturnInst::Create(ctx, nullptr /*retVal*/, insertBefore);
}

void InterpreterFunctionInterface::CreateDispatchToCallee(llvm::Value* codePointer, llvm::Value* coroutineCtx, llvm::Value* preFixupStackBase, llvm::Value* calleeCodeBlockHeapPtr, llvm::Value* numArgs, llvm::Value* isMustTail, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = codePointer->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(codePointer));
    ReleaseAssert(llvm_value_has_type<void*>(coroutineCtx));
    ReleaseAssert(llvm_value_has_type<void*>(preFixupStackBase));
    ReleaseAssert(llvm_value_has_type<HeapPtr<void>>(calleeCodeBlockHeapPtr));
    ReleaseAssert(llvm_value_has_type<uint64_t>(numArgs));
    ReleaseAssert(llvm_value_has_type<bool>(isMustTail));

    IntToPtrInst* numArgsAsPtr = new IntToPtrInst(numArgs, llvm_type_of<void*>(ctx), "", insertBefore);
    ZExtInst* isMustTail64 = new ZExtInst(isMustTail, llvm_type_of<uint64_t>(ctx), "", insertBefore);
    // We need this cast only because LLVM's rule of musttail which requires identical prototype..
    // Callee will cast it back to HeapPtr
    //
    Value* fakeCodeBlockPtr = new AddrSpaceCastInst(calleeCodeBlockHeapPtr, llvm_type_of<void*>(ctx), "", insertBefore);

    CallInst* callInst = CallInst::Create(
        GetType(ctx),
        codePointer,
        {
            coroutineCtx, preFixupStackBase, numArgsAsPtr, fakeCodeBlockPtr, isMustTail64
        },
        "" /*name*/,
        insertBefore);
    callInst->setTailCallKind(CallInst::TailCallKind::TCK_MustTail);
    ReleaseAssert(llvm_value_has_type<void>(callInst));

    std::ignore = ReturnInst::Create(ctx, nullptr /*retVal*/, insertBefore);
}

}   // namespace dast
