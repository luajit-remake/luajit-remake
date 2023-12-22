#include "deegen_ast_throw_error.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_interpreter_function_interface.h"
#include "misc_llvm_helper.h"
#include "deegen_ast_simple_lowering_utils.h"

namespace dast {

namespace {

llvm::Function* WARN_UNUSED GetThrowErrorDispatchTargetFunctionImpl(llvm::Module* module, const std::string& errorHandlerFnName)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    Function* errorHandlerFn = module->getFunction(errorHandlerFnName);
    if (errorHandlerFn == nullptr)
    {
        errorHandlerFn = InterpreterFunctionInterface::CreateFunction(module, errorHandlerFnName);
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

struct LowerThrowErrorApiPass final : public DeegenAbstractSimpleApiLoweringPass
{
    virtual bool WARN_UNUSED IsMagicCSymbol(const std::string& symbolName) override
    {
        return symbolName == "DeegenImpl_ThrowErrorTValue" || symbolName == "DeegenImpl_ThrowErrorCString";
    }

    virtual void DoLowering(DeegenBytecodeImplCreatorBase* ifi, llvm::CallInst* origin) override
    {
        using namespace llvm;
        ReleaseAssert(origin->arg_size() == 1);
        std::string calleeName = origin->getCalledFunction()->getName().str();
        ReleaseAssert(calleeName == "DeegenImpl_ThrowErrorTValue" || calleeName == "DeegenImpl_ThrowErrorCString");

        LLVMContext& ctx = ifi->GetModule()->getContext();

        if (ifi->IsInterpreter())
        {
            InterpreterBytecodeImplCreator* i = assert_cast<InterpreterBytecodeImplCreator*>(ifi);
            i->CallDeegenCommonSnippet("UpdateInterpreterTierUpCounterForReturnOrThrow", { i->GetInterpreterCodeBlock(), i->GetCurBytecode() }, origin);
        }

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
};

DEEGEN_REGISTER_SIMPLE_API_LOWERING_PASS(LowerThrowErrorApiPass);

}   // namespace dast
