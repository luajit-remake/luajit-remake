#include "dfg_prediction_propagation.h"
#include "tvalue.h"
#include "dfg_node.h"
#include "dfg_prediction_propagation_util.h"
#include "strongly_connected_components_util.h"
#include "topologically_sorted_scc_util.h"
#include "bytecode_builder.h"

namespace dfg {

namespace {

struct BasicBlockPropagationRules;

struct PropagationOrderFinder
{
    void Initialize(uint32_t numBBs, uint32_t numLogicalVars, uint32_t numCapturedVarLogicalVars)
    {
        uint32_t numNodes = numBBs + numLogicalVars + numCapturedVarLogicalVars;
        m_numNodes = numNodes;
        m_numBBs = numBBs;
        m_cvNodeOffset = numBBs + numLogicalVars;
        m_edgeHead = m_alloc.AllocateArray<uint32_t*>(numNodes);
        m_edgeTail = m_alloc.AllocateArray<uint32_t*>(numNodes);
        for (size_t i = 0; i < numNodes; i++)
        {
            EdgePack* e = m_alloc.AllocateObject<EdgePack>();
            m_edgeHead[i] = &e->m_word[0];
            m_edgeTail[i] = &e->m_word[0];
        }
    }

    void NotifySetLocal(uint32_t bbOrd, uint32_t logicalVarOrd)
    {
        TestAssert(bbOrd < m_numBBs);
        TestAssert(m_numBBs + logicalVarOrd < m_cvNodeOffset);
        AddEdge(bbOrd, m_numBBs + logicalVarOrd);
    }

    void NotifyCreateOrSetCapturedVar(uint32_t bbOrd, uint32_t capturedVarLogicalOrd)
    {
        TestAssert(bbOrd < m_numBBs);
        AddEdge(bbOrd, m_cvNodeOffset + capturedVarLogicalOrd);
    }

    void NotifyGetLocal(uint32_t bbOrd, uint32_t logicalVarOrd)
    {
        TestAssert(bbOrd < m_numBBs);
        TestAssert(m_numBBs + logicalVarOrd < m_cvNodeOffset);
        AddEdge(m_numBBs + logicalVarOrd, bbOrd);
    }

    void NotifyGetCapturedVar(uint32_t bbOrd, uint32_t capturedVarLogicalOrd)
    {
        TestAssert(bbOrd < m_numBBs);
        AddEdge(m_cvNodeOffset + capturedVarLogicalOrd, bbOrd);
    }

    struct Plan
    {
        struct Item
        {
            BasicBlockPropagationRules** m_range;
            uint32_t m_numElements;
            bool m_runOnceIsEnough;
        };

        size_t m_numComponents;
        Item* m_components;
    };

    Plan WARN_UNUSED ComputePlan(BasicBlockPropagationRules** bbs)
    {
        FinalizeGraph();

        uint32_t* sccInfo = m_alloc.AllocateArray<uint32_t>(m_numNodes);
        uint32_t numSCCs = StronglyConnectedComponentsFinder<PropagationOrderFinder, EdgeIter>::ComputeForGenericGraph(
            m_alloc, this, m_numNodes, sccInfo /*out*/);

        TopologicallySortedSccInfo info = TopologicallySortedCondensationGraphBuilder<PropagationOrderFinder, EdgeIter>::Compute(
            m_alloc, this, m_numNodes, numSCCs, sccInfo);

        TestAssert(info.m_numSCCs == numSCCs);
        TestAssert(info.m_numNodes == m_numNodes);

        uint32_t numBBs = m_numBBs;

#ifdef TESTBUILD
        TempVector<bool> showedUp(m_alloc);
        showedUp.resize(numBBs, false /*value*/);
#endif

        Plan r;
        size_t numUsefulComponents = 0;
        r.m_components = m_alloc.AllocateArray<Plan::Item>(numSCCs);
        for (size_t i = 0; i < numSCCs; i++)
        {
            uint32_t cnt = 0;
            auto item = info.m_sccList[i];
            std::span<uint32_t> span(info.m_nodeList + item.m_offset, item.m_size);
            for (uint32_t nodeIdx : span)
            {
                if (nodeIdx < numBBs)
                {
                    cnt++;
                }
            }

            if (cnt > 0)
            {
                BasicBlockPropagationRules** data = m_alloc.AllocateArray<BasicBlockPropagationRules*>(cnt);
                size_t curIdx = 0;
                for (uint32_t nodeIdx : span)
                {
                    if (nodeIdx < numBBs)
                    {
#ifdef TESTBUILD
                        TestAssert(nodeIdx < showedUp.size() && !showedUp[nodeIdx]);
                        showedUp[nodeIdx] = true;
#endif
                        data[curIdx] = bbs[nodeIdx];
                        curIdx++;
                    }
                }
                TestAssert(curIdx == cnt);

                // If the SCC contains only one BB, and the BB has no self-edge (which is indicated by the fact that
                // the SCC contains no non-BB nodes, since all edges are in the form of BB->non-BB or non-BB->BB),
                // then the BB is guaranteed to reach fixpoint after one iteration: even if that iteration resulted
                // in changes, the next iteration definitely won't, since the iteration cannot inflict changes to
                // the inputs of the BB.
                //
                bool runOnceIsEnough = (cnt == 1 && item.m_size == 1);

                r.m_components[numUsefulComponents] = {
                    .m_range = data,
                    .m_numElements = cnt,
                    .m_runOnceIsEnough = runOnceIsEnough
                };
                numUsefulComponents++;
            }
        }
        TestAssert(numUsefulComponents <= numSCCs);
        r.m_numComponents = numUsefulComponents;

#ifdef TESTBUILD
        for (bool value : showedUp) { TestAssert(value); }
#endif
        return r;
    }

private:
    static bool IsAtLastEdgeInPack(uint32_t* tail)
    {
        uint64_t k = reinterpret_cast<uint64_t>(tail) & 31;
        TestAssert(k <= 24 && k % 4 == 0);
        return k == 24;
    }

