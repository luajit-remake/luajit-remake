#include "dfg_bytecode_liveness.h"
#include "dfg_control_flow_and_upvalue_analysis.h"
#include "bytecode_builder.h"

namespace dfg {

// Takes two bitvectors of equal length as input.
// This function does the following:
// (1) Assert that 'copyFrom' is a superset of 'bv'
// (2) Check if 'copyFrom' and 'bv' are different
// (3) Set bv = copyFrom
//
// Returns true if 'copyFrom' and 'bv' are different
//
static bool WARN_UNUSED UpdateBitVectorAfterMonotonicPropagation(TempBitVector& bv /*inout*/, const TempBitVector& copyFrom)
{
    TestAssert(bv.m_length == copyFrom.m_length);
    TestAssert(copyFrom.m_data.get() != bv.m_data.get());
    size_t bvAllocLength = bv.GetAllocLength();
    ConstRestrictPtr<uint64_t> copyFromData = copyFrom.m_data.get();
    RestrictPtr<uint64_t> bvData = bv.m_data.get();
    bool changed = false;
    for (size_t i = 0; i < bvAllocLength; i++)
    {
        TestAssert((bvData[i] & copyFromData[i]) == bvData[i]);
        changed |= (bvData[i] != copyFromData[i]);
        bvData[i] = copyFromData[i];
    }
    return changed;
}

struct BytecodeLivenessBBInfo
{
    size_t m_numBytecodesInBB;
    size_t m_firstBytecodeIndex;
    // The uses and defs of the k-th bytecode can be parsed as the following:
    // Uses: [m_infoIndex[k*2], m_infoIndex[k*2+1]) of m_info where m_infoIndex[0] is 0
    // Defs: [m_infoIndex[k*2+1], m_infoIndex[k*2+2]) of m_info
    //
    TempVector<uint32_t> m_info;
    uint32_t* m_infoIndex;

    // Liveness state at block head/tail
    //
    TempBitVector m_atHead;
    TempBitVector m_atTail;

    // Once m_atTail is updated,
    // m_atHead should be updated by m_atTail & m_andMask | m_orMask
    //
    TempBitVector m_andMask;
    TempBitVector m_orMask;

    // Note: Successor and predecessor information are populated by outside logic
    //
    BytecodeLivenessBBInfo** m_successors;
    size_t m_numSuccessors;

    // if m_lastCheckedEpoch is greater than all the m_lastChangedEpoch of its successors, this node has work to update.
    //
    size_t m_lastChangedEpoch;
    size_t m_lastCheckedEpoch;

    bool m_hasPrecedessor;

