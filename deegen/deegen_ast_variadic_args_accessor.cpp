#include "deegen_ast_simple_lowering_utils.h"
#include "deegen_interpreter_bytecode_impl_creator.h"

namespace dast {

struct LowerVarArgsAccessorApiPass final : public DeegenAbstractSimpleApiLoweringPass
{
    virtual bool WARN_UNUSED IsMagicCSymbol(const std::string& symbolName) override
    {
        return symbolName == x_getStartApi || symbolName == x_getNumApi || symbolName == x_storeAsVarResApi;
    }

    virtual void DoLowering(DeegenBytecodeImplCreatorBase* ifi, llvm::CallInst* origin) override
    {
        using namespace llvm;
        std::string symbolName = origin->getCalledFunction()->getName().str();
        if (symbolName == x_getStartApi)
        {
            ReleaseAssert(origin->arg_size() == 0);
            ReleaseAssert(llvm_value_has_type<void*>(origin));
            CallInst* replacement = ifi->CallDeegenCommonSnippet("GetVariadicArgStart", { ifi->GetStackBase() }, origin /*insertBefore*/);
            ReleaseAssert(origin->getType() == replacement->getType());
            origin->replaceAllUsesWith(replacement);
            origin->eraseFromParent();
        }
        else if (symbolName == x_getNumApi)
        {
            ReleaseAssert(origin->arg_size() == 0);
            ReleaseAssert(llvm_value_has_type<uint64_t>(origin));
            CallInst* replacement = ifi->CallDeegenCommonSnippet("GetNumVariadicArgs", { ifi->GetStackBase() }, origin /*insertBefore*/);
            ReleaseAssert(origin->getType() == replacement->getType());
            origin->replaceAllUsesWith(replacement);
            origin->eraseFromParent();
        }
        else
        {
            ReleaseAssert(symbolName == x_storeAsVarResApi);
            ReleaseAssert(origin->arg_size() == 0);
            ReleaseAssert(llvm_value_has_type<void>(origin));
            CallInst* replacement = ifi->CallDeegenCommonSnippet("StoreVariadicArgsAsVariadicResults", { ifi->GetStackBase(), ifi->GetCoroutineCtx() }, origin /*insertBefore*/);
            ReleaseAssert(llvm_value_has_type<void>(replacement));
            origin->eraseFromParent();
        }
    }

    static constexpr const char* x_getStartApi = "DeegenImpl_GetVarArgsStart";
    static constexpr const char* x_getNumApi = "DeegenImpl_GetNumVarArgs";
    static constexpr const char* x_storeAsVarResApi = "DeegenImpl_StoreVarArgsAsVariadicResults";
};

DEEGEN_REGISTER_SIMPLE_API_LOWERING_PASS(LowerVarArgsAccessorApiPass);

}   // namespace dast