    void ALWAYS_INLINE AddEdge(uint32_t source, uint32_t dest)
    {
        TestAssert(source < m_numNodes && dest < m_numNodes);
        AddEdge(m_edgeTail[source] /*inout*/, dest);
    }

    void ALWAYS_INLINE AddEdge(uint32_t*& tail, uint32_t dest)
    {
        if (unlikely(IsAtLastEdgeInPack(tail)))
        {
            EdgePack* next = m_alloc.AllocateObject<EdgePack>();
            next->m_word[0] = dest;
            UnalignedStore<EdgePack*>(tail, next);
            tail = &next->m_word[1];
        }
        else
        {
            *tail = dest;
            tail++;
        }
    }

    void FinalizeGraph()
    {
        size_t numNodes = m_numNodes;
        for (size_t i = 0; i < numNodes; i++)
        {
            *m_edgeTail[i] = static_cast<uint32_t>(-1);
        }
    }

    struct alignas(32) EdgePack
    {
        uint32_t m_word[8];
    };
    static_assert(sizeof(EdgePack) == 32);

    struct EdgeIter
    {
        ALWAYS_INLINE EdgeIter(PropagationOrderFinder& graph, uint32_t nodeOrd)
        {
            TestAssert(nodeOrd < graph.m_numNodes);
            m_sourceNode = nodeOrd;
            uint32_t* head = graph.m_edgeHead[nodeOrd];
            m_destNode = *head;
            m_nextPtr = head + 1;
        }

        bool ALWAYS_INLINE IsValid(PropagationOrderFinder& /*graph*/) const
        {
            return m_destNode != static_cast<uint32_t>(-1);
        }

        bool ALWAYS_INLINE Advance(PropagationOrderFinder& /*graph*/)
        {
            TestAssert(m_destNode != static_cast<uint32_t>(-1));
            if (*m_nextPtr == static_cast<uint32_t>(-1))
            {
                m_destNode = static_cast<uint32_t>(-1);
                return false;
            }
            if (unlikely(IsAtLastEdgeInPack(m_nextPtr)))
            {
                m_nextPtr = UnalignedLoad<uint32_t*>(m_nextPtr);
            }
            m_destNode = *m_nextPtr;
            m_nextPtr++;
            TestAssert(m_destNode != static_cast<uint32_t>(-1));
            return true;
        }

        uint32_t ALWAYS_INLINE GetSourceNode(PropagationOrderFinder& /*graph*/) const
        {
            return m_sourceNode;
        }

        uint32_t ALWAYS_INLINE GetDestNode(PropagationOrderFinder& /*graph*/) const
        {
            TestAssert(m_destNode != static_cast<uint32_t>(-1));
            return m_destNode;
        }

        uint32_t m_sourceNode;
        uint32_t m_destNode;
        uint32_t* m_nextPtr;
    };

    TempArenaAllocator m_alloc;
    uint32_t m_numNodes;
    uint32_t m_numBBs;
    uint32_t m_cvNodeOffset;
    uint32_t** m_edgeHead;
    uint32_t** m_edgeTail;
};

// *dst |= mask
// Return true if *dst changed
//
static bool WARN_UNUSED ALWAYS_INLINE UpdatePrediction(TypeMaskTy* dst /*inout*/, TypeMaskTy mask)
{
    TypeMaskTy oldMask = *dst;
    TypeMaskTy newMask = oldMask | mask;
    *dst = newMask;
    return newMask != oldMask;
}

// The propagation rule for GetLocal and GetCapturedVar:
// the result prediction is simply the prediction for the LogicalVariable
//
struct GetVariablePropagationRule
{
    TypeMaskTy* m_variablePrediction;
    TypeMaskTy* m_prediction;

    bool WARN_UNUSED ALWAYS_INLINE Run() const
    {
        return UpdatePrediction(m_prediction /*inout*/, *m_variablePrediction);
    }
};

struct GenericNodePropagationRule
{
    DfgPredictionPropagationImplFuncTy m_func;
    void* m_data;
    void* m_nsd;

    bool WARN_UNUSED ALWAYS_INLINE Run() const
    {
        return m_func(m_data, m_nsd);
    }
};

// Note that the nodes in a basic block can be divided into three categories:
//     1. GetLocal and GetCapturedVar:
//          The prediction of these nodes is the prediction of the logical variables, which may be affected
//          by other basic blocks. These are the only nodes whose prediction are directly affected by other basic blocks.
//     2. Constant-prediction nodes:
//          The prediction of these nodes never changes, so we only need to compute them once and we are done.
//     3. Nodes that does not directly or indirectly use GetLocal and GetCaptureVar:
//          The prediction of these nodes are never affected by the other basic blocks.
//          So similar to constant-prediction nodes, we only need to compute them once.
//     4. Nodes that directly or indirectly use use GetLocal and GetCaptureVar:
//          These nodes need to be recomputed if any of the GetLocal/GetCaptureVar prediction changed.
//
// So we can propagate all GetLocal/GetCapturedVar in the basic block first.
// If none of them changed, we can skip this basic block. Otherwise, we propagate node category (4) above.
//
// GetCapturedVar is a bit tricky since GetCapturedVar and SetCapturedVar cannot be trivially load/store-forwarded,
// so a GetCapturedVar may see result of a SetCapturedVar in the same block.
// Fortunately, one can see that if we compute the GetCapturedVar with a private m_prediction field (or in other words,
// make a clone of the GetCapturedVar at block start), the rule would work correctly.
//
struct BasicBlockPropagationRules
{
    GetVariablePropagationRule* m_gvRules;
    GetVariablePropagationRule* m_gvRulesEnd;

