#include "deegen_ast_simple_lowering_utils.h"
#include "deegen_interpreter_bytecode_impl_creator.h"

namespace dast {

std::vector<std::unique_ptr<DeegenAbstractSimpleApiLoweringPass>>* g_deegenAllRegisteredSimpleApiLoweringPasses = nullptr;

void DeegenAllSimpleApiLoweringPasses::Register(std::unique_ptr<DeegenAbstractSimpleApiLoweringPass> pass)
{
    ReleaseAssert(pass.get() != nullptr);
    if (g_deegenAllRegisteredSimpleApiLoweringPasses == nullptr)
    {
        g_deegenAllRegisteredSimpleApiLoweringPasses = new std::vector<std::unique_ptr<DeegenAbstractSimpleApiLoweringPass>>();
    }
    g_deegenAllRegisteredSimpleApiLoweringPasses->push_back(std::move(pass));
}

DeegenAbstractSimpleApiLoweringPass* WARN_UNUSED DeegenAllSimpleApiLoweringPasses::GetHandlerMaybeNull(const std::string& symbolName)
{
    if (g_deegenAllRegisteredSimpleApiLoweringPasses == nullptr)
    {
        return nullptr;
    }

    DeegenAbstractSimpleApiLoweringPass* result = nullptr;
    bool isCXXSymbol = IsCXXSymbol(symbolName);
    std::string cxxSymbolName;
    if (isCXXSymbol) { cxxSymbolName = DemangleCXXSymbol(symbolName); }

    for (auto& it : *g_deegenAllRegisteredSimpleApiLoweringPasses)
    {
        bool shouldHandle = false;
        if (it->IsMagicCSymbol(symbolName))
        {
            shouldHandle = true;
        }
        else if (isCXXSymbol && it->IsMagicCXXSymbol(cxxSymbolName))
        {
            shouldHandle = true;
        }
        if (shouldHandle)
        {
            if (result != nullptr)
            {
                fprintf(stderr, "[ERROR] More than one SimpleApiLoweringPass claimed to handle symbol '%s'!\n", symbolName.c_str());
                abort();
            }
            result = it.get();
            ReleaseAssert(result != nullptr);
        }
    }
    return result;
}

void DeegenAllSimpleApiLoweringPasses::LowerAllForInterpreter(InterpreterBytecodeImplCreator* ifi, llvm::Function* func)
{
    using namespace llvm;
    std::vector<std::pair<DeegenAbstractSimpleApiLoweringPass*, CallInst*>> allUsesInFunction;
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
                    DeegenAbstractSimpleApiLoweringPass* handler = GetHandlerMaybeNull(calleeName);
                    if (handler != nullptr)
                    {
                        // Important to collect everything first and then run each of them, so we won't get into iterator invalidation problems
                        //
                        allUsesInFunction.push_back(std::make_pair(handler, callInst));
                    }
                }
            }
        }
    }

    for (auto& it : allUsesInFunction)
    {
        DeegenAbstractSimpleApiLoweringPass* handler = it.first;
        CallInst* callInst = it.second;
        handler->DoLoweringForInterpreter(ifi, callInst);
    }
}

}   // namespace dast
