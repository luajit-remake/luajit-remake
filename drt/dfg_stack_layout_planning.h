#pragma once

#include "common_utils.h"
#include "temp_arena_allocator.h"

namespace dfg {

struct Graph;

struct StackLayoutPlanningResult
{
    StackLayoutPlanningResult(TempArenaAllocator& resultAlloc)
        : m_constantTable(resultAlloc)
    { }

    // This value is the first slot that is available for use for temporaries
    //
    uint32_t m_numTotalPhysicalSlots;
    uint32_t m_m_inlineFrameOsrInfoDataBlockSize;
    uint32_t m_numTotalBoxedConstants;
    // Note that this constant table is in forward order, not the reversed order expected in DfgCodeBlock
    // The first m_numTotalBoxedConstants values are boxed constants, the rest are unboxed constants
    //
    TempVector<uint64_t> m_constantTable;
    // An array of length #InlinedCallFrame
    // Each value stores the offset into the data block below for the DfgInlinedCallFrameOsrInfo of that InlinedCallFrame
    //
    uint32_t* m_inlineFrameOsrInfoOffsets;
    uint8_t* m_inlineFrameOsrInfoDataBlock;
};

// This pass does the following:
// 1. Collect all needed constants and construct the constant table.
// 2. Construct the base OSR-exit map
// 3. Decide the physical stack slot for each GetLocal and SetLocal
//
StackLayoutPlanningResult WARN_UNUSED RunStackLayoutPlanningPass(TempArenaAllocator& resultAlloc, Graph* graph);

}   // namespace dfg
