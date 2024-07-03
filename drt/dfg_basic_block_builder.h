#pragma once

#include "common.h"
#include "dfg_node.h"
#include "dfg_control_flow_and_upvalue_analysis.h"
#include "dfg_speculative_inliner.h"
#include "bytecode_builder.h"

namespace dfg {

struct DfgTranslateFunctionContext;

struct DfgBuildBasicBlockContext
{
    MAKE_NONCOPYABLE(DfgBuildBasicBlockContext);
    MAKE_NONMOVABLE(DfgBuildBasicBlockContext);

    DfgBuildBasicBlockContext(DfgTranslateFunctionContext& tfCtx, DfgControlFlowAndUpvalueAnalysisResult& info);

    TempArenaAllocator& m_alloc;

    InlinedCallFrame* m_inlinedCallFrame;
    CodeBlock* m_codeBlock;
    Graph* m_graph;
    DeegenBytecodeBuilder::BytecodeDecoder m_decoder;

    // The number of primitive basic blocks, obtained from ControlFlowAndUpvalue analysis
    // Note that the generated IR graph may contain more basic blocks as we may need to insert
    // intermediate basic blocks to properly handle captured locals
    //
    size_t m_numPrimBasicBlocks;

    // An array of length m_numPrimBasicBlocks
    // The information obtained from ControlFlowAndUpvalue analysis
    //
    BasicBlockUpvalueInfo** m_primBBInfo;

    // An array of length m_numPrimBasicBlocks
    // The resulted basic blocks in the same order as m_primBBInfo
    //
    // nullptr means that the basic block has not been built yet
    //
    BasicBlock** m_primBBs;

    // All the basic blocks (primitive BBs + intermediate BBs for handling captured locals)
    //
    TempVector<BasicBlock*> m_allBBs;

    TempVector<std::pair<ArenaPtr<ArenaPtr<BasicBlock>> /*loc*/, uint32_t /*bbOrdToPatch*/>> m_branchDestPatchList;

    BasicBlock* m_functionEntry;

    DfgTranslateFunctionContext& m_tfCtx;

    VirtualRegisterAllocator& m_vrState;

    SpeculativeInliner m_speculativeInliner;

    // Scratchpad data below
    //
    // Whether the local is currently being captured
    //
    bool* m_isLocalCaptured;

    // The current basic block
    //
    BasicBlock* m_currentBlock;
    BasicBlockUpvalueInfo* m_currentBBInfo;

    // The current value stored in each local ordinal
    //
    Value* m_valueAtTail;

    // Cache the GetLocal for accessing variadic args
    //
    Value* m_valueForVariadicArgs;

    // Sometimes we need to read two special locals: functionObject and numVarArgs
    // In this case, these two records are responsible for avoiding emitting redundant GetLocals in the same BB
    //
    Node* m_cachedGetFunctionObjectNode;
    Node* m_cachedGetNumVarArgsNode;

    // The most recent node that produces VariadicResults
    //
    Node* m_currentVariadicResult;

    // If false, one cannot use 'GetLocalVariableValue' and 'SetLocalVariableValue', and is reponsible for
    // avoiding redundant GetLocals in the same BB by themselves.
    //
    bool m_hasLocalCaptureInfo;

    // Records whether an OSR exit is allowed right now
    //
    bool m_isOSRExitOK;

    // Records the current code origin
    //
    CodeOrigin m_currentCodeOrigin;

    // Records the current code origin for OSR exit
    //
    OsrExitDestination m_currentOriginForExit;

    // Returns the index of the main node in m_nodes, or -1 if the node ends up not being inserted (due to bytecode intrinsic)
    //
    size_t ParseAndProcessBytecode(size_t curBytecodeOffset, size_t curBytecodeIndex, bool forReturnContinuation);

    void BuildDfgBasicBlockFromBytecode(size_t bbOrd);

    void ALWAYS_INLINE SetupNodeCommonInfo(Node* node)
    {
        node->SetExitOK(m_isOSRExitOK);
        node->SetNodeOrigin(m_currentCodeOrigin);
        node->SetOsrExitDest(m_currentOriginForExit);
    }

    void ALWAYS_INLINE SetupNodeCommonInfoAndPushBack(Node* node)
    {
        SetupNodeCommonInfo(node);
        m_currentBlock->m_nodes.push_back(node);
    }

    VirtualRegister GetRegisterForLocalOrd(size_t localOrd)
    {
        return m_inlinedCallFrame->GetRegisterForLocalOrd(localOrd);
    }

