#include "dfg_basic_block_builder.h"
#include "dfg_frontend.h"

namespace dfg {

DfgBuildBasicBlockContext::DfgBuildBasicBlockContext(DfgTranslateFunctionContext& tfCtx, DfgControlFlowAndUpvalueAnalysisResult& info)
    : m_alloc(tfCtx.m_alloc)
    , m_inlinedCallFrame(tfCtx.m_inlinedCallFrame)
    , m_codeBlock(m_inlinedCallFrame->GetCodeBlock())
    , m_graph(tfCtx.m_graph)
    , m_decoder(m_codeBlock)
    , m_numPrimBasicBlocks(info.m_basicBlocks.size())
    , m_primBBInfo(info.m_basicBlocks.data())
    , m_primBBs(nullptr)
    , m_allBBs(m_alloc)
    , m_branchDestPatchList(m_alloc)
    , m_functionEntry(nullptr)
    , m_tfCtx(tfCtx)
    , m_vrState(tfCtx.m_vrState)
    , m_speculativeInliner(m_alloc, &m_decoder, this, m_inlinedCallFrame)
    , m_isLocalCaptured(nullptr)
    , m_currentBlock(nullptr)
    , m_currentBBInfo(nullptr)
    , m_valueAtTail(nullptr)
    , m_valueForVariadicArgs(nullptr)
    , m_cachedGetFunctionObjectNode(nullptr)
    , m_cachedGetNumVarArgsNode(nullptr)
    , m_currentVariadicResult(nullptr)
    , m_isOSRExitOK(false)
{
    m_primBBs = m_alloc.AllocateArray<BasicBlock*>(m_numPrimBasicBlocks);
    for (size_t i = 0; i < m_numPrimBasicBlocks; i++)
    {
        m_primBBs[i] = nullptr;
    }
    m_isLocalCaptured = m_alloc.AllocateArray<bool>(m_codeBlock->m_stackFrameNumSlots);
    m_valueAtTail = m_alloc.AllocateArray<Value>(m_codeBlock->m_stackFrameNumSlots);

    if (!m_inlinedCallFrame->IsRootFrame())
    {
        size_t maxNumVarArgs = m_inlinedCallFrame->MaxVarArgsAllowed();
        if (maxNumVarArgs > 0)
        {
            m_valueForVariadicArgs = m_alloc.AllocateArray<Value>(maxNumVarArgs);
        }
    }

    m_inlinedCallFrame->AssertVirtualRegisterConsistency(m_vrState);
}

Value WARN_UNUSED DfgBuildBasicBlockContext::GetLocalVariableValue(size_t localOrd)
{
    TestAssert(m_currentBBInfo != nullptr);
    TestAssert(localOrd < m_codeBlock->m_stackFrameNumSlots);
    if (m_isLocalCaptured[localOrd])
    {
        if (m_valueAtTail[localOrd].IsNull())
        {
            Node* getLocal = Node::CreateGetLocalNode(m_inlinedCallFrame, InterpreterFrameLocation::Local(localOrd));
            SetupNodeCommonInfoAndPushBack(getLocal);
            m_valueAtTail[localOrd] = Value(getLocal, 0 /*outputOrd*/);
        }

        Value capturedVar = m_valueAtTail[localOrd];
        Assert(!capturedVar.IsNull());
        TestAssert(capturedVar.GetOperand()->GetNodeKind() == NodeKind_CreateCapturedVar ||
                   capturedVar.GetOperand()->GetNodeKind() == NodeKind_GetLocal);

        Node* getCapturedVar = Node::CreateGetCapturedVarNode(capturedVar);
        SetupNodeCommonInfoAndPushBack(getCapturedVar);
        return Value(getCapturedVar, 0 /*outputOrd*/);
    }
    else
    {
        if (m_valueAtTail[localOrd].IsNull())
        {
            Node* getLocal = Node::CreateGetLocalNode(m_inlinedCallFrame, InterpreterFrameLocation::Local(localOrd));
            SetupNodeCommonInfoAndPushBack(getLocal);
            m_valueAtTail[localOrd] = Value(getLocal, 0 /*outputOrd*/);
        }
        return m_valueAtTail[localOrd];
    }
}

Node* DfgBuildBasicBlockContext::SetLocalVariableValue(size_t localOrd, Value value)
{
    TestAssert(m_currentBBInfo != nullptr);
    TestAssert(localOrd < m_codeBlock->m_stackFrameNumSlots);
    if (m_isLocalCaptured[localOrd])
    {
        if (m_valueAtTail[localOrd].IsNull())
        {
            Node* getLocal = Node::CreateGetLocalNode(m_inlinedCallFrame, InterpreterFrameLocation::Local(localOrd));
            SetupNodeCommonInfoAndPushBack(getLocal);
            m_valueAtTail[localOrd] = Value(getLocal, 0 /*outputOrd*/);
        }

        Value capturedVar = m_valueAtTail[localOrd];
        Assert(!capturedVar.IsNull());
        TestAssert(capturedVar.GetOperand()->GetNodeKind() == NodeKind_CreateCapturedVar ||
                   capturedVar.GetOperand()->GetNodeKind() == NodeKind_GetLocal);

        Node* setCapturedVar = Node::CreateSetCapturedVarNode(capturedVar, value);
        SetupNodeCommonInfoAndPushBack(setCapturedVar);
        return setCapturedVar;
    }
    else
    {
        Node* setLocal = Node::CreateSetLocalNode(m_inlinedCallFrame, InterpreterFrameLocation::Local(localOrd), value);
        SetupNodeCommonInfoAndPushBack(setLocal);
        m_valueAtTail[localOrd] = value;
        return setLocal;
    }
}

size_t DfgBuildBasicBlockContext::ParseAndProcessBytecode(size_t curBytecodeOffset, size_t curBytecodeIndex, bool forReturnContinuation)
{
    TestAssert(m_codeBlock->m_baselineCodeBlock->GetBytecodeOffsetFromBytecodeIndex(curBytecodeIndex) == curBytecodeOffset);

    if (!forReturnContinuation)
    {
        m_currentCodeOrigin = CodeOrigin(m_inlinedCallFrame, curBytecodeIndex);
        m_currentOriginForExit = OsrExitDestination(false /*isBranchDest*/, CodeOrigin(m_inlinedCallFrame, curBytecodeIndex));
        m_isOSRExitOK = true;
    }
    else
    {
        // For return continuation, caller should have set up the codeOrigin and exitDest for us
        //
        TestAssert(!m_isOSRExitOK);
    }

    BCKind bcKind = m_decoder.GetBytecodeKind(curBytecodeOffset);
    Node* node = Node::CreateGuestLanguageNode(bcKind);

    // The default assumption is that all the guest language bytecode could clobber VariadicResults.
    // This is fine as we have required that the VR produced by a bytecode must be immediately consumed
    // by the immediate next bytecode.
    //
    node->SetNodeClobbersVR(true);

    if (m_decoder.BCKindMayMakeTailCall(bcKind))
    {
        TestAssert(curBytecodeOffset == m_currentBBInfo->m_terminalNodeBcOffset);
        node->SetNodeMakesTailCallNotConsideringTransform(true);
    }

    if (m_decoder.BCKindIsBarrier(bcKind))
    {
        node->SetNodeIsBarrier(true);
    }

    if (m_decoder.BCKindMayBranch(bcKind))
    {
        node->SetNodeHasBranchTarget(true);
    }

    TestAssertImp(node->IsTerminal(), curBytecodeOffset == m_currentBBInfo->m_terminalNodeBcOffset);

    size_t numLocals = m_codeBlock->m_stackFrameNumSlots;
    bool skipNodeInsertionAltogether = false;

    // Emit logic that fetches the input values for this node
    //
    {
        BytecodeRWCInfo inputs = m_decoder.GetDataFlowReadInfo(curBytecodeOffset);

        size_t numNodeInputs = 0;
        {
#ifdef TESTBUILD
            bool hasSeenRangeInputs = false;
#endif
            for (size_t itemOrd = 0; itemOrd < inputs.GetNumItems(); itemOrd++)
            {
                BytecodeRWCDesc item = inputs.GetDesc(itemOrd);
                if (item.IsLocal() || item.IsConstant())
                {
                    // The range-operand reads must come after all the normal operands
                    //
                    TestAssert(!hasSeenRangeInputs);
                    numNodeInputs++;
                }
                else if (item.IsRange())
                {
                    // Infinite range is only allowed in clobber declaration
                    //
                    TestAssert(item.GetRangeLength() >= 0);
                    numNodeInputs += static_cast<size_t>(item.GetRangeLength());
#ifdef TESTBUILD
                    hasSeenRangeInputs = true;
#endif
                }
                else if (item.IsVarRets())
                {
                    // If the currentVR is marked as clobbered, it means that this bytecode is not preceded
                    // by a byteocode that produces variadic result, so the bytecode is illegal.
                    // Note that if this bytecode is the first in the BB, the check will pass here, but we
                    // can still detect this case when we build block-local SSA.
                    //
                    TestAssert(reinterpret_cast<uint64_t>(m_currentVariadicResult) != 1);
                    if (m_currentVariadicResult == nullptr)
                    {
                        Node* vrNode = Node::CreatePrependVariadicResNode(nullptr);
                        vrNode->SetNumInputs(0);
                        SetupNodeCommonInfoAndPushBack(vrNode);
                        m_currentVariadicResult = vrNode;
                    }

                    node->SetNodeAccessesVR(true);
                    node->SetVariadicResultInputNode(m_currentVariadicResult);
                }
            }
        }

        bool skipStandardInputGenerationStep = false;

        // Special handling for all sorts of intrinsic bytecodes
        //
        if (m_decoder.IsBytecodeIntrinsic<BytecodeIntrinsicInfo::UpvalueClose>(curBytecodeOffset))
        {
            TestAssert(!forReturnContinuation);     // This bytecode intrinsic is not supposed to contain a call

            // For UpvalueClose intrinsic, we must emit logic to write the current value of all the
            // Upvalue to be closed back to the stack frame.
            // Further, the node itself becomes a no-op
            //
            TestAssert(numNodeInputs == 0);
            BytecodeIntrinsicInfo::UpvalueClose info = m_decoder.GetBytecodeIntrinsicInfo<BytecodeIntrinsicInfo::UpvalueClose>(curBytecodeOffset);
            TestAssert(info.start.IsLocal());
            size_t uvCloseStart = info.start.AsLocal();
            TestAssert(uvCloseStart <= numLocals);

            for (size_t localOrd = uvCloseStart; localOrd < numLocals; localOrd++)
            {
                if (m_isLocalCaptured[localOrd])
                {
                    // This gives us the value stored inside the localOrd
                    //
                    Value value = GetLocalVariableValue(localOrd);

                    Node* shadowStore = Node::CreateShadowStoreNode(GetInterpreterSlotForLocalOrd(localOrd), value);
                    SetupNodeCommonInfoAndPushBack(shadowStore);

                    // Set isLocalCaptured to false and store, so that the store directly writes the local
                    //
                    // Note that OSR-exiting at any point here is fine: the interpreter will see a state where
                    // some of the upvalues have been closed, but the open upvalues are on the open upvalue
                    // linked list in a consistent state (as the interpreter stack frame reconstruction logic
                    // knows exactly which locals are captured from the ShadowStores). Then the interpreter
                    // will execute UpvalueClose, which closes all the remaining open upvalues on the linked
                    // list, just as desired.
                    //
                    m_isLocalCaptured[localOrd] = false;
                    SetLocalVariableValue(localOrd, value);
                }
            }

            skipStandardInputGenerationStep = true;
            skipNodeInsertionAltogether = true;
        }
        else if (m_decoder.IsBytecodeIntrinsic<BytecodeIntrinsicInfo::CreateClosure>(curBytecodeOffset))
        {
            TestAssert(!forReturnContinuation);     // This bytecode intrinsic is not supposed to contain a call

            // For CreateClosure intrinsic, we should convert each captured local to CapturedVar
            // if it hasn't been converted yet, and add all the values this node captures to the node's input.
            //
            TestAssert(numNodeInputs == 1);
            BytecodeIntrinsicInfo::CreateClosure info = m_decoder.GetBytecodeIntrinsicInfo<BytecodeIntrinsicInfo::CreateClosure>(curBytecodeOffset);
            TestAssert(info.proto.IsConstant());
            UnlinkedCodeBlock* createClosureUcb = reinterpret_cast<UnlinkedCodeBlock*>(info.proto.AsConstant().m_value);
            TestAssert(createClosureUcb != nullptr);

            BytecodeRWCInfo outputs = m_decoder.GetDataFlowWriteInfo(curBytecodeOffset);
            TestAssert(outputs.GetNumItems() == 1);
            BytecodeRWCDesc item = outputs.GetDesc(0 /*itemOrd*/);
            TestAssert(item.IsLocal());
            size_t destLocalOrd = item.GetLocalOrd();

            size_t selfReferenceUvOrd = static_cast<size_t>(-1);

            numNodeInputs = 2;

            for (size_t uvOrd = 0; uvOrd < createClosureUcb->m_numUpvalues; uvOrd++)
            {
                UpvalueMetadata& uvmt = createClosureUcb->m_upvalueInfo[uvOrd];
                if (uvmt.m_isParentLocal)
                {
                    // If this is an immutable capture and is a self-reference, its value ought to be the result of the CreateClosure,
                    // so this value cannot be given as an input and we need to record that specially
                    //
                    if (uvmt.m_isImmutable && uvmt.m_slot == destLocalOrd)
                    {
                        TestAssert(selfReferenceUvOrd == static_cast<size_t>(-1));
                        selfReferenceUvOrd = uvOrd;
                    }
                    else
                    {
                        // Otherwise, the value should be an input that we provide
                        // Note that mutable self-reference *is* an input: we must provide the Upvalue object we created.
                        //
                        numNodeInputs++;
                    }

                    if (!uvmt.m_isImmutable)
                    {
                        // This is a mutable capture in the parent frame, we need to convert this local to CaptureVar, if not already
                        //
                        size_t localOrd = uvmt.m_slot;
                        TestAssert(localOrd < m_codeBlock->m_stackFrameNumSlots);
                        if (!m_isLocalCaptured[localOrd])
                        {
                            Value value;
                            if (uvmt.m_slot == destLocalOrd)
                            {
                                // This is a mutable self-reference. The Upvalue object is still created, but the content doesn't matter,
                                // as it will be overwritten by this bytecode.
                                //
                                value = m_graph->GetUndefValue();
                            }
                            else
                            {
                                // Need to call GetLocalVariableValue while isLocalCaptured is false
                                //
                                value = GetLocalVariableValue(localOrd);
                            }
                            Node* createClosureVar = Node::CreateCreateCapturedVarNode(value);
                            SetupNodeCommonInfoAndPushBack(createClosureVar);
                            Node* shadowStore = Node::CreateShadowStoreNode(GetInterpreterSlotForLocalOrd(localOrd), Value(createClosureVar, 0 /*outputOrd*/));
                            SetupNodeCommonInfoAndPushBack(shadowStore);
                            // This SetLocal shouldn't exit, but even if does, we are still in a consistent state,
                            // since bytecode execution is agnostic of which local is captured by an upvalue.
                            //
                            // Must call SetLocalVariableValue before setting m_isLocalCaptured to true
                            //
                            SetLocalVariableValue(localOrd, Value(createClosureVar, 0 /*outputOrd*/));
                            m_isLocalCaptured[localOrd] = true;
                        }
                    }
                }
            }

            node->m_nodeKind = NodeKind_CreateFunctionObject;
            node->SetNumInputs(numNodeInputs);
            node->GetInputEdge(0) = GetCurrentFunctionObject();
            node->GetInputEdge(1) = m_graph->GetUnboxedConstant(reinterpret_cast<uint64_t>(createClosureUcb));

            uint32_t curInputOrd = 2;
            // We need to additionally add all the immutable locals and CapturedVar objects to the node's input
            //
            for (size_t uvOrd = 0; uvOrd < createClosureUcb->m_numUpvalues; uvOrd++)
            {
                UpvalueMetadata& uvmt = createClosureUcb->m_upvalueInfo[uvOrd];
                if (uvmt.m_isParentLocal)
                {
                    size_t localOrd = uvmt.m_slot;
                    TestAssert(localOrd < m_codeBlock->m_stackFrameNumSlots);
                    if (!uvmt.m_isImmutable)
                    {
                        TestAssert(m_isLocalCaptured[localOrd]);

                        // Temporarily set isCaptured to false so GetLocalVariableValue gives us the CapturedVar object,
                        // not the value stored in the object
                        //
                        m_isLocalCaptured[localOrd] = false;
                        TestAssert(curInputOrd < numNodeInputs);
                        node->GetInputEdge(curInputOrd) = GetLocalVariableValue(localOrd);
                        curInputOrd++;
                        m_isLocalCaptured[localOrd] = true;
                    }
                    else
                    {
                        TestAssertIff(localOrd == destLocalOrd, uvOrd == selfReferenceUvOrd);
                        // The local must not be captured. If it were, this indicates a bug in the user-written parser,
                        // since immmutability is a per-local property, not a per-closure-and-local property.
                        // (That is, even if closure A captures but never modify local X, if local X is captured and modified by
                        // another closure B, then X is also mutable for closure A, since B may modify it.)
                        //
                        TestAssert(!m_isLocalCaptured[localOrd]);
                        if (uvOrd != selfReferenceUvOrd)
                        {
                            // We need to pass the value of the local to the node
                            //
                            TestAssert(curInputOrd < numNodeInputs);
                            node->GetInputEdge(curInputOrd) = GetLocalVariableValue(localOrd);
                            curInputOrd++;
                        }
                    }
                }
            }

            TestAssert(curInputOrd == numNodeInputs);
            node->SetNodeSpecificDataAsUInt64(selfReferenceUvOrd);

            skipStandardInputGenerationStep = true;
        }
        else if (m_decoder.IsBytecodeIntrinsic<BytecodeIntrinsicInfo::UpvalueGetImmutable>(curBytecodeOffset))
        {
            TestAssert(!forReturnContinuation);     // This bytecode intrinsic is not supposed to contain a call

            BytecodeIntrinsicInfo::UpvalueGetImmutable info = m_decoder.GetBytecodeIntrinsicInfo<BytecodeIntrinsicInfo::UpvalueGetImmutable>(curBytecodeOffset);
            TestAssert(info.ord.IsNumber());
            uint32_t upvalueGetOrdinal = SafeIntegerCast<uint32_t>(info.ord.AsNumber());

            TestAssert(numNodeInputs == 0);
            node->m_nodeKind = NodeKind_GetUpvalueImmutable;
            node->SetNumInputs(1);
            node->GetInputEdgeForNodeWithFixedNumInputs<1>(0) = GetCurrentFunctionObject();
            node->GetInfoForGetUpvalue() = { .m_ordinal = upvalueGetOrdinal };
            skipStandardInputGenerationStep = true;
        }
        else if (m_decoder.IsBytecodeIntrinsic<BytecodeIntrinsicInfo::UpvalueGetMutable>(curBytecodeOffset))
        {
            TestAssert(!forReturnContinuation);     // This bytecode intrinsic is not supposed to contain a call

            BytecodeIntrinsicInfo::UpvalueGetMutable info = m_decoder.GetBytecodeIntrinsicInfo<BytecodeIntrinsicInfo::UpvalueGetMutable>(curBytecodeOffset);
            TestAssert(info.ord.IsNumber());
            uint32_t upvalueGetOrdinal = SafeIntegerCast<uint32_t>(info.ord.AsNumber());

            TestAssert(numNodeInputs == 0);
            node->m_nodeKind = NodeKind_GetUpvalueMutable;
            node->SetNumInputs(1);
            node->GetInputEdgeForNodeWithFixedNumInputs<1>(0) = GetCurrentFunctionObject();
            node->GetInfoForGetUpvalue() = { .m_ordinal = upvalueGetOrdinal };
            skipStandardInputGenerationStep = true;
        }
        else if (m_decoder.IsBytecodeIntrinsic<BytecodeIntrinsicInfo::UpvaluePut>(curBytecodeOffset))
        {
            TestAssert(!forReturnContinuation);     // This bytecode intrinsic is not supposed to contain a call

            BytecodeIntrinsicInfo::UpvaluePut info = m_decoder.GetBytecodeIntrinsicInfo<BytecodeIntrinsicInfo::UpvaluePut>(curBytecodeOffset);
            TestAssert(info.ord.IsNumber());
            uint32_t upvalueGetOrdinal = SafeIntegerCast<uint32_t>(info.ord.AsNumber());

            Value valueToPut;
            TestAssert(info.value.IsLocal() || info.value.IsConstant());
            if (info.value.IsLocal())
            {
                valueToPut = GetLocalVariableValue(info.value.AsLocal());
            }
            else
            {
                TestAssert(info.value.IsConstant());
                valueToPut = m_graph->GetConstant(info.value.AsConstant());
            }

            TestAssert(numNodeInputs == 1);
            node->m_nodeKind = NodeKind_SetUpvalue;
            node->SetNumInputs(2);
            node->GetInputEdgeForNodeWithFixedNumInputs<2>(0) = GetCurrentFunctionObject();
            node->GetInputEdgeForNodeWithFixedNumInputs<2>(1) = valueToPut;
            node->SetNodeSpecificDataAsUInt64(upvalueGetOrdinal);

            skipStandardInputGenerationStep = true;
        }
        else if (m_decoder.IsBytecodeIntrinsic<BytecodeIntrinsicInfo::GetVarArgPrefix>(curBytecodeOffset))
        {
            TestAssert(!forReturnContinuation);     // This bytecode intrinsic is not supposed to contain a call

            BytecodeIntrinsicInfo::GetVarArgPrefix info = m_decoder.GetBytecodeIntrinsicInfo<BytecodeIntrinsicInfo::GetVarArgPrefix>(curBytecodeOffset);
            TestAssert(info.num.IsNumber());
            size_t numToGet = SafeIntegerCast<size_t>(info.num.AsNumber());
            TestAssert(info.dst.IsLocal());
            size_t dstLocalStart = info.dst.AsLocal();

            m_isOSRExitOK = false;
            for (size_t i = 0; i < numToGet; i++)
            {
                size_t dstSlot = dstLocalStart + i;
                TestAssert(dstSlot < numLocals);
                if (!m_isLocalCaptured[dstSlot])
                {
                    Value val = GetVariadicArgument(i);
                    Node* shadowStore = Node::CreateShadowStoreNode(GetInterpreterSlotForLocalOrd(dstSlot), val);
                    SetupNodeCommonInfoAndPushBack(shadowStore);
                }
            }

            m_currentOriginForExit = OsrExitDestination(false /*isBranchDest*/, CodeOrigin(m_inlinedCallFrame, curBytecodeIndex + 1));
            m_isOSRExitOK = true;

            for (size_t i = 0; i < numToGet; i++)
            {
                Value val = GetVariadicArgument(i);
                size_t dstSlot = dstLocalStart + i;
                TestAssert(dstSlot < numLocals);
                SetLocalVariableValue(dstSlot, val);
            }

            skipStandardInputGenerationStep = true;
            skipNodeInsertionAltogether = true;
        }
        else if (m_decoder.IsBytecodeIntrinsic<BytecodeIntrinsicInfo::GetAllVarArgsAsVarRet>(curBytecodeOffset))
        {
            TestAssert(!forReturnContinuation);     // This bytecode intrinsic is not supposed to contain a call

            if (!m_inlinedCallFrame->IsRootFrame())
            {
                // If we are not root frame, the vararg this bytecode referring to is not the root frame's varargs,
                // so we need to convert this node to a CreateVariadicRes node
                //
                node->m_nodeKind = NodeKind_CreateVariadicRes;
                if (m_inlinedCallFrame->StaticallyKnowsNumVarArgs())
                {
                    size_t numVarArgs = m_inlinedCallFrame->GetNumVarArgs();
                    node->SetNumInputs(numVarArgs + 1);
                    node->GetInputEdge(0) = m_graph->GetUnboxedConstant(0);
                    node->SetNodeSpecificDataAsUInt64(numVarArgs);
                    for (uint32_t i = 0; i < numVarArgs; i++)
                    {
                        node->GetInputEdge(i + 1) = GetVariadicArgument(i);
                    }
                }
                else
                {
                    size_t maxNumVarArgs = m_inlinedCallFrame->MaxVarArgsAllowed();
                    node->SetNumInputs(maxNumVarArgs + 1);
                    node->GetInputEdge(0) = GetNumVariadicArguments();
                    for (uint32_t i = 0; i < maxNumVarArgs; i++)
                    {
                        node->GetInputEdge(i + 1) = GetVariadicArgument(i);
                    }
                    node->SetNodeSpecificDataAsUInt64(0);
                }

                skipStandardInputGenerationStep = true;
            }
        }
        else if (m_decoder.IsBytecodeIntrinsic<BytecodeIntrinsicInfo::FunctionReturn0>(curBytecodeOffset))
        {
            TestAssert(!forReturnContinuation);     // This bytecode intrinsic is not supposed to contain a call
            node->m_nodeKind = NodeKind_Return;
            TestAssert(numNodeInputs == 0);
            TestAssert(m_decoder.GetDataFlowWriteInfo(curBytecodeOffset).GetNumItems() == 0);
            node->SetNumInputs(0);
            TestAssert(node->GetNumNodeControlFlowSuccessors() == 0 && !node->IsNodeMakesTailCallNotConsideringTransform());
            skipStandardInputGenerationStep = true;
        }
        else if (m_decoder.IsBytecodeIntrinsic<BytecodeIntrinsicInfo::FunctionReturn>(curBytecodeOffset))
        {
            TestAssert(!forReturnContinuation);     // This bytecode intrinsic is not supposed to contain a call
            node->m_nodeKind = NodeKind_Return;
            TestAssert(m_decoder.GetDataFlowWriteInfo(curBytecodeOffset).GetNumItems() == 0);
            TestAssert(!node->IsNodeAccessesVR());
            BytecodeIntrinsicInfo::FunctionReturn info = m_decoder.GetBytecodeIntrinsicInfo<BytecodeIntrinsicInfo::FunctionReturn>(curBytecodeOffset);
            TestAssert(info.start.IsLocal() && info.length.IsNumber());
            size_t srcSlotStart = info.start.AsLocal();
            size_t numSlots = SafeIntegerCast<size_t>(info.length.AsNumber());
            TestAssert(numNodeInputs == numSlots);
            node->SetNumInputs(numSlots);
            for (uint32_t i = 0; i < numSlots; i++)
            {
                node->GetInputEdge(i) = GetLocalVariableValue(srcSlotStart + i);
            }
            TestAssert(node->GetNumNodeControlFlowSuccessors() == 0 && !node->IsNodeMakesTailCallNotConsideringTransform());
            skipStandardInputGenerationStep = true;
        }
        else if (m_decoder.IsBytecodeIntrinsic<BytecodeIntrinsicInfo::FunctionReturnAppendingVarRet>(curBytecodeOffset))
        {
            TestAssert(!forReturnContinuation);     // This bytecode intrinsic is not supposed to contain a call
            node->m_nodeKind = NodeKind_Return;
            TestAssert(m_decoder.GetDataFlowWriteInfo(curBytecodeOffset).GetNumItems() == 0);
            TestAssert(node->IsNodeAccessesVR());
            BytecodeIntrinsicInfo::FunctionReturnAppendingVarRet info = m_decoder.GetBytecodeIntrinsicInfo<BytecodeIntrinsicInfo::FunctionReturnAppendingVarRet>(curBytecodeOffset);
            TestAssert(info.start.IsLocal() && info.length.IsNumber());
            size_t srcSlotStart = info.start.AsLocal();
            size_t numSlots = SafeIntegerCast<size_t>(info.length.AsNumber());
            TestAssert(numNodeInputs == numSlots);
            node->SetNumInputs(numSlots);
            for (uint32_t i = 0; i < numSlots; i++)
            {
                node->GetInputEdge(i) = GetLocalVariableValue(srcSlotStart + i);
            }
            TestAssert(node->GetNumNodeControlFlowSuccessors() == 0 && !node->IsNodeMakesTailCallNotConsideringTransform());
            skipStandardInputGenerationStep = true;
        }

        if (!skipStandardInputGenerationStep)
        {
            node->SetNumInputs(numNodeInputs);

            uint32_t curInputOrd = 0;
            for (size_t itemOrd = 0; itemOrd < inputs.GetNumItems(); itemOrd++)
            {
                BytecodeRWCDesc item = inputs.GetDesc(itemOrd);
                if (item.IsLocal())
                {
                    TestAssert(curInputOrd < numNodeInputs);
                    node->GetInputEdge(curInputOrd) = GetLocalVariableValue(item.GetLocalOrd());
                    curInputOrd++;
                }
                else if (item.IsConstant())
                {
                    TestAssert(curInputOrd < numNodeInputs);
                    node->GetInputEdge(curInputOrd) = m_graph->GetConstant(item.GetConstant());
                    curInputOrd++;
                }
                else if (item.IsRange())
                {
                    size_t rangeBegin = item.GetRangeStart();
                    int64_t rangeLen = item.GetRangeLength();
                    TestAssert(rangeLen >= 0);
                    for (size_t i = rangeBegin; i < rangeBegin + static_cast<size_t>(rangeLen); i++)
                    {
                        TestAssert(curInputOrd < numNodeInputs);
                        node->GetInputEdge(curInputOrd) = GetLocalVariableValue(i);
                        curInputOrd++;
                    }
                }
            }

            TestAssert(curInputOrd == numNodeInputs);
        }
    }

    size_t nodeIndexInVector = static_cast<size_t>(-1);

    if (skipNodeInsertionAltogether)
    {
        return nodeIndexInVector;
    }

    // Populate node-specific data
    // If the node has been specialized to a builtin bytecode due to intrinsic, we must not do it as those node already
    // populated the node-specific data in their own format.
    //
    if (!node->IsBuiltinNodeKind())
    {
        node->SetNodeSpecificDataLength(m_decoder.GetDfgNodeSpecificDataLength(curBytecodeOffset));
        m_decoder.PopulateDfgNodeSpecificData(node->GetNodeSpecificData(), curBytecodeOffset);
    }

    SpeculativeInliner::InliningResultInfo inlineResultInfo;
    if (!forReturnContinuation && m_speculativeInliner.TrySpeculativeInlining(node, curBytecodeOffset, curBytecodeIndex, inlineResultInfo /*out*/))
    {
        return inlineResultInfo.m_nodeIndexOfReturnContinuation;
    }

    nodeIndexInVector = m_currentBlock->m_nodes.size();
    SetupNodeCommonInfoAndPushBack(node);

    // At this point, the node is executed, and the currentVR is clobbered
    //
    m_currentVariadicResult = reinterpret_cast<Node*>(1);

    // At this point, we executed something effectful but haven't finished up all the semantics, so it's no longer OK to exit
    //
    m_isOSRExitOK = false;

    // Execute the store effects
    //
    {
        bool hasDirectOutput = m_decoder.BytecodeHasOutputOperand(curBytecodeOffset);

        BytecodeRWCInfo outputs = m_decoder.GetDataFlowWriteInfo(curBytecodeOffset);
        size_t numNodeOutputs = 0;
        for (size_t itemOrd = 0; itemOrd < outputs.GetNumItems(); itemOrd++)
        {
            BytecodeRWCDesc item = outputs.GetDesc(itemOrd);
            if (item.IsLocal())
            {
                // The Local() corresponds to the output, it should only show up once and at the start
                //
                TestAssert(itemOrd == 0 && hasDirectOutput);
                numNodeOutputs++;
            }
            else if (item.IsRange())
            {
                // Infinite range is only allowed in clobber declaration
                //
                TestAssert(item.GetRangeLength() >= 0);
                numNodeOutputs += static_cast<size_t>(item.GetRangeLength());
            }
            else
            {
                TestAssert(item.IsVarRets());
                node->SetNodeGeneratesVR(true);
                m_currentVariadicResult = node;
            }
        }

        TestAssertImp(hasDirectOutput, numNodeOutputs > 0);
        size_t numExtraOutputs = numNodeOutputs - (hasDirectOutput ? 1 : 0);
        node->SetNumOutputs(hasDirectOutput, numExtraOutputs);

        size_t directOutputSlot = static_cast<size_t>(-1);
        if (hasDirectOutput)
        {
            TestAssert(outputs.GetNumItems() > 0 && outputs.GetDesc(0).IsLocal());
            directOutputSlot = outputs.GetDesc(0).GetLocalOrd();
        }

        // Since SetLocal can OSR-exit, the ordering of storing the outputs of a node to locals is tricky:
        // 1. All the stores into the CapturedVar must come first
        // 2. Then comes all the ShadowStores to the locals that are not CapturedVar
        // 3. Finally, all the SetLocals to the locals that are not CapturedVar
        //
        // This way, at stage (3) where an OSR-exit may happen, all the CapturedVar (on the heap) have
        // got their desired updated values, and all the ShadowStore must have been executed,
        // so the OSR-exit point sees a coherent state where it can reconstruct the stack
        // frame based on ShadowStore, then rewrite all the locals that are CaptureVars (by loading
        // their values on the heap, which have been updated), to get the coherent state expected
        // by the lower tiers.
        //
        enum class SetLocalStage
        {
            CaptureVarStores,
            ShadowStores,
            SetLocals
        };

        // Generate nodes for all the store effects using 2-phase commit (see above)
        //
        auto emitSetLocalStage = [&](SetLocalStage setLocalStage) ALWAYS_INLINE
        {
            auto emitImpl = [&](size_t localOrd, Value value) ALWAYS_INLINE
            {
                switch (setLocalStage)
                {
                case SetLocalStage::CaptureVarStores:
                {
                    if (m_isLocalCaptured[localOrd])
                    {
                        SetLocalVariableValue(localOrd, value);
                    }
                    break;
                }
                case SetLocalStage::ShadowStores:
                {
                    if (!m_isLocalCaptured[localOrd])
                    {
                        Node* shadowStore = Node::CreateShadowStoreNode(GetInterpreterSlotForLocalOrd(localOrd), value);
                        SetupNodeCommonInfoAndPushBack(shadowStore);
                    }
                    break;
                }
                case SetLocalStage::SetLocals:
                {
                    if (!m_isLocalCaptured[localOrd])
                    {
                        SetLocalVariableValue(localOrd, value);
                    }
                    break;
                }
                } /*switch*/
            };

            size_t curOutputOrd = (hasDirectOutput ? 0 : 1);
            for (size_t itemOrd = 0; itemOrd < outputs.GetNumItems(); itemOrd++)
            {
                BytecodeRWCDesc item = outputs.GetDesc(itemOrd);
                if (item.IsLocal())
                {
                    TestAssert(curOutputOrd <= numExtraOutputs);
                    TestAssert(itemOrd == 0 && hasDirectOutput);
                    emitImpl(item.GetLocalOrd(), Value(node, SafeIntegerCast<uint16_t>(curOutputOrd)));
                    curOutputOrd++;
                }
                else if (item.IsRange())
                {
                    size_t rangeBegin = item.GetRangeStart();
                    int64_t rangeLen = item.GetRangeLength();
                    TestAssert(rangeLen >= 0);
                    for (size_t i = rangeBegin; i < rangeBegin + static_cast<size_t>(rangeLen); i++)
                    {
                        TestAssert(curOutputOrd <= numExtraOutputs);
                        // If the bytecode writes to a range-operand slot that is also the output slot,
                        // the output slot always wins (since the output slot is always written at the end of the bytecode logic)
                        // So here if the slot equals the output slot, the write has no effect. We must not emit anything.
                        //
                        if (i != directOutputSlot)
                        {
                            emitImpl(i, Value(node, SafeIntegerCast<uint16_t>(curOutputOrd)));
                        }
                        curOutputOrd++;
                    }
                }
            }
            TestAssert(curOutputOrd == numExtraOutputs + 1);
        };

        emitSetLocalStage(SetLocalStage::CaptureVarStores);
        emitSetLocalStage(SetLocalStage::ShadowStores);

        // At this point, we have completed executing all the semantics of the current bytecode,
        // and the OSR information is complete, so OSR exit is OK now
        //
        m_isOSRExitOK = true;

        // Since we're done with the current bytecode, from now on, the exit destination should be the next bytecode
        // However, if the current bytecode is doing a (potentially conditional) branch, the exit destination should be
        // the destination of the runtime taken branch
        //
        if (curBytecodeOffset == m_currentBBInfo->m_terminalNodeBcOffset)
        {
            if (m_currentBBInfo->m_numSuccessors == 1)
            {
                // This is an unconditional branch, the exit destination is its unique destination
                //
                m_currentOriginForExit = OsrExitDestination(false /*isBranchDest*/, CodeOrigin(m_inlinedCallFrame, m_currentBBInfo->m_successors[0]->m_bytecodeIndex));
            }
            else if (m_currentBBInfo->m_numSuccessors == 0)
            {
                // This is a bytecode with no successor (e.g., a return), which is not allowed to write anything.
                // No need to do anything since the parser loop ends right now
                //
                TestAssert(!hasDirectOutput && numExtraOutputs == 0);
            }
            else
            {
                TestAssert(m_currentBBInfo->m_numSuccessors == 2);
                // This is a conditional branch bytecode
                //
                m_currentOriginForExit = OsrExitDestination(true /*isBranchDest*/, m_currentCodeOrigin);
            }
        }
        else
        {
            // Not a branchy bytecode, the destination is just the next bytecode
            //
            m_currentOriginForExit = OsrExitDestination(false /*isBranchDest*/, CodeOrigin(m_inlinedCallFrame, curBytecodeIndex + 1));
        }

        emitSetLocalStage(SetLocalStage::SetLocals);
    }

    return nodeIndexInVector;
}

void DfgBuildBasicBlockContext::BuildDfgBasicBlockFromBytecode(size_t bbOrd)
{
    TestAssert(bbOrd < m_numPrimBasicBlocks);
    TestAssert(m_primBBs[bbOrd] == nullptr);

    size_t numLocals = m_codeBlock->m_stackFrameNumSlots;

    BasicBlockUpvalueInfo* bbUvInfo = m_primBBInfo[bbOrd];
    BasicBlock* mainBlockEntry = DfgAlloc()->AllocateObject<BasicBlock>();

    StartNewBasicBlock(mainBlockEntry, bbUvInfo);

    TestAssert(m_primBBs[bbOrd] == nullptr);
    m_primBBs[bbOrd] = m_currentBlock;
    m_allBBs.push_back(m_currentBlock);

    m_currentBlock->m_bcForInterpreterStateAtBBStart = CodeOrigin(m_inlinedCallFrame, bbUvInfo->m_bytecodeIndex);

    Node* terminatorNode = nullptr;

    size_t curBytecodeOffset = bbUvInfo->m_bytecodeOffset;
    size_t curBytecodeIndex = bbUvInfo->m_bytecodeIndex;
    size_t bbTerminalBytecodeOffset = bbUvInfo->m_terminalNodeBcOffset;
    while (curBytecodeOffset <= bbTerminalBytecodeOffset)
    {
        size_t nodeIndexInVector = ParseAndProcessBytecode(curBytecodeOffset, curBytecodeIndex, false /*forReturnContinuation*/);

        if (nodeIndexInVector != static_cast<size_t>(-1) && curBytecodeOffset == bbTerminalBytecodeOffset)
        {
            TestAssert(m_currentBlock != nullptr);
            TestAssert(nodeIndexInVector < m_currentBlock->m_nodes.size());
            TestAssert(terminatorNode == nullptr);
            terminatorNode = m_currentBlock->m_nodes[nodeIndexInVector];
        }

        // Advance to next bytecode
        //
        size_t nextBytecodeOffset = m_decoder.GetNextBytecodePosition(curBytecodeOffset);
        TestAssertIff(nextBytecodeOffset > bbTerminalBytecodeOffset, curBytecodeOffset == bbTerminalBytecodeOffset);
        curBytecodeOffset = nextBytecodeOffset;
        curBytecodeIndex++;
    }

    // Note that mainBlockEnd does not necessarily equal mainBlockEntry due to function inlining.
    // Also, if a tail call is inlined, m_currentBlock and m_currentBBInfo will become nullptr, so mainBlockEnd will also be nullptr
    //
    BasicBlock* mainBlockEnd = m_currentBlock;

    // The only case that mainBlockEnd can become nullptr is when a tail call is inlined.
    // So if the BB has successors, mainBlockEnd must not be nullptr.
    //
    TestAssertImp(bbUvInfo->m_numSuccessors > 0, mainBlockEnd != nullptr);

    if (x_isTestBuild && m_currentBlock != nullptr)
    {
        for (size_t localOrd = 0; localOrd < numLocals; localOrd++)
        {
            TestAssertIff(m_isLocalCaptured[localOrd], bbUvInfo->m_isLocalCapturedAtTail.IsSet(localOrd));
        }
    }

    // Set up remaining info of m_currentBlock
    //
    // If m_currentBlock is nullptr, the speculative inliner has done setting up all the info for us,
    // in which case we don't need to do anything
    //
    if (m_currentBlock != nullptr)
    {
        uint8_t numBBSuccessors = bbUvInfo->m_numSuccessors;
        TestAssert(bbUvInfo->m_numSuccessors <= 2);
        m_currentBlock->m_numSuccessors = numBBSuccessors;

        // It's possible that we get an empty block in edge cases. In that case, insert a nop node to make everything happy.
        //
        if (m_currentBlock->m_nodes.size() == 0)
        {
            TestAssert(numBBSuccessors == 1);
            Node* nopNode = Node::CreateNoopNode();
            m_isOSRExitOK = false;
            m_currentCodeOrigin = CodeOrigin(m_inlinedCallFrame, bbUvInfo->m_bytecodeIndex);
            m_currentOriginForExit = OsrExitDestination(false /*isBranchDest*/, m_currentCodeOrigin);
            SetupNodeCommonInfoAndPushBack(nopNode);
        }

        TestAssert(m_currentBlock->m_nodes.size() > 0);
        if (numBBSuccessors != 1)
        {
            TestAssert(terminatorNode != nullptr);
            m_currentBlock->SetTerminator(terminatorNode);
            if (numBBSuccessors == 0)
            {
                // This is a static property and ideally should have been asserted statically. But for now..
                //
                TestAssert(terminatorNode == m_currentBlock->m_nodes.back() && "a node that has no successor must have no output!");
            }
        }
        m_currentBlock->AssertTerminatorNodeConsistent();
    }

    // We are done with emitting nodes into the main logic block, clear info to prevent accidental use
    //
    StartNewBasicBlock(nullptr /*basicBlock*/, nullptr /*basicBlockInfo*/);

    // Set up the successor blocks of the main logic block, intermediate blocks may be needed to fix up captures
    //
    for (size_t succIdx = 0; succIdx < bbUvInfo->m_numSuccessors; succIdx++)
    {
        TestAssert(succIdx < 2);
        BasicBlockUpvalueInfo* succBBInfo = bbUvInfo->m_successors[succIdx];
        uint32_t succBBOrd = succBBInfo->m_ord;

        // If our isCapturedAtTail differs from the successor BB's isCapturedAtHead,
        // we need to create an intermediate BB that fixes those difference and branch there instead
        //
        bool needIntermediateBB = false;
        TestAssert(bbUvInfo->m_isLocalCapturedAtTail.m_length == succBBInfo->m_isLocalCapturedAtHead.m_length);
        TestAssert(bbUvInfo->m_isLocalCapturedAtTail.m_length == numLocals);
        for (size_t k = 0; k < bbUvInfo->m_isLocalCapturedAtTail.GetAllocLength(); k++)
        {
            if (bbUvInfo->m_isLocalCapturedAtTail.m_data[k] != succBBInfo->m_isLocalCapturedAtHead.m_data[k])
            {
                needIntermediateBB = true;
                break;
            }
        }

        // The field that we should write the succesor BB into
        //
        ArenaPtr<BasicBlock>* succField;
        if (!needIntermediateBB)
        {
            // If an intermediate block is not needed, we should just write the successor into the corresponding slot in our BB
            //
            TestAssert(mainBlockEnd != nullptr);
            succField = mainBlockEnd->m_successors + succIdx;
        }
        else
        {
            // Create the intermediate block
            //
            m_isOSRExitOK = true;

            {
                TestAssert(mainBlockEnd != nullptr);
                Node* incomingBBTerminator = mainBlockEnd->GetTerminator();
                TestAssert(incomingBBTerminator != nullptr);
                m_currentCodeOrigin = incomingBBTerminator->GetNodeOrigin();
            }

            m_currentOriginForExit = OsrExitDestination(false /*isBranchDest*/, CodeOrigin(m_inlinedCallFrame, succBBInfo->m_bytecodeIndex));

            BasicBlock* ib = DfgAlloc()->AllocateObject<BasicBlock>();
            ib->m_numSuccessors = 1;
            succField = ib->m_successors;

            ib->m_bcForInterpreterStateAtBBStart = CodeOrigin(m_inlinedCallFrame, succBBInfo->m_bytecodeIndex);

            StartNewBasicBlock(ib, nullptr /*bbInfo*/);
            m_allBBs.push_back(ib);

            mainBlockEnd->m_successors[succIdx] = ib;

            for (size_t localOrd = 0; localOrd < numLocals; localOrd++)
            {
                TestAssertImp(bbUvInfo->m_isLocalCapturedAtTail.IsSet(localOrd), succBBInfo->m_isLocalCapturedAtHead.IsSet(localOrd));
                if (succBBInfo->m_isLocalCapturedAtHead.IsSet(localOrd) && !bbUvInfo->m_isLocalCapturedAtTail.IsSet(localOrd))
                {
                    Node* getLocal = Node::CreateGetLocalNode(m_inlinedCallFrame, InterpreterFrameLocation::Local(localOrd));
                    SetupNodeCommonInfoAndPushBack(getLocal);
                    Node* createClosureVar = Node::CreateCreateCapturedVarNode(Value(getLocal, 0 /*outputOrd*/));
                    SetupNodeCommonInfoAndPushBack(createClosureVar);
                    Node* shadowStore = Node::CreateShadowStoreNode(GetInterpreterSlotForLocalOrd(localOrd), Value(createClosureVar, 0 /*outputOrd*/));
                    SetupNodeCommonInfoAndPushBack(shadowStore);
                    // This SetLocal shouldn't exit, but even if does, we are still in a consistent state,
                    // since bytecode execution is agnostic of which local is captured by an upvalue.
                    //
                    Node* setLocal = Node::CreateSetLocalNode(m_inlinedCallFrame, InterpreterFrameLocation::Local(localOrd), Value(createClosureVar, 0 /*outputOrd*/));
                    SetupNodeCommonInfoAndPushBack(setLocal);
                }
            }

            StartNewBasicBlock(nullptr /*bb*/, nullptr /*bbInfo*/);
        }

        if (m_primBBs[succBBOrd] != nullptr)
        {
            *succField = m_primBBs[succBBOrd];
        }
        else
        {
            m_branchDestPatchList.push_back(std::make_pair(DfgAlloc()->GetArenaPtr(succField), succBBOrd));
        }
    }

    if (bbOrd == 0)
    {
        // Create the entry block, which needs to:
        // (1) Set up all the arguments if we are the root function
        // (2) If the real logic entry BB already has captured variables at head, we need to set them up
        // (3) Transfer control to the real logic entry
        //
        m_isOSRExitOK = false;
        m_currentCodeOrigin = CodeOrigin(m_inlinedCallFrame, 0 /*bytecodeIndex*/);
        m_currentOriginForExit = OsrExitDestination(false /*isBranchDest*/, m_currentCodeOrigin);

        BasicBlock* ib = DfgAlloc()->AllocateObject<BasicBlock>();
        ib->m_numSuccessors = 1;
        ib->m_successors[0] = mainBlockEntry;

        // The root function's entry BB does not have a valid m_bcForInterpreterStateAtBBStart
        // If we are not the root frame, at the time this block is executed, all the arguments have been set up,
        // so the interpreter state *is* what we expect to have before bytecode 0.
        //
        if (!m_inlinedCallFrame->IsRootFrame())
        {
            ib->m_bcForInterpreterStateAtBBStart = CodeOrigin(m_inlinedCallFrame, 0 /*bytecodeIndex*/);
        }

        StartNewBasicBlock(ib, nullptr /*bbInfo*/);

        uint32_t numArgs = m_codeBlock->m_owner->m_numFixedArguments;
        if (m_inlinedCallFrame->IsRootFrame())
        {
            // If we are the root frame, we need to initialize local [0, numArgs) with the argument values
            // As usual, we need 2-phase commit
            //
            m_isOSRExitOK = false;
            for (size_t localOrd = 0; localOrd < numArgs; localOrd++)
            {
                Node* shadowStore = Node::CreateShadowStoreNode(GetInterpreterSlotForLocalOrd(localOrd), m_graph->GetArgumentNode(localOrd));
                SetupNodeCommonInfoAndPushBack(shadowStore);
            }

            // We've set up the ShadowStores for the arguments, now we are allowed to exit now
            //
            m_isOSRExitOK = true;
            for (size_t localOrd = 0; localOrd < numArgs; localOrd++)
            {
                Node* setLocal = Node::CreateSetLocalNode(m_inlinedCallFrame, InterpreterFrameLocation::Local(localOrd), m_graph->GetArgumentNode(localOrd));
                SetupNodeCommonInfoAndPushBack(setLocal);
            }
        }

        m_isOSRExitOK = true;

        // If the entry block already have captures at head, we need to
        // create the CaptureVar for these variables as well
        //
        for (size_t localOrd = 0; localOrd < numLocals; localOrd++)
        {
            if (bbUvInfo->m_isLocalCapturedAtHead.IsSet(localOrd))
            {
                Value closureVarInitValue;
                if (localOrd < numArgs)
                {
                    if (m_inlinedCallFrame->IsRootFrame())
                    {
                        closureVarInitValue = m_graph->GetArgumentNode(localOrd);
                    }
                    else
                    {
                        // The speculative inliner is responsible for setting up the values before
                        // branching to this BB, we just need to do GetLocal
                        //
                        Node* getLocal = Node::CreateGetLocalNode(m_inlinedCallFrame, InterpreterFrameLocation::Local(localOrd));
                        SetupNodeCommonInfoAndPushBack(getLocal);
                        closureVarInitValue = Value(getLocal, 0 /*outputOrd*/);
                    }
                }
                else
                {
                    closureVarInitValue = m_graph->GetUndefValue();
                }

                Node* createClosureVar = Node::CreateCreateCapturedVarNode(closureVarInitValue);
                SetupNodeCommonInfoAndPushBack(createClosureVar);
                Node* shadowStore = Node::CreateShadowStoreNode(GetInterpreterSlotForLocalOrd(localOrd), Value(createClosureVar, 0 /*outputOrd*/));
                SetupNodeCommonInfoAndPushBack(shadowStore);
                // This SetLocal shouldn't exit, but even if does, we are still in a consistent state,
                // since bytecode execution is agnostic of which local is captured by an upvalue.
                //
                Node* setLocal = Node::CreateSetLocalNode(m_inlinedCallFrame, InterpreterFrameLocation::Local(localOrd), Value(createClosureVar, 0 /*outputOrd*/));
                SetupNodeCommonInfoAndPushBack(setLocal);
            }
        }

        // If we are the root frame, we always create this entry block even if it's empty,
        // so that we satisfy the requirement that the function entry block is not a valid target for branch
        // CFG cleanup will remove this block if possible.
        //
        if (ib->m_nodes.empty() && m_inlinedCallFrame->IsRootFrame())
        {
            m_isOSRExitOK = false;
            Node* nopNode = Node::CreateNoopNode();
            SetupNodeCommonInfoAndPushBack(nopNode);
            TestAssert(!ib->m_nodes.empty());
        }

        if (ib->m_nodes.empty())
        {
            m_functionEntry = mainBlockEntry;
        }
        else
        {
            m_functionEntry = ib;
            m_allBBs.push_back(ib);
        }

        StartNewBasicBlock(nullptr /*bb*/, nullptr /*bbInfo*/);
    }
}

void DfgBuildBasicBlockContext::Finalize()
{
#ifdef TESTBUILD
    for (size_t i = 0; i < m_numPrimBasicBlocks; i++)
    {
        TestAssert(m_primBBs[i] != nullptr);
    }
#endif

    for (const auto& item : m_branchDestPatchList)
    {
        ArenaPtr<BasicBlock>* dest = item.first;
        uint32_t bbOrd = item.second;
        TestAssert(bbOrd < m_numPrimBasicBlocks);
        *dest = m_primBBs[bbOrd];
    }
}

}   // namespace dfg
