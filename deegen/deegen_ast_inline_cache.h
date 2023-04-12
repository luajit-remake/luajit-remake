#pragma once

#include "common.h"
#include "misc_llvm_helper.h"
#include "cxx_symbol_demangler.h"
#include "api_inline_cache.h"
#include "deegen_bytecode_metadata.h"

namespace dast {

class AstInlineCache
{
public:
    struct Effect
    {
        struct SpecializationInfo
        {
            // The LLVM value in the body function that is being specialized
            //
            llvm::Value* m_valueInBodyFn;
            // Whether this specialization is full coverage
            //
            bool m_isFullCoverage;
            // The list of each specialization
            //
            std::vector<llvm::Constant*> m_specializations;
        };

        // The place in the IC body where this effect is called
        //
        llvm::CallInst* m_origin;

        // The IC pointer in the IC body
        //
        llvm::Value* m_icPtr;

        // The list of specializations
        //
        std::vector<SpecializationInfo> m_specializations;

        // The list of wrapped effect function that is callable from the main function
        //
        std::vector<llvm::Function*> m_effectFnMain;

        // The captures of 'm_effectFnMain' in the main function
        //
        std::vector<llvm::Value*> m_effectFnMainArgs;

        // The lambda and capture used to call the effect from the IC body
        //
        llvm::Function* m_effectFnBody;
        llvm::AllocaInst* m_effectFnBodyCapture;

        // The placeholder functions describing how to encode/decode the IC state
        //
        llvm::Function* m_icStateEncoder;
        llvm::Function* m_icStateDecoder;

        // The LLVM values (defined in the IC body) of the IC state
        //
        std::vector<llvm::Value*> m_icStateVals;

        // The effect functions in this effect should occupy ordinal [m_effectStartOrdinal, m_effectStartOrdinal + m_effectFnMain.size())
        //
        size_t m_effectStartOrdinal;

        size_t m_ordinalInEffectArray;
    };

    static bool WARN_UNUSED IsIcPtrGetterFunctionCall(llvm::CallInst* inst);

    static void PreprocessModule(llvm::Module* module);

    // Perform trivial lowering for all inline caches in 'func'
    //
    // For testing and debugging purpose only.
    //
    static void TriviallyLowerAllInlineCaches(llvm::Function* func);

    // Parse out all the inline caches defined in the given function
    // Note that unlike many GetAllUseInFunction() in other classes, this GetAllUseInFunction() is not idempotent.
    // That is, it can be only called once on each function.
    //
    static std::vector<AstInlineCache> WARN_UNUSED GetAllUseInFunction(llvm::Function* func);

    // This lowers everything except the 'MakeInlineCache()' call (the m_icPtrOrigin, which returns the IC pointer)
    // The main motivation is to separate the IC lowering logic and the interpreter-specific logic. Such design also
    // makes unit testing easier.
    //
    // This function also populates 'm_icStruct', the state definition of this IC.
    //
    void DoLoweringForInterpreter();

    // Perform trivial lowering: the execution semantics of this inline cache is preserved,
    // but no inling caching ever happens (i.e., execution simply unconditionally execute the IC body).
    //
    // For testing and debugging purpose only.
    //
    void DoTrivialLowering();

    // The CallInst that retrieves the IC pointer in the main function
    //
    llvm::CallInst* m_icPtrOrigin;
    // The CallInst that invokes the IC body in the main function
    //
    llvm::CallInst* m_origin;
    // Whether 'FuseICIntoInterpreterOpcode()' is specified
    //
    bool m_shouldFuseICIntoInterpreterOpcode;
    // The transformed body function (at this stage, the body lambda has been transformed into a normal function)
    //
    llvm::Function* m_bodyFn;
    // The arguments of the body function
    //
    std::vector<llvm::Value*> m_bodyFnArgs;
    // The arg ordinal of the body function that passes over the IC pointer
    //
    uint32_t m_bodyFnIcPtrArgOrd;
    // The arg ordinal of the body function that passes over the current IC key
    //
    uint32_t m_bodyFnIcKeyArgOrd;
    // The arg ordinal of the body function that passes over the bytecode ptr, only exists if m_shouldFuseICIntoInterpreterOpcode is true
    //
    uint32_t m_bodyFnBytecodePtrArgOrd;
    // The key and impossible value (if no impossible value is specified, the field is nullptr)
    //
    llvm::Value* m_icKey;
    llvm::ConstantInt* m_icKeyImpossibleValueMaybeNull;
    // The list of effect lambdas of this IC
    //
    std::vector<Effect> m_effects;
    size_t m_totalEffectKinds;
    // The list of other misc API calls used in this IC
    //
    std::vector<llvm::CallInst*> m_setUncacheableApiCalls;

    // This is populated by 'DoLoweringForInterpreter'
    // The IC state definition
    //
    std::unique_ptr<BytecodeMetadataStruct> m_icStruct;
};

constexpr const char* x_get_bytecode_ptr_placeholder_fn_name = "__DeegenImpl_GetInterpreterBytecodePtrPlaceholder";

class InterpreterBytecodeImplCreator;

void LowerInterpreterGetBytecodePtrInternalAPI(InterpreterBytecodeImplCreator* ifi, llvm::Function* func);

// Always takes i1 and returns i1
// Only used if 'FuseICIntoInterpreterOpcode' is true
//
constexpr const char* x_adapt_ic_hit_check_behavior_placeholder_fn = "__deegen_internal_adapt_ic_hit_check_behavior_fn";

// Always takes i8 and returns i8
// Only used if 'FuseICIntoInterpreterOpcode' is true
//
constexpr const char* x_adapt_get_ic_effect_ord_behavior_placeholder_fn = "__deegen_internal_adapt_get_ic_effect_ord_behavior_fn";

}   // namespace dast
