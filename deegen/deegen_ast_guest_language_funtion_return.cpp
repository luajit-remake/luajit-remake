#include "deegen_ast_simple_lowering_utils.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_interpreter_function_interface.h"

namespace dast {

struct LowerGuestLanguageFunctionReturnPass final : public DeegenAbstractSimpleApiLoweringPass
{
    virtual bool WARN_UNUSED IsMagicCSymbol(const std::string& symbolName) override
    {
        return symbolName == x_retNoneSymbol || symbolName == x_retSymbol || symbolName == x_retVarResSymbol;
    }

    virtual void DoLowering(DeegenBytecodeImplCreatorBase* ifi, llvm::CallInst* origin) override
    {
        using namespace llvm;
        std::string symbolName = origin->getCalledFunction()->getName().str();
        LLVMContext& ctx = ifi->GetModule()->getContext();

        if (ifi->IsInterpreter())
        {
            InterpreterBytecodeImplCreator* i = assert_cast<InterpreterBytecodeImplCreator*>(ifi);
            i->CallDeegenCommonSnippet("UpdateInterpreterTierUpCounterForReturnOrThrow", { i->GetInterpreterCodeBlock(), i->GetCurBytecode() }, origin);
        }

        Value* retStart = nullptr;
        Value* numRet = nullptr;
        if (symbolName == x_retNoneSymbol)
        {
            ReleaseAssert(origin->arg_size() == 0);
            retStart = ifi->GetStackBase();
            numRet = CreateLLVMConstantInt<uint64_t>(ctx, 0);
        }
        else if (symbolName == x_retSymbol)
        {
            ReleaseAssert(origin->arg_size() == 2);
            retStart = origin->getArgOperand(0);
            numRet = origin->getArgOperand(1);
        }
        else
        {
            ReleaseAssert(symbolName == x_retVarResSymbol);
            ReleaseAssert(origin->arg_size() == 2);
            retStart = origin->getArgOperand(0);
            Value* numFixedRet = origin->getArgOperand(1);
            numRet = ifi->CallDeegenCommonSnippet("AppendVariadicResultsToFunctionReturns", { ifi->GetStackBase(), retStart, numFixedRet, ifi->GetCoroutineCtx() }, origin);
        }

        ReleaseAssert(llvm_value_has_type<void*>(retStart));
        ReleaseAssert(llvm_value_has_type<uint64_t>(numRet));
        ifi->CallDeegenCommonSnippet("PopulateNilForReturnValues", { retStart, numRet }, origin);

        Value* retAddr = CreateCallToDeegenCommonSnippet(ifi->GetModule(), "GetRetAddrFromStackBase", { ifi->GetStackBase() }, origin);
        InterpreterFunctionInterface::CreateDispatchToReturnContinuation(retAddr, ifi->GetCoroutineCtx(), ifi->GetStackBase(), retStart, numRet, origin);

        AssertInstructionIsFollowedByUnreachable(origin);
        Instruction* unreachableInst = origin->getNextNode();
        origin->eraseFromParent();
        unreachableInst->eraseFromParent();
    }

    static constexpr const char* x_retNoneSymbol = "DeegenImpl_GuestLanguageFunctionReturn_NoValue";
    static constexpr const char* x_retSymbol = "DeegenImpl_GuestLanguageFunctionReturn";
    static constexpr const char* x_retVarResSymbol = "DeegenImpl_GuestLanguageFunctionReturnAppendingVariadicResults";
};

DEEGEN_REGISTER_SIMPLE_API_LOWERING_PASS(LowerGuestLanguageFunctionReturnPass);

}   // namespace dast
