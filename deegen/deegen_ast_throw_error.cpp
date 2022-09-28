#include "deegen_ast_throw_error.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_interpreter_function_interface.h"
#include "misc_llvm_helper.h"

namespace dast {

namespace {

llvm::Function* WARN_UNUSED GetThrowErrorDispatchTargetFunctionImpl(llvm::Module* module, const std::string& errorHandlerFnName)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    Function* errorHandlerFn = module->getFunction(errorHandlerFnName);
    if (errorHandlerFn == nullptr)
    {
        ReleaseAssert(module->getNamedValue(errorHandlerFnName) == nullptr);
        FunctionType* fty = InterpreterFunctionInterface::GetType(ctx);
        errorHandlerFn = Function::Create(fty, GlobalValue::ExternalLinkage, errorHandlerFnName, module);
        ReleaseAssert(errorHandlerFn->getName() == errorHandlerFnName);
        errorHandlerFn->addFnAttr(Attribute::AttrKind::NoUnwind);
    }
    else
    {
        ReleaseAssert(errorHandlerFn->getFunctionType() == InterpreterFunctionInterface::GetType(ctx));
    }

    if (!errorHandlerFn->empty())
    {
        ReleaseAssert(!errorHandlerFn->hasFnAttribute(Attribute::AttrKind::AlwaysInline));
        errorHandlerFn->addFnAttr(Attribute::AttrKind::NoInline);
    }
    return errorHandlerFn;
}

}   // anonymous namespace

llvm::Function* WARN_UNUSED GetThrowTValueErrorDispatchTargetFunction(llvm::Module* module)
{
    return GetThrowErrorDispatchTargetFunctionImpl(module, "DeegenInternal_UserLibFunctionTrueEntryPoint_DeegenInternal_ThrowTValueErrorImpl");
}

llvm::Function* WARN_UNUSED GetThrowCStringErrorDispatchTargetFunction(llvm::Module* module)
{
    return GetThrowErrorDispatchTargetFunctionImpl(module, "DeegenInternal_UserLibFunctionTrueEntryPoint_DeegenInternal_ThrowCStringErrorImpl");
}

void DeegenLowerThrowErrorAPIForInterpreter(InterpreterBytecodeImplCreator* ifi, llvm::Function* func)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();

    std::vector<CallInst*> allUsesInFunction;
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
                    if (calleeName == "DeegenImpl_ThrowErrorTValue" || calleeName == "DeegenImpl_ThrowErrorCString")
                    {
                        allUsesInFunction.push_back(callInst);
                    }
                }
            }
        }
    }

    if (allUsesInFunction.empty())
    {
        return;
    }

    for (CallInst* origin : allUsesInFunction)
    {
        ReleaseAssert(origin->arg_size() == 1);
        std::string calleeName = origin->getCalledFunction()->getName().str();
        ReleaseAssert(calleeName == "DeegenImpl_ThrowErrorTValue" || calleeName == "DeegenImpl_ThrowErrorCString");

        Function* dispatchTarget;
        Value* errorObject = origin->getArgOperand(0);
        if (calleeName == "DeegenImpl_ThrowErrorTValue")
        {
            ReleaseAssert(llvm_value_has_type<uint64_t>(errorObject));
            dispatchTarget = GetThrowTValueErrorDispatchTargetFunction(ifi->GetModule());
        }
        else
        {
            ReleaseAssert(llvm_value_has_type<void*>(errorObject));
            errorObject = new PtrToIntInst(errorObject, llvm_type_of<uint64_t>(ctx), "", origin /*insertBefore*/);
            dispatchTarget = GetThrowCStringErrorDispatchTargetFunction(ifi->GetModule());
        }

        InterpreterFunctionInterface::CreateDispatchToCallee(
            dispatchTarget,
            ifi->GetCoroutineCtx(),
            ifi->GetStackBase(),
            UndefValue::get(llvm_type_of<HeapPtr<void>>(ctx)),
            errorObject /*numArgs repurposed as errorObj*/,
            UndefValue::get(llvm_type_of<uint64_t>(ctx)),
            origin /*insertBefore*/);

        AssertInstructionIsFollowedByUnreachable(origin);
        Instruction* unreachableInst = origin->getNextNode();
        origin->eraseFromParent();
        unreachableInst->eraseFromParent();
    }
}

}   // namespace dast
