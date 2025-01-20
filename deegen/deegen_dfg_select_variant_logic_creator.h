#pragma once

#include "common_utils.h"
#include "deegen_bytecode_ir_components.h"
#include "deegen_bytecode_operand.h"
#include "tvalue_typecheck_optimization.h"

namespace dast {

// Generate the C++ logic that selects the DFG variant based on the prediction propagation result,
// and set the speculation decision for each input edge
//
void GenerateSelectDfgVariantAndSetSpeculationLogic(std::vector<BytecodeVariantDefinition*> dfgVariants, FILE* hdrFp, FILE* cppFp);

// Generate the automata that given prediction mask M, find the minimal speculation mask (an index into x_list_of_type_speculation_masks) that covers M
//
void GenerateGetSpeculationFromPredictionMaskAutomata(FILE* hdrFp, FILE* cppFp);

// Generate automatas to solve the following problem:
// Given speculation X and mask M, find the cheapest speculation Y such that X \subset Y \subset M
//
void GenerateFindCheapestSpecWithinMaskCoveringExistingSpecAutomatas(std::vector<TypecheckStrengthReductionCandidate> tcInfoList, FILE* hdrFp, FILE* cppFp);

}   // namespace dast
