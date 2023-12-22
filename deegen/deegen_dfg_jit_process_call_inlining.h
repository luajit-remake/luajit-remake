#pragma once

#include "common.h"
#include "misc_llvm_helper.h"
#include "deegen_simple_operand_expression.h"
#include "deegen_bytecode_ir_components.h"

namespace dast {

// Determine if a bytecode that makes a guest language function call is eligible for speculative call inlining
//
// This currently works by converting all paths that does not lead to the target call to OSR exit,
// then determine if the remaining logic is not effectful (although idempotent side-effects are OK, we have
// no way of knowing if a side-effect is idempotent).
//
// If the residue is effectful, the bytecode is not eligible for call inlining, since there is no straightforward
// way to do OSR exit. This is probably solvable with a more complicated design + engineering work, but we don't
// have such use cases right now, so we just stay simple for now.
//
// We also opportunistically analyze the return continuation, so for trivial return continuation (that merely
// stores X return values to location Y) we can convert it to data flow
//
struct DeegenOptJitSpeculativeInliningInfo
{
    DeegenOptJitSpeculativeInliningInfo(BytecodeIrComponent* mainJitComponent, size_t callSiteOrdinal)
    {
        m_bytecodeDef = mainJitComponent->m_bytecodeDef;
        m_callSiteOrdinal = callSiteOrdinal;
        m_component = mainJitComponent;
        ReleaseAssert(m_callSiteOrdinal < m_bytecodeDef->GetNumCallICsInJitTier());
        m_success = false;
    }

    // Return false if this call site is not eligible for inlining
    //
    bool WARN_UNUSED TryGenerateInfo();

    // Returns the variable name of the trait object
    //
    std::string WARN_UNUSED GetCppTraitObjectName();

    void PrintCppFile(FILE* file);

    // If false, this call site is not eligible for speculative call inlining
    //
    bool m_success;
    BytecodeVariantDefinition* m_bytecodeDef;
    size_t m_callSiteOrdinal;
    BytecodeIrComponent* m_component;
    std::unique_ptr<llvm::Module> m_prologue;

    // Information about the ranged arguments, if it exists
    // If we cannot deduce the range base and length the call site will be not eligible for inlining
    //
    bool m_hasRangedArgs;
    SimpleOperandExprNode* m_rangeArgBase;
    SimpleOperandExprNode* m_numRangeArgs;

    bool m_isMustTailCall;
    bool m_isInPlaceCall;
    bool m_isCallPassingVarRes;

    // Number of singleton arguments passed to the call directly
    //
    size_t m_numSingletonArgs;
    // The first value in the ranged arg should show up as the 'm_rangedArgLocInArguments'-th arg (0-based) in the call
    //
    size_t m_rangedArgLocInArguments;

    // Whether we can figure out the read locations of the prologue logic from LLVM IR
    // We try to do this because the prologue usually reads less locations than the whole bytecode,
    // allowing us to get rid of unnecessary data flow edges
    //
    // If not, we will have to fallback to use the bytecode's read info
    //
    bool m_canDeterminePrologueReads;
    std::vector<SimpleOperandExprNode*> m_prologueReadInfo;
    std::vector<size_t> m_prologueReadDirectInputs;

    // Whether we can convert the return continuation logic to pure data flow
    //
    bool m_isRetContConvertibleToPureDataFlow;

    enum class TrivialRetContLogicKind
    {
        // Return the kth value in the results as the result of the bytecode
        //
        ReturnOne,
        // Store the first k return values into the stack frame
        //
        StoreRange,
        // Store all the return values as variadic results
        //
        StoreAsVarRes
    };

    TrivialRetContLogicKind m_trivialRetContLogicKind;
    // For "ReturnOne" logic, records the ordinal being returned
    //
    SimpleOperandExprNode* m_trivialRetContReturnOneOrd;
    // For "StoreRange" logic, records the frame offset (in bytes) and the # of return values to store
    //
    SimpleOperandExprNode* m_trivialRetContStoreRangeOffset;
    SimpleOperandExprNode* m_trivialRetContStoreRangeNum;
};

}   // namespace dast
