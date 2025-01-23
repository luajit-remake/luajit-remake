#include "common_utils.h"
#include "dfg_speculation_assignment.h"
#include "dfg_node.h"
#include "dfg_variant_trait_table.h"

namespace dfg {

// Return the raw prediction mask for an input operand
//
static TypeMaskTy ALWAYS_INLINE GetRawPredictionForEdge(Edge& e)
{
    TypeMaskTy* addr = e.GetOperand()->GetOutputPredictionAddr(e.GetOutputOrdinal());
    return *addr;
}

// Implementation for the API expected by the generated C++ code
//
struct NodeAccessorForSpeculationAssignment
{
    NodeAccessorForSpeculationAssignment(Node* node) : m_node(node) { }

    uint8_t* ALWAYS_INLINE GetInlinedNsd()
    {
        return m_node->GetNodeSpecificDataMustInlined();
    }

    uint8_t* ALWAYS_INLINE GetOutlinedNsd()
    {
        return m_node->GetNodeSpecificDataMustOutlined();
    }

    template<size_t knownInputSize>
    TypeMaskTy ALWAYS_INLINE GetPredictionForNodesWithFixedNumInputs(uint32_t inputOrd)
    {
        Edge& e = m_node->GetInputEdgeForNodeWithFixedNumInputs<knownInputSize>(inputOrd);
        return GetRawPredictionForEdge(e);
    }

    TypeMaskTy ALWAYS_INLINE GetPredictionForInput(uint32_t inputOrd)
    {
        Edge& e = m_node->GetInputEdge(inputOrd);
        return GetRawPredictionForEdge(e);
    }

    void ALWAYS_INLINE SetVariantOrd(uint8_t variantOrd)
    {
        m_node->SetDfgVariantOrd(variantOrd);
    }

    void ALWAYS_INLINE SetEdgeInfo(Edge& e, UseKind useKind, bool predictionIsDoubleNotNaN)
    {
        TestAssert(useKind < UseKind_X_END_OF_ENUM);
        e.SetUseKind(useKind);
        e.SetPredictionMaskIsDoubleNotNaNFlag(predictionIsDoubleNotNaN);
    }

    template<size_t knownInputSize>
    void ALWAYS_INLINE AssignUseKindForNodeWithFixedNumInputs(uint32_t inputOrd, UseKind useKind, bool predictionIsDoubleNotNaN)
    {
        Edge& e = m_node->GetInputEdgeForNodeWithFixedNumInputs<knownInputSize>(inputOrd);
        SetEdgeInfo(e, useKind, predictionIsDoubleNotNaN);
    }

    void ALWAYS_INLINE AssignUseKind(uint32_t inputOrd, UseKind useKind, bool predictionIsDoubleNotNaN)
    {
        Edge& e = m_node->GetInputEdge(inputOrd);
        SetEdgeInfo(e, useKind, predictionIsDoubleNotNaN);
    }