    GenericNodePropagationRule* m_nodeRules;
    GenericNodePropagationRule* m_nodeRulesEnd;

    bool WARN_UNUSED Run(bool isFirst)
    {
        bool changed = false;

        // Update the GetLocal and GetCapturedVar predictions
        //
        {
            GetVariablePropagationRule* rule = m_gvRules;
            GetVariablePropagationRule* ruleEnd = m_gvRulesEnd;
            while (rule < ruleEnd)
            {
                changed |= rule->Run();
                rule++;
            }
        }
        if (!changed && !isFirst)
        {
            // None of the GetLocal or GetCapturedVar changed, no need to run the rest
            //
            // Note that we still need to run the rest if this is the first iteration,
            // since it's possible that we have a SetCapturedVar -> GetCapturedVar,
            // in which case GetCapturedVar may see tBottom (thus not changed) but that's
            // really because the SetCapturedVar hasn't executed.
            //
            return false;
        }

        // Update prediction for each node in the BB
        //
        {
            GenericNodePropagationRule* rule = m_nodeRules;
            GenericNodePropagationRule* ruleEnd = m_nodeRulesEnd;
            while (rule < ruleEnd)
            {
                changed |= rule->Run();
                rule++;
            }
        }
        return changed;
    }

    [[maybe_unused]] void AssertFixpointIsReached()
    {
#ifdef TESTBUILD
        {
            GetVariablePropagationRule* rule = m_gvRules;
            GetVariablePropagationRule* ruleEnd = m_gvRulesEnd;
            while (rule < ruleEnd)
            {
                TestAssert(!rule->Run());
                rule++;
            }
        }
        {
            GenericNodePropagationRule* rule = m_nodeRules;
            GenericNodePropagationRule* ruleEnd = m_nodeRulesEnd;
            while (rule < ruleEnd)
            {
                TestAssert(!rule->Run());
                rule++;
            }
        }
#endif
    }
};

template<bool noValueProfile>
struct PredictionPropagationPass
{
    static PredictionPropagationResult WARN_UNUSED RunPass(TempArenaAllocator& alloc, Graph* graph)
    {
        PredictionPropagationPass pass(alloc, graph);
        pass.RunPropagation();
        return {
            .m_varPredictions = pass.m_varPredictions,
            .m_capturedVarPredictions = pass.m_capturedVarPredictions,
            .m_totalNodesWithPredictions = pass.m_numNodesAssignedPredictions
        };
    }

private:
    PredictionPropagationPass(TempArenaAllocator& resultAlloc, Graph* graph)
        : m_tempAlloc()
        , m_resultAlloc(resultAlloc)
        , m_gvRuleBuffer(m_tempAlloc)
        , m_nodeRuleBuffer(m_tempAlloc)
        , m_graph(graph)
        , m_setupState(resultAlloc, m_tempAlloc)
        , m_planFinder()
#ifdef TESTBUILD
        , m_seenGetLocalLogicalVars(m_tempAlloc)
        , m_seenSetLocalLogicalVars(m_tempAlloc)
        , m_extraRulesForFixpointAssert(m_tempAlloc)
#endif
    {
        TestAssert(graph->IsBlockLocalSSAForm());
        m_cvLogicalOrdSeenColor = 0;
        m_numNodesAssignedPredictions = 0;
        size_t numCVLVs = graph->GetAllCapturedVarLogicalVariables().size();
        m_cvLogicalOrdSeen = m_tempAlloc.AllocateArray<uint32_t>(numCVLVs, 0U /*value*/);
        m_capturedVarPredictions = m_resultAlloc.AllocateArray<TypeMaskTy>(numCVLVs, x_typeMaskFor<tBottom> /*value*/);

        size_t numLVs = graph->GetAllLogicalVariables().size();
        m_varPredictions = m_resultAlloc.AllocateArray<TypeMaskTy>(numLVs, x_typeMaskFor<tBottom> /*value*/);

        size_t numBBs = graph->m_blocks.size();
        m_bbPropagators = m_tempAlloc.AllocateArray<BasicBlockPropagationRules*>(numBBs);
        for (size_t i = 0; i < numBBs; i++)
        {
            m_bbPropagators[i] = m_tempAlloc.AllocateObject<BasicBlockPropagationRules>();
        }

        m_planFinder.Initialize(SafeIntegerCast<uint32_t>(numBBs),
                                SafeIntegerCast<uint32_t>(numLVs),
                                SafeIntegerCast<uint32_t>(numCVLVs));
    }

    void ClearSeenCapturedVars()
    {
        m_cvLogicalOrdSeenColor++;
    }

    bool HasSeenCapturedVar(size_t ord)
    {
        TestAssert(ord < m_graph->GetAllCapturedVarLogicalVariables().size());
        return m_cvLogicalOrdSeen[ord] == m_cvLogicalOrdSeenColor;
    }

    void SetSeenCapturedVar(size_t ord)
    {
        TestAssert(!HasSeenCapturedVar(ord));
        m_cvLogicalOrdSeen[ord] = m_cvLogicalOrdSeenColor;
    }

    void SetupPredictionArrayForNode(Node* node, TypeMaskTy* predictions)
    {
        TestAssert(node->HasDirectOutput() || node->HasExtraOutput());

        // Note that the prediction array for node is expected to map directly to outputOrd.
        // The valid outputOrd range is [hasDirectOutput ? 0 : 1, numExtraOutputs], so we need to make this adjustment below..
        //
        node->SetOutputPredictionsArray(predictions - (node->HasDirectOutput() ? 0 : 1));
        m_numNodesAssignedPredictions++;
    }

