#include "common_utils.h"
#include "deegen/deegen_options.h"
#include "dfg_speculative_inliner.h"
#include "dfg_speculative_inliner_heuristic.h"
#include "constexpr_array_builder_helper.h"
#include "dfg_basic_block_builder.h"
#include "dfg_frontend.h"
#include "generated/deegen_dfg_jit_all_generated_info.h"

namespace dfg {

#define macro(e) , PP_CAT(build_bytecode_speculative_inline_info_array_, e)
constexpr std::array<BytecodeSpeculativeInliningInfo, DeegenBytecodeBuilder::BytecodeDecoder::GetTotalBytecodeKinds()> x_dfgSpeculativeInliningTraits =
    constexpr_multipart_array_builder_helper<
        BytecodeSpeculativeInliningInfo,
        DeegenBytecodeBuilder::BytecodeDecoder::GetTotalBytecodeKinds()
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_VARIANT_NAMES)
    >::get();
#undef macro

static_assert([]() {
    constexpr size_t limit = (1ULL << 31) - (16ULL << 20) - 1024;
    if (x_forbid_tier_up_to_dfg_num_bytecodes_threshold > limit / 4) { return false; }
    size_t numBits = 0, val = x_forbid_tier_up_to_dfg_num_bytecodes_threshold;
    while (val != 0) { numBits++; val /= 2; }
    size_t rem = limit >> numBits;
    return SpeculativeInliner::x_maxTotalInliningFramesAllowed + 2 < rem;
}(), "Please decrease x_forbid_tier_up_to_dfg_num_bytecodes_threshold or x_maxTotalInliningFramesAllowed");

SpeculativeInliner::SpeculativeInliner(TempArenaAllocator& alloc,
                                       DeegenBytecodeBuilder::BytecodeDecoder* decoder,
                                       DfgBuildBasicBlockContext* bbContext,
                                       InlinedCallFrame* inlinedCallFrame)
    : m_bcTraitArray(x_dfgSpeculativeInliningTraits.data())
    , m_decoder(decoder)
    , m_bbContext(bbContext)
    , m_graph(bbContext->m_graph)
    , m_inlinedCallFrame(inlinedCallFrame)
    , m_baselineCodeBlock(inlinedCallFrame->GetCodeBlock()->m_baselineCodeBlock)
    , m_tempInputEdgeRes(alloc)
    , m_remainingInlineBudget(0)
{
    m_tempBv.Reset(alloc, inlinedCallFrame->GetCodeBlock()->m_stackFrameNumSlots);
    if (inlinedCallFrame->IsRootFrame())
    {
        if (m_baselineCodeBlock->m_numBytecodes >= SpeculativeInlinerHeuristic::x_disableAllInliningCutOff)
        {
            m_remainingInlineBudget = 0;
        }
        else
        {
            m_remainingInlineBudget = SpeculativeInlinerHeuristic::x_inlineBudgetForRootFunction;
        }
    }
    else if (inlinedCallFrame->IsDirectCall())
    {
        m_remainingInlineBudget = SpeculativeInlinerHeuristic::x_inlineBudgetForInlinedDirectCall;
    }
    else
    {
        m_remainingInlineBudget = SpeculativeInlinerHeuristic::x_inlineBudgetForInlinedClosureCall;
    }
}

const BytecodeSpeculativeInliningInfo* SpeculativeInliner::GetBytecodeSpeculativeInliningInfoArray()
{
    return x_dfgSpeculativeInliningTraits.data();
}

// Check if the bytecode has seen exactly one (closure or direct) call target.
// If so, returns the call site where the target was seen. Otherwise, return -1.
//
static size_t WARN_UNUSED FindMonomorphicSpeculativeInliningCallSite(JitCallInlineCacheSite* icSiteList, size_t numIcSites)
{
    size_t candidate = static_cast<size_t>(-1);
    for (size_t i = 0; i < numIcSites; i++)
    {
        if (icSiteList[i].ObservedNoTarget())
        {
            continue;
        }
        if (icSiteList[i].ObservedExactlyOneTarget())
        {
            if (candidate != static_cast<size_t>(-1))
            {
                return static_cast<size_t>(-1);
            }
            candidate = i;
        }
        else
        {
            return static_cast<size_t>(-1);
        }
    }
    TestAssertImp(candidate != static_cast<size_t>(-1), icSiteList[candidate].ObservedExactlyOneTarget());
    return candidate;
}

static size_t ComputeInliningCostForFunction(InlinedCallFrame* activeInlineFrame,
                                             BaselineCodeBlock* targetBcb,
                                             size_t remainingInlineBudget)
{
    size_t infiniteCost = 1000000000;

    if (targetBcb->m_numBytecodes > remainingInlineBudget)
    {
        return infiniteCost;
    }

    CodeBlock* targetCb = targetBcb->m_owner;

    {
        size_t depth = 0;
        size_t recursiveDepth = 0;
        InlinedCallFrame* frame = activeInlineFrame;
        while (true)
        {
            if (frame->GetCodeBlock() == targetCb)
            {
                recursiveDepth++;
            }
            depth++;
            if (frame->IsRootFrame())
            {
                break;
            }
            frame = frame->GetCallerCodeOrigin().GetInlinedCallFrame();
        }

        if (depth > SpeculativeInlinerHeuristic::x_maximumInliningDepth)
        {
            return infiniteCost;
        }

        if (recursiveDepth > SpeculativeInlinerHeuristic::x_maximumRecursiveInliningCount)
        {
            return infiniteCost;
        }
    }

    return targetBcb->m_numBytecodes;
}

