#pragma once

#include "common_utils.h"
#include "misc_llvm_helper.h"
#include "deegen_simple_operand_expression.h"
#include "deegen_bytecode_operand.h"

namespace dast {

// Attempt to analyze all the read locations based on 'ptr'
// On failure, or if a read location cannot be expressed by a OperandExpr, return false
// 'argMapper' should map each Argument to BcOperand or nullptr
//
// TODO: currently if 'ptr' is passed in a call, we will always conservatively return false
//
bool WARN_UNUSED TryAnalyzeBytecodeReadLocations(llvm::Argument* ptr,
                                                 std::function<BcOperand*(llvm::Argument*)> argMapper,
                                                 std::vector<SimpleOperandExprNode*>& result /*out*/);

}   // namespace dast

