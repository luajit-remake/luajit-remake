#include "deegen_ast_simple_lowering_utils.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_dfg_jit_impl_creator.h"

namespace dast {

#define macro(e) std::unique_ptr<DeegenAbstractSimpleApiLoweringPass> WARN_UNUSED DEEGEN_CREATE_WRAPPER_NAME_FOR_SIMPLE_API_LOWERING_PASS(e) ();
PP_FOR_EACH(macro, DEEGEN_ALL_SIMPLE_API_LOWERING_PASS_NAMES)
#undef macro

std::vector<std::unique_ptr<DeegenAbstractSimpleApiLoweringPass>> WARN_UNUSED DeegenAllSimpleApiLoweringPasses::GetAllPasses()
{
    std::vector<std::unique_ptr<DeegenAbstractSimpleApiLoweringPass>> res;
#define macro(e) res.push_back(DEEGEN_CREATE_WRAPPER_NAME_FOR_SIMPLE_API_LOWERING_PASS(e)());
    PP_FOR_EACH(macro, DEEGEN_ALL_SIMPLE_API_LOWERING_PASS_NAMES)
#undef macro
    return res;
}

static DeegenAbstractSimpleApiLoweringPass* WARN_UNUSED GetPassHandlerMaybeNull(std::vector<std::unique_ptr<DeegenAbstractSimpleApiLoweringPass>>& passes, const std::string& symbolName)
{
    DeegenAbstractSimpleApiLoweringPass* result = nullptr;
    bool isCXXSymbol = IsCXXSymbol(symbolName);
    std::string cxxSymbolName;
    if (isCXXSymbol) { cxxSymbolName = DemangleCXXSymbol(symbolName); }

    for (auto& it : passes)
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

template<typename Functor>
static void ForEachUseOfDeegenSimpleApiImpl(std::vector<std::unique_ptr<DeegenAbstractSimpleApiLoweringPass>>& passes, llvm::Function* func, const Functor& action)
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
                    DeegenAbstractSimpleApiLoweringPass* handler = GetPassHandlerMaybeNull(passes, calleeName);
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
        action(handler, callInst);
    }
}

template<typename Functor>
static void ForEachUseOfDeegenSimpleApiImpl(llvm::Function* func, const Functor& action)
{
    std::vector<std::unique_ptr<DeegenAbstractSimpleApiLoweringPass>> passes = DeegenAllSimpleApiLoweringPasses::GetAllPasses();
    ForEachUseOfDeegenSimpleApiImpl(passes, func, action);
}

void DeegenAllSimpleApiLoweringPasses::LowerAllForInterpreter(InterpreterBytecodeImplCreator* ifi, llvm::Function* func)
{
    using namespace llvm;
    ForEachUseOfDeegenSimpleApiImpl(func, [&](DeegenAbstractSimpleApiLoweringPass* handler, CallInst* origin) {
        handler->DoLoweringForInterpreter(ifi, origin);
    });
}

void DeegenAllSimpleApiLoweringPasses::LowerAllForBaselineJIT(BaselineJitImplCreator* ifi, llvm::Function* func)
{
    using namespace llvm;
    ForEachUseOfDeegenSimpleApiImpl(func, [&](DeegenAbstractSimpleApiLoweringPass* handler, CallInst* origin) {
        handler->DoLoweringForBaselineJIT(ifi, origin);
    });
}

void DeegenAllSimpleApiLoweringPasses::LowerAllForDfgJIT(DfgJitImplCreator* ifi, llvm::Function* func)
{
    using namespace llvm;
    ForEachUseOfDeegenSimpleApiImpl(func, [&](DeegenAbstractSimpleApiLoweringPass* handler, CallInst* origin) {
        handler->DoLoweringForDfgJIT(ifi, origin);
    });
}

void ForEachUseOfDeegenSimpleApi(llvm::Function* func, const DeegenSimpleApiUseHandler& action)
{
    std::vector<std::unique_ptr<DeegenAbstractSimpleApiLoweringPass>> passes = DeegenAllSimpleApiLoweringPasses::GetAllPasses();

    // This relies on that GetAllPasses() pushes all the passes in the order they are listed. Sort of fragile, but should be fine..
    //
    std::unordered_map<DeegenAbstractSimpleApiLoweringPass*, DeegenSimpleApiLoweringPassName> nameMap;
#define macro(passName)                                                                                                             \
    ReleaseAssert(static_cast<size_t>(DeegenSimpleApiLoweringPassName::passName) < passes.size());                                  \
    nameMap[passes[static_cast<size_t>(DeegenSimpleApiLoweringPassName::passName)].get()] = DeegenSimpleApiLoweringPassName::passName;

    PP_FOR_EACH(macro, DEEGEN_ALL_SIMPLE_API_LOWERING_PASS_NAMES)
#undef macro

    ReleaseAssert(nameMap.size() == passes.size());
    ForEachUseOfDeegenSimpleApiImpl(passes, func, [&](DeegenAbstractSimpleApiLoweringPass* handler, llvm::CallInst* origin) {
        ReleaseAssert(nameMap.count(handler));
        action(nameMap[handler], handler, origin);
    });
}

void DeegenAbstractSimpleApiLoweringPass::DoLoweringForInterpreter(InterpreterBytecodeImplCreator* ifi, llvm::CallInst* origin)
{
    DoLowering(ifi, origin);
}

void DeegenAbstractSimpleApiLoweringPass::DoLoweringForBaselineJIT(BaselineJitImplCreator* ifi, llvm::CallInst* origin)
{
    DoLoweringForJIT(ifi, origin);
}

void DeegenAbstractSimpleApiLoweringPass::DoLoweringForDfgJIT(DfgJitImplCreator* ifi, llvm::CallInst* origin)
{
    DoLoweringForJIT(ifi, origin);
}

void DeegenAbstractSimpleApiLoweringPass::DoLoweringForJIT(JitImplCreatorBase* ifi, llvm::CallInst* origin)
{
    DoLowering(ifi, origin);
}

}   // namespace dast