    BytecodeLivenessBBInfo(TempArenaAllocator& alloc,
                           DeegenBytecodeBuilder::BytecodeDecoder& decoder,
                           BasicBlockUpvalueInfo* bbInfo,
                           size_t numLocals)
        : m_info(alloc)
    {
        m_numBytecodesInBB = bbInfo->m_numBytecodesInBB;
        TestAssert(m_numBytecodesInBB > 0);

        m_firstBytecodeIndex = bbInfo->m_bytecodeIndex;

        m_infoIndex = alloc.AllocateArray<uint32_t>(m_numBytecodesInBB * 2 + 1);
        m_infoIndex[0] = 0;

        m_atHead.Reset(alloc, numLocals);
        m_atTail.Reset(alloc, numLocals);
        m_successors = alloc.AllocateArray<BytecodeLivenessBBInfo*>(bbInfo->m_numSuccessors);
        m_numSuccessors = bbInfo->m_numSuccessors;
        m_hasPrecedessor = false;

        m_lastChangedEpoch = 0;
        m_lastCheckedEpoch = 0;

        bool* isCaptured = alloc.AllocateArray<bool>(numLocals);
        for (size_t localOrd = 0; localOrd < numLocals; localOrd++)
        {
            isCaptured[localOrd] = bbInfo->m_isLocalCapturedAtHead.IsSet(localOrd);
        }

        size_t curBytecodeOffset = bbInfo->m_bytecodeOffset;
        for (size_t index = 0; index < m_numBytecodesInBB; index++)
        {
            TestAssertIff(index == m_numBytecodesInBB - 1, curBytecodeOffset == bbInfo->m_terminalNodeBcOffset);

            if (decoder.IsBytecodeIntrinsic<BytecodeIntrinsicInfo::CreateClosure>(curBytecodeOffset))
            {
                // Special handling: CreateClosure intrinsic uses all the locals it captures.
                // The self-reference local (if it exists) is special: for immutable self-reference, it is never used (but def'ed by the CreateClosure).
                // For mutable self-reference, if the local is already a CapturedVar, it is used.
                // But if the local is not a CapturedVar, it is not used (but def'ed by the CreateClosure).
                //
                BytecodeIntrinsicInfo::CreateClosure info = decoder.GetBytecodeIntrinsicInfo<BytecodeIntrinsicInfo::CreateClosure>(curBytecodeOffset);
                TestAssert(info.proto.IsConstant());
                UnlinkedCodeBlock* createClosureUcb = reinterpret_cast<UnlinkedCodeBlock*>(info.proto.AsConstant().m_value);
                TestAssert(createClosureUcb != nullptr);

#ifdef TESTBUILD
                BytecodeRWCInfo inputs = decoder.GetDataFlowReadInfo(curBytecodeOffset);
                TestAssert(inputs.GetNumItems() == 1);
                TestAssert(inputs.GetDesc(0).IsConstant());
#endif
                BytecodeRWCInfo outputs = decoder.GetDataFlowWriteInfo(curBytecodeOffset);
                TestAssert(outputs.GetNumItems() == 1);
                BytecodeRWCDesc item = outputs.GetDesc(0 /*itemOrd*/);
                TestAssert(item.IsLocal());
                size_t destLocalOrd = item.GetLocalOrd();
                TestAssert(destLocalOrd < numLocals);

                for (size_t uvOrd = 0; uvOrd < createClosureUcb->m_numUpvalues; uvOrd++)
                {
                    UpvalueMetadata& uvmt = createClosureUcb->m_upvalueInfo[uvOrd];
                    if (uvmt.m_isParentLocal)
                    {
                        TestAssert(uvmt.m_slot < numLocals);
                        if (uvmt.m_slot == destLocalOrd)
                        {
                            // This is a self-reference.
                            // It is only used if it is a mutable self-reference and the local is already a CapturedVar
                            //
                            if (!uvmt.m_isImmutable && isCaptured[uvmt.m_slot])
                            {
                                m_info.push_back(uvmt.m_slot);
                            }
                        }
                        else
                        {
                            m_info.push_back(uvmt.m_slot);
                        }
                    }
                }
                m_infoIndex[index * 2 + 1] = SafeIntegerCast<uint32_t>(m_info.size());

                for (size_t uvOrd = 0; uvOrd < createClosureUcb->m_numUpvalues; uvOrd++)
                {
                    UpvalueMetadata& uvmt = createClosureUcb->m_upvalueInfo[uvOrd];
                    if (uvmt.m_isParentLocal)
                    {
                        TestAssert(uvmt.m_slot < numLocals);
                        // If the local is converted to a CapturedVar by this CreateClosure, it is def'ed
                        //
                        if (!uvmt.m_isImmutable)
                        {
                            if (!isCaptured[uvmt.m_slot])
                            {
                                m_info.push_back(uvmt.m_slot);
                                isCaptured[uvmt.m_slot] = true;
                            }
                        }
                    }
                }

                // If the destination slot is not a CapturedVar, it is also def'ed
                //
                if (!isCaptured[destLocalOrd])
                {
                    m_info.push_back(SafeIntegerCast<uint32_t>(destLocalOrd));
                }

                m_infoIndex[index * 2 + 2] = SafeIntegerCast<uint32_t>(m_info.size());
            }
            else if (decoder.IsBytecodeIntrinsic<BytecodeIntrinsicInfo::UpvalueClose>(curBytecodeOffset))
            {
                // Special handling: UpvalueClose intrinsic uses and defs all the captured locals that it closes
                //
                TestAssert(curBytecodeOffset == bbInfo->m_terminalNodeBcOffset);
                TestAssert(decoder.GetDataFlowReadInfo(curBytecodeOffset).GetNumItems() == 0);
                TestAssert(decoder.GetDataFlowWriteInfo(curBytecodeOffset).GetNumItems() == 0);
                BytecodeIntrinsicInfo::UpvalueClose info = decoder.GetBytecodeIntrinsicInfo<BytecodeIntrinsicInfo::UpvalueClose>(curBytecodeOffset);
                TestAssert(info.start.IsLocal());
                size_t uvCloseStart = info.start.AsLocal();
                TestAssert(uvCloseStart <= numLocals);

                for (size_t localOrd = uvCloseStart; localOrd < numLocals; localOrd++)
                {
                    if (isCaptured[localOrd])
                    {
                        // This value is captured before the UpvalueClose and closed by the upvalue.
                        // This means that UpvalueClose needs to read the CapturedVar stored in this local (use it),
                        // then write to the local the value stored in the CapturedVar (def it)
                        //
                        m_info.push_back(SafeIntegerCast<uint32_t>(localOrd));
                    }
                }
                m_infoIndex[index * 2 + 1] = SafeIntegerCast<uint32_t>(m_info.size());

                for (size_t localOrd = uvCloseStart; localOrd < numLocals; localOrd++)
                {
                    if (isCaptured[localOrd])
                    {
                        m_info.push_back(SafeIntegerCast<uint32_t>(localOrd));
                        isCaptured[localOrd] = false;
                    }
                }
                m_infoIndex[index * 2 + 2] = SafeIntegerCast<uint32_t>(m_info.size());
            }
            else
            {
                // This is a conventional bytecode
                // Generate the uses of the bytecode
                //
                {
                    BytecodeRWCInfo inputs = decoder.GetDataFlowReadInfo(curBytecodeOffset);
                    for (size_t itemOrd = 0; itemOrd < inputs.GetNumItems(); itemOrd++)
                    {
                        BytecodeRWCDesc item = inputs.GetDesc(itemOrd);
                        if (item.IsLocal())
                        {
                            m_info.push_back(SafeIntegerCast<uint32_t>(item.GetLocalOrd()));
                        }
                        else if (item.IsRange())
                        {
                            TestAssert(item.GetRangeLength() >= 0);
                            for (size_t i = 0; i < static_cast<size_t>(item.GetRangeLength()); i++)
                            {
                                m_info.push_back(SafeIntegerCast<uint32_t>(item.GetRangeStart() + i));
                            }
                        }
                    }
                    m_infoIndex[index * 2 + 1] = SafeIntegerCast<uint32_t>(m_info.size());
                }

                // Generate the defs of the bytecode
                // Note that a local is only def'ed if it is not captured:
                // if it is captured, we are writing into the CapturedVar, not the local itself!
                //
                {
                    BytecodeRWCInfo outputs = decoder.GetDataFlowWriteInfo(curBytecodeOffset);
                    for (size_t itemOrd = 0; itemOrd < outputs.GetNumItems(); itemOrd++)
                    {
                        BytecodeRWCDesc item = outputs.GetDesc(itemOrd);
                        if (item.IsLocal())
                        {
                            size_t localOrd = item.GetLocalOrd();
                            TestAssert(localOrd < numLocals);
                            if (!isCaptured[localOrd])
                            {
                                m_info.push_back(SafeIntegerCast<uint32_t>(localOrd));
                            }
                        }
                        else if (item.IsRange())
                        {
                            TestAssert(item.GetRangeLength() >= 0);
                            for (size_t i = 0; i < static_cast<size_t>(item.GetRangeLength()); i++)
                            {
                                size_t localOrd = item.GetRangeStart() + i;
                                TestAssert(localOrd < numLocals);
                                if (!isCaptured[localOrd])
                                {
                                    m_info.push_back(SafeIntegerCast<uint32_t>(localOrd));
                                }
                            }
                        }
                    }
                    m_infoIndex[index * 2 + 2] = SafeIntegerCast<uint32_t>(m_info.size());
                }
            }

            curBytecodeOffset = decoder.GetNextBytecodePosition(curBytecodeOffset);
        }

#ifdef TESTBUILD
        // Assert every value is in range
        //
        for (uint32_t val : m_info) { TestAssert(val < numLocals); }

        // Assert that the current capture state agrees with the tail state
        //
        for (size_t localOrd = 0; localOrd < numLocals; localOrd++)
        {
            TestAssertIff(isCaptured[localOrd], bbInfo->m_isLocalCapturedAtTail.IsSet(localOrd));
        }
#endif

        // Compute the masks for quick update
        //
        m_andMask.Reset(alloc, numLocals);
        m_atTail.SetAllOne();
        ComputeHeadBasedOnTail(m_andMask /*out*/);

        // Important to compute orMask later, since we want m_atTail to be all zero in the end
        //
        m_orMask.Reset(alloc, numLocals);
        m_atTail.Clear();
        ComputeHeadBasedOnTail(m_orMask /*out*/);

#ifdef TESTBUILD
        // The set of bits that are set to 1 should never overlap with the set of bits that are set to 0
        //
        for (size_t i = 0; i < numLocals; i++)
        {
            TestAssertImp(m_orMask.IsSet(i), m_andMask.IsSet(i));
        }
#endif
    }

