#pragma once

#include "common_utils.h"

namespace dfg {

// Config options for the speculative inliner
// TODO: tune these parameters
//
struct SpeculativeInlinerHeuristic
{
    // The root function is not allowed to inline anything if it contains more than this many bytecodes
    //
    static constexpr size_t x_disableAllInliningCutOff = 5000;

    // The root function is disallowed from inlining a function if doing so would make the total sum of
    // bytecodes of the functions that it had already *directly* inlined (i.e., functions further nestly
    // inlined by callees are excluded from the count) exceed this value.
    //
    static constexpr size_t x_inlineBudgetForRootFunction = 120;

    // Similar to above, but applies for each inlined function in direct call mode.
    //
    static constexpr size_t x_inlineBudgetForInlinedDirectCall = 90;

    // Similar to above, but applies for each inlined function in closure call mode.
    //
    static constexpr size_t x_inlineBudgetForInlinedClosureCall = 75;

    // The maximum depth of nested inlining (the root function is considered at depth 0)
    //
    static constexpr size_t x_maximumInliningDepth = 4;

    // A function can at most recursively inline itself this many times
    //
    static constexpr size_t x_maximumRecursiveInliningCount = 1;
};

}   // namespace dfg