    // Assert that the edge speculation is OK:
    // 1. It should be a superset of predictionMask
    // 2. It should be a subset of speculationMask
    //
    void AssertEdgeFulfillsMask([[maybe_unused]] uint32_t inputOrd,
                                [[maybe_unused]] TypeMask predictionMask,
                                [[maybe_unused]] TypeMask speculationMask)
    {
#ifdef TESTBUILD
        Edge& e = m_node->GetInputEdge(inputOrd);
        UseKind useKind = e.GetUseKind();
        TestAssert(useKind == UseKind_Untyped || useKind == UseKind_FullTop || useKind == UseKind_Unreachable || useKind >= UseKind_FirstUnprovenUseKind);
        if (predictionMask.Empty())
        {
            TestAssert(useKind == UseKind_Unreachable);
            return;
        }

        TestAssert(useKind != UseKind_Unreachable);
        if (useKind == UseKind_FullTop)
        {
            TestAssert(e.MaybeInvalidBoxedValue());
            TestAssert(predictionMask.SubsetOf(x_typeMaskFor<tFullTop>));
            TestAssert(speculationMask == x_typeMaskFor<tFullTop>);
            TestAssert(!e.IsPredictionMaskDoubleNotNaN());
            return;
        }

        TestAssert(!e.MaybeInvalidBoxedValue());
        TestAssertIff(e.IsPredictionMaskDoubleNotNaN(), predictionMask.SubsetOf(x_typeMaskFor<tDoubleNotNaN>));

        TestAssert(speculationMask.SubsetOf(x_typeMaskFor<tBoxedValueTop>));
        TestAssert(predictionMask.SubsetOf(x_typeMaskFor<tBoxedValueTop>));
        TestAssert(speculationMask.SupersetOf(predictionMask));

        if (useKind == UseKind_Untyped)
        {
            TestAssert(speculationMask == x_typeMaskFor<tBoxedValueTop>);
            return;
        }

        TestAssert(useKind >= UseKind_FirstUnprovenUseKind);
        size_t diff = static_cast<size_t>(useKind) - static_cast<size_t>(UseKind_FirstUnprovenUseKind);
        TestAssert(diff % 2 == 0);
        diff /= 2;

        TestAssert(diff + 1 < x_list_of_type_speculation_masks.size());
        TypeMask checkedMask = x_list_of_type_speculation_masks[diff + 1];
        TestAssert(checkedMask.SupersetOf(predictionMask));
        TestAssert(checkedMask.SubsetOf(speculationMask));
#endif
    }

    Node* m_node;
};

using SpeculationAssignmentImplFn = void(*)(NodeAccessorForSpeculationAssignment);

constexpr std::array<SpeculationAssignmentImplFn, static_cast<size_t>(BCKind::X_END_OF_ENUM)> x_speculation_assignment_fn_for_guest_language_nodes = []() {
    std::array<SpeculationAssignmentImplFn, static_cast<size_t>(BCKind::X_END_OF_ENUM)> res;

#define macro(e)   \
    res[static_cast<size_t>(BCKind::e)] = deegen_dfg_variant_selection_generated_impl_ ## e <NodeAccessorForSpeculationAssignment>;

    PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro

    return res;
}();

namespace {

struct SpeculationAssignmentPass
{
    static void Run(Graph* graph)
    {
        SpeculationAssignmentPass pass;
        pass.RunOnGraph(graph);
    }

private:
    SpeculationAssignmentPass()
        : m_tempAlloc()
        , m_setLocals(m_tempAlloc)
    { }

    void ProcessGuestLanguageNode(Node* node)
    {
        TestAssert(!node->IsBuiltinNodeKind());
        TestAssert(node->GetNodeSpecializedForInliningKind() == Node::SISKind::None && "speculative inlining not handled yet");
        TestAssert(!node->HasAssignedDfgVariantOrd());

        BCKind kind = node->GetGuestLanguageBCKind();
        TestAssert(static_cast<size_t>(kind) < x_speculation_assignment_fn_for_guest_language_nodes.size());
        SpeculationAssignmentImplFn implFn = x_speculation_assignment_fn_for_guest_language_nodes[static_cast<size_t>(kind)];

        NodeAccessorForSpeculationAssignment accessor(node);
        implFn(accessor);

        TestAssert(node->HasAssignedDfgVariantOrd());
    }