    // tmpBv must have length numLocals
    // Set 'tmpBv' to be the new head value based on the current m_atTail. Note that m_atHead is not changed.
    //
    void ComputeHeadBasedOnTail(TempBitVector& tmpBv /*out*/)
    {
        TestAssert(tmpBv.m_length == m_atTail.m_length);
        tmpBv.CopyFromEqualLengthBitVector(m_atTail);

        uint32_t* infoData = m_info.data();
        for (size_t indexInBB = m_numBytecodesInBB; indexInBB--;)
        {
            // Process all the defs
            //
            {
                size_t curIndex = m_infoIndex[indexInBB * 2 + 1];
                size_t endIndex = m_infoIndex[indexInBB * 2 + 2];
                TestAssert(endIndex <= m_info.size());
                TestAssert(curIndex <= endIndex);

                while (curIndex < endIndex)
                {
                    uint32_t defSlot = infoData[curIndex];
                    curIndex++;
                    TestAssert(defSlot < tmpBv.m_length);
                    tmpBv.ClearBit(defSlot);
                }
            }

            // Process all the uses
            //
            {
                size_t curIndex = m_infoIndex[indexInBB * 2];
                size_t endIndex = m_infoIndex[indexInBB * 2 + 1];
                TestAssert(endIndex <= m_info.size());
                TestAssert(curIndex <= endIndex);

                while (curIndex < endIndex)
                {
                    uint32_t useSlot = infoData[curIndex];
                    curIndex++;
                    TestAssert(useSlot < tmpBv.m_length);
                    tmpBv.SetBit(useSlot);
                }
            }
        }
    }

