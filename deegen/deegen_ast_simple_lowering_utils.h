#pragma once

#include "common.h"
#include "misc_llvm_helper.h"
#include "cxx_symbol_demangler.h"
#include "deegen_api.h"

namespace dast {

class InterpreterBytecodeImplCreator;

// A simple abstraction for a common pattern of simple lowering passes: the pass defined some 'magic API'
// exposed to the bytecode, and the lowering simply replaces the API call to some concrete implementation.
//
struct DeegenAbstractSimpleApiLoweringPass
{
public:
    virtual ~DeegenAbstractSimpleApiLoweringPass() { }

    // If any of the two functions below returned true, the 'DoLowering' method will be called
    //
    virtual bool WARN_UNUSED IsMagicCSymbol(const std::string& /*symbolName*/) { return false; }
    virtual bool WARN_UNUSED IsMagicCXXSymbol(const std::string& /*demangledSymbolName*/) { return false; }

    virtual void DoLoweringForInterpreter(InterpreterBytecodeImplCreator* ifi, llvm::CallInst* origin) = 0;

private:
    // Do not inherit this class directly: inherit DeegenSimpleApiLoweringPass instead
    //
    virtual void DoNotInheritDirectly_InheritDeegenSimpleApiLoweringPass() = 0;
};

// Intentionally declared as a plain pointer global variable (not an object, and no C++17 inline), so that it has constant initialization.
// All the registration helpers (the 's_registrationHelper' globals) have dynamic initialization, which happens after constant/zero initialization.
// This guarantees that this variable is initialized before all the registration helpers, so we won't get into weird initialization order issues.
//
extern std::vector<std::unique_ptr<DeegenAbstractSimpleApiLoweringPass>>* g_deegenAllRegisteredSimpleApiLoweringPasses;

class DeegenAllSimpleApiLoweringPasses
{
public:
    static void Register(std::unique_ptr<DeegenAbstractSimpleApiLoweringPass> pass);
    static DeegenAbstractSimpleApiLoweringPass* WARN_UNUSED GetHandlerMaybeNull(const std::string& symbolName);
    static void LowerAllForInterpreter(InterpreterBytecodeImplCreator* ifi, llvm::Function* func);
};

template<typename CRTP>
struct DeegenSimpleApiLoweringPass : public DeegenAbstractSimpleApiLoweringPass
{
private:
    virtual void DoNotInheritDirectly_InheritDeegenSimpleApiLoweringPass() override final { ReleaseAssert(false); }

    struct RegistrationHelper
    {
        RegistrationHelper()
        {
            static_assert(std::is_base_of_v<DeegenAbstractSimpleApiLoweringPass, CRTP>);
            static_assert(std::is_base_of_v<DeegenSimpleApiLoweringPass<CRTP>, CRTP>);
            DeegenAllSimpleApiLoweringPasses::Register(std::make_unique<CRTP>());
        }
    };

    static inline RegistrationHelper s_registrationHelper;

    // Force ODR-use of 's_registrationHelper' so that it is instantiated
    //
    template<auto> struct value_tag {};
    value_tag<&s_registrationHelper> force_odr_use() = delete;
};

}   // namespace dast
