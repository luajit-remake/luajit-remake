#pragma once

#include "common.h"
#include "dfg_node.h"

namespace dfg {

constexpr bool x_run_validation_after_each_pass_in_test_build = true;

struct IRValidateOptions
{
    IRValidateOptions()
        : allowUnreachableBlocks(false)
    { }

    IRValidateOptions WARN_UNUSED SetAllowUnreachableBlocks(bool value = true) const
    {
        IRValidateOptions ret = *this;
        ret.allowUnreachableBlocks = value;
        return ret;
    }

    bool allowUnreachableBlocks;
};

bool WARN_UNUSED ValidateDfgIrGraph(Graph* graph, IRValidateOptions validateOptions = IRValidateOptions());

}   // namespace dfg