bool WARN_UNUSED SpeculativeInliner::TrySpeculativeInliningSlowPath(Node* prologue, size_t bcOffset, size_t bcIndex, size_t opcode, InliningResultInfo& inlineResultInfo /*out*/)
{
    const BytecodeSpeculativeInliningInfo& siInfo = m_bcTraitArray[opcode];
    TestAssert(siInfo.m_isInitialized && siInfo.m_numCallSites > 0 && siInfo.m_info != nullptr);

    size_t callSiteTargetOrdinal;
    JitCallInlineCacheSite* callSiteTarget;
    {
        uint8_t* icSiteListVoid = m_baselineCodeBlock->GetSlowPathDataAtBytecodeIndex(bcIndex) + siInfo.m_callIcOffsetInSlowPathData;
        JitCallInlineCacheSite* icSiteList = reinterpret_cast<JitCallInlineCacheSite*>(icSiteListVoid);
        callSiteTargetOrdinal = FindMonomorphicSpeculativeInliningCallSite(icSiteList, siInfo.m_numCallSites);
        if (callSiteTargetOrdinal == static_cast<size_t>(-1))
        {
            // Not a monomorphic (direct or closure) call, give up
            //
            return false;
        }
        TestAssert(callSiteTargetOrdinal < siInfo.m_numCallSites);
        callSiteTarget = icSiteList + callSiteTargetOrdinal;
    }

    const BytecodeSpeculativeInliningTrait* trait = siInfo.m_info[callSiteTargetOrdinal];
    if (trait == nullptr)
    {
        // This call site is not eligible for inlining, give up
        //
        return false;
    }

    // Figure out the call target
    //
    TestAssert(callSiteTarget->ObservedExactlyOneTarget());
    TestAssert(!TCGet(callSiteTarget->m_linkedListHead).IsInvalidPtr());
    VM* vm = VM::GetActiveVMForCurrentThread();
    JitCallInlineCacheEntry* callTarget = TranslateToRawPointer(vm, TCGet(callSiteTarget->m_linkedListHead).AsPtr());
    TestAssert(callTarget->m_callSiteNextNode.IsInvalidPtr());
    ExecutableCode* calleeEc = callTarget->GetTargetExecutableCode(vm);

    if (!calleeEc->IsBytecodeFunction())
    {
        // The call target is not a bytecode function, nothing to inline
        // TODO: think about speculatively inlining library functions later
        //
        return false;
    }

    CodeBlock* calleeCb = static_cast<CodeBlock*>(calleeEc);
    BaselineCodeBlock* calleeBcb = calleeCb->m_baselineCodeBlock;
    if (calleeBcb == nullptr)
    {
        // The call target has not tiered up to baseline JIT tier, not helpful to inline as we won't have profile data
        //
        return false;
    }

    if (m_graph->GetNumInlinedCallFrames() >= x_maxTotalInliningFramesAllowed)
    {
        // Too many inlined functions already, give up
        //
        return false;
    }

    // At this point, we know we are able to inline this function.
    // Now determine if we should inline based on heuristic
    //
    {
        size_t inliningCost = ComputeInliningCostForFunction(m_inlinedCallFrame, calleeBcb, m_remainingInlineBudget);
        if (inliningCost > m_remainingInlineBudget)
        {
            return false;
        }

        // At this point we are committed to inlining this function
        //
        m_remainingInlineBudget -= inliningCost;
    }

    TestAssert(m_bbContext->m_isOSRExitOK);

    // The allocator used for all the temporary data structures during inlining of this function
    //
    TempArenaAllocator alloc;

#ifdef TESTBUILD
    // If this is a tail call, no local value should have been captured (bytecode should have properly generated
    // UpvalueClose bytecode to close all of them). If not, the bytecode is illegal.
    //
    if (trait->m_isTailCall)
    {
        for (size_t localOrd = 0; localOrd < m_baselineCodeBlock->m_stackFrameNumSlots; localOrd++)
        {
            TestAssert(!m_bbContext->m_isLocalCaptured[localOrd]);
        }
    }
#endif

    // The prologue node should no longer clobber VR since it is not effectful
    //
    prologue->SetNodeClobbersVR(false);

    // The prologue node is now a straightline node (the original node might not be!)
    //
    prologue->SetNodeIsBarrier(false);
    prologue->SetNodeHasBranchTarget(false);
    prologue->SetNodeMakesTailCallNotConsideringTransform(false);

    prologue->SetNodeInlinerSpecialization(true /*isPrologue*/, SafeIntegerCast<uint8_t>(callSiteTargetOrdinal));

    bool isOriginalNodeAccessesVR = prologue->IsNodeAccessesVR();
    TestAssertImp(trait->m_appendsVariadicResultsToArgs, prologue->IsNodeAccessesVR());
    if (prologue->IsNodeAccessesVR())
    {
        TestAssert(m_bbContext->m_currentVariadicResult != nullptr);
        TestAssert(reinterpret_cast<uint64_t>(m_bbContext->m_currentVariadicResult) != 1);
        TestAssert(m_bbContext->m_currentVariadicResult == prologue->GetVariadicResultInputNode());
    }

    TestAssert(callSiteTarget->m_mode == JitCallInlineCacheSite::Mode::DirectCall ||
               callSiteTarget->m_mode == JitCallInlineCacheSite::Mode::ClosureCall);
    bool isDirectCall = (callSiteTarget->m_mode == JitCallInlineCacheSite::Mode::DirectCall);

    BytecodeSpeculativeInliningTrait::RangeArgInfo rangeArgInfo;
    if (trait->m_hasRangeArgs)
    {
        rangeArgInfo = trait->m_rangeInfoGetter(m_decoder, bcOffset);
        TestAssert(rangeArgInfo.m_rangeStart >= x_numSlotsForStackFrameHeader);
        TestAssert(rangeArgInfo.m_rangeStart + rangeArgInfo.m_rangeLen <= m_baselineCodeBlock->m_stackFrameNumSlots);

#ifdef TESTBUILD
        // If this is a in-place call, all the locals in the range of the in-place call must not have been captured.
        // Assert this. If not, the bytecode illegal.
        //
        if (trait->m_isInPlaceCall)
        {
            for (size_t localOrd = rangeArgInfo.m_rangeStart - x_numSlotsForStackFrameHeader; localOrd < m_baselineCodeBlock->m_stackFrameNumSlots; localOrd++)
            {
                TestAssert(!m_bbContext->m_isLocalCaptured[localOrd]);
            }
        }
#endif
    }

    size_t numStaticArgs = trait->m_numExtraOutputs;
    if (trait->m_hasRangeArgs)
    {
        numStaticArgs += rangeArgInfo.m_rangeLen;
    }

    // Figure out information about the arguments and variadic arguments
    //
    bool isVariadicResStatic = false;
    size_t numStaticVariadicRes = 0;
    if (trait->m_appendsVariadicResultsToArgs)
    {
        if (m_bbContext->m_currentVariadicResult->GetNodeKind() == NodeKind_CreateVariadicRes)
        {
            Node* dynNumVarRes = m_bbContext->m_currentVariadicResult->GetInputEdge(0).GetOperand();
            if (dynNumVarRes->IsConstantNode() && dynNumVarRes->GetConstantNodeValue().m_value == 0)
            {
                isVariadicResStatic = true;
                numStaticVariadicRes = dynNumVarRes->GetNodeParamAsUInt64();
                TestAssert(numStaticVariadicRes + 1 == dynNumVarRes->GetNumInputs());
            }
        }
    }

    bool canStaticallyDetermineNumVarArgs;
    size_t maxNumVarArgs;
    if (!calleeCb->m_hasVariadicArguments)
    {
        canStaticallyDetermineNumVarArgs = true;
        maxNumVarArgs = 0;
    }
    else if (!trait->m_appendsVariadicResultsToArgs)
    {
        // 'numStaticArgs' is just how many arguments we are passing to the callee
        //
        canStaticallyDetermineNumVarArgs = true;
        if (numStaticArgs >= calleeCb->m_numFixedArguments)
        {
            maxNumVarArgs = numStaticArgs - calleeCb->m_numFixedArguments;
        }
        else
        {
            maxNumVarArgs = 0;
        }
    }
    else
    {
        if (isVariadicResStatic)
        {
            canStaticallyDetermineNumVarArgs = true;
            if (numStaticArgs + numStaticVariadicRes >= calleeCb->m_numFixedArguments)
            {
                maxNumVarArgs = numStaticArgs + numStaticVariadicRes - calleeCb->m_numFixedArguments;
            }
            else
            {
                maxNumVarArgs = 0;
            }
        }
        else
        {
            canStaticallyDetermineNumVarArgs = false;
            if (numStaticArgs >= calleeCb->m_numFixedArguments)
            {
                maxNumVarArgs = numStaticArgs - calleeCb->m_numFixedArguments;
            }
            else
            {
                maxNumVarArgs = 0;
            }
            size_t profileSeenData = m_baselineCodeBlock->m_maxObservedNumVariadicArgs;
            maxNumVarArgs = std::max(maxNumVarArgs, profileSeenData);
        }
    }

    // Reset the input edges if the trait tells us a reduced set
    //
    if (trait->m_hasReducedReadInfo)
    {
        trait->m_prologueReadInfoGetter(m_decoder, bcOffset, m_tempBv, m_tempInputEdgeRes /*out*/);
        prologue->ResetNumInputs(m_tempInputEdgeRes.size());
        for (size_t i = 0; i < m_tempInputEdgeRes.size(); i++)
        {
            size_t localOrd = m_tempInputEdgeRes[i];
            Value val = m_bbContext->GetLocalVariableValue(localOrd);
            prologue->GetInputEdge(static_cast<uint32_t>(i)) = Edge(val);
        }
        // The prologue must not access VR and VA, as that would be considered an effectful operation by our check
        //
        prologue->SetNodeAccessesVR(false);
        prologue->SetNodeAccessesVA(false);
    }

    // If we cannot statically know how many variadic args we have, emit OSR speculation that checks the
    // runtime value is no larger than the maximum we can accept here.
    //
    Node* getVRLengthNode = nullptr;
    if (!canStaticallyDetermineNumVarArgs)
    {
        TestAssert(maxNumVarArgs + calleeCb->m_numFixedArguments >= numStaticArgs);
        size_t maxVariadicResultsAllowed = maxNumVarArgs + calleeCb->m_numFixedArguments - numStaticArgs;
        getVRLengthNode = Node::CreateGetNumVariadicResNode(m_bbContext->m_currentVariadicResult);
        m_bbContext->SetupNodeCommonInfoAndPushBack(getVRLengthNode);
        Node* checkInBoundNode = Node::CreateCheckU64InBoundNode(Value(getVRLengthNode, 0 /*outputOrd*/), maxVariadicResultsAllowed);
        m_bbContext->SetupNodeCommonInfoAndPushBack(checkInBoundNode);
    }

    m_bbContext->SetupNodeCommonInfoAndPushBack(prologue);

    // The prologue node produces the function object (an unboxed pointer) if it is a closure call,
    // and it also produces all the singleton arguments passed to the call
    //
    prologue->SetNumOutputs(!isDirectCall /*hasDirectOutput*/, trait->m_numExtraOutputs);

    // The value of each argument in the call
    //
    TempVector<Value> argValues(alloc);
    size_t argValueListLength = calleeCb->m_numFixedArguments + maxNumVarArgs;
    argValues.reserve(argValueListLength);

    // Populate all the non-variadic-result arguments into the argument list
    //
    {
        size_t rangeLocInArgs = (trait->m_hasRangeArgs) ? trait->m_rangeLocationInArgs : 0;
        for (size_t i = 0; i < rangeLocInArgs; i++)
        {
            if (argValues.size() == argValueListLength) { break; }
            argValues.push_back(Value(prologue, SafeIntegerCast<uint16_t>(i + 1)));
        }

        if (trait->m_hasRangeArgs)
        {
            for (size_t i = 0; i < rangeArgInfo.m_rangeLen; i++)
            {
                if (argValues.size() == argValueListLength) { break; }
                argValues.push_back(m_bbContext->GetLocalVariableValue(rangeArgInfo.m_rangeStart + i));
            }
        }

        for (size_t i = rangeLocInArgs; i < trait->m_numExtraOutputs; i++)
        {
            if (argValues.size() == argValueListLength) { break; }
            argValues.push_back(Value(prologue, SafeIntegerCast<uint16_t>(i + 1)));
        }
    }

    // Populate all the arguments from variadic results into the argument list
    //
    if (trait->m_appendsVariadicResultsToArgs)
    {
        Node* vrNode = m_bbContext->m_currentVariadicResult;
        if (vrNode->GetNodeKind() == NodeKind_CreateVariadicRes)
        {
            // CreateVariadicRes has the nice property that all the "extra" nodes must have nil value,
            // so we can simply append everything to the argument list (as the unpopulated arguments
            // ought to get nil anyway).
            //
            for (uint32_t i = 1; i < vrNode->GetNumInputs(); i++)
            {
                if (argValues.size() == argValueListLength) { break; }
                argValues.push_back(vrNode->GetInputEdge(i).GetValue());
            }
        }
        else
        {
            // Create GetKthVariadicResult node for each remaining argument in the argument list
            //
            size_t curVarResOrd = 0;
            while (argValues.size() < argValueListLength)
            {
                Node* argNode = Node::CreateGetKthVariadicResNode(curVarResOrd, vrNode);
                curVarResOrd++;
                argValues.push_back(Value(argNode, 0 /*outputOrd*/));
                m_bbContext->SetupNodeCommonInfoAndPushBack(argNode);
            }
        }
    }

    // If the argument list is still not long enough, populate nil
    //
    if (argValues.size() < argValueListLength)
    {
        Value nilConstant = m_graph->GetConstant(TValue::Create<tNil>());
        while (argValues.size() < argValueListLength)
        {
            argValues.push_back(nilConstant);
        }
    }

    TestAssert(argValues.size() == argValueListLength);

    // Calculate the interpreter stack frame layout for the new frame
    //
    size_t newFrameInterpreterBaseOrd;
    if (trait->m_isTailCall)
    {
        // We already need a memcpy to move the whole frame from the scratchpad to the real stack on OSR exit,
        // so this doesn't really matter. To make the interpreter stack shorter if possible, we simply let the
        // new frame sit on the old frame.
        //
        if (m_inlinedCallFrame->IsRootFrame())
        {
            newFrameInterpreterBaseOrd = maxNumVarArgs + x_numSlotsForStackFrameHeader;
        }
        else
        {
            size_t currentFrameStart = m_inlinedCallFrame->GetInterpreterSlotForFrameStart().Value();
            newFrameInterpreterBaseOrd = currentFrameStart + maxNumVarArgs + x_numSlotsForStackFrameHeader;
        }
    }
    else if (trait->m_isInPlaceCall)
    {
        // For in-place call, the stack frame may start at where the in-place call starts
        //
        TestAssert(trait->m_hasRangeArgs);
        TestAssert(rangeArgInfo.m_rangeStart >= x_numSlotsForStackFrameHeader);
        newFrameInterpreterBaseOrd = m_inlinedCallFrame->GetInterpreterSlotForStackFrameBase().Value() + rangeArgInfo.m_rangeStart + maxNumVarArgs;
    }
    else
    {
        // The stack frame must start at the end of the current call frame
        //
        size_t currentFrameEnd = m_inlinedCallFrame->GetInterpreterSlotForFrameEnd().Value();
        newFrameInterpreterBaseOrd = currentFrameEnd + maxNumVarArgs + x_numSlotsForStackFrameHeader;
    }

    TestAssert(newFrameInterpreterBaseOrd >= maxNumVarArgs + x_numSlotsForStackFrameHeader);

    // Compute the DFG virtual register mapping for the new frame
    //
    DfgTranslateFunctionContext tfCtx(alloc);
    tfCtx.m_graph = m_graph;
    m_bbContext->m_tfCtx.m_vrState.CopyStateTo(tfCtx.m_vrState /*out*/);

    // For tail call or in-place call, we can salvage the range-argument part and reuse those registers for the new frame
    //
    bool canReuseRangeArgRegs = false;
    if (trait->m_hasRangeArgs && (trait->m_isTailCall || trait->m_isInPlaceCall))
    {
        canReuseRangeArgRegs = true;
    }

    // Records the list of virtual registers that we temporarily yielded ownership to the callee
    // When the callee returns, we must initialize these virtual registers to UndefValue if they
    // were actually used by the callee (or any further nestedly inlined callee)
    // This is because we allow bytecodes to use uninitialized slots, but they must not see the stale
    // values in the callee, otherwise our invariant that every SetLocal that can flow to a GetLocal
    // must have the same interpreter slot could be broken.
    //
    TempUnorderedSet<size_t /*virtualRegisterOrd*/> vregYieldedToCallee(alloc);
    auto deallocateCallerRegisterForCallee = [&](VirtualRegister vreg) ALWAYS_INLINE
    {
        tfCtx.m_vrState.Deallocate(vreg);
        TestAssert(!vregYieldedToCallee.count(vreg.Value()));
        vregYieldedToCallee.insert(vreg.Value());
    };

    // Free up invalidated virtual registers in the current call frame, to make the total # of virtual registers as small as possible
    //
    if (trait->m_isTailCall)
    {
        // For tail call, free every virtual register except those to be reused for the new frame
        //
        if (!m_inlinedCallFrame->IsRootFrame())
        {
            for (size_t i = 0; i < m_inlinedCallFrame->MaxVarArgsAllowed(); i++)
            {
                deallocateCallerRegisterForCallee(m_inlinedCallFrame->GetRegisterForVarArgOrd(i));
            }
            if (!m_inlinedCallFrame->IsDirectCall())
            {
                deallocateCallerRegisterForCallee(m_inlinedCallFrame->GetClosureCallFunctionObjectRegister());
            }
            if (!m_inlinedCallFrame->StaticallyKnowsNumVarArgs())
            {
                deallocateCallerRegisterForCallee(m_inlinedCallFrame->GetNumVarArgsRegister());
            }
        }
        for (size_t localOrd = 0; localOrd < m_baselineCodeBlock->m_stackFrameNumSlots; localOrd++)
        {
            // If we can reuse range args,
            // and the ordinal is indeed one of the range args,
            // and the ordinal is passed to the callee as argument or vararg without being discarded,
            // then the virtual register can be reused
            //
            if (canReuseRangeArgRegs &&
                rangeArgInfo.m_rangeStart <= localOrd &&
                localOrd < rangeArgInfo.m_rangeStart + rangeArgInfo.m_rangeLen &&
                trait->m_rangeLocationInArgs + (localOrd - rangeArgInfo.m_rangeStart) < argValueListLength)
            {
                // This virtual register can be directly passed to callee
                //
                VirtualRegister vreg = m_inlinedCallFrame->GetRegisterForLocalOrd(localOrd);
                TestAssert(!vregYieldedToCallee.count(vreg.Value()));
                vregYieldedToCallee.insert(vreg.Value());
            }
            else
            {
                // This virtual register can be deallocated, so callee may use it if it wants to
                //
                deallocateCallerRegisterForCallee(m_inlinedCallFrame->GetRegisterForLocalOrd(localOrd));
            }
        }
    }
    else if (trait->m_isInPlaceCall)
    {
        // For in-place call, free every virtual register >= where the call starts except those to be reused for the new frame
        //
        TestAssert(trait->m_hasRangeArgs && canReuseRangeArgRegs);
        TestAssert(trait->m_rangeLocationInArgs == 0);
        TestAssert(rangeArgInfo.m_rangeStart >= x_numSlotsForStackFrameHeader);
        size_t localOrdForCallStart = rangeArgInfo.m_rangeStart - x_numSlotsForStackFrameHeader;
        for (size_t localOrd = localOrdForCallStart; localOrd < m_baselineCodeBlock->m_stackFrameNumSlots; localOrd++)
        {
            // Similar to above, if the ordinal is indeed one of the range args,
            // and the ordinal is passed to the callee as argument or vararg without being discarded,
            // then the virtual register can be reused
            //
            if (rangeArgInfo.m_rangeStart <= localOrd &&
                localOrd < rangeArgInfo.m_rangeStart + rangeArgInfo.m_rangeLen &&
                trait->m_rangeLocationInArgs + (localOrd - rangeArgInfo.m_rangeStart) < argValueListLength)
            {
                VirtualRegister vreg = m_inlinedCallFrame->GetRegisterForLocalOrd(localOrd);
                TestAssert(!vregYieldedToCallee.count(vreg.Value()));
                vregYieldedToCallee.insert(vreg.Value());
            }
            else
            {
                deallocateCallerRegisterForCallee(m_inlinedCallFrame->GetRegisterForLocalOrd(localOrd));
            }
        }
    }

    // Create and set up the new InlinedCallFrame
    //
    InlinedCallFrame* newFrame = InlinedCallFrame::CreateInlinedFrame(calleeCb,
                                                                      CodeOrigin(m_inlinedCallFrame, bcIndex),
                                                                      isDirectCall,
                                                                      trait->m_isTailCall,
                                                                      SafeIntegerCast<uint8_t>(callSiteTargetOrdinal),
                                                                      canStaticallyDetermineNumVarArgs,
                                                                      SafeIntegerCast<uint32_t>(maxNumVarArgs),
                                                                      InterpreterSlot(newFrameInterpreterBaseOrd));

    m_graph->RegisterNewInlinedCallFrame(newFrame);

    // Set up the virtual register mapping for each VarArg and local
    //
    if (canReuseRangeArgRegs)
    {
        // First set up the mapping for the argument sequence
        //
        for (size_t ordInArgSeq = 0; ordInArgSeq < argValueListLength; ordInArgSeq++)
        {
            VirtualRegister vreg;
            if (trait->m_rangeLocationInArgs <= ordInArgSeq && ordInArgSeq < trait->m_rangeLocationInArgs + rangeArgInfo.m_rangeLen)
            {
                size_t localOrdInCurrentFrame = rangeArgInfo.m_rangeStart + (ordInArgSeq - trait->m_rangeLocationInArgs);
                vreg = m_inlinedCallFrame->GetRegisterForLocalOrd(localOrdInCurrentFrame);
            }
            else
            {
                vreg = tfCtx.m_vrState.Allocate();
            }
            if (ordInArgSeq < calleeCb->m_numFixedArguments)
            {
                newFrame->SetRegisterForLocalOrd(ordInArgSeq, vreg);
            }
            else
            {
                newFrame->SetRegisterForVarArgOrd(ordInArgSeq - calleeCb->m_numFixedArguments, vreg);
            }
        }

        // Now set up the mapping for the remaining locals
        //
        TestAssert(calleeCb->m_stackFrameNumSlots >= calleeCb->m_numFixedArguments);
        for (size_t localOrd = calleeCb->m_numFixedArguments; localOrd < calleeCb->m_stackFrameNumSlots; localOrd++)
        {
            newFrame->SetRegisterForLocalOrd(localOrd, tfCtx.m_vrState.Allocate());
        }
    }
    else
    {
        for (size_t localOrd = 0; localOrd < calleeCb->m_stackFrameNumSlots; localOrd++)
        {
            newFrame->SetRegisterForLocalOrd(localOrd, tfCtx.m_vrState.Allocate());
        }
        for (size_t varArgOrd = 0; varArgOrd < maxNumVarArgs; varArgOrd++)
        {
            newFrame->SetRegisterForVarArgOrd(varArgOrd, tfCtx.m_vrState.Allocate());
        }
    }

    if (!canStaticallyDetermineNumVarArgs)
    {
        newFrame->SetNumVarArgsRegister(tfCtx.m_vrState.Allocate());
    }

    if (!isDirectCall)
    {
        newFrame->SetClosureCallFunctionObjectRegister(tfCtx.m_vrState.Allocate());
    }
    else
    {
        TestAssert(callTarget->m_entity.IsUserHeapPointer());
        TestAssert(callTarget->m_entity.As<UserHeapGcObjectHeader>()->m_type == HeapEntityType::Function);
        FunctionObject* targetFunctionObject = TranslateToRawPointer(vm, callTarget->m_entity.As<FunctionObject>());
        newFrame->SetDirectCallFunctionObject(targetFunctionObject);
    }

    tfCtx.m_inlinedCallFrame = newFrame;

    // Having set up everything for newFrame, assert that the virtual register mapping is consistent
    //
    newFrame->AssertVirtualRegisterConsistency(tfCtx.m_vrState);

    newFrame->InitializeVirtualRegisterUsageArray(tfCtx.m_vrState.GetVirtualRegisterVectorLength());

    // Update the max # of locals in the graph
    //
    tfCtx.m_graph->UpdateTotalNumLocals(tfCtx.m_vrState.GetVirtualRegisterVectorLength());

    // Initialize the new call frame. As usual, we need to emit all the ShadowStores first and then SetLocals.
    //
    // Emit ShadowStore for the new stack frame header. Our protocol is:
    //     hdr[0]: an unboxed HeapPtr<FunctionObject>, the function object of the call
    //     hdr[1]: an unboxed uint64_t, the number of variadic arguments
    //     hdr[2]: an unboxed void* pointer, the return address (points to baseline JIT code)
    //     hdr[3]: an unboxed uint64_t, the interpreter base slot number for the parent frame
    //
    Value calleeFunctionObjectValue;
    if (newFrame->IsDirectCall())
    {
        FunctionObject* fo = newFrame->GetDirectCallFunctionObject();
        // Note that the stack frame expects a HeapPtr<FunctionObject>, not FunctionObject*
        //
        calleeFunctionObjectValue = m_graph->GetUnboxedConstant(reinterpret_cast<uint64_t>(TranslateToHeapPtr(fo)));
    }
    else
    {
        // For closure call, the function object is the direct output of the prologue
        //
        calleeFunctionObjectValue = Value(prologue, 0 /*outputOrd*/);
    }

    Value numVariadicArgumentsValue;
    if (newFrame->StaticallyKnowsNumVarArgs())
    {
        numVariadicArgumentsValue = m_graph->GetUnboxedConstant(newFrame->GetNumVarArgs());
    }
    else
    {
        TestAssert(getVRLengthNode != nullptr);
        int64_t valToSubtract = static_cast<int64_t>(calleeCb->m_numFixedArguments) - static_cast<int64_t>(numStaticArgs);
        Node* numVarArgsNode = Node::CreateU64SaturateSubNode(Value(getVRLengthNode, 0 /*outputOrd*/), valToSubtract);
        m_bbContext->SetupNodeCommonInfoAndPushBack(numVarArgsNode);
        numVariadicArgumentsValue = Value(numVarArgsNode, 0 /*outputOrd*/);
    }

    Value sfHdrReturnAddrValue;
    Value sfHdrCallerInterpreterBaseSlot;
    {
        InlinedCallFrame* parentFrameForReturn = newFrame->GetParentFrameForReturn();
        if (parentFrameForReturn == nullptr)
        {
            TestAssert(trait->m_isTailCall);
            // Our frame should overtake the root frame, so the frame header value should be sentry values (see dfg_osr_restore_stack_frame_layout.h)
            //
            sfHdrCallerInterpreterBaseSlot = m_graph->GetUnboxedConstant((1ULL << 30) - 1);
            sfHdrReturnAddrValue = m_graph->GetUnboxedConstant(0);
        }
        else
        {
            sfHdrCallerInterpreterBaseSlot = m_graph->GetUnboxedConstant(parentFrameForReturn->GetInterpreterSlotForStackFrameBase().Value());
            // TODO: FIXME: this needs to be the code pointer into the baseline JIT return continuation
            //
            sfHdrReturnAddrValue = m_graph->GetUnboxedConstant(0);
        }
    }

    // As soon as we start emitting ShadowStores, OSR exit is no longer allowed
    //
    m_bbContext->m_isOSRExitOK = false;

    // Create ShadowStore for everything in the stack frame header
    //
    {
        Node* shadowStore = Node::CreateShadowStoreNode(newFrame->GetInterpreterSlotForStackFrameHeader(0), calleeFunctionObjectValue);
        m_bbContext->SetupNodeCommonInfoAndPushBack(shadowStore);
    }
    {
        Node* shadowStore = Node::CreateShadowStoreNode(newFrame->GetInterpreterSlotForStackFrameHeader(1), numVariadicArgumentsValue);
        m_bbContext->SetupNodeCommonInfoAndPushBack(shadowStore);
    }
    {
        Node* shadowStore = Node::CreateShadowStoreNode(newFrame->GetInterpreterSlotForStackFrameHeader(2), sfHdrReturnAddrValue);
        m_bbContext->SetupNodeCommonInfoAndPushBack(shadowStore);
    }
    {
        Node* shadowStore = Node::CreateShadowStoreNode(newFrame->GetInterpreterSlotForStackFrameHeader(3), sfHdrCallerInterpreterBaseSlot);
        m_bbContext->SetupNodeCommonInfoAndPushBack(shadowStore);
    }

    // The helper function that populates argument value for each argument.
    // As usual, we need to emit all the ShadowStores first, then SetLocals
    //
    auto populateArgValuesForNewFrame = [&](bool emitShadowStore) ALWAYS_INLINE
    {
        for (size_t ordInArgSeq = 0; ordInArgSeq < argValueListLength; ordInArgSeq++)
        {
            InterpreterSlot destInterpSlot;
            VirtualRegister destVReg;
            if (ordInArgSeq < calleeCb->m_numFixedArguments)
            {
                destInterpSlot = newFrame->GetInterpreterSlotForLocalOrd(ordInArgSeq);
                destVReg = newFrame->GetRegisterForLocalOrd(ordInArgSeq);
            }
            else
            {
                destInterpSlot = newFrame->GetInterpreterSlotForVariadicArgument(ordInArgSeq - calleeCb->m_numFixedArguments);
                destVReg = newFrame->GetRegisterForVarArgOrd(ordInArgSeq - calleeCb->m_numFixedArguments);
            }

            if (canReuseRangeArgRegs && trait->m_rangeLocationInArgs <= ordInArgSeq && ordInArgSeq < trait->m_rangeLocationInArgs + rangeArgInfo.m_rangeLen)
            {
                // The local in the new frame and old frame should be using the same virtual register, assert this
                //
#ifdef TESTBUILD
                size_t oldLocalOrd = rangeArgInfo.m_rangeStart + ordInArgSeq - trait->m_rangeLocationInArgs;
                TestAssert(oldLocalOrd < m_baselineCodeBlock->m_stackFrameNumSlots);
                VirtualRegister oldVreg = m_inlinedCallFrame->GetRegisterForLocalOrd(oldLocalOrd);
                TestAssert(oldVreg.Value() == destVReg.Value());
                TestAssert(m_bbContext->m_valueAtTail[oldLocalOrd].m_node.m_value == argValues[ordInArgSeq].m_node.m_value);
                TestAssert(m_bbContext->m_valueAtTail[oldLocalOrd].m_outputOrd == argValues[ordInArgSeq].m_outputOrd);
#endif
                // However, we still must emit SetLocal, since their interpreter slots might be different,
                // so they must not use the same LogicalVariable!
                //
            }

            if (emitShadowStore)
            {
                Node* shadowStore = Node::CreateShadowStoreNode(destInterpSlot, argValues[ordInArgSeq]);
                m_bbContext->SetupNodeCommonInfoAndPushBack(shadowStore);
            }
            else
            {
                Node* setLocal = Node::CreateSetLocalNode(destVReg, destInterpSlot, argValues[ordInArgSeq]);
                m_bbContext->SetupNodeCommonInfoAndPushBack(setLocal);
            }
        }
    };

    // Emit shadow stores for each argument
    //
    populateArgValuesForNewFrame(true /*emitShadowStore*/);

    // We need to initialize each remaining local in the callee frame to Undef
    // This is required, since we allow bytecode to access uninitialized locals.. In that case, the bytecode could
    // see the stale local value in the parent function or in other inlined callees, and break our invariant that
    // for every SetLocal that can flow to a GetLocal, they must share the same interpreter slot.
    //
    // Technically we do not need to emit ShadowStores to initialize the interpreter stack frame to Undef as well,
    // but we emit them anyway so it is consistent that every SetLocal is always preceded by a ShadowStore.
    //
    for (size_t localOrd = calleeCb->m_numFixedArguments; localOrd < calleeCb->m_stackFrameNumSlots; localOrd++)
    {
        InterpreterSlot slot = newFrame->GetInterpreterSlotForLocalOrd(localOrd);
        Node* shadowStore = Node::CreateShadowStoreNode(slot, m_graph->GetUndefValue());
        m_bbContext->SetupNodeCommonInfoAndPushBack(shadowStore);
    }

    // All ShadowStores are complete, OSR exit is OK again
    //
    m_bbContext->m_isOSRExitOK = true;

    // Emit stores for each argument and the information about the call frame as needed
    //
    populateArgValuesForNewFrame(false /*emitShadowStore*/);

    // Initialize every remaining local to UndefValue, see comments earlier
    //
    for (size_t localOrd = calleeCb->m_numFixedArguments; localOrd < calleeCb->m_stackFrameNumSlots; localOrd++)
    {
        InterpreterSlot slot = newFrame->GetInterpreterSlotForLocalOrd(localOrd);
        VirtualRegister vreg = newFrame->GetRegisterForLocalOrd(localOrd);
        Node* setLocal = Node::CreateSetLocalNode(vreg, slot, m_graph->GetUndefValue());
        m_bbContext->SetupNodeCommonInfoAndPushBack(setLocal);
    }

    if (!newFrame->IsDirectCall())
    {
        Node* storeFnObj = Node::CreateSetLocalNode(newFrame->GetClosureCallFunctionObjectRegister(),
                                                    newFrame->GetInterpreterSlotForStackFrameHeader(0),
                                                    calleeFunctionObjectValue);
        m_bbContext->SetupNodeCommonInfoAndPushBack(storeFnObj);
    }

    if (!newFrame->StaticallyKnowsNumVarArgs())
    {
        Node* storeNumVarArgs = Node::CreateSetLocalNode(newFrame->GetNumVarArgsRegister(),
                                                         newFrame->GetInterpreterSlotForStackFrameHeader(1),
                                                         numVariadicArgumentsValue);
        m_bbContext->SetupNodeCommonInfoAndPushBack(storeNumVarArgs);
    }

#ifdef TESTBUILD
    if (newFrame->IsTailCall())
    {
        for (size_t localOrd = 0; localOrd < m_baselineCodeBlock->m_stackFrameNumSlots; localOrd++)
        {
            // At a tail call site, no local should be in captured state. Assert this.
            //
            TestAssert(!m_bbContext->m_isLocalCaptured[localOrd]);
            TestAssert(!m_bbContext->m_currentBBInfo->m_isLocalCapturedAtTail.IsSet(localOrd));
        }
    }
#endif

    // The stack frame initialization is complete at this point, we are ready to execute the real function logic now
    //
    // Translate the callee to IR graph
    //
    DfgTranslateFunctionResult tfRes = DfgTranslateFunction(tfCtx);

#ifdef TESTBUILD
    for (BasicBlock* bb : tfRes.m_allBBs)
    {
        TestAssert(bb->GetTerminator()->GetNumNodeControlFlowSuccessors() == bb->GetNumSuccessors());
    }
#endif

    // Update all the virtual registers usage after we've done generating IR for the callee
    //
    m_inlinedCallFrame->UpdateVirtualRegisterUsageArray(newFrame);

    // Finish up the current block, and let the current block jump to the entry point of the callee function
    //
    TestAssert(m_bbContext->m_currentBlock->m_nodes.size() > 0);
    m_bbContext->m_currentBlock->m_terminator = m_bbContext->m_currentBlock->m_nodes.back();
    m_bbContext->m_currentBlock->m_numSuccessors = 1;
    m_bbContext->m_currentBlock->m_successors[0] = tfRes.m_functionEntry;

    inlineResultInfo.m_nodeIndexOfReturnContinuation = static_cast<size_t>(-1);

    using TrivialRCKind = BytecodeSpeculativeInliningTrait::TrivialRCKind;

    // If the call is not a tail call, we need to convert all the untransformed tail calls in the callee to a normal call
    // that continues at our return continuation instead, and transform all the return nodes to pass results to our join block instead.
    //
    if (!newFrame->IsTailCall())
    {
        // Create the return continuation BB
        //
        BasicBlock* rcBB = DfgAlloc()->AllocateObject<BasicBlock>();
        m_bbContext->m_allBBs.push_back(rcBB);

        // The codeOrigin and exitDst used for trivial return continuation logic
        // Note that trivial return continuations must fallthrough to the next bytecode,
        // so the exitDstForTrivialRc is always the next bytecode.
        // However, if the rc is not trivial, the calling bytecode may branch, so one must not use exitDstForTrivialRc!
        //
        CodeOrigin codeOrigin = CodeOrigin(m_inlinedCallFrame, bcIndex);
        OsrExitDestination exitDstForTrivialRc = OsrExitDestination(false /*isBranchDest*/, CodeOrigin(m_inlinedCallFrame, bcIndex + 1));

        // One should never exit to here. This destination should only be used when m_exitOK is false (since every node needs an exitDest).
        //
        OsrExitDestination disallowedExitDst = OsrExitDestination(false /*isBranchDest*/, codeOrigin);

        BytecodeSpeculativeInliningTrait::TrivialRCInfo trivialRcInfo;
        if (trait->m_rcTrivialness == TrivialRCKind::ReturnKthResult || trait->m_rcTrivialness == TrivialRCKind::StoreFirstKResults)
        {
            trivialRcInfo = trait->m_trivialRCInfoGetter(m_decoder, bcOffset);
        }

        for (BasicBlock* bb : tfRes.m_allBBs)
        {
            if (bb->GetNumSuccessors() == 0)
            {
                Node* terminator = bb->GetTerminator();
                TestAssert(terminator == bb->m_nodes.back());
                if (terminator->IsNodeMakesTailCallNotConsideringTransform())
                {
                    TestAssert(!terminator->IsNodeTailCallTransformedToNormalCall());
                    terminator->SetNodeTailCallTransformedToNormalCall(true);
                    // The tail call node should produce variadic results now, since it has been converted to a normal call
                    //
                    TestAssert(!terminator->IsNodeGeneratesVR());
                    terminator->SetNodeGeneratesVR(true);

                    // If the return continuation is trivial with kind ReturnKthResult or StoreFirstKResults,
                    // we need to append logic that store the results to stack frame.
                    // Otherwise, 'terminator' already produces variadic results, so we are good.
                    //
                    if (trait->m_rcTrivialness == TrivialRCKind::ReturnKthResult)
                    {
                        TestAssert(m_decoder->BytecodeHasOutputOperand(bcOffset));
                        size_t localOrd = m_decoder->GetOutputOperand(bcOffset);
                        Node* getKthResult = Node::CreateGetKthVariadicResNode(trivialRcInfo.m_num, terminator);
                        getKthResult->SetExitOK(false);
                        getKthResult->SetNodeOrigin(codeOrigin);
                        getKthResult->SetOsrExitDest(disallowedExitDst);
                        bb->m_nodes.push_back(getKthResult);
                        Node* shadowStore = Node::CreateShadowStoreNode(m_inlinedCallFrame->GetInterpreterSlotForLocalOrd(localOrd),
                                                                        Value(getKthResult, 0 /*outputOrd*/));
                        shadowStore->SetExitOK(false);
                        shadowStore->SetNodeOrigin(codeOrigin);
                        shadowStore->SetOsrExitDest(disallowedExitDst);
                        bb->m_nodes.push_back(shadowStore);
                        Node* setLocal = Node::CreateSetLocalNode(m_inlinedCallFrame->GetRegisterForLocalOrd(localOrd),
                                                                  m_inlinedCallFrame->GetInterpreterSlotForLocalOrd(localOrd),
                                                                  Value(getKthResult, 0 /*outputOrd*/));
                        setLocal->SetExitOK(true);
                        setLocal->SetNodeOrigin(codeOrigin);
                        setLocal->SetOsrExitDest(exitDstForTrivialRc);
                        bb->m_nodes.push_back(setLocal);
                        bb->m_terminator = bb->m_nodes.back();
                    }
                    else if (trait->m_rcTrivialness == TrivialRCKind::StoreFirstKResults)
                    {
                        size_t getResStart = bb->m_nodes.size();
                        for (size_t i = 0; i < trivialRcInfo.m_num; i++)
                        {
                            Node* getKthResult = Node::CreateGetKthVariadicResNode(i, terminator);
                            getKthResult->SetExitOK(false);
                            getKthResult->SetNodeOrigin(codeOrigin);
                            getKthResult->SetOsrExitDest(disallowedExitDst);
                            bb->m_nodes.push_back(getKthResult);
                        }
                        for (size_t i = 0; i < trivialRcInfo.m_num; i++)
                        {
                            size_t localOrd = trivialRcInfo.m_rangeStart + i;
                            Node* getKthResult = bb->m_nodes[getResStart + i];
                            TestAssert(getKthResult->IsGetKthVariadicResNode());
                            Node* shadowStore = Node::CreateShadowStoreNode(m_inlinedCallFrame->GetInterpreterSlotForLocalOrd(localOrd),
                                                                            Value(getKthResult, 0 /*outputOrd*/));
                            shadowStore->SetExitOK(false);
                            shadowStore->SetNodeOrigin(codeOrigin);
                            shadowStore->SetOsrExitDest(disallowedExitDst);
                            bb->m_nodes.push_back(shadowStore);
                        }
                        for (size_t i = 0; i < trivialRcInfo.m_num; i++)
                        {
                            size_t localOrd = trivialRcInfo.m_rangeStart + i;
                            Node* getKthResult = bb->m_nodes[getResStart + i];
                            TestAssert(getKthResult->IsGetKthVariadicResNode());
                            Node* setLocal = Node::CreateSetLocalNode(m_inlinedCallFrame->GetRegisterForLocalOrd(localOrd),
                                                                      m_inlinedCallFrame->GetInterpreterSlotForLocalOrd(localOrd),
                                                                      Value(getKthResult, 0 /*outputOrd*/));
                            setLocal->SetExitOK(true);
                            setLocal->SetNodeOrigin(codeOrigin);
                            setLocal->SetOsrExitDest(exitDstForTrivialRc);
                            bb->m_nodes.push_back(setLocal);
                        }
                        TestAssert(bb->m_nodes.size() > 0);
                        bb->m_terminator = bb->m_nodes.back();
                    }

                    bb->m_numSuccessors = 1;
                    bb->m_successors[0] = rcBB;
                }
            }
        }

        // All the return instructions need to be transformed to pass results to rcBB instead
        //
        // Specifically, if the RC is ReturnKthResult or StoreFirstKResults,
        // we need to append logic that store the results to stack frame.
        // If the RC is not trivial or is StoreAllAsVariadicResults, we need to create the variadic result
        //
        for (BasicBlock* bb : tfRes.m_allBBs)
        {
            if (bb->GetNumSuccessors() == 0)
            {
                Node* terminator = bb->GetTerminator();
                TestAssert(terminator == bb->m_nodes.back());
                if (terminator->IsReturnNode())
                {
                    bb->m_nodes.pop_back();

                    auto getKthValueInReturnNode = [&](size_t k) WARN_UNUSED ALWAYS_INLINE -> Value
                    {
                        if (k >= terminator->GetNumInputs())
                        {
                            if (terminator->IsNodeAccessesVR())
                            {
                                Node* vrNode = terminator->GetVariadicResultInputNode();
                                TestAssert(vrNode != nullptr);
                                Node* getKthVarRes = Node::CreateGetKthVariadicResNode(k - terminator->GetNumInputs(), vrNode);
                                getKthVarRes->SetExitOK(false);
                                getKthVarRes->SetNodeOrigin(codeOrigin);
                                getKthVarRes->SetOsrExitDest(disallowedExitDst);
                                bb->m_nodes.push_back(getKthVarRes);
                                return Value(getKthVarRes, 0 /*outputOrd*/);
                            }
                            else
                            {
                                return m_graph->GetConstant(TValue::Create<tNil>());
                            }
                        }
                        else
                        {
                            return terminator->GetInputEdge(static_cast<uint32_t>(k)).GetValue();
                        }
                    };

                    if (trait->m_rcTrivialness == TrivialRCKind::ReturnKthResult)
                    {
                        Value val = getKthValueInReturnNode(trivialRcInfo.m_num);
                        TestAssert(m_decoder->BytecodeHasOutputOperand(bcOffset));
                        size_t localOrd = m_decoder->GetOutputOperand(bcOffset);
                        Node* shadowStore = Node::CreateShadowStoreNode(m_inlinedCallFrame->GetInterpreterSlotForLocalOrd(localOrd), val);
                        shadowStore->SetExitOK(false);
                        shadowStore->SetNodeOrigin(codeOrigin);
                        shadowStore->SetOsrExitDest(disallowedExitDst);
                        bb->m_nodes.push_back(shadowStore);
                        Node* setLocal = Node::CreateSetLocalNode(m_inlinedCallFrame->GetRegisterForLocalOrd(localOrd),
                                                                  m_inlinedCallFrame->GetInterpreterSlotForLocalOrd(localOrd),
                                                                  val);
                        setLocal->SetExitOK(true);
                        setLocal->SetNodeOrigin(codeOrigin);
                        setLocal->SetOsrExitDest(exitDstForTrivialRc);
                        bb->m_nodes.push_back(setLocal);
                    }
                    else if (trait->m_rcTrivialness == TrivialRCKind::StoreFirstKResults)
                    {
                        TempVector<Value> allValues(alloc);
                        for (size_t i = 0; i < trivialRcInfo.m_num; i++)
                        {
                            allValues.push_back(getKthValueInReturnNode(i));
                        }
                        for (size_t i = 0; i < trivialRcInfo.m_num; i++)
                        {
                            size_t localOrd = trivialRcInfo.m_rangeStart + i;
                            Node* shadowStore = Node::CreateShadowStoreNode(m_inlinedCallFrame->GetInterpreterSlotForLocalOrd(localOrd),
                                                                            allValues[i]);
                            shadowStore->SetExitOK(false);
                            shadowStore->SetNodeOrigin(codeOrigin);
                            shadowStore->SetOsrExitDest(disallowedExitDst);
                            bb->m_nodes.push_back(shadowStore);
                        }
                        for (size_t i = 0; i < trivialRcInfo.m_num; i++)
                        {
                            size_t localOrd = trivialRcInfo.m_rangeStart + i;
                            Node* setLocal = Node::CreateSetLocalNode(m_inlinedCallFrame->GetRegisterForLocalOrd(localOrd),
                                                                      m_inlinedCallFrame->GetInterpreterSlotForLocalOrd(localOrd),
                                                                      allValues[i]);
                            setLocal->SetExitOK(true);
                            setLocal->SetNodeOrigin(codeOrigin);
                            setLocal->SetOsrExitDest(exitDstForTrivialRc);
                            bb->m_nodes.push_back(setLocal);
                        }
                    }
                    else
                    {
                        if (terminator->IsNodeAccessesVR())
                        {
                            Node* vrNode = terminator->GetVariadicResultInputNode();
                            TestAssert(vrNode != nullptr);
                            Node* resNode = Node::CreatePrependVariadicResNode(vrNode);
                            resNode->SetNumInputs(terminator->GetNumInputs());
                            for (uint32_t i = 0; i < terminator->GetNumInputs(); i++)
                            {
                                resNode->GetInputEdge(i) = terminator->GetInputEdge(i);
                            }
                            resNode->SetExitOK(false);
                            resNode->SetNodeOrigin(codeOrigin);
                            resNode->SetOsrExitDest(disallowedExitDst);
                            bb->m_nodes.push_back(resNode);
                        }
                        else
                        {
                            size_t numResults = terminator->GetNumInputs();
                            Node* resNode = Node::CreateCreateVariadicResNode(numResults);
                            resNode->SetNumInputs(numResults + 1);
                            resNode->GetInputEdge(0) = m_graph->GetUnboxedConstant(0);
                            resNode->SetNodeParamAsUInt64(numResults);
                            for (uint32_t i = 0; i < numResults; i++)
                            {
                                resNode->GetInputEdge(i + 1) = terminator->GetInputEdge(i);
                            }
                            resNode->SetExitOK(false);
                            resNode->SetNodeOrigin(codeOrigin);
                            resNode->SetOsrExitDest(disallowedExitDst);
                            bb->m_nodes.push_back(resNode);
                        }
                    }

                    // It's possible that bb->m_nodes is empty if the return continuation is to discard all results
                    //
                    if (bb->m_nodes.size() == 0)
                    {
                        Node* nopNode = Node::CreateNoopNode();
                        nopNode->SetExitOK(false);
                        nopNode->SetNodeOrigin(codeOrigin);
                        nopNode->SetOsrExitDest(disallowedExitDst);
                        bb->m_nodes.push_back(nopNode);
                    }

                    TestAssert(bb->m_nodes.size() > 0);
                    bb->m_terminator = bb->m_nodes.back();
                    bb->m_terminator = bb->m_nodes.back();
                    bb->m_numSuccessors = 1;
                    bb->m_successors[0] = rcBB;
                }
            }
        }

        // Switch m_currentBlock to rcBB.
        // Note that we must clear the cached states, but we must keep the m_isLocalCaptured!
        //
        m_bbContext->ClearBasicBlockCacheStates();
        m_bbContext->m_currentBlock = rcBB;

        TempUnorderedSet<size_t /*virtualRegisterOrdinal*/> virtualRegistersUsedForResultOfCall(alloc);

        // Implement logic for the return continuation block
        // (1) If RCTrivialness is ReturnKthResult or StoreFirstKResults, those logic have been emitted in the terminator blocks,
        //     so nothing to do in the join block.
        // (2) If RCTrivialness is StoreAllAsVariadicResults, we have created the variadic result in the terminator blocks,
        //     so nothing to do in the join block.
        // (3) If RCTrivialness is NotTrivial, we need to emit the epilogue node, which is the return continuation logic for this call.
        //
        if (trait->m_rcTrivialness == TrivialRCKind::NotTrivial)
        {
            // Ugly: the epilogue will access VR even if the original node doesn't.
            // Since this is always the start of the BB, we need to generate a PrependVariadicResults node,
            // then make the epilogue node use that node
            //
            Node* vrNodeToUse = nullptr;
            if (!isOriginalNodeAccessesVR)
            {
                TestAssert(m_bbContext->m_currentVariadicResult == nullptr);
                vrNodeToUse = Node::CreatePrependVariadicResNode(nullptr);
                vrNodeToUse->SetNumInputs(0);
                m_bbContext->SetupNodeCommonInfoAndPushBack(vrNodeToUse);
                m_bbContext->m_currentVariadicResult = vrNodeToUse;
            }

            // For now, for simplicity, we conservatively assume that the return continuation takes all inputs the bytecode takes
            //
            size_t nodeIndexInVector = m_bbContext->ParseAndProcessBytecode(bcOffset, bcIndex, true /*forReturnContinuation*/);
            TestAssert(nodeIndexInVector != static_cast<size_t>(-1));
            TestAssert(m_bbContext->m_currentBlock == rcBB);
            inlineResultInfo.m_nodeIndexOfReturnContinuation = nodeIndexInVector;

            TestAssert(nodeIndexInVector < rcBB->m_nodes.size());
            Node* rcNode = rcBB->m_nodes[nodeIndexInVector];
            TestAssert(rcNode->GetNodeKind() == prologue->GetNodeKind());

            rcNode->SetNodeInlinerSpecialization(false /*isPrologue*/, SafeIntegerCast<uint8_t>(callSiteTargetOrdinal));
            if (!isOriginalNodeAccessesVR)
            {
                TestAssert(!rcNode->IsNodeAccessesVR());
                rcNode->SetNodeAccessesVR(true);
                rcNode->SetVariadicResultInputNode(vrNodeToUse);
            }
            else
            {
                TestAssert(rcNode->IsNodeAccessesVR());
                TestAssert(rcNode->GetVariadicResultInputNode() != nullptr);
                TestAssert(rcNode->GetVariadicResultInputNode() == vrNodeToUse);
            }

            // Figure out all the locals that this node writes to by inspecting all the SetLocals after the node. A bit ugly, but it works..
            //
            for (size_t nodeIndex = nodeIndexInVector + 1; nodeIndex < rcBB->m_nodes.size(); nodeIndex++)
            {
                Node* node = rcBB->m_nodes[nodeIndex];
                if (node->IsSetLocalNode())
                {
                    TestAssert(node->GetNumInputs() == 1);
                    TestAssert(node->GetInputEdge(0).GetOperand() == rcNode);
                    VirtualRegister vreg = node->GetLocalOperationVirtualRegister();
                    TestAssert(!virtualRegistersUsedForResultOfCall.count(vreg.Value()));
                    virtualRegistersUsedForResultOfCall.insert(vreg.Value());
                }
            }
            TestAssert(virtualRegistersUsedForResultOfCall.size() == rcNode->GetNumTotalOutputs());
        }
        else if (trait->m_rcTrivialness == TrivialRCKind::ReturnKthResult)
        {
            // The VirtualRegister used to store the output is just the virtual register for the output operand
            //
            TestAssert(m_decoder->BytecodeHasOutputOperand(bcOffset));
            size_t localOrd = m_decoder->GetOutputOperand(bcOffset);
            VirtualRegister vreg = m_inlinedCallFrame->GetRegisterForLocalOrd(localOrd);
            TestAssert(virtualRegistersUsedForResultOfCall.empty());
            virtualRegistersUsedForResultOfCall.insert(vreg.Value());
        }
        else if (trait->m_rcTrivialness == TrivialRCKind::StoreFirstKResults)
        {
            for (size_t i = 0; i < trivialRcInfo.m_num; i++)
            {
                size_t localOrd = trivialRcInfo.m_rangeStart + i;
                VirtualRegister vreg = m_inlinedCallFrame->GetRegisterForLocalOrd(localOrd);
                TestAssert(!virtualRegistersUsedForResultOfCall.count(vreg.Value()));
                virtualRegistersUsedForResultOfCall.insert(vreg.Value());
            }
            TestAssert(virtualRegistersUsedForResultOfCall.size() == trivialRcInfo.m_num);
        }
        else
        {
            // No locals are used to store the result if the return continuation is to store everthing as variadic results
            //
            TestAssert(trait->m_rcTrivialness == TrivialRCKind::StoreAllAsVariadicResults);
        }

        // For each virtual register owned by the caller frame, borrowed to and used by the callee frame,
        // but not used by the caller frame to store the result of the call, we must explicitly assign Undef to it.
        // This is because we allow bytecodes to use uninitialized slots. They must not see the stale local value in
        // the callee, otherwise this could break our invariant that for any SetLocal that can flow to a GetLocal,
        // they must share the same interpreter slot.
        //
        // This might seem problematic with variadic results at first glance: the variadic results might be stored
        // in the callee frame, could it be clobbered by those SetLocals? Actually it couldn't, since in DFG, we do
        // all the calls at the end of the whole frame, so the varadic results must not overlap with any of the DFG slots.
        //
        for (size_t localOrd = 0; localOrd < m_baselineCodeBlock->m_stackFrameNumSlots; localOrd++)
        {
            VirtualRegister vreg = m_inlinedCallFrame->GetRegisterForLocalOrd(localOrd);
            if (!vregYieldedToCallee.count(vreg.Value()))
            {
                // This virtual register is not yielded to callee, it should not be clobbered
                //
                continue;
            }
            if (!newFrame->IsVirtualRegisterUsedConsideringInlines(vreg))
            {
                // This virtual register is not actually used by the callee, no need to clobber it
                //
                continue;
            }
            if (virtualRegistersUsedForResultOfCall.count(vreg.Value()))
            {
                // This virtual register is used to store the call results, it contains valid info now
                //
                continue;
            }

            InterpreterSlot slot = m_inlinedCallFrame->GetInterpreterSlotForLocalOrd(localOrd);
            Node* shadowStore = Node::CreateShadowStoreNode(slot, m_graph->GetUndefValue());
            m_bbContext->SetupNodeCommonInfoAndPushBack(shadowStore);
            // It's fine that the SetLocal exits: we are just storing undef to uninitialized locals,
            // interpreters should work regardless of what value is stored in uninitialized locals
            //
            Node* setLocal = Node::CreateSetLocalNode(vreg, slot, m_graph->GetUndefValue());
            m_bbContext->SetupNodeCommonInfoAndPushBack(setLocal);
        }
    }
    else
    {
        // This is a tail call, so we've done with m_currentBlock and it's illegal to generate anything more.
        //
        m_bbContext->StartNewBasicBlock(nullptr, nullptr);
    }

    // Add all the callee's basic blocks to the result basic block list
    //
    for (BasicBlock* bb : tfRes.m_allBBs)
    {
        TestAssert(bb->GetTerminator()->GetNumNodeControlFlowSuccessors() == bb->GetNumSuccessors());
        m_bbContext->m_allBBs.push_back(bb);
    }

    return true;
}

}   // namespace dfg

