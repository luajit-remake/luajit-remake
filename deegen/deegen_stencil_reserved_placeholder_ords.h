#pragma once

#include "common.h"

namespace dast {

// Specially reserved placeholder ordinals. Must be >= 10000
//
enum : size_t
{
    // Used by Call IC direct-call mode, the TValue target used by the call
    //
    CP_PLACEHOLDER_CALL_IC_DIRECT_CALL_TVALUE = 10000,
    // Used by Call IC, the CodeBlock of the target function
    //
    CP_PLACEHOLDER_CALL_IC_CALLEE_CB32 = 10001,
    // Used by Call IC, the target function's entry point
    //
    CP_PLACEHOLDER_CALL_IC_CALLEE_CODE_PTR = 10002,
    // Used by IC, where to branch to when the IC misses
    //
    CP_PLACEHOLDER_IC_MISS_DEST = 10003,
    // The SlowPath address of the current stencil (if used by a stencil), or of the owning stencil (not bytecode!) if used by the IC
    // This placeholder is automatically desugared by the stencil parser frontend, so backend does not need to provide anything
    //
    CP_PLACEHOLDER_STENCIL_SLOW_PATH_ADDR = 10004,
    // The DataSection address of the current stencil (if used by a stencil), or of the owning stencil (not bytecode!) if used by the IC
    // This placeholder is automatically desugared by the stencil parser frontend, so backend does not need to provide anything
    //
    CP_PLACEHOLDER_STENCIL_DATA_SEC_ADDR = 10005,
    // Used by IC, the owning bytecode's conditional branch destination
    //
    CP_PLACEHOLDER_BYTECODE_CONDBR_DEST = 10006,
    // Used by Generic IC, the IC key
    //
    CP_PLACEHOLDER_GENERIC_IC_KEY = 10007
};

}   // namespace dast