    // Setup the prediction array for nodes with DirectOutput and no ExtraOutput
    //
    TypeMaskTy* WARN_UNUSED AllocatePredictionForNodeWithOneOutput(Node* node)
    {
        TestAssert(node->HasDirectOutput() && !node->HasExtraOutput());
        TypeMaskTy* prediction = m_resultAlloc.AllocateObject<TypeMaskTy>();
        // Since the node has direct output, no offset adjustment needed
        //
        node->SetOutputPredictionsArray(prediction);
        m_numNodesAssignedPredictions++;
        return prediction;
    }

    TypeMaskTy* GetPredictionAddressForInputOperand(Value input)
    {
        return input.GetOperand()->GetOutputPredictionAddr(input.m_outputOrd);
    }

    void ALWAYS_INLINE AddOrRunRule(Node* node, bool mustAdd, const GenericNodePropagationRule& rule)
    {
        // In this pass, we use the referenced bit to denote if a node needs to be fixpoint'ed
        // If false, it means the predictions in this node is already up-to-date and final
        //
        TestAssert(!node->IsNodeReferenced());
        if (mustAdd)
        {
            // This node needs to be fixpoint'ed, so any node that uses it also needs to be fixpoint'ed
            //
            node->SetNodeReferenced(true);
            m_nodeRuleBuffer.push_back(rule);
        }
        else
        {
            // This node does not need to be fixpoint'ed, we can just run the rule once and the result will be guaranteed final
            //
            std::ignore = rule.Run();
#ifdef TESTBUILD
            // In test build, add this rule to a hidden list so we can also check this rule when confirming fixpoint at the end
            //
            m_extraRulesForFixpointAssert.push_back(rule);
#endif
        }
    }

    void ProcessGetLocal(uint32_t bbOrd, Node* node)
    {
        TestAssert(node->IsGetLocalNode());
        uint32_t lvOrd = node->GetLogicalVariable()->m_logicalVariableOrdinal;
        TestAssert(lvOrd < m_graph->GetAllLogicalVariables().size());

#ifdef TESTBUILD
        // In block-local SSA form, there should be at most one GetLocal on each
        // logical variable in each BB, and it should come before any SetLocal
        // This is required for correctness since we are doing all the GetLocal upfront
        //
        TestAssert(!m_seenGetLocalLogicalVars.count(lvOrd));
        m_seenGetLocalLogicalVars.insert(lvOrd);
        TestAssert(!m_seenSetLocalLogicalVars.count(lvOrd));
#endif

        TypeMaskTy* prediction = AllocatePredictionForNodeWithOneOutput(node);
        *prediction = x_typeMaskFor<tBottom>;

        TestAssert(!node->IsNodeReferenced());
        node->SetNodeReferenced(true);

        // Add to gvRuleBuffer, no need to add to m_nodeRuleBuffer again since 'prediction' is already the prediction field of GetLocal
        //
        m_gvRuleBuffer.push_back({
            .m_variablePrediction = m_varPredictions + lvOrd,
            .m_prediction = prediction
        });

        m_planFinder.NotifyGetLocal(bbOrd, lvOrd);
    }

    // The propagation function that propagates from a single source field to a single dest field
    //
    static bool SssdPropagationFn(void* dstVoid /*inout*/, void* srcVoid)
    {
        TypeMaskTy* dst = reinterpret_cast<TypeMaskTy*>(dstVoid);
        TypeMaskTy* src = reinterpret_cast<TypeMaskTy*>(srcVoid);

        return UpdatePrediction(dst /*inout*/, *src);
    }

    void ProcessSetLocal(uint32_t bbOrd, Node* node)
    {
        TestAssert(node->IsSetLocalNode());
        uint32_t lvOrd = node->GetLogicalVariable()->m_logicalVariableOrdinal;
        TestAssert(lvOrd < m_graph->GetAllLogicalVariables().size());

#ifdef TESTBUILD
        // In block-local SSA form, there should be at most one SetLocal on each logical variable in each BB
        //
        TestAssert(!m_seenSetLocalLogicalVars.count(lvOrd));
        m_seenSetLocalLogicalVars.insert(lvOrd);
#endif

        Value operand = node->GetSoleInput().GetValue();
        TypeMaskTy* inputAddr = GetPredictionAddressForInputOperand(operand);
        TypeMaskTy* outputAddr = m_varPredictions + lvOrd;

        bool mustFixpoint = operand.GetOperand()->IsNodeReferenced();
        AddOrRunRule(
            node, mustFixpoint,
            GenericNodePropagationRule {
                .m_func = SssdPropagationFn,
                .m_data = outputAddr,
                .m_nsd = inputAddr
            });

        m_planFinder.NotifySetLocal(bbOrd, lvOrd);
    }

