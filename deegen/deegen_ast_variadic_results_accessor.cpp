#include "deegen_ast_simple_lowering_utils.h"
#include "deegen_interpreter_bytecode_impl_creator.h"

namespace dast {

struct LowerVariadicResultsAccessorApiPass final : public DeegenAbstractSimpleApiLoweringPass
{
    virtual bool WARN_UNUSED IsMagicCSymbol(const std::string& symbolName) override
    {
        return symbolName == x_getStartApi || symbolName == x_getNumApi;
    }

    virtual void DoLowering(DeegenBytecodeImplCreatorBase* ifi, llvm::CallInst* origin) override
    {
        using namespace llvm;
        std::string symbolName = origin->getCalledFunction()->getName().str();
        if (symbolName == x_getStartApi)
        {
            ReleaseAssert(origin->arg_size() == 0);
            ReleaseAssert(llvm_value_has_type<void*>(origin));
            CallInst* replacement = ifi->CallDeegenCommonSnippet("GetVariadicResultsStart", { ifi->GetStackBase(), ifi->GetCoroutineCtx() }, origin /*insertBefore*/);
            ReleaseAssert(origin->getType() == replacement->getType());
            origin->replaceAllUsesWith(replacement);
            origin->eraseFromParent();
        }
        else
        {
            ReleaseAssert(symbolName == x_getNumApi);
            ReleaseAssert(origin->arg_size() == 0);
            ReleaseAssert(llvm_value_has_type<uint64_t>(origin));
            CallInst* replacement = ifi->CallDeegenCommonSnippet("GetNumVariadicResults", { ifi->GetCoroutineCtx() }, origin /*insertBefore*/);
            ReleaseAssert(origin->getType() == replacement->getType());
            origin->replaceAllUsesWith(replacement);
            origin->eraseFromParent();
        }
    }

    static constexpr const char* x_getStartApi = "DeegenImpl_GetVariadicResultsStart";
    static constexpr const char* x_getNumApi = "DeegenImpl_GetNumVariadicResults";
};

DEEGEN_REGISTER_SIMPLE_API_LOWERING_PASS(LowerVariadicResultsAccessorApiPass);

}   // namespace dast