    void ProcessBuiltinNode(Node* node)
    {
        TestAssert(node->IsBuiltinNodeKind());
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
            TestAssert(false && "constant-like node should not show up in basic block!");
            __builtin_unreachable();
        }
        case NodeKind_Phi:
        case NodeKind_FirstAvailableGuestLanguageNodeKind:
        {
            TestAssert(false && "unexpected node kind!");
            __builtin_unreachable();
        }
        case NodeKind_Nop:
        case NodeKind_GetLocal:
        case NodeKind_ShadowStore:
        case NodeKind_ShadowStoreUndefToRange:
        case NodeKind_Phantom:
        case NodeKind_GetKthVariadicRes:
        case NodeKind_GetNumVariadicRes:
        case NodeKind_PrependVariadicRes:
        case NodeKind_Return:
        {
            // Speculation does not make sense for for these nodes
            //
            break;
        }
        case NodeKind_CreateVariadicRes:
        {
            TestAssert(node->GetNumInputs() > 0);
            TestAssert(node->GetInputEdge(0).IsStaticallyKnownNoCheckNeeded());
            TestAssert(node->GetInputEdge(0).GetUseKind() == UseKind_KnownUnboxedInt64);
            break;
        }
        case NodeKind_GetCapturedVar:
        {
            TestAssert(node->GetSoleInput().IsStaticallyKnownNoCheckNeeded());
            TestAssert(node->GetSoleInput().GetUseKind() == UseKind_KnownCapturedVar);
            break;
        }
        case NodeKind_SetCapturedVar:
        {
            TestAssert(node->GetInputEdgeForNodeWithFixedNumInputs<2>(0).IsStaticallyKnownNoCheckNeeded());
            TestAssert(node->GetInputEdgeForNodeWithFixedNumInputs<2>(0).GetUseKind() == UseKind_KnownCapturedVar);
            break;
        }
        case NodeKind_CreateCapturedVar:
        {
            // Speculating on the value written into a CaptureVar/Upvalue is not useful, since any function call can clobber their values
            // so doing so cannot give us proof for GetCapturedVar/GetUpvalue
            //
            break;
        }
        case NodeKind_GetUpvalueImmutable:
        case NodeKind_GetUpvalueMutable:
        {
            TestAssert(node->GetSoleInput().IsStaticallyKnownNoCheckNeeded());
            TestAssert(node->GetSoleInput().GetUseKind() == UseKind_KnownUnboxedInt64);
            break;
        }
        case NodeKind_SetUpvalue:
        {
            TestAssert(node->GetInputEdgeForNodeWithFixedNumInputs<2>(0).IsStaticallyKnownNoCheckNeeded());
            TestAssert(node->GetInputEdgeForNodeWithFixedNumInputs<2>(0).GetUseKind() == UseKind_KnownUnboxedInt64);
            break;
        }
        case NodeKind_SetLocal:
        {
            Edge& e = node->GetSoleInput();
            LogicalVariableInfo* info = node->GetLogicalVariable();
            if (e.IsStaticallyKnownNoCheckNeeded())
            {
                // This node is writing a statically known unboxed value to the local
                // This node does not need to speculate anything, and the local is now potentially tOpaque
                //
                info->m_speculationMask = info->m_speculationMask.Cup(x_typeMaskFor<tOpaque>);
            }
            else
            {
                TypeMask prediction = GetRawPredictionForEdge(e);

                // If the input is guaranteed to see only boxed value, we can filter out the prediction for non-boxed values
                //
                if (!e.MaybeInvalidBoxedValue())
                {
                    prediction = prediction.Cap(x_typeMaskFor<tBoxedValueTop>);
                }

                // If the input might be non-boxed-values, we cannot speculate anything to filter on the type of the boxed
                // values (since we may see a non-boxed-value), so the local can only be speculated to be FullTop.
                //
                // Note that under our requirement for tOpaque, each output of the bytecode is statically known
                // to either be a valid boxed value (including tGarbage), or a tOpaque.
                // So the prediction propagation result is a proof on whether a value might be tOpaque at runtime:
                // if the prediction says it's not a tOpaque, it never will at runtime.
                //
                if (prediction.OverlapsWith(x_typeMaskFor<tOpaque>))
                {
                    TestAssert(e.GetUseKind() == UseKind_FullTop);
                    info->m_speculationMask = info->m_speculationMask.Cup(prediction.Cup(x_typeMaskFor<tBoxedValueTop>));
                }
                else
                {
                    // TODO: ideally we should support "nil or XX" speculation.
                    // Now we compile tGarbage to tNil, but the minimal speculation that can hold both is usually just tBoxedValueTop
                    //
                    TypeMask mask = prediction.Cap(x_typeMaskFor<tBoxedValueTop>);
                    if (prediction.OverlapsWith(x_typeMaskFor<tGarbage>))
                    {
                        // tGarbage is compiled to tNil in DFG
                        //
                        mask = mask.Cup(x_typeMaskFor<tNil>);
                    }
                    TestAssert(mask.SubsetOf(x_typeMaskFor<tBoxedValueTop>));
                    TypeMaskOrd specOrd = GetMinimalSpeculationCoveringPredictionMask(mask);
                    TypeMask specMask = GetTypeMaskFromOrd(specOrd);
                    TestAssert(mask.SubsetOf(specMask));
                    info->m_speculationMask = info->m_speculationMask.Cup(specMask);
                    m_setLocals.push_back(std::make_pair(node, specOrd));
                }
            }
            break;
        }
        case NodeKind_CheckU64InBound:
        case NodeKind_I64SubSaturateToZero:
        {
            // These nodes operate on values statically known to be uint64_t
            //
            TestAssert(node->GetSoleInput().IsStaticallyKnownNoCheckNeeded());
            TestAssert(node->GetSoleInput().GetUseKind() == UseKind_KnownUnboxedInt64);
            break;
        }
        case NodeKind_CreateFunctionObject:
        {
            // The first two inputs are statically known to be uint64_t, no speculations needed for the other inputs
            //
            TestAssert(node->GetNumInputs() >= 2);
            TestAssert(node->GetInputEdge(0).IsStaticallyKnownNoCheckNeeded());
            TestAssert(node->GetInputEdge(0).GetUseKind() == UseKind_KnownUnboxedInt64);
            TestAssert(node->GetInputEdge(1).IsStaticallyKnownNoCheckNeeded());
            TestAssert(node->GetInputEdge(1).GetUseKind() == UseKind_KnownUnboxedInt64);
            break;
        }
        }   /*switch*/
    }