    void ProcessCreateCapturedVar(uint32_t bbOrd, Node* node)
    {
        TestAssert(node->IsCreateCapturedVarNode());
        CapturedVarLogicalVariableInfo* lvi = node->GetBuiltinNodeInlinedNsdRefAs<Nsd_CapturedVarInfo>().m_logicalCV;
        uint32_t ord = lvi->GetLogicalVariableOrdinal();
        TypeMaskTy* cvPred = m_capturedVarPredictions + ord;

        // Update prediction for the CapturedVar
        //
        if (node->IsCreateCapturedVarMutableSelfReference())
        {
            // This CreateCapturedVar is used as mutable self-reference of a CreateClosure
            // The input to this CreateCapturedVar is UndefValue, but it is never visible,
            // so we should predict tFunction instead of tGarbage to not pollute the CapturedVar prediction.
            //
            TestAssert(node->GetSoleInput().GetOperand()->IsUndefValueNode());
            *cvPred |= x_typeMaskFor<tFunction>;
        }
        else
        {
            Value operand = node->GetSoleInput().GetValue();
            TypeMaskTy* inputAddr = GetPredictionAddressForInputOperand(operand);

            bool mustFixpoint = operand.GetOperand()->IsNodeReferenced();
            AddOrRunRule(
                node, mustFixpoint,
                GenericNodePropagationRule {
                    .m_func = SssdPropagationFn,
                    .m_data = cvPred,
                    .m_nsd = inputAddr
                });
        }

        // The node itself always output a CapturedVar
        //
        TypeMaskTy* prediction = AllocatePredictionForNodeWithOneOutput(node);
        *prediction = x_typeMaskFor<tOpaque>;

        m_planFinder.NotifyCreateOrSetCapturedVar(bbOrd, ord);
    }

    void ProcessGetCapturedVar(uint32_t bbOrd, Node* node)
    {
        TestAssert(node->IsGetCapturedVarNode());
        CapturedVarLogicalVariableInfo* lvi = node->GetBuiltinNodeInlinedNsdRefAs<Nsd_CapturedVarInfo>().m_logicalCV;
        uint32_t ord = lvi->GetLogicalVariableOrdinal();
        TypeMaskTy* cvPred = m_capturedVarPredictions + ord;

        // If this is the first occurance of 'ord', we must make a GetVariablePropagationRule
        //
        if (!HasSeenCapturedVar(ord))
        {
            SetSeenCapturedVar(ord);

            // Note that this GetVariablePropagationRule must hold its own prediction field, not the prediction of the GetCapturedVar
            // Also, since this prediction is only used during the propagation, it can be allocated by tempAlloc
            //
            TypeMaskTy* prediction = m_tempAlloc.AllocateObject<TypeMaskTy>(x_typeMaskFor<tBottom>);
            m_gvRuleBuffer.push_back({
                .m_variablePrediction = cvPred,
                .m_prediction = prediction
            });

            m_planFinder.NotifyGetCapturedVar(bbOrd, ord);
        }

        TypeMaskTy* prediction = AllocatePredictionForNodeWithOneOutput(node);
        *prediction = x_typeMaskFor<tBottom>;

        AddOrRunRule(
            node, true /*mustFixpoint*/,
            GenericNodePropagationRule {
                .m_func = SssdPropagationFn,
                .m_data = prediction,
                .m_nsd = cvPred
            });
    }

    void ProcessSetCapturedVar(uint32_t bbOrd, Node* node)
    {
        TestAssert(node->IsSetCapturedVarNode());
        CapturedVarLogicalVariableInfo* lvi = node->GetBuiltinNodeInlinedNsdRefAs<Nsd_CapturedVarInfo>().m_logicalCV;
        uint32_t ord = lvi->GetLogicalVariableOrdinal();
        TypeMaskTy* cvPred = m_capturedVarPredictions + ord;

        Value operand = node->GetInputEdgeForNodeWithFixedNumInputs<2>(1).GetValue();
        TypeMaskTy* inputAddr = GetPredictionAddressForInputOperand(operand);

        bool mustFixpoint = operand.GetOperand()->IsNodeReferenced();
        AddOrRunRule(
            node, mustFixpoint,
            GenericNodePropagationRule {
                .m_func = SssdPropagationFn,
                .m_data = cvPred,
                .m_nsd = inputAddr
            });

        // There may be duplicate edges, but this is fine
        //
        m_planFinder.NotifyCreateOrSetCapturedVar(bbOrd, ord);
    }

    void ProcessGetKthVariadicRes(Node* node)
    {
        TestAssert(node->IsGetKthVariadicResNode());
        TypeMaskTy* prediction = AllocatePredictionForNodeWithOneOutput(node);

        // The variadic result may be anything, including invalid boxed values, but not garbage values
        // TODO: sometimes we can get more accurate information from argument profile
        //
        *prediction = x_typeMaskFor<tFullTop> ^ x_typeMaskFor<tGarbage>;
    }

    void ProcessGetNumVariadicRes(Node* node)
    {
        TestAssert(node->IsGetNumVariadicResNode());
        TypeMaskTy* prediction = AllocatePredictionForNodeWithOneOutput(node);
        *prediction = x_typeMaskFor<tOpaque>;
    }

    void ProcessI64SubSaturateToZero(Node* node)
    {
        TestAssert(node->IsI64SubSaturateToZeroNode());
        TypeMaskTy* prediction = AllocatePredictionForNodeWithOneOutput(node);
        *prediction = x_typeMaskFor<tOpaque>;
    }

    void ProcessCreateFunctionObject(Node* node)
    {
        TestAssert(node->IsCreateFunctionObjectNode());
        TypeMaskTy* prediction = AllocatePredictionForNodeWithOneOutput(node);
        *prediction = x_typeMaskFor<tFunction>;
    }

    void ProcessGetUpvalueImmutableOrMutable(Node* node)
    {
        TestAssert(node->IsGetUpvalueNode());
        TypeMaskTy* prediction = AllocatePredictionForNodeWithOneOutput(node);

        // TODO: we should have a way to value profile this
        //
        *prediction = x_typeMaskFor<tFullTop> ^ x_typeMaskFor<tGarbage>;
    }

