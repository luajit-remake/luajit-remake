#include "deegen_ast_simple_lowering_utils.h"
#include "deegen_interpreter_bytecode_impl_creator.h"

namespace dast {

struct LowerGetOutputSlotApiPass final : public DeegenAbstractSimpleApiLoweringPass
{
    virtual bool WARN_UNUSED IsMagicCSymbol(const std::string& symbolName) override
    {
        return symbolName == "DeegenImpl_GetOutputBytecodeSlotOrdinal";
    }

    virtual void DoLowering(DeegenBytecodeImplCreatorBase* ifi, llvm::CallInst* origin) override
    {
        using namespace llvm;
        ReleaseAssert(origin->arg_size() == 0);
        Value* outputSlot = ifi->GetOutputSlot();
        ReleaseAssert(llvm_value_has_type<size_t>(outputSlot) && llvm_value_has_type<size_t>(origin));
        origin->replaceAllUsesWith(outputSlot);
        origin->eraseFromParent();
    }
};

DEEGEN_REGISTER_SIMPLE_API_LOWERING_PASS(LowerGetOutputSlotApiPass);

}   // namespace dast
