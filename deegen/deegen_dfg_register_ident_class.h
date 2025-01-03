#pragma once
#include "common_utils.h"

// This header file is included by the runtime as well, so it must not include other Deegen builder header files
//

namespace dast {

enum class StencilRegIdentClass : uint8_t
{
    // Sc = scratch, Pt = passthru
    // G = GPR, F = FPR
    // NonExt = first 8 registers (rax, rbx, etc), Ext = latter 8 registers (r8, r9, etc)
    //
    // There may be places that hardcode assumptions about this class, so be very careful if you want to change it...
    //
    Operand,
    ScNonExtG,
    ScExtG,
    ScF,
    PtNonExtG,
    PtExtG,
    PtF,
    X_END_OF_ENUM
};

}   // namespace dast