    InterpreterSlot GetInterpreterSlotForLocalOrd(size_t localOrd)
    {
        return m_inlinedCallFrame->GetInterpreterSlotForLocalOrd(localOrd);
    }

    Value GetCurrentFunctionObject()
    {
        if (m_inlinedCallFrame->IsRootFrame())
        {
            return m_graph->GetRootFunctionObject();
        }
        else if (m_inlinedCallFrame->IsDirectCall())
        {
            FunctionObject* fo = m_inlinedCallFrame->GetDirectCallFunctionObject();
            return m_graph->GetUnboxedConstant(static_cast<uint64_t>(VM_PointerToOffset(fo)));
        }
        else
        {
            if (m_cachedGetFunctionObjectNode == nullptr)
            {
                m_cachedGetFunctionObjectNode = Node::CreateGetLocalNode(m_inlinedCallFrame, InterpreterFrameLocation::FunctionObjectLoc());
                SetupNodeCommonInfoAndPushBack(m_cachedGetFunctionObjectNode);
            }
            return Value(m_cachedGetFunctionObjectNode, 0 /*outputOrd*/);
        }
    }

    Value GetNumVariadicArguments()
    {
        if (m_inlinedCallFrame->IsRootFrame())
        {
            return m_graph->GetRootFunctionNumVarArgs();
        }
        else if (m_inlinedCallFrame->StaticallyKnowsNumVarArgs())
        {
            return m_graph->GetUnboxedConstant(m_inlinedCallFrame->GetNumVarArgs());
        }
        else
        {
            if (m_cachedGetNumVarArgsNode == nullptr)
            {
                m_cachedGetNumVarArgsNode = Node::CreateGetLocalNode(m_inlinedCallFrame, InterpreterFrameLocation::NumVarArgsLoc());
                SetupNodeCommonInfoAndPushBack(m_cachedGetNumVarArgsNode);
            }
            return Value(m_cachedGetNumVarArgsNode, 0 /*outputOrd*/);
        }
    }

    Value GetVariadicArgument(size_t varArgOrd)
    {
        if (m_inlinedCallFrame->IsRootFrame())
        {
            return m_graph->GetRootFunctionVariadicArg(varArgOrd);
        }

        if (varArgOrd >= m_inlinedCallFrame->MaxVarArgsAllowed())
        {
            return m_graph->GetConstant(TValue::Create<tNil>());
        }

        if (m_valueForVariadicArgs[varArgOrd].IsNull())
        {
            Node* getLocal = Node::CreateGetLocalNode(m_inlinedCallFrame, InterpreterFrameLocation::VarArg(varArgOrd));
            SetupNodeCommonInfoAndPushBack(getLocal);
            m_valueForVariadicArgs[varArgOrd] = Value(getLocal, 0 /*outputOrd*/);
        }

        return m_valueForVariadicArgs[varArgOrd];
    }

    void ClearBasicBlockCacheStates()
    {
        m_cachedGetFunctionObjectNode = nullptr;
        m_cachedGetNumVarArgsNode = nullptr;
        m_currentVariadicResult = nullptr;
        if (!m_inlinedCallFrame->IsRootFrame())
        {
            size_t maxVarArgs = m_inlinedCallFrame->MaxVarArgsAllowed();
            for (size_t i = 0; i < maxVarArgs; i++)
            {
                m_valueForVariadicArgs[i] = nullptr;
            }
        }
        size_t numLocals = m_codeBlock->m_stackFrameNumSlots;
        for (size_t i = 0; i < numLocals; i++)
        {
            m_valueAtTail[i] = nullptr;
        }
    }

    // Called when start generating logic for a new basic block, so we need to reset the caching states.
    //
    // If bbInfo is nullptr, one will not be able to use 'GetLocalVariableValue' and 'SetLocalVariableValue' utility
    //
    // Note that this function only reset the caching states, it does not set up the OSR-related info (m_isExitOK etc)
    //
    void StartNewBasicBlock(BasicBlock* bb, BasicBlockUpvalueInfo* bbInfo)
    {
        m_currentBlock = bb;
        m_currentBBInfo = bbInfo;
        if (bbInfo != nullptr)
        {
            size_t numLocals = m_codeBlock->m_stackFrameNumSlots;
            for (size_t i = 0; i < numLocals; i++)
            {
                m_isLocalCaptured[i] = bbInfo->m_isLocalCapturedAtHead.IsSet(i);
            }
        }
        ClearBasicBlockCacheStates();
    }

    Value WARN_UNUSED GetLocalVariableValue(size_t localOrd);
    Node* SetLocalVariableValue(size_t localOrd, Value value);

    void Finalize();
};

}   // namespace dfg
