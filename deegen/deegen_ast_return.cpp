#include "deegen_ast_return.h"
#include "deegen_interpreter_interface.h"

namespace dast {

std::vector<AstBytecodeReturn> WARN_UNUSED AstBytecodeReturn::GetAllUseInFunction(llvm::Function* func)
{
    using namespace llvm;
    std::vector<AstBytecodeReturn> result;
    for (BasicBlock& bb : *func)
    {
        for (Instruction& inst : bb)
        {
            CallInst* callInst = dyn_cast<CallInst>(&inst);
            if (callInst != nullptr)
            {
                Function* callee = callInst->getCalledFunction();
                if (callee != nullptr)
                {
                    std::string calleeName = callee->getName().str();
                    if (calleeName == "DeegenImpl_ReturnValue" ||
                        calleeName == "DeegenImpl_ReturnNone" ||
                        calleeName == "DeegenImpl_ReturnValueAndBranch" ||
                        calleeName == "DeegenImpl_ReturnNoneAndBranch")
                    {
                        AstBytecodeReturn item;
                        item.m_origin = callInst;
                        if (calleeName == "DeegenImpl_ReturnValue" || calleeName == "DeegenImpl_ReturnValueAndBranch")
                        {
                            ReleaseAssert(callInst->arg_size() == 1);
                            item.m_valueOperand = callInst->getArgOperand(0);
                        }
                        else
                        {
                            item.m_valueOperand = nullptr;
                        }
                        item.m_doesBranch = (calleeName == "DeegenImpl_ReturnValueAndBranch" || calleeName == "DeegenImpl_ReturnNoneAndBranch");
                        result.push_back(item);
                    }
                }
            }
        }
    }
    return result;
}

void AstBytecodeReturn::DoLoweringForInterpreter(InterpreterFunctionInterface* ifi)
{
    // TODO: implement
    ReleaseAssert(!m_doesBranch);


}

}   // namespace dast
