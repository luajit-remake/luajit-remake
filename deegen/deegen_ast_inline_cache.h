#pragma once

#include "common.h"
#include "misc_llvm_helper.h"
#include "cxx_symbol_demangler.h"
#include "api_inline_cache.h"

namespace dast {

struct AstInlineCache
{
    struct Effect
    {
        // The place in the IC body where this effect is called
        //
        llvm::CallInst* m_origin;

        // The wrapped effect function that is callable from the main function
        //
        llvm::Function* m_effectFnMain;

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

        // The effect lambda's struct element ordinal of each IC state value
        // This vector must have same length as m_icStateVals
        //
        std::vector<uint32_t> m_icStateOrdInEffectCapture;
        llvm::CallInst* m_decoderCall;

        // Each 'Effect' in the IC cache has an unique ordinal
        //
        size_t m_effectOrdinal;
    };

    struct EffectValue
    {
        llvm::CallInst* m_origin;
        llvm::Value* m_effectValue;
    };

    static void PreprocessModule(llvm::Module* module);
    static std::vector<AstInlineCache> WARN_UNUSED GetAllUseInFunction(llvm::Function* func);

    // The CallInst that retrieves the IC pointer in the main function
    //
    llvm::CallInst* m_icPtrOrigin;
    // The CallInst that invokes the IC body in the main function
    //
    llvm::CallInst* m_origin;
    // The transformed body function (at this stage, the body lambda has been transformed into a normal function)
    //
    llvm::Function* m_bodyFn;
    // The arguments of the body function
    //
    std::vector<llvm::Value*> m_bodyFnArgs;
    // The arg ordinal of the body function that passes over the IC pointer
    //
    uint32_t m_bodyFnIcPtrArgOrd;
    // The key and impossible value (if no impossible value is specified, the field is nullptr)
    //
    llvm::Value* m_icKey;
    llvm::Constant* m_icKeyImpossibleValueMaybeNull;
    // The list of effect lambdas of this IC
    //
    std::vector<Effect> m_effects;
    // The list of other misc API calls used in this IC
    //
    std::vector<EffectValue> m_effectValues;
    std::vector<llvm::CallInst*> m_setUncacheableApiCalls;
};

}   // namespace dast
