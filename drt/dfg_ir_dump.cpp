#include "dfg_ir_dump.h"
#include "bytecode_builder.h"
#include "temp_arena_allocator.h"
#include "dfg_ir_validator.h"

namespace dfg {

using BCKind = DeegenBytecodeBuilder::BCKind;

void DumpDfgIrGraph(FILE* file, Graph* graph, const DumpIrOptions& dumpIrOptions)
{
    bool wellFormed;
    if (dumpIrOptions.forValidationErrorDump)
    {
        wellFormed = false;
    }
    else
    {
        wellFormed = ValidateDfgIrGraph(graph);
    }

    if (!wellFormed)
    {
        fprintf(file, "[DUMP] WARNING: DFG IR validation failed, the dump may be misleading in obvious or subtle ways!\n");
    }

    TempArenaAllocator alloc;

    // Dump all the InlinedCallFrames
    //
    {
        size_t numICF = graph->GetNumInlinedCallFrames();
        TestAssert(numICF > 0);

        for (size_t icfOrd = 1; icfOrd < numICF; icfOrd++)
        {
            InlinedCallFrame* frame = graph->GetInlinedCallFrameFromOrdinal(icfOrd);
            fprintf(file, "InlinedCallFrame #%d:\n", static_cast<int>(icfOrd));
            fprintf(file, "    CallSite: bc#%u@%u (icSite%d)\n",
                    static_cast<unsigned int>(frame->GetCallerCodeOrigin().GetBytecodeIndex()),
                    static_cast<unsigned int>(frame->GetCallerCodeOrigin().GetInlinedCallFrame()->GetInlineCallFrameOrdinal()),
                    static_cast<int>(frame->GetCallerBytecodeCallSiteOrdinal()));
            fprintf(file, "    Property: %s",
                    (frame->IsDirectCall() ? "direct" : "closure"));
            if (!frame->IsDirectCall())
            {
                fprintf(file, " (loc%d)", static_cast<int>(frame->GetClosureCallFunctionObjectRegister().Value()));
            }
            fprintf(file, ", %s\n", frame->IsTailCall() ? "tail" : "non-tail");
            fprintf(file, "    #VarArgs: ");
            if (frame->StaticallyKnowsNumVarArgs())
            {
                fprintf(file, "statically known, %d\n", static_cast<int>(frame->GetNumVarArgs()));
            }
            else
            {
                fprintf(file, "speculate <= %d, actual number in loc%d\n",
                        static_cast<int>(frame->MaxVarArgsAllowed()),
                        static_cast<int>(frame->GetNumVarArgsRegister().Value()));
            }
            fprintf(file, "    InterpBase: %d\n", static_cast<int>(frame->GetInterpreterSlotForStackFrameBase().Value()));
            fprintf(file, "    Locals: ");
            for (size_t i = 0; i < frame->GetCodeBlock()->m_stackFrameNumSlots; i++)
            {
                if (i > 0) { fprintf(file, ", "); }
                fprintf(file, "loc%d", static_cast<int>(frame->GetRegisterForLocalOrd(i).Value()));
            }
            fprintf(file, "\n");

            if (frame->MaxVarArgsAllowed() > 0)
            {
                fprintf(file, "    VarArgs: ");
                for (size_t i = 0; i < frame->MaxVarArgsAllowed(); i++)
                {
                    if (i > 0) { fprintf(file, ", "); }
                    fprintf(file, "loc%d", static_cast<int>(frame->GetRegisterForVarArgOrd(i).Value()));
                }
                fprintf(file, "\n");
            }
            fprintf(file, "\n");
        }
    }

    // Sort all the basic blocks breadth-first for human readability
    //
    TempVector<BasicBlock*> BBs(alloc);
    {
        TempUnorderedSet<BasicBlock*> visited(alloc);
        TempQueue<BasicBlock*> q(alloc);
        auto pushQueue = [&](BasicBlock* bb) ALWAYS_INLINE
        {
            if (visited.count(bb))
            {
                return;
            }
            visited.insert(bb);
            BBs.push_back(bb);
            q.push(bb);
        };

        TestAssert(graph->m_blocks.size() > 0);
        pushQueue(graph->m_blocks[0]);

        while (!q.empty())
        {
            BasicBlock* bb = q.front();
            q.pop();
            for (size_t i = 0; i < bb->GetNumSuccessors(); i++)
            {
                BasicBlock* successor = bb->m_successors[i];
                pushQueue(successor);
            }
        }

        TestAssert(visited.size() == BBs.size());
        TestAssertImp(wellFormed, BBs.size() == graph->m_blocks.size());

        if (BBs.size() != graph->m_blocks.size())
        {
            for (BasicBlock* bb : graph->m_blocks)
            {
                if (!visited.count(bb))
                {
                    BBs.push_back(bb);
                    visited.insert(bb);
                }
            }
        }
        TestAssert(BBs.size() == graph->m_blocks.size());
    }

    TempUnorderedMap<BasicBlock*, size_t> bbLabelMap(alloc);
    for (size_t i = 0; i < BBs.size(); i++)
    {
        TestAssert(!bbLabelMap.count(BBs[i]));
        bbLabelMap[BBs[i]] = i;
    }

    TempUnorderedMap<Node*, size_t> ssaLabelMap(alloc);
    {
        size_t nodeOrd = 0;
        for (BasicBlock* bb : BBs)
        {
            for (Node* node : bb->m_nodes)
            {
                if (node->IsConstantLikeNode())
                {
                    TestAssert(!wellFormed);
                    continue;
                }
                TestAssertImp(wellFormed, !ssaLabelMap.count(node));
                ssaLabelMap[node] = nodeOrd;
                nodeOrd += node->GetNumExtraOutputs() + (node->HasDirectOutput() ? 1 : 0);
            }
        }
    }

    TempUnorderedMap<Node*, size_t> vrLabelMap(alloc);
    {
        size_t nodeOrd = 0;
        for (BasicBlock* bb : BBs)
        {
            for (Node* node : bb->m_nodes)
            {
                if (node->IsNodeGeneratesVR())
                {
                    TestAssertImp(wellFormed, !vrLabelMap.count(node));
                    vrLabelMap[node] = nodeOrd;
                    nodeOrd++;
                }
            }
        }
    }

    TempUnorderedMap<LogicalVariableInfo*, size_t> logicalVarLabelMap(alloc);
    {
        size_t logicalVarOrd = 0;
        for (BasicBlock* bb : BBs)
        {
            for (Node* node : bb->m_nodes)
            {
                if (node->HasLogicalVariableInfo())
                {
                    LogicalVariableInfo* logicalVar = node->Debug_TryGetLogicalVariableInfo();
                    if (logicalVar != nullptr)
                    {
                        if (!logicalVarLabelMap.count(logicalVar))
                        {
                            logicalVarLabelMap[logicalVar] = logicalVarOrd;
                            logicalVarOrd++;
                        }
                    }
                }
            }
        }
    }

    TempVector<TempVector<BasicBlock*>> bbPreds(alloc);
    bbPreds.resize(BBs.size(), TempVector<BasicBlock*>(alloc));

    for (size_t bbOrd = 0; bbOrd < BBs.size(); bbOrd++)
    {
        for (size_t i = 0; i < BBs[bbOrd]->GetNumSuccessors(); i++)
        {
            BasicBlock* succ = BBs[bbOrd]->m_successors[i];
            TestAssertImp(wellFormed, bbLabelMap.count(succ));
            if (bbLabelMap.count(succ))
            {
                bbPreds[bbLabelMap[succ]].push_back(BBs[bbOrd]);
            }
        }
    }

    for (BasicBlock* bb : BBs)
    {
        fprintf(file, "%s.BB_%d:                ; pred =",
                (bb == dumpIrOptions.highlightBB ? "[!] " : ""),
                static_cast<int>(bbLabelMap[bb]));
        for (BasicBlock* pred : bbPreds[bbLabelMap[bb]])
        {
            TestAssert(bbLabelMap.count(pred));
            fprintf(file, " .BB_%d", static_cast<int>(bbLabelMap[pred]));
        }
        if (bbPreds[bbLabelMap[bb]].empty())
        {
            fprintf(file, " (none)");
        }
        fprintf(file, "\n");

        for (Node* curNode : bb->m_nodes)
        {
            if (curNode == dumpIrOptions.highlightNode)
            {
                fprintf(file, "[!] ");
            }
            else
            {
                fprintf(file, "    ");
            }

            // Print the output part
            //
            {
                size_t numNodeOutputs = curNode->GetNumExtraOutputs() + (curNode->HasDirectOutput() ? 1 : 0);
                size_t nodeOutputStart = ssaLabelMap[curNode];
                if (numNodeOutputs > 0)
                {
                    if (numNodeOutputs == 1 && !curNode->IsNodeGeneratesVR())
                    {
                        fprintf(file, "%%%d := ", static_cast<int>(nodeOutputStart));
                    }
                    else
                    {
                        fprintf(file, "<");
                        for (size_t i = nodeOutputStart; i < nodeOutputStart + numNodeOutputs; i++)
                        {
                            if (i != nodeOutputStart) { fprintf(file, ", "); }
                            fprintf(file, "%%%d", static_cast<int>(i));
                        }
                        if (curNode->IsNodeGeneratesVR())
                        {
                            TestAssert(vrLabelMap.count(curNode));
                            fprintf(file, ", vr%%%d", static_cast<int>(vrLabelMap[curNode]));
                        }
                        fprintf(file, "> := ");
                    }
                }
                else
                {
                    if (curNode->IsNodeGeneratesVR())
                    {
                        TestAssert(vrLabelMap.count(curNode));
                        fprintf(file, "vr%%%d := ", static_cast<int>(vrLabelMap[curNode]));
                    }
                }
            }

            // Print the node name
            //
            if (curNode->IsBuiltinNodeKind())
            {
                fprintf(file, "%s", GetDfgBuiltinNodeKindName(curNode->GetNodeKind()));
            }
            else
            {
                BCKind bcKind = curNode->GetGuestLanguageBCKind();
                TestAssert(bcKind < BCKind::X_END_OF_ENUM);
                fprintf(file, "%s", DeegenBytecodeBuilder::GetBytecodeHumanReadableNameFromBCKind(bcKind));
            }

            fprintf(file, "(");

            // Print the input nodes
            //
            {
                uint32_t numInputs = curNode->GetNumInputs();
                for (uint32_t i = 0; i < numInputs; i++)
                {
                    if (i != 0) { fprintf(file, ", "); }
                    Edge e = curNode->GetInputEdge(i);
                    Node* inputNode = e.GetOperand();
                    if (inputNode->IsConstantLikeNode())
                    {
                        if (e.GetOutputOrdinal() != 0)
                        {
                            TestAssert(!wellFormed);
                            fprintf(file, "**CORRUPT**");
                            continue;
                        }
                        if (inputNode->IsConstantNode())
                        {
                            TValue val = inputNode->GetConstantNodeValue();
                            if (val.IsInt32())
                            {
                                fprintf(file, "Int32(%d)", static_cast<int>(val.AsInt32()));
                            }
                            else if (val.IsDouble())
                            {
                                double dbl = val.AsDouble();
                                char buf[x_default_tostring_buffersize_double];
                                StringifyDoubleUsingDefaultLuaFormattingOptions(buf /*out*/, dbl);
                                fprintf(file, "Double(%s)", buf);
                            }
                            else if (val.IsMIV())
                            {
                                // TODO: unfortunately this is currently coupled with the guest language definition
                                //
                                MiscImmediateValue miv = val.AsMIV();
                                if (miv.IsNil())
                                {
                                    fprintf(file, "Nil");
                                }
                                else
                                {
                                    assert(miv.IsBoolean());
                                    fprintf(file, "%s", (miv.GetBooleanValue() ? "True" : "False"));
                                }
                            }
                            else
                            {
                                // TODO: unfortunately this is currently coupled with the guest language definition
                                //
                                assert(val.IsPointer());
                                UserHeapGcObjectHeader* p = TranslateToRawPointer(val.AsPointer<UserHeapGcObjectHeader>().As());
                                if (p->m_type == HeapEntityType::String)
                                {
                                    HeapString* hs = reinterpret_cast<HeapString*>(p);
                                    fprintf(file, "\"");
                                    fwrite(hs->m_string, sizeof(char), hs->m_length /*length*/, file);
                                    fprintf(file, "\"");
                                }
                                else
                                {
                                    if (p->m_type == HeapEntityType::Function)
                                    {
                                        fprintf(file, "Function(0x");
                                    }
                                    else if (p->m_type == HeapEntityType::Table)
                                    {
                                        fprintf(file, "Table(0x");
                                    }
                                    else if (p->m_type == HeapEntityType::Thread)
                                    {
                                        fprintf(file, "Thread(0x");
                                    }
                                    else
                                    {
                                        fprintf(file, "(ObjectType%d)(0x", static_cast<int>(p->m_type));
                                    }
                                    fprintf(file, "%p)", static_cast<void*>(p));
                                }
                            }
                        }
                        else if (inputNode->IsUnboxedConstantNode())
                        {
                            fprintf(file, "%llu", static_cast<unsigned long long>(inputNode->GetNodeParamAsUInt64()));
                        }
                        else if (inputNode->IsUndefValueNode())
                        {
                            fprintf(file, "UndefVal");
                        }
                        else if (inputNode->IsArgumentNode())
                        {
                            fprintf(file, "Argument(%d)", static_cast<int>(inputNode->GetArgumentOrdinal()));
                        }
                        else if (inputNode->IsGetNumVarArgsNode())
                        {
                            fprintf(file, "NumVarArgs");
                        }
                        else if (inputNode->IsGetKthVarArgNode())
                        {
                            fprintf(file, "VarArg(%d)", static_cast<int>(inputNode->GetNodeParamAsUInt64()));
                        }
                        else if (inputNode->IsGetFunctionObjectNode())
                        {
                            fprintf(file, "FnObject");
                        }
                        else
                        {
                            ReleaseAssert(false && "unhandled node type!");
                        }
                    }
                    else
                    {
                        if (!ssaLabelMap.count(inputNode))
                        {
                            TestAssert(!wellFormed);
                            fprintf(file, "**CORRUPT**");
                            continue;
                        }
                        uint32_t ord = e.GetOutputOrdinal();
                        uint32_t ordStart = inputNode->HasDirectOutput() ?  0 : 1;
                        if (!(ordStart <= ord && ord <= inputNode->GetNumExtraOutputs()))
                        {
                            TestAssert(!wellFormed);
                            fprintf(file, "**CORRUPT**");
                            continue;
                        }
                        size_t varOrd = ssaLabelMap[inputNode] + ord - ordStart;
                        fprintf(file, "%%%d", static_cast<int>(varOrd));
                    }
                }
            }

            bool shouldPrefixCommaForFirstItem = (curNode->GetNumInputs() > 0);

            if (curNode->IsNodeAccessesVR())
            {
                Node* vrNode = curNode->GetVariadicResultInputNode();
                if (vrNode == nullptr)
                {
                    fprintf(file, "%svrNode = nullptr", (shouldPrefixCommaForFirstItem ? ", " : ""));
                }
                else if (vrNode->IsConstantLikeNode())
                {
                    TestAssert(!wellFormed);
                    fprintf(file, "%svrNode = **CORRUPT**", (shouldPrefixCommaForFirstItem ? ", " : ""));
                }
                else
                {
                    if (!vrLabelMap.count(vrNode))
                    {
                        TestAssert(!wellFormed);
                        fprintf(file, "%svrNode = **CORRUPT**", (shouldPrefixCommaForFirstItem ? ", " : ""));
                    }
                    else
                    {
                        size_t varOrd = vrLabelMap[vrNode];
                        fprintf(file, "%s%%vr%d", (shouldPrefixCommaForFirstItem ? ", " : ""), static_cast<int>(varOrd));
                    }
                }
                shouldPrefixCommaForFirstItem = true;
            }

            // Print the parameters
            //
            if (curNode->IsBuiltinNodeKind())
            {
                switch (curNode->GetNodeKind())
                {
                case NodeKind_Constant:
                case NodeKind_UnboxedConstant:
                case NodeKind_UndefValue:
                case NodeKind_Argument:
                case NodeKind_GetNumVariadicArgs:
                case NodeKind_GetKthVariadicArg:
                case NodeKind_GetFunctionObject:
                {
                    TestAssert(!wellFormed);
                    break;
                }
                case NodeKind_GetLocal:
                case NodeKind_SetLocal:
                {
                    Node::LocalVarAccessInfo info = curNode->Debug_GetLocalVarAccessInfo();
                    size_t localOrd;
                    size_t interpreterSlotOrd;
                    if (info.IsLogicalVariableInfoPointerSetUp())
                    {
                        localOrd = info.GetLogicalVariableInfo()->m_localOrd;
                        interpreterSlotOrd = info.GetLogicalVariableInfo()->m_interpreterSlotOrd;
                        TestAssert(localOrd == info.GetVirtualRegister().Value());
                        TestAssert(interpreterSlotOrd == info.GetInterpreterSlot().Value());
                    }
                    else
                    {
                        localOrd = info.GetVirtualRegister().Value();
                        interpreterSlotOrd = info.GetInterpreterSlot().Value();
                    }
                    std::ignore = interpreterSlotOrd;

                    fprintf(file, "%sloc%d",
                            (curNode->GetNodeKind() == NodeKind_GetLocal ? "" : " => "), static_cast<int>(localOrd));
                    shouldPrefixCommaForFirstItem = true;
                    break;
                }
                case NodeKind_ShadowStore:
                {
                    size_t interpreterSlot = curNode->GetShadowStoreInterpreterSlotOrd().Value();
                    fprintf(file, " ~> slot%llu", static_cast<unsigned long long>(interpreterSlot));
                    shouldPrefixCommaForFirstItem = true;
                    break;
                }
                case NodeKind_Nop:
                case NodeKind_CreateCapturedVar:
                case NodeKind_GetCapturedVar:
                case NodeKind_SetCapturedVar:
                case NodeKind_GetNumVariadicRes:
                case NodeKind_PrependVariadicRes:
                case NodeKind_Return:
                {
                    break;
                }
                case NodeKind_GetKthVariadicRes:
                {
                    fprintf(file, "%llu", static_cast<unsigned long long>(curNode->GetNodeParamAsUInt64()));
                    shouldPrefixCommaForFirstItem = true;
                    break;
                }
                case NodeKind_CreateVariadicRes:
                {
                    fprintf(file, ", numFixedItms=%llu", static_cast<unsigned long long>(curNode->GetNodeParamAsUInt64()));
                    shouldPrefixCommaForFirstItem = true;
                    break;
                }
                case NodeKind_CheckU64InBound:
                {
                    fprintf(file, " <= %llu", static_cast<unsigned long long>(curNode->GetNodeParamAsUInt64()));
                    shouldPrefixCommaForFirstItem = true;
                    break;
                }
                case NodeKind_U64SaturateSub:
                {
                    fprintf(file, " - (%lld)", static_cast<long long>(curNode->GetNodeParamAsUInt64()));
                    shouldPrefixCommaForFirstItem = true;
                    break;
                }
                case NodeKind_CreateFunctionObject:
                {
                    fprintf(file, ", sr=%lld", static_cast<long long>(curNode->GetNodeParamAsUInt64()));
                    shouldPrefixCommaForFirstItem = true;
                    break;
                }
                case NodeKind_GetUpvalue:
                {
                    Node::UpvalueInfo info = curNode->GetInfoForGetUpvalue();
                    fprintf(file, ", ord=%u, mutable=%s",
                            static_cast<unsigned int>(info.m_ordinal),
                            (info.m_isImmutable ? "false" : "true"));
                    shouldPrefixCommaForFirstItem = true;
                    break;
                }
                case NodeKind_SetUpvalue:
                {
                    fprintf(file, ", ord=%u", static_cast<unsigned int>(curNode->GetNodeParamAsUInt64()));
                    shouldPrefixCommaForFirstItem = true;
                    break;
                }
                default:
                {
                    ReleaseAssert(false);
                }
                }
            }
            else
            {
                DeegenBytecodeBuilder::BytecodeDecoder::DumpDfgNodeSpecificData(
                    curNode->GetGuestLanguageBCKind(),
                    file,
                    curNode->GetNodeSpecificData(),
                    shouldPrefixCommaForFirstItem);
            }

            if (shouldPrefixCommaForFirstItem)
            {
                fprintf(file, ", ");
            }

            if (!curNode->GetOsrExitDest().IsBranchDest() &&
                curNode->GetNodeOrigin() == curNode->GetOsrExitDest().GetNormalDestination())
            {
                fprintf(file, "bc#%u", static_cast<unsigned int>(curNode->GetNodeOrigin().GetBytecodeIndex()));
                if (!curNode->GetNodeOrigin().GetInlinedCallFrame()->IsRootFrame())
                {
                    fprintf(file, "@%u", static_cast<unsigned int>(curNode->GetNodeOrigin().GetInlinedCallFrame()->GetInlineCallFrameOrdinal()));
                }
            }
            else
            {
                fprintf(file, "bc#%u", static_cast<unsigned int>(curNode->GetNodeOrigin().GetBytecodeIndex()));
                if (!curNode->GetNodeOrigin().GetInlinedCallFrame()->IsRootFrame())
                {
                    fprintf(file, "@%u", static_cast<unsigned int>(curNode->GetNodeOrigin().GetInlinedCallFrame()->GetInlineCallFrameOrdinal()));
                }
                fprintf(file, ", Exit: ");
                if (curNode->GetOsrExitDest().IsBranchDest())
                {
                    fprintf(file, "BrDest(bc#%u", static_cast<unsigned int>(curNode->GetOsrExitDest().GetBranchBytecodeOrigin().GetBytecodeIndex()));
                    if (!curNode->GetOsrExitDest().GetBranchBytecodeOrigin().GetInlinedCallFrame()->IsRootFrame())
                    {
                        fprintf(file, "@%u", static_cast<unsigned int>(curNode->GetOsrExitDest().GetBranchBytecodeOrigin().GetInlinedCallFrame()->GetInlineCallFrameOrdinal()));
                    }
                    fprintf(file, ")");
                }
                else
                {
                    fprintf(file, "bc#%u", static_cast<unsigned int>(curNode->GetOsrExitDest().GetNormalDestination().GetBytecodeIndex()));
                    if (!curNode->GetOsrExitDest().GetNormalDestination().GetInlinedCallFrame()->IsRootFrame())
                    {
                        fprintf(file, "@%u", static_cast<unsigned int>(curNode->GetOsrExitDest().GetNormalDestination().GetInlinedCallFrame()->GetInlineCallFrameOrdinal()));
                    }
                }
            }

            if (curNode->IsExitOK())
            {
                if (curNode->MayOsrExit())
                {
                    fprintf(file, ", Exits");
                }
                else
                {
                    fprintf(file, ", ExitOK");
                }
            }
            else
            {
                if (curNode->MayOsrExit())
                {
                    TestAssert(!wellFormed);
                    fprintf(file, ", **ExitCorrupted**");
                }
                else
                {
                    fprintf(file, ", ExitInvalid");
                }
            }

            fprintf(file, ")");

            if (curNode->IsGetLocalNode() || curNode->IsSetLocalNode())
            {
                fprintf(file, "  ; lovar");
                Node::LocalVarAccessInfo info = curNode->Debug_GetLocalVarAccessInfo();
                if (info.IsLogicalVariableInfoPointerSetUp())
                {
                    LogicalVariableInfo* lvi = info.GetLogicalVariableInfo();
                    TestAssert(logicalVarLabelMap.count(lvi));
                    fprintf(file, "#%d)", static_cast<int>(logicalVarLabelMap[lvi]));
                }
                else
                {
                    fprintf(file, " not setup");
                }
                fprintf(file, ", slot%d", static_cast<int>(info.GetInterpreterSlot().Value()));
            }

            fprintf(file, "\n");
        }

        fprintf(file, "    --> ");
        if (bb->GetNumSuccessors() == 0)
        {
            fprintf(file, "(no successors)");
        }
        else
        {
            for (size_t i = 0; i < bb->GetNumSuccessors(); i++)
            {
                if (i != 0) { fprintf(file, ", "); }
                BasicBlock* succ = bb->m_successors[i];
                if (!bbLabelMap.count(succ))
                {
                    TestAssert(!wellFormed);
                    fprintf(file, "**CORRUPT**");
                    continue;
                }
                fprintf(file, "BB_%d", static_cast<int>(bbLabelMap[succ]));
            }
        }
        fprintf(file, "\n\n");
    }
}

}   // namespace dfg