    void ProcessGuestLanguageNode(Node* node)
    {
        TestAssert(!node->IsBuiltinNodeKind());
        if (!node->HasDirectOutput() && !node->HasExtraOutput())
        {
            return;
        }
        BCKind bcKind = node->GetGuestLanguageBCKind();
        uint8_t* nsd = node->GetNodeSpecificData();
        DeegenBytecodeBuilder::BytecodeDecoder::SetupPredictionPropagationData(bcKind, m_setupState /*inout*/, nsd);
        TypeMaskTy* predictions = m_setupState.m_predictions;
        SetupPredictionArrayForNode(node, predictions);

        bool mustFixpoint = false;
        {
            uint8_t* curInput = reinterpret_cast<uint8_t*>(m_setupState.m_inputOrds);
            size_t numInputs = m_setupState.m_inputListLen;
            uint8_t* curInputEnd = curInput + numInputs * sizeof(uint64_t);
            while (curInput < curInputEnd)
            {
                uint64_t value = UnalignedLoad<uint64_t>(curInput);
                TypeMaskTy* addr;
                if (value == static_cast<uint64_t>(-1))
                {
                    addr = nullptr;
                }
                else
                {
                    Value inputVal = node->GetInputEdge(SafeIntegerCast<uint32_t>(value)).GetValue();
                    addr = GetPredictionAddressForInputOperand(inputVal);
                    mustFixpoint |= inputVal.GetOperand()->IsNodeReferenced();
                }
                UnalignedStore<TypeMaskTy*>(curInput, addr);
                curInput += sizeof(uint64_t);
            }
        }

        if (m_setupState.m_valueProfileOrds.size() > 0)
        {
            if (noValueProfile)
            {
                for (uint32_t ord : m_setupState.m_valueProfileOrds)
                {
                    TestAssert(ord < (node->HasDirectOutput() ? 1U : 0U) + node->GetNumExtraOutputs());
                    predictions[ord] = x_typeMaskFor<tBoxedValueTop>;
                }
            }
            else
            {
                ReleaseAssert(false && "not implemented");
            }
        }

        TestAssert(static_cast<size_t>(bcKind) < x_dfgPredictionPropagationFuncs.size());
        DfgPredictionPropagationImplFuncTy propagationFn = x_dfgPredictionPropagationFuncs[static_cast<size_t>(bcKind)];

        if (propagationFn == nullptr)
        {
            // It is possible that the propagationFn is nullptr if there is trivially no propagation work to do
            // (e.g., all output is specified to be value-profiled)
            //
            TestAssert(!mustFixpoint);
            TestAssert(m_setupState.m_inputListLen == 0);
        }
        else
        {
            AddOrRunRule(
                node, mustFixpoint,
                GenericNodePropagationRule {
                    .m_func = propagationFn,
                    .m_data = m_setupState.m_data,
                    .m_nsd = nsd
                });
        }
    }

    void ProcessAllConstantLikeNodes()
    {
        m_graph->ForEachConstantLikeNode(
            [&](Node* node) ALWAYS_INLINE
            {
                NodeKind nodeKind = node->GetNodeKind();

                // Constant-like nodes never need to be fixpointed: we know their predictions now
                //
                node->SetNodeReferenced(false);

                TypeMaskTy* prediction = AllocatePredictionForNodeWithOneOutput(node);
                switch (nodeKind)
                {
                case NodeKind_Constant:
                {
                    *prediction = GetTypeForBoxedValue(node->GetConstantNodeValue());
                    break;
                }
                case NodeKind_UnboxedConstant:
                {
                    *prediction = x_typeMaskFor<tOpaque>;
                    break;
                }
                case NodeKind_UndefValue:
                {
                    *prediction = x_typeMaskFor<tGarbage>;
                    break;
                }
                case NodeKind_Argument:
                {
                    // TODO: we need to value profile arguments
                    //
                    *prediction = x_typeMaskFor<tBoxedValueTop>;
                    break;
                }
                case NodeKind_GetNumVariadicArgs:
                {
                    *prediction = x_typeMaskFor<tOpaque>;
                    break;
                }
                case NodeKind_GetKthVariadicArg:
                {
                    // TODO: we may consider value profile variadic arguments as well
                    //
                    *prediction = x_typeMaskFor<tBoxedValueTop>;
                    break;
                }
                case NodeKind_GetFunctionObject:
                {
                    // Note that GetFunctionObject returns the unboxed HeapPtr, not the function object, so it's tOpaque
                    //
                    *prediction = x_typeMaskFor<tOpaque>;
                    break;
                }
                default:
                    TestAssert(false && "constant-like node expected!");
                    __builtin_unreachable();
                }   /*switch*/
            });
    }