    void ComputeHeadBasedOnTailFast(TempBitVector& tmpBv /*out*/)
    {
        TestAssert(tmpBv.m_length == m_atTail.m_length);
        TestAssert(tmpBv.m_length == m_andMask.m_length);
        TestAssert(tmpBv.m_length == m_orMask.m_length);
        size_t allocLen = tmpBv.GetAllocLength();
        for (size_t i = 0; i < allocLen; i++)
        {
            tmpBv.m_data[i] = (m_atTail.m_data[i] & m_andMask.m_data[i]) | m_orMask.m_data[i];
        }
    }

    void ComputePerBytecodeLiveness(BytecodeLiveness& r /*out*/)
    {
        TestAssert(r.m_beforeUse.size() == r.m_afterUse.size());
        TestAssert(m_numBytecodesInBB > 0);

        size_t numLocals = m_atHead.m_length;
        TestAssert(m_firstBytecodeIndex + m_numBytecodesInBB <= r.m_beforeUse.size());
        for (size_t bytecodeIndex = m_firstBytecodeIndex; bytecodeIndex < m_firstBytecodeIndex + m_numBytecodesInBB; bytecodeIndex++)
        {
            TestAssert(r.m_beforeUse[bytecodeIndex].m_length == 0);
            TestAssert(r.m_afterUse[bytecodeIndex].m_length == 0);
            r.m_beforeUse[bytecodeIndex].Reset(numLocals);
            r.m_afterUse[bytecodeIndex].Reset(numLocals);
        }

        uint32_t* infoData = m_info.data();
        for (size_t indexInBB = m_numBytecodesInBB; indexInBB--;)
        {
            // "afterUse" is computed by the next bytecode's "beforeUse" + all defs set to false
            //
            DBitVector& afterUse = r.m_afterUse[m_firstBytecodeIndex + indexInBB];
            if (indexInBB + 1 < m_numBytecodesInBB)
            {
                TestAssert(m_firstBytecodeIndex + indexInBB + 1 < r.m_beforeUse.size());
                afterUse.CopyFromEqualLengthBitVector(r.m_beforeUse[m_firstBytecodeIndex + indexInBB + 1]);
            }
            else
            {
                afterUse.CopyFromEqualLengthBitVector(m_atTail);
            }

            {
                size_t curIndex = m_infoIndex[indexInBB * 2 + 1];
                size_t endIndex = m_infoIndex[indexInBB * 2 + 2];
                TestAssert(endIndex <= m_info.size() && curIndex <= endIndex);
                while (curIndex < endIndex)
                {
                    uint32_t defSlot = infoData[curIndex];
                    curIndex++;
                    TestAssert(defSlot < afterUse.m_length);
                    afterUse.ClearBit(defSlot);
                }
            }

            // "beforeUse" is computed by "afterUse" + all uses set to true
            //
            DBitVector& beforeUse = r.m_beforeUse[m_firstBytecodeIndex + indexInBB];
            beforeUse.CopyFromEqualLengthBitVector(afterUse);

            {
                size_t curIndex = m_infoIndex[indexInBB * 2];
                size_t endIndex = m_infoIndex[indexInBB * 2 + 1];
                TestAssert(endIndex <= m_info.size() && curIndex <= endIndex);
                while (curIndex < endIndex)
                {
                    uint32_t useSlot = infoData[curIndex];
                    curIndex++;
                    TestAssert(useSlot < beforeUse.m_length);
                    beforeUse.SetBit(useSlot);
                }
            }
        }

#ifdef TESTBUILD
        if (m_numBytecodesInBB > 0)
        {
            for (size_t localOrd = 0; localOrd < numLocals; localOrd++)
            {
                TestAssertIff(r.m_beforeUse[m_firstBytecodeIndex].IsSet(localOrd), m_atHead.IsSet(localOrd));
            }
        }
#endif
    }
};

BytecodeLiveness* WARN_UNUSED BytecodeLiveness::ComputeBytecodeLiveness(CodeBlock* codeBlock, const DfgControlFlowAndUpvalueAnalysisResult& cfUvInfo)
{
    TempArenaAllocator alloc;

    // Sort the basic blocks in reverse order of the starting bytecodeIndex
    // This doesn't affect correctness, but may affect how many iterations we need to reach fixpoint.
    // Why do we sort them by bytecodeIndex? Because that's the heuristic JSC uses..
    //
    TempVector<BasicBlockUpvalueInfo*> bbInReverseOrder(alloc);
    for (BasicBlockUpvalueInfo* bb : cfUvInfo.m_basicBlocks)
    {
        bbInReverseOrder.push_back(bb);
    }

    std::sort(bbInReverseOrder.begin(),
              bbInReverseOrder.end(),
              [](BasicBlockUpvalueInfo* lhs, BasicBlockUpvalueInfo* rhs)
              {
                  TestAssertIff(lhs->m_bytecodeIndex == rhs->m_bytecodeIndex, lhs == rhs);
                  return lhs->m_bytecodeIndex > rhs->m_bytecodeIndex;
              });

#ifdef TESTBUILD
    for (size_t i = 0; i + 1 < bbInReverseOrder.size(); i++)
    {
        TestAssert(bbInReverseOrder[i]->m_bytecodeIndex > bbInReverseOrder[i + 1]->m_bytecodeIndex);
    }
#endif

    size_t numBBs = bbInReverseOrder.size();
    size_t numLocals = codeBlock->m_stackFrameNumSlots;

    BytecodeLivenessBBInfo** bbLivenessInfo = alloc.AllocateArray<BytecodeLivenessBBInfo*>(numBBs, nullptr);

    {
        DeegenBytecodeBuilder::BytecodeDecoder decoder(codeBlock);
        TempUnorderedMap<BasicBlockUpvalueInfo*, BytecodeLivenessBBInfo*> bbInfoMap(alloc);
        for (size_t i = 0; i < numBBs; i++)
        {
            bbLivenessInfo[i] = alloc.AllocateObject<BytecodeLivenessBBInfo>(alloc, decoder, bbInReverseOrder[i], numLocals);
            TestAssert(!bbInfoMap.count(bbInReverseOrder[i]));
            bbInfoMap[bbInReverseOrder[i]] = bbLivenessInfo[i];
        }

        // Set up the successor edges
        //
        for (size_t bbOrd = 0; bbOrd < numBBs; bbOrd++)
        {
            BytecodeLivenessBBInfo* bbInfo = bbLivenessInfo[bbOrd];
            BasicBlockUpvalueInfo* bbUvInfo = bbInReverseOrder[bbOrd];
            TestAssert(bbInfo->m_numSuccessors == bbUvInfo->m_numSuccessors);
            for (size_t succOrd = 0; succOrd < bbInfo->m_numSuccessors; succOrd++)
            {
                TestAssert(bbInfoMap.count(bbUvInfo->m_successors[succOrd]));
                BytecodeLivenessBBInfo* succ = bbInfoMap[bbUvInfo->m_successors[succOrd]];
                bbInfo->m_successors[succOrd] = succ;
                succ->m_hasPrecedessor = true;
            }
        }
    }

    // Propagate to fixpoint
    //
    {
        TempBitVector tmpBv;
        tmpBv.Reset(alloc, numLocals);

        size_t currentEpoch = 1;
        bool isFirstIteration = true;
        while (true)
        {
            bool needMoreIterations = false;
            for (size_t bbOrd = 0; bbOrd < numBBs; bbOrd++)
            {
                BytecodeLivenessBBInfo* bb = bbLivenessInfo[bbOrd];

                // Our tail value could potentially change if one of our successor's head value has received an update
                // after the last time we checked them (or if this is the first iteration).
                //
                bool shouldCheck = false;
                for (size_t i = 0; i < bb->m_numSuccessors; i++)
                {
                    if (bb->m_successors[i]->m_lastChangedEpoch > bb->m_lastCheckedEpoch)
                    {
                        shouldCheck = true;
                        break;
                    }
                }

                if (shouldCheck || isFirstIteration)
                {
                    currentEpoch++;
                    bb->m_lastCheckedEpoch = currentEpoch;

                    // Initialize tmpBv to be the union of all the successors' head state
                    //
                    {
                        tmpBv.Clear();
                        uint64_t* tmpBvData = tmpBv.m_data.get();
                        size_t tmpBvAllocLength = tmpBv.GetAllocLength();
                        for (size_t succOrd = 0; succOrd < bb->m_numSuccessors; succOrd++)
                        {
                            BytecodeLivenessBBInfo* succ = bb->m_successors[succOrd];
                            TestAssert(succ->m_atHead.m_length == tmpBv.m_length);
                            uint64_t* srcData = succ->m_atHead.m_data.get();
                            for (size_t i = 0; i < tmpBvAllocLength; i++)
                            {
                                tmpBvData[i] |= srcData[i];
                            }
                        }
                    }

                    // Set bb->m_atTail to be tmpBv and check if it changes the tail value
                    //
                    bool tailChanged = UpdateBitVectorAfterMonotonicPropagation(bb->m_atTail /*inout*/, tmpBv /*copyFrom*/);

                    if (tailChanged || isFirstIteration)
                    {
                        // Compute the new head state from the tail state, and store it into tmpBv
                        //
                        bb->ComputeHeadBasedOnTailFast(tmpBv /*out*/);

                        // Set bb->m_head to tmpBv and check if it changes the head value
                        //
                        bool headChanged = UpdateBitVectorAfterMonotonicPropagation(bb->m_atHead /*inout*/, tmpBv /*copyFrom*/);

#ifdef TESTBUILD
                        // In test build, assert that ComputeHeadBasedOnTailFast produces the same result as ComputeHeadBasedOnTail
                        //
                        bb->ComputeHeadBasedOnTail(tmpBv /*out*/);
                        for (size_t i = 0; i < tmpBv.GetAllocLength(); i++)
                        {
                            TestAssert(tmpBv.m_data[i] == bb->m_atHead.m_data[i]);
                        }
#endif
                        // We do not need to update lastChangedEpoch if only tail changed but head did not change,
                        // since all our predecessors only look at our head, never our tail.
                        // Similarly, needMoreIterations is also not updated, since there's nothing in our state changed so that it can affect others.
                        //
                        if (headChanged)
                        {
                            currentEpoch++;
                            bb->m_lastChangedEpoch = currentEpoch;
                            // If we do not have predecessor, our state change cannot affect anyone else.
                            //
                            if (bb->m_hasPrecedessor)
                            {
                                needMoreIterations = true;
                            }
                        }
                    }
                }
            }

            if (!needMoreIterations)
            {
                break;
            }
            isFirstIteration = false;
        }
    }

#ifdef TESTBUILD
    // Assert that fixpoint is indeed reached
    //
    {
        TempBitVector tmpBv;
        tmpBv.Reset(alloc, numLocals);

        for (size_t bbOrd = 0; bbOrd < numBBs; bbOrd++)
        {
            BytecodeLivenessBBInfo* bb = bbLivenessInfo[bbOrd];

            tmpBv.Clear();
            for (size_t succOrd = 0; succOrd < bb->m_numSuccessors; succOrd++)
            {
                BytecodeLivenessBBInfo* succ = bb->m_successors[succOrd];
                TestAssert(succ->m_atHead.m_length == tmpBv.m_length);
                for (size_t i = 0; i < tmpBv.m_length; i++)
                {
                    if (succ->m_atHead.IsSet(i))
                    {
                        tmpBv.SetBit(i);
                    }
                }
            }

            TestAssert(bb->m_atTail.m_length == tmpBv.m_length);
            for (size_t i = 0; i < tmpBv.m_length; i++)
            {
                TestAssertIff(tmpBv.IsSet(i), bb->m_atTail.IsSet(i));
            }

            bb->ComputeHeadBasedOnTail(tmpBv /*out*/);

            TestAssert(bb->m_atHead.m_length == tmpBv.m_length);
            for (size_t i = 0; i < tmpBv.m_length; i++)
            {
                TestAssertIff(tmpBv.IsSet(i), bb->m_atHead.IsSet(i));
            }
        }
    }
#endif

    // Compute the liveness state for each bytecode
    //
    TestAssert(codeBlock->m_baselineCodeBlock != nullptr);
    size_t numBytecodes = codeBlock->m_baselineCodeBlock->m_numBytecodes;

    BytecodeLiveness* r = DfgAlloc()->AllocateObject<BytecodeLiveness>();
    TestAssert(r->m_beforeUse.size() == 0);
    TestAssert(r->m_afterUse.size() == 0);
    r->m_beforeUse.resize(numBytecodes);
    r->m_afterUse.resize(numBytecodes);

    for (size_t bbOrd = 0; bbOrd < numBBs; bbOrd++)
    {
        BytecodeLivenessBBInfo* bb = bbLivenessInfo[bbOrd];
        bb->ComputePerBytecodeLiveness(*r /*out*/);
    }

    // It's possible that the bytecode stream contains trivially unreachable bytecodes
    // (e.g., the source function contains a dead loop followed by a bunch of code),
    // in which case those bytecodes will not show up in any basic blocks.
    // Users of this class should never need to query liveness info for those bytecodes,
    // but for sanity, allocate arrays for those bytecodes (with everything is dead) as well.
    //
    for (size_t bytecodeIndex = 0; bytecodeIndex < numBytecodes; bytecodeIndex++)
    {
        if (r->m_beforeUse[bytecodeIndex].m_length == 0)
        {
            TestAssert(r->m_afterUse[bytecodeIndex].m_length == 0);
            r->m_beforeUse[bytecodeIndex].Reset(numLocals);
            r->m_afterUse[bytecodeIndex].Reset(numLocals);
        }

        TestAssert(r->m_beforeUse[bytecodeIndex].m_length == numLocals);
        TestAssert(r->m_afterUse[bytecodeIndex].m_length == numLocals);
    }

    // Unfortunately there isn't much more that we can assert.
    // We allow bytecodes to use undefined values, and our parser in fact will generate such bytecodes
    // in rare cases (specifically, the ISTC and ISFC bytecodes). Which is unfortunate, but that's what
    // we have in hand..
    //
    // So it's possible that a local that is not an argument is live at function entry, or a bytecode
    // used a value that is live in our analysis but actually clobbered by a previous bytecode, etc..
    //
    // But as long as our liveness result is an overapproximation of the real liveness (i.e., we never
    // report something is dead when it is actually live), we are good.
    //
    return r;
}

}   // namespace dfg
