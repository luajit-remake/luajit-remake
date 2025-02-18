#pragma once

#include "common.h"
#include "dfg_node.h"

namespace dfg {

struct DumpIrOptions
{
    DumpIrOptions()
        : forValidationErrorDump(false)
        , highlightNode(nullptr)
        , highlightBB(nullptr)
    { }

    // This option should only be used by DFG IR validator
    //
    DumpIrOptions WARN_UNUSED ForValidationErrorDump(bool value = true) const
    {
        DumpIrOptions ret = *this;
        ret.forValidationErrorDump = value;
        return ret;
    }

    // Causes the dump line of that node to be prefixed with "[!]"
    //
    DumpIrOptions WARN_UNUSED HighlightNode(Node* node) const
    {
        DumpIrOptions ret = *this;
        ret.highlightNode = node;
        return ret;
    }

    // Causes the dump line of that basic block to be prefixed with "[!]"
    //
    DumpIrOptions WARN_UNUSED HighlightBasicBlock(BasicBlock* bb) const
    {
        DumpIrOptions ret = *this;
        ret.highlightBB = bb;
        return ret;
    }

    bool forValidationErrorDump;
    Node* highlightNode;
    BasicBlock* highlightBB;
};

void DumpTValueValue(FILE* file, TValue val);

// Produce a human-readable dump of the DFG IR graph, for testing and debug only
//
void DumpDfgIrGraph(FILE* file, Graph* graph, const DumpIrOptions& dumpIrOptions = DumpIrOptions());

}   // namespace dfg
