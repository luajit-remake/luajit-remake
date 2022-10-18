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
        llvm::CallInst* m_origin;
        llvm::Function* m_lambda;
        llvm::AllocaInst* m_capture;
        std::vector<std::pair<size_t /*ordInCapture*/, llvm::Value* /*valueInOriginFn*/>> m_envCapture;
        std::vector<std::pair<size_t /*ordInCapture*/, llvm::Value* /*valueInMainLambda*/>> m_icCapture;
        size_t m_effectOrdinal;
    };

    struct EffectValue
    {
        llvm::CallInst* m_origin;
        llvm::Value* m_effectValue;
    };

    static void PreprocessModule(llvm::Module* module);
    static std::vector<AstInlineCache> WARN_UNUSED GetAllUseInFunction(llvm::Function* func);

    llvm::CallInst* m_origin;
    llvm::Function* m_mainLambda;
    llvm::AllocaInst* m_mainLambdaCapture;
    std::vector<llvm::Value* /*valueInOriginFn*/> m_mainLambdaCapturedValues;
    llvm::Value* m_icKey;
    llvm::Constant* m_icKeyImpossibleValueMaybeNull;
    std::vector<Effect> m_effects;
    std::vector<EffectValue> m_effectValues;
};

}   // namespace dast
