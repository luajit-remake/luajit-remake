#pragma once

#include "misc_llvm_helper.h"
#include "deegen_bytecode_impl_creator_base.h"

// Utility functions for type-based hot-cold-splitting
//
namespace dast {

struct TypeBasedHCSHelper
{
    // Emit the type-check logic and the dispatch-to-slow-path logic at end of 'bb'.
    // After this function call, at the end of 'bb', all checks are known to have passed.
    //
    static void GenerateCheckConditionLogic(DeegenBytecodeImplCreatorBase* ifi,
                                            std::vector<llvm::Value*> bytecodeOperandUsageValueList,
                                            llvm::BasicBlock*& bb /*inout*/);

    // Get the arguments to be passed to the slow path
    //
    static std::unordered_map<uint64_t /*operandOrd*/, uint64_t /*argOrd*/> GetQuickeningSlowPathAdditionalArgs(BytecodeVariantDefinition* bytecodeDef);

    static llvm::Value* WARN_UNUSED GetBytecodeOperandUsageValueFromAlreadyDecodedArgs(llvm::Function* interfaceFn, uint64_t argOrd, llvm::BasicBlock* bb);
};

}   // namespace dast
