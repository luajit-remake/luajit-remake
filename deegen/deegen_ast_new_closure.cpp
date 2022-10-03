#include "deegen_ast_simple_lowering_utils.h"
#include "deegen_interpreter_bytecode_impl_creator.h"

namespace dast {

struct LowerCreateNewClosureApiPass final : public DeegenAbstractSimpleApiLoweringPass
{
    virtual bool WARN_UNUSED IsMagicCSymbol(const std::string& symbolName) override
    {
        return symbolName == "DeegenImpl_CreateNewClosure";
    }

    virtual void DoLoweringForInterpreter(InterpreterBytecodeImplCreator* ifi, llvm::CallInst* origin) override
    {
        using namespace llvm;
        ReleaseAssert(origin->arg_size() == 1);
        Value* codeblockOfClosureToCreate = origin->getArgOperand(0);
        ReleaseAssert(llvm_value_has_type<void*>(codeblockOfClosureToCreate));

        CallInst* replacement = CreateCallToDeegenCommonSnippet(ifi->GetModule(), "CreateNewClosureFromCodeBlock", { codeblockOfClosureToCreate, ifi->GetCoroutineCtx(), ifi->GetStackBase() }, origin /*insertBefore*/);
        ReleaseAssert(origin->getType() == replacement->getType());
        origin->replaceAllUsesWith(replacement);
        origin->eraseFromParent();
    }
};

DEEGEN_REGISTER_SIMPLE_API_LOWERING_PASS(LowerCreateNewClosureApiPass);

}   // namespace dast
