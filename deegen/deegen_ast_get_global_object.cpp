#include "deegen_ast_simple_lowering_utils.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_baseline_jit_impl_creator.h"

namespace dast {

struct LowerGetGlobalObjectApiPass final : public DeegenAbstractSimpleApiLoweringPass
{
    virtual bool WARN_UNUSED IsMagicCSymbol(const std::string& symbolName) override
    {
        return symbolName == "DeegenImpl_GetFEnvGlobalObject";
    }

    virtual void DoLoweringForInterpreter(InterpreterBytecodeImplCreator* ifi, llvm::CallInst* origin) override
    {
        using namespace llvm;
        ReleaseAssert(origin->arg_size() == 0);
        CallInst* replacement = CreateCallToDeegenCommonSnippet(ifi->GetModule(), "GetGlobalObjectFromCodeBlock", { ifi->GetInterpreterCodeBlock() }, origin /*insertBefore*/);
        ReleaseAssert(origin->getType() == replacement->getType());
        origin->replaceAllUsesWith(replacement);
        origin->eraseFromParent();
    }

    virtual void DoLoweringForJIT(JitImplCreatorBase* ifi, llvm::CallInst* origin) override
    {
        using namespace llvm;
        ReleaseAssert(origin->arg_size() == 0);
        std::string dcsName;
        if (ifi->IsBaselineJIT())
        {
            dcsName = "GetGlobalObjectFromBaselineCodeBlock";
        }
        else
        {
            ReleaseAssert(ifi->IsDfgJIT());
            dcsName = "GetGlobalObjectFromDfgCodeBlock";
        }
        CallInst* replacement = CreateCallToDeegenCommonSnippet(ifi->GetModule(), dcsName, { ifi->GetJitCodeBlock() }, origin /*insertBefore*/);
        ReleaseAssert(origin->getType() == replacement->getType());
        origin->replaceAllUsesWith(replacement);
        origin->eraseFromParent();
    }
};

DEEGEN_REGISTER_SIMPLE_API_LOWERING_PASS(LowerGetGlobalObjectApiPass);

}   // namespace dast
