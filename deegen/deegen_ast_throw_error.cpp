#include "deegen_ast_throw_error.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_register_pinning_scheme.h"
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
        errorHandlerFn = RegisterPinningScheme::CreateFunction(module, errorHandlerFnName);
    }
    else
    {
        ReleaseAssert(errorHandlerFn->getFunctionType() == RegisterPinningScheme::GetFunctionType(ctx));
    }

    if (!errorHandlerFn->empty())
    {
        ReleaseAssert(!errorHandlerFn->hasFnAttribute(Attribute::AlwaysInline));
        errorHandlerFn->addFnAttr(Attribute::NoInline);
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
            InterpreterBytecodeImplCreator* i = ifi->AsInterpreter();
            i->CallDeegenCommonSnippet("UpdateInterpreterTierUpCounterForReturnOrThrow", { i->GetInterpreterCodeBlock(), i->GetCurBytecode() }, origin);
        }

        Function* dispatchTarget;
        Value* errorObject = origin->getArgOperand(0);
        Value* errorObjectAsPtr = nullptr;
        if (calleeName == "DeegenImpl_ThrowErrorTValue")
        {
            ReleaseAssert(llvm_value_has_type<uint64_t>(errorObject));
            errorObjectAsPtr = new IntToPtrInst(errorObject, llvm_type_of<void*>(ctx), "", origin /*insertBefore*/);
            dispatchTarget = GetThrowTValueErrorDispatchTargetFunction(ifi->GetModule());
        }
        else
        {
            ReleaseAssert(llvm_value_has_type<void*>(errorObject));
            errorObjectAsPtr = errorObject;
            dispatchTarget = GetThrowCStringErrorDispatchTargetFunction(ifi->GetModule());
        }

        ifi->GetExecFnContext()->PrepareDispatch<FunctionEntryInterface>()
            .Set<RPV_StackBase>(ifi->GetStackBase())
            .Set<RPV_NumArgsAsPtr>(errorObjectAsPtr)        // numArgs repurposed as errorObj
            .Set<RPV_InterpCodeBlockHeapPtrAsPtr>(UndefValue::get(llvm_type_of<void*>(ctx)))
            .Set<RPV_IsMustTailCall>(UndefValue::get(llvm_type_of<uint64_t>(ctx)))
            .Dispatch(dispatchTarget, origin /*insertBefore*/);

        AssertInstructionIsFollowedByUnreachable(origin);
        Instruction* unreachableInst = origin->getNextNode();
        origin->eraseFromParent();
        unreachableInst->eraseFromParent();
    }
};

DEEGEN_REGISTER_SIMPLE_API_LOWERING_PASS(LowerThrowErrorApiPass);

}   // namespace dast
