#pragma once

#include "common_utils.h"
#include "misc_llvm_helper.h"

namespace dast {

class CPRuntimeConstantNodeBase;

struct DeegenPlaceholderUtils
{
    // Currently the ordinals are as follows:
    //
    // 0 ~ 99    | placeholder for bytecode operands
    //           |
    // 100 ~ 199 | special placeholders
    //       100 | output slot
    //       101 | fallthrough target
    //       102 | conditional branch target
    //       103 | SlowPathData offset
    //       104 | CodeBlock32, the lower 32 bits of the JitCodeBlock pointer
    //       105 | condBrDecision slot (for DFG, branch is not made directly but outputted as a decision value)
    //           |
    // 200+      | placeholders for generic IC state
    //           |
    // 10000+    | various special values supplied directly to the codegen func, see deegen_stencil_reserved_placeholder_ords.h
    //

    // Create a new placeholder with ordinal 'ordinal' and type 'operandTy'.
    // Asserts that the placeholder with this ordinal does not exist before this call.
    //
    static llvm::CallInst* WARN_UNUSED CreateConstantPlaceholderForOperand(llvm::Module* module,
                                                                           size_t ordinal,
                                                                           llvm::Type* operandTy,
                                                                           llvm::Instruction* insertBefore);

    // Similar to above, except that it is for a placeholder that has been created (and it asserts that the type is equal)
    //
    static llvm::CallInst* WARN_UNUSED GetConstantPlaceholderForOperand(llvm::Module* module,
                                                                        size_t ordinal,
                                                                        llvm::Type* operandTy,
                                                                        llvm::Instruction* insertBefore);

    static bool WARN_UNUSED IsConstantPlaceholderAlreadyDefined(llvm::Module* module, size_t ordinal)
    {
        std::string placeholderName = DeegenPlaceholderUtils::GetRawRuntimeConstantPlaceholderName(ordinal);
        ReleaseAssertIff(module->getNamedValue(placeholderName) == nullptr, module->getFunction(placeholderName) == nullptr);
        return module->getNamedValue(placeholderName) != nullptr;
    }

    // Return -1 if not found
    //
    static size_t WARN_UNUSED FindFallthroughPlaceholderOrd(const std::vector<CPRuntimeConstantNodeBase*>& rcDef);

    // Return "" if not found
    //
    static std::string WARN_UNUSED FindFallthroughPlaceholderSymbolName(const std::vector<CPRuntimeConstantNodeBase*>& rcDef);

    static std::string WARN_UNUSED GetRawRuntimeConstantPlaceholderName(size_t ordinal);
};

}   // namespace dast
