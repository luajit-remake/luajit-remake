#include "deegen_ast_simple_lowering_utils.h"
#include "deegen_interpreter_bytecode_impl_creator.h"

namespace dast {

struct LowerGetVMBasePointerApiPass final : public DeegenAbstractSimpleApiLoweringPass
{
    virtual bool WARN_UNUSED IsMagicCSymbol(const std::string& symbolName) override
    {
        return symbolName == "DeegenImpl_GetVMBasePointer";
    }

    virtual void DoLowering(DeegenBytecodeImplCreatorBase*, llvm::CallInst* origin) override
    {
        using namespace llvm;

        origin->replaceAllUsesWith(
            origin->getFunction()->getArg(7)
        );
        origin->eraseFromParent();
    }
};

DEEGEN_REGISTER_SIMPLE_API_LOWERING_PASS(LowerGetVMBasePointerApiPass);

}   // namespace dast
