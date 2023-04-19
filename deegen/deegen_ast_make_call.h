#pragma once

#include "common.h"
#include "misc_llvm_helper.h"
#include "cxx_symbol_demangler.h"
#include "deegen_api.h"

namespace dast {

class DeegenBytecodeImplCreatorBase;
class InterpreterBytecodeImplCreator;
class BaselineJitImplCreator;

inline bool x_deegen_unit_test = false;

class AstMakeCall
{
public:
    class Arg
    {
    public:
        Arg(llvm::Value* arg)
            : m_isArgRange(false)
            , m_arg(arg)
            , m_start(nullptr)
            , m_len(nullptr)
        {
            ReleaseAssert(llvm_value_has_type<uint64_t>(arg));
        }

        Arg(llvm::Value* argStart, llvm::Value* argLen)
            : m_isArgRange(true)
            , m_arg(nullptr)
            , m_start(argStart)
            , m_len(argLen)
        {
            ReleaseAssert(llvm_value_has_type<void*>(argStart));
            ReleaseAssert(llvm_value_has_type<uint64_t>(argLen));
        }

        bool IsArgRange() const { return m_isArgRange; }
        llvm::Value* GetArg() const { ReleaseAssert(!IsArgRange()); return m_arg; }
        llvm::Value* GetArgStart() const { ReleaseAssert(IsArgRange()); return m_start; }
        llvm::Value* GetArgNum() const { ReleaseAssert(IsArgRange()); return m_len; }

    private:
        bool m_isArgRange;
        llvm::Value* m_arg;
        llvm::Value* m_start;
        llvm::Value* m_len;
    };

    // Whether this is a in-place call (see comments on MakeInPlaceCall)
    // When this is true, the args are guaranteed to contain only a TValue range
    //
    bool m_isInPlaceCall;
    // Whether variadic results of the previous bytecode is passed as additional arguments to the call
    //
    bool m_passVariadicRes;
    // Whether this is required to be a tail call
    //
    bool m_isMustTailCall;

    MakeCallOption m_callOption;

    // The origin of this API call in the source IR
    //
    llvm::CallInst* m_origin;

    llvm::Value* m_target;
    std::vector<Arg> m_args;
    // This is the continuation implementation function, not the wrapped function. To get the wrapped function, use 'GetContinuationDispatchTarget()'
    //
    llvm::Function* m_continuation;

    // Preprocess the module to pre-parse all use of the MakeCall family APIs
    // After this function call, one can use GetAllUseInFunction to iterate through each use of MakeCall family API.
    // The desugaring level of the module should be at least PerFunctionSimplifyOnly
    //
    static void PreprocessModule(llvm::Module* module);

    // Get a vector of each use of MakeCall in the function
    //
    static std::vector<AstMakeCall> WARN_UNUSED GetAllUseInFunction(llvm::Function* func);

    // Emit generic logic that set up the new call frame
    // 'callSiteInfo' should be nullptr for tail call, or the tier-specific CallSiteInfo (a uint32_t value, e.g., curBytecode for interpreter, etc) for normal calls
    // The logic is emitted before 'm_origin'.
    // The returned 'newSfBase' has type void*, and 'totalNumArgs' has type uint64_t
    //
    std::pair<llvm::Value* /*newSfBase*/, llvm::Value* /*totalNumArgs*/> WARN_UNUSED EmitGenericNewCallFrameSetupLogic(DeegenBytecodeImplCreatorBase* ifi, llvm::Value* callSiteInfo);

    // Lower the call to concrete interpreter logic
    //
    void DoLoweringForInterpreter(InterpreterBytecodeImplCreator* ifi);

    static void LowerForInterpreter(InterpreterBytecodeImplCreator* ifi, llvm::Function* func)
    {
        std::vector<AstMakeCall> res = GetAllUseInFunction(func);
        for (AstMakeCall& item : res)
        {
            item.DoLoweringForInterpreter(ifi);
        }

        ValidateLLVMFunction(func);
    }

    void DoLoweringForBaselineJIT(BaselineJitImplCreator* ifi);

    static void LowerForBaselineJIT(BaselineJitImplCreator* ifi, llvm::Function* func)
    {
        std::vector<AstMakeCall> res = GetAllUseInFunction(func);
        for (AstMakeCall& item : res)
        {
            item.DoLoweringForBaselineJIT(ifi);
        }

        ValidateLLVMFunction(func);
    }

private:
    static llvm::Function* WARN_UNUSED CreatePlaceholderFunction(llvm::Module* module, const std::vector<bool /*isArgRange*/>& argDesc);

    llvm::Function* WARN_UNUSED GetContinuationDispatchTarget();

    static AstMakeCall WARN_UNUSED ParseFromApiUse(llvm::CallInst* callInst);

    static constexpr const char* x_placeholderPrefix = "__DeegenInternal_AstMakeCallIdentificationFunc_";

    static constexpr uint32_t x_ord_inplaceCall = 0;
    static constexpr uint32_t x_ord_passVariadicRes = 1;
    static constexpr uint32_t x_ord_isMustTailCall = 2;
    static constexpr uint32_t x_ord_target = 3;
    static constexpr uint32_t x_ord_continuation = 4;
    static constexpr uint32_t x_ord_callOption = 5;
    static constexpr uint32_t x_ord_arg_start = 6;
};

}   // namespace dast
