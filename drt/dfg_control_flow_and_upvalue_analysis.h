#pragma once

#include "common.h"
#include "dfg_node.h"
#include "bit_vector_utils.h"
#include "temp_arena_allocator.h"

class CodeBlock;

namespace dfg {

struct BasicBlockUpvalueInfo
{
    // The bytecode offset corresponding to the start of this basic block
    //
    uint32_t m_bytecodeOffset;
    // The bytecode index corresponding to the start of this basic block
    //
    uint32_t m_bytecodeIndex;
    // The bytecode offset corresponding to the terminal node of this basic block
    //
    uint32_t m_terminalNodeBcOffset;
    // The number of bytecodes in this basic block
    //
    uint32_t m_numBytecodesInBB;
    // Unique ordinal of the basic block
    //
    uint32_t m_ord;

    // If true, the terminal node of this BB is not a real terminator, one needs to manually add a jump later.
    //
    bool m_isTerminalInstImplicitTrivialBranch;

    // The successors of this BB
    //
    uint8_t m_numSuccessors;
    BasicBlockUpvalueInfo* m_successors[2];

    // Describes whether each local variable is captured at each basic block
    //
    // One needs to use GetClosureVar to access the local if capturedAtHead is true.
    // One needs to generate an intermediate BB for a control flow edge if capturedAtTail does not equal succ.capturedAtHead
    //
    // Specifically, m_isLocalCapturedAtHead[i] is true means that there exists another basic block B such that:
    // (1) B can reach this basic block
    // (2) Local i is captured in B
    // (3) No UpvalueClose covering local i exists on one path from B to this basic block
    //
    TempBitVector m_isLocalCapturedAtHead;
    TempBitVector m_isLocalCapturedAtTail;

    // Whether each local is captured by a CreateClosure inside the BB
    // (As long as a local is captured by a CreateClosure, the bit will be true, even if it is closed by the UpvalueClose at the end of the BB)
    //
    TempBitVector m_isLocalCapturedInBB;
};

struct DfgControlFlowAndUpvalueAnalysisResult
{
    // List of basic blocks in this function
    // [0] is always the function entry
    //
    TempVector<BasicBlockUpvalueInfo*> m_basicBlocks;
};

DfgControlFlowAndUpvalueAnalysisResult WARN_UNUSED RunControlFlowAndUpvalueAnalysis(TempArenaAllocator& alloc, CodeBlock* codeBlock);

}   // namespace dfg