    void ProcessBasicBlock(uint32_t bbOrd, BasicBlock* bb)
    {
        m_gvRuleBuffer.clear();
        m_nodeRuleBuffer.clear();
        ClearSeenCapturedVars();
#ifdef TESTBUILD
        m_seenGetLocalLogicalVars.clear();
        m_seenSetLocalLogicalVars.clear();
#endif

        TestAssert(m_graph->m_blocks[bbOrd] == bb);

        auto& nodes = bb->m_nodes;
        for (Node* node : nodes)
        {
            // Initialize the node as not needed to be fixpointed
            // The ProcessXXX functions will set it to true if it turns out that fixpointing is needed
            //
            node->SetNodeReferenced(false);

            if (!node->IsBuiltinNodeKind())
            {
                // TODO: handle nodes specialized as speculative inlining prologue or epilogue
                //
                TestAssert(node->GetNodeSpecializedForInliningKind() == Node::SISKind::None && "this function does not handle speculative inlining nodes yet!");
                ProcessGuestLanguageNode(node);
            }
            else
            {
                NodeKind nodeKind = node->GetNodeKind();
                switch (nodeKind)
                {
                case NodeKind_Constant:
                case NodeKind_UnboxedConstant:
                case NodeKind_UndefValue:
                case NodeKind_Argument:
                case NodeKind_GetNumVariadicArgs:
                case NodeKind_GetKthVariadicArg:
                case NodeKind_GetFunctionObject:
                {
                    TestAssert(false && "constant-like nodes should not show up in basic block!");
                    __builtin_unreachable();
                }
                case NodeKind_Phi:
                {
                    TestAssert(false && "Phi node should not show up in basic block!");
                    __builtin_unreachable();
                }
                case NodeKind_Nop:
                case NodeKind_ShadowStore:
                case NodeKind_ShadowStoreUndefToRange:
                case NodeKind_Phantom:
                case NodeKind_CreateVariadicRes:
                case NodeKind_PrependVariadicRes:
                case NodeKind_CheckU64InBound:
                case NodeKind_SetUpvalue:
                case NodeKind_Return:
                {
                    // These nodes have no SSA outputs
                    //
                    break;
                }
                case NodeKind_GetLocal:
                {
                    ProcessGetLocal(bbOrd, node);
                    break;
                }
                case NodeKind_SetLocal:
                {
                    ProcessSetLocal(bbOrd, node);
                    break;
                }
                case NodeKind_CreateCapturedVar:
                {
                    ProcessCreateCapturedVar(bbOrd, node);
                    break;
                }
                case NodeKind_GetCapturedVar:
                {
                    ProcessGetCapturedVar(bbOrd, node);
                    break;
                }
                case NodeKind_SetCapturedVar:
                {
                    ProcessSetCapturedVar(bbOrd, node);
                    break;
                }
                case NodeKind_GetKthVariadicRes:
                {
                    ProcessGetKthVariadicRes(node);
                    break;
                }
                case NodeKind_GetNumVariadicRes:
                {
                    ProcessGetNumVariadicRes(node);
                    break;
                }
                case NodeKind_I64SubSaturateToZero:
                {
                    ProcessI64SubSaturateToZero(node);
                    break;
                }
                case NodeKind_CreateFunctionObject:
                {
                    ProcessCreateFunctionObject(node);
                    break;
                }
                case NodeKind_GetUpvalueImmutable:
                case NodeKind_GetUpvalueMutable:
                {
                    ProcessGetUpvalueImmutableOrMutable(node);
                    break;
                }
                case NodeKind_FirstAvailableGuestLanguageNodeKind:
                {
                    TestAssert(false && "should not reach here");
                    __builtin_unreachable();
                }
                }   /*switch*/
            }
        }

#ifdef TESTBUILD
        // Check that the predictions field has been set up for all nodes with output
        //
        for (Node* node : nodes)
        {
            if (node->HasDirectOutput() || node->HasExtraOutput())
            {
                // This will trigger the assert that the predictions is valid
                //
                std::ignore = node->GetOutputPredictionsArray();
            }
        }
#endif

        BasicBlockPropagationRules* r = m_bbPropagators[bbOrd];
        TestAssert(r != nullptr);

        r->m_gvRules = m_tempAlloc.AllocateArray<GetVariablePropagationRule>(m_gvRuleBuffer.size());
        memcpy(r->m_gvRules, m_gvRuleBuffer.data(), sizeof(GetVariablePropagationRule) * m_gvRuleBuffer.size());
        r->m_gvRulesEnd = r->m_gvRules + m_gvRuleBuffer.size();

        r->m_nodeRules = m_tempAlloc.AllocateArray<GenericNodePropagationRule>(m_nodeRuleBuffer.size());
        memcpy(r->m_nodeRules, m_nodeRuleBuffer.data(), sizeof(GenericNodePropagationRule) * m_nodeRuleBuffer.size());
        r->m_nodeRulesEnd = r->m_nodeRules + m_nodeRuleBuffer.size();
    }

    void AssertFixpointIsReached()
    {
#ifdef TESTBUILD
        // Since a lot of the propagators are generated code, be extra paranoid and do not trust its return value
        // Instead, we make a copy all the predictions beforehand, run all the rules,
        // and check that indeed none of the predictions changed
        //
        TempVector<TypeMaskTy> capturedVarPredictions(m_tempAlloc);
        capturedVarPredictions.resize(m_graph->GetAllCapturedVarLogicalVariables().size());
        memcpy(capturedVarPredictions.data(), m_capturedVarPredictions, sizeof(TypeMaskTy) * capturedVarPredictions.size());

        TempVector<TypeMaskTy> varPredictions(m_tempAlloc);
        varPredictions.resize(m_graph->GetAllLogicalVariables().size());
        memcpy(varPredictions.data(), m_varPredictions, sizeof(TypeMaskTy) * varPredictions.size());

        TempUnorderedMap<Node*, TypeMaskTy*> nodePredictions(m_tempAlloc);
        auto copyNodePredictions = [&](Node* node)
        {
            bool hasDirectOutput = node->HasDirectOutput();
            size_t numExtraOutput = node->GetNumExtraOutputs();
            if (!hasDirectOutput && numExtraOutput == 0)
            {
                return;
            }
            size_t base = (hasDirectOutput ? 0 : 1);
            size_t len = (hasDirectOutput ? 1 : 0) + numExtraOutput;

            TypeMaskTy* predictions = node->GetOutputPredictionsArray();
            TypeMaskTy* copy = m_tempAlloc.AllocateArray<TypeMaskTy>(len);
            memcpy(copy, predictions + base, len * sizeof(TypeMaskTy));

            TestAssert(!nodePredictions.count(node));
            nodePredictions[node] = copy;
        };

        m_graph->ForEachConstantLikeNode(
            [&](Node* node) ALWAYS_INLINE
            {
                copyNodePredictions(node);
            });

        for (BasicBlock* bb : m_graph->m_blocks)
        {
            for (Node* node : bb->m_nodes)
            {
                copyNodePredictions(node);
            }
        }

        TestAssert(nodePredictions.size() == m_numNodesAssignedPredictions);

        // Run all the rules, including the extra rules that we determined as only needs to be run once
        //
        size_t numBBs = m_graph->m_blocks.size();
        for (size_t bbOrd = 0; bbOrd < numBBs; bbOrd++)
        {
            m_bbPropagators[bbOrd]->AssertFixpointIsReached();
        }
        for (GenericNodePropagationRule& rule : m_extraRulesForFixpointAssert)
        {
            TestAssert(!rule.Run());
        }

        // Check that nothing changed
        //
        TestAssert(memcmp(capturedVarPredictions.data(), m_capturedVarPredictions, sizeof(TypeMaskTy) * capturedVarPredictions.size()) == 0);
        TestAssert(memcmp(varPredictions.data(), m_varPredictions, sizeof(TypeMaskTy) * varPredictions.size()) == 0);

        auto checkNodePrediction = [&](Node* node)
        {
            bool hasDirectOutput = node->HasDirectOutput();
            size_t numExtraOutput = node->GetNumExtraOutputs();
            if (!hasDirectOutput && numExtraOutput == 0)
            {
                return;
            }
            size_t base = (hasDirectOutput ? 0 : 1);
            size_t len = (hasDirectOutput ? 1 : 0) + numExtraOutput;

            TypeMaskTy* predictions = node->GetOutputPredictionsArray();

            TestAssert(nodePredictions.count(node));
            TypeMaskTy* copy = nodePredictions[node];

            TestAssert(memcmp(copy, predictions + base, len * sizeof(TypeMaskTy)) == 0);

            nodePredictions.erase(nodePredictions.find(node));
        };

        m_graph->ForEachConstantLikeNode(
            [&](Node* node) ALWAYS_INLINE
            {
                checkNodePrediction(node);
            });

        for (BasicBlock* bb : m_graph->m_blocks)
        {
            for (Node* node : bb->m_nodes)
            {
                checkNodePrediction(node);
            }
        }

        TestAssert(nodePredictions.size() == 0);
#endif
    }

