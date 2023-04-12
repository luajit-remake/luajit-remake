#include "deegen_ast_simple_lowering_utils.h"
#include "deegen_interpreter_bytecode_impl_creator.h"

namespace dast {

struct LowerUpvalueAccessorApiPass final : public DeegenAbstractSimpleApiLoweringPass
{
    virtual bool WARN_UNUSED IsMagicCXXSymbol(const std::string& symbolName) override
    {
        return symbolName.starts_with(x_getImmutableApi) || symbolName.starts_with(x_getMutableApi) || symbolName.starts_with(x_putApi) || symbolName.starts_with(x_closeApi);
    }

    virtual void DoLowering(DeegenBytecodeImplCreatorBase* ifi, llvm::CallInst* origin) override
    {
        using namespace llvm;
        std::string demangledName = DemangleCXXSymbol(origin->getCalledFunction()->getName().str());
        if (demangledName.starts_with(x_getImmutableApi) || demangledName.starts_with(x_getMutableApi))
        {
            ReleaseAssert(origin->arg_size() == 1);
            Value* ord = origin->getArgOperand(0);
            ReleaseAssert(llvm_value_has_type<uint64_t>(ord));
            ReleaseAssert(llvm_value_has_type<uint64_t>(origin));

            const char* implFnName = demangledName.starts_with(x_getImmutableApi) ? "GetImmutableUpvalueValue" : "GetMutableUpvalueValue";
            CallInst* replacement = ifi->CallDeegenCommonSnippet(implFnName, { ifi->GetStackBase(), ord }, origin /*insertBefore*/);
            ReleaseAssert(origin->getType() == replacement->getType());
            origin->replaceAllUsesWith(replacement);
            origin->eraseFromParent();
        }
        else if (demangledName.starts_with(x_putApi))
        {
            ReleaseAssert(origin->arg_size() == 2);
            Value* ord = origin->getArgOperand(0);
            ReleaseAssert(llvm_value_has_type<uint64_t>(ord));
            Value* valueToPut = origin->getArgOperand(1);
            ReleaseAssert(llvm_value_has_type<uint64_t>(valueToPut));
            ReleaseAssert(llvm_value_has_type<void>(origin));

            CallInst* replacement = ifi->CallDeegenCommonSnippet("PutUpvalue", { ifi->GetStackBase(), ord, valueToPut }, origin /*insertBefore*/);
            ReleaseAssert(llvm_value_has_type<void>(replacement));
            origin->eraseFromParent();
        }
        else
        {
            ReleaseAssert(demangledName.starts_with(x_closeApi));
            ReleaseAssert(origin->arg_size() == 1);
            Value* limit = origin->getArgOperand(0);
            ReleaseAssert(llvm_value_has_type<void*>(limit));
            ReleaseAssert(llvm_value_has_type<void>(origin));

            CallInst* replacement = ifi->CallDeegenCommonSnippet("CloseUpvalues", { limit, ifi->GetCoroutineCtx() }, origin /*insertBefore*/);
            ReleaseAssert(llvm_value_has_type<void>(replacement));
            origin->eraseFromParent();
        }
    }

    static constexpr const char* x_getImmutableApi = "DeegenImpl_UpvalueAccessor_GetImmutable(";
    static constexpr const char* x_getMutableApi = "DeegenImpl_UpvalueAccessor_GetMutable(";
    static constexpr const char* x_putApi = "DeegenImpl_UpvalueAccessor_Put(";
    static constexpr const char* x_closeApi = "DeegenImpl_UpvalueAccessor_Close(";
};

DEEGEN_REGISTER_SIMPLE_API_LOWERING_PASS(LowerUpvalueAccessorApiPass);

}   // namespace dast