    void RunOnGraph(Graph* graph)
    {
        for (LogicalVariableInfo* info : graph->GetAllLogicalVariables())
        {
            info->m_speculationMask = x_typeMaskFor<tBottom>;
        }

        for (BasicBlock* bb : graph->m_blocks)
        {
            for (Node* node : bb->m_nodes)
            {
                if (node->IsBuiltinNodeKind())
                {
                    ProcessBuiltinNode(node);
                }
                else
                {
                    ProcessGuestLanguageNode(node);
                }
            }
        }

        // TODO:
        //     Think about if we should widen the speculation of the locals to a more useful set.
        //     Currently the speculation is simply the union of all SetLocal speculations,
        //     but this might not be useful: the goal is to eliminate or strength-reduce checks
        //     that uses the GetLocal values. If the speculation mask of the local cannot help
        //     with the goal, it's better to not pay the cost of speculating on the SetLocals at all.
        //
        // TODO: should also special case the tDoubleNotNaN verus tDouble case here
        //
        for (auto& it : m_setLocals)
        {
            Node* node = it.first;
            TypeMaskOrd specOrd = it.second;

            TestAssert(node->IsSetLocalNode());
            LogicalVariableInfo* info = node->GetLogicalVariable();

            UseKind useKind = GetCheapestSpecWithinMaskCoveringExistingSpec(specOrd, info->m_speculationMask);
            node->GetSoleInput().SetUseKind(useKind);
        }
    }

    TempArenaAllocator m_tempAlloc;
    TempVector<std::pair<ArenaPtr<Node>, TypeMaskOrd>> m_setLocals;
};

}   // anonymous namespace

void RunSpeculationAssignmentPass(Graph* graph)
{
    SpeculationAssignmentPass::Run(graph);
}

}   // namespace dfg
