#pragma once

#include "common.h"
#include "misc_llvm_helper.h"
#include "cxx_symbol_demangler.h"
#include "deegen_api.h"

namespace dast {

class DeegenBytecodeImplCreatorBase;
class InterpreterBytecodeImplCreator;
class BaselineJitImplCreator;
class DfgJitImplCreator;
class JitImplCreatorBase;

// A simple abstraction for a common pattern of simple lowering passes: the pass defined some 'magic API'
// exposed to the bytecode, and the lowering simply replaces the API call to some concrete implementation.
//
struct DeegenAbstractSimpleApiLoweringPass
{
public:
    virtual ~DeegenAbstractSimpleApiLoweringPass() { }

    // If any of the two functions below returned true, the 'DoLoweringForXXX' method will be called
    //
    virtual bool WARN_UNUSED IsMagicCSymbol(const std::string& /*symbolName*/) { return false; }
    virtual bool WARN_UNUSED IsMagicCXXSymbol(const std::string& /*demangledSymbolName*/) { return false; }

    // The generic DoLowering that will be called if the tier-specific DoLoweringForXXX is not overridden.
    // User may override this for simplicity if the lowering logic is the same for any tier.
    //
    virtual void DoLowering(DeegenBytecodeImplCreatorBase* /*ifi*/, llvm::CallInst* /*origin*/)
    {
        ReleaseAssert(false);
    }

    // The tier-specific lowering action. If not overridden, the default action is to call the generic DoLowering()
    //
    virtual void DoLoweringForInterpreter(InterpreterBytecodeImplCreator* ifi, llvm::CallInst* origin);
    virtual void DoLoweringForBaselineJIT(BaselineJitImplCreator* ifi, llvm::CallInst* origin);
    virtual void DoLoweringForDfgJIT(DfgJitImplCreator* ifi, llvm::CallInst* origin);
    virtual void DoLoweringForJIT(JitImplCreatorBase* ifi, llvm::CallInst* origin);
};

class DeegenAllSimpleApiLoweringPasses
{
public:
    static std::vector<std::unique_ptr<DeegenAbstractSimpleApiLoweringPass>> WARN_UNUSED GetAllPasses();
    static void LowerAllForInterpreter(InterpreterBytecodeImplCreator* ifi, llvm::Function* func);
    static void LowerAllForBaselineJIT(BaselineJitImplCreator* ifi, llvm::Function* func);
    static void LowerAllForDfgJIT(DfgJitImplCreator* ifi, llvm::Function* func);
};

// Each pass should use 'DEEGEN_REGISTER_SIMPLE_API_LOWERING_PASS(name)' to register the pass, then put the pass class name into the following list
//
#define DEEGEN_ALL_SIMPLE_API_LOWERING_PASS_NAMES   \
    LowerThrowErrorApiPass                          \
  , LowerGetGlobalObjectApiPass                     \
  , LowerGuestLanguageFunctionReturnPass            \
  , LowerCreateNewClosureApiPass                    \
  , LowerUpvalueAccessorApiPass                     \
  , LowerVarArgsAccessorApiPass                     \
  , LowerVariadicResultsAccessorApiPass             \
  , LowerGetOutputSlotApiPass                       \

/* The helper macro to register the classes */
#define DEEGEN_CREATE_WRAPPER_NAME_FOR_SIMPLE_API_LOWERING_PASS(name) createDeegenSimpleLoweringPass_ ## name
#define DEEGEN_REGISTER_SIMPLE_API_LOWERING_PASS(name)                                                                                          \
    std::unique_ptr<DeegenAbstractSimpleApiLoweringPass> WARN_UNUSED DEEGEN_CREATE_WRAPPER_NAME_FOR_SIMPLE_API_LOWERING_PASS(name) ();          \
    std::unique_ptr<DeegenAbstractSimpleApiLoweringPass> WARN_UNUSED DEEGEN_CREATE_WRAPPER_NAME_FOR_SIMPLE_API_LOWERING_PASS(name) () {         \
        return std::make_unique<name>();                                                                                                        \
    }                                                                                                                                           \
    static_assert(std::is_base_of_v<DeegenAbstractSimpleApiLoweringPass, name>)

enum class DeegenSimpleApiLoweringPassName
{
    DEEGEN_ALL_SIMPLE_API_LOWERING_PASS_NAMES
};

using DeegenSimpleApiUseHandler = std::function<void(DeegenSimpleApiLoweringPassName /*passName*/,
                                                     DeegenAbstractSimpleApiLoweringPass* /*passHandler*/,
                                                     llvm::CallInst* /*useOrigin*/)>;

void ForEachUseOfDeegenSimpleApi(llvm::Function* func, const DeegenSimpleApiUseHandler& action);

}   // namespace dast