    void RunPropagation()
    {
        ProcessAllConstantLikeNodes();

        auto& BBs = m_graph->m_blocks;
        size_t numBBs = BBs.size();
        for (uint32_t bbOrd = 0; bbOrd < numBBs; bbOrd++)
        {
            ProcessBasicBlock(bbOrd, BBs[bbOrd]);
        }

        // TODO: figure out a good order of the basic blocks inside each SCC
        //
        PropagationOrderFinder::Plan plan = m_planFinder.ComputePlan(m_bbPropagators);

        for (size_t componentIdx = 0; componentIdx < plan.m_numComponents; componentIdx++)
        {
            auto& component = plan.m_components[componentIdx];
            BasicBlockPropagationRules** listStart = component.m_range;
            BasicBlockPropagationRules** listEnd = component.m_range + component.m_numElements;

            if (component.m_runOnceIsEnough)
            {
                TestAssert(component.m_numElements == 1);
                std::ignore = listStart[0]->Run(true /*isFirst*/);
            }
            else
            {
                bool isFirst = true;
                while (true)
                {
                    bool changed = false;
                    for (BasicBlockPropagationRules** cur = listStart; cur != listEnd; cur++)
                    {
                        changed |= cur[0]->Run(isFirst);
                    }
                    if (!changed)
                    {
                        break;
                    }
                    isFirst = false;

                    // TODO:
                    //     JSC uses a heuristic that propagate in forward order once, and then in backward order once
                    //     On preliminary test set (DfgFrontend.Parser_Stress_1) it seems like this is harmful,
                    //     but I still need to see how it interacts with other basic block orderings, and how it
                    //     interacts with speculative inlining to reach a final conclusion.
                    //
                    bool alternatePropagationOrder = false;
                    if (alternatePropagationOrder)
                    {
                        for (BasicBlockPropagationRules** cur = listEnd; cur-- > listStart;)
                        {
                            changed |= cur[0]->Run(isFirst);
                        }
                        if (!changed)
                        {
                            break;
                        }
                    }
                }
            }
        }

        AssertFixpointIsReached();
    }

    TempArenaAllocator m_tempAlloc;
    TempArenaAllocator& m_resultAlloc;
    TempVector<GetVariablePropagationRule> m_gvRuleBuffer;
    TempVector<GenericNodePropagationRule> m_nodeRuleBuffer;
    Graph* m_graph;
    uint32_t* m_cvLogicalOrdSeen;
    TypeMaskTy* m_capturedVarPredictions;
    TypeMaskTy* m_varPredictions;
    uint32_t m_cvLogicalOrdSeenColor;
    uint32_t m_numNodesAssignedPredictions;
    BasicBlockPropagationRules** m_bbPropagators;
    DfgPredictionPropagationSetupInfo m_setupState;
    PropagationOrderFinder m_planFinder;

#ifdef TESTBUILD
    TempUnorderedSet<uint32_t> m_seenGetLocalLogicalVars;
    TempUnorderedSet<uint32_t> m_seenSetLocalLogicalVars;
    TempVector<GenericNodePropagationRule> m_extraRulesForFixpointAssert;
#endif
};

}   // anonymous namespace

PredictionPropagationResult WARN_UNUSED RunPredictionPropagation(TempArenaAllocator& alloc, Graph* graph)
{
    return PredictionPropagationPass<false /*noValueProfile*/>::RunPass(alloc, graph);
}

PredictionPropagationResult WARN_UNUSED RunPredictionPropagationWithoutValueProfile(TempArenaAllocator& alloc, Graph* graph)
{
    return PredictionPropagationPass<true /*noValueProfile*/>::RunPass(alloc, graph);
}

}   // namespace dfg
