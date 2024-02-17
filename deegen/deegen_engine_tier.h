#pragma once

#include "common_utils.h"

namespace dast {

enum class DeegenEngineTier
{
    Interpreter,
    BaselineJIT,
    DfgJIT
};

}   // namespace dast
