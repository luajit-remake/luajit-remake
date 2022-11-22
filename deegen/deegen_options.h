#pragma once

#include "common_utils.h"

// This file should hold the global option knobs of Deegen
//

// If true, when a interpreter function dispatches to the next bytecode, it will additionally
// load the next bytecode's first 4 bytes of operands and pass it to the next bytecode via register,
// in the hope that:
// 1. The latency of the extra load would be shadowed by the indirect jump, so the load won't increase overall latency.
// 2. The next bytecode can make use of the preloaded value to speed up the decoding of its operands.
//
constexpr bool x_deegen_enable_interpreter_optimistic_preloading = false;
