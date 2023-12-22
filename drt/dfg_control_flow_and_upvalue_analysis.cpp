#include "dfg_control_flow_and_upvalue_analysis.h"
#include "dfg_node.h"
#include "bytecode_builder.h"
#include "bit_vector_utils.h"

using namespace dfg;

using BytecodeDecoder = DeegenBytecodeBuilder::BytecodeDecoder;
using BytecodeOpcodeTy = DeegenBytecodeBuilder::BytecodeDecoder::BytecodeOpcodeTy;

namespace {

struct PassImpl
{
    TempArenaAllocator& m_resultAlloc;
    TempArenaAllocator m_tempAlloc;

    CodeBlock* m_codeBlock;
    BytecodeDecoder m_decoder;

    // Records all the bytecode offsets that corresponds to the start of a basic block, lastly followed by the end of bytecode
    //
    TempVector<size_t> m_bbStartOffsets;
    // The corresponding BasicBlock, in the same order as m_bbStartOffsets
    // Note that this list may contain trivially unreachable basic blocks
    //
    TempVector<BasicBlockUpvalueInfo*> m_bbForStartOffsets;

    // All the reachable basic blocks
    // This vector is owned by m_resultAlloc
    //
    TempVector<BasicBlockUpvalueInfo*> m_bbInNaturalOrder;    // [0] is always the function entry

    PassImpl(TempArenaAllocator& resultAlloc, CodeBlock* codeBlock)
        : m_resultAlloc(resultAlloc)
        , m_codeBlock(codeBlock)
        , m_decoder(codeBlock)
        , m_bbStartOffsets(m_tempAlloc)
        , m_bbForStartOffsets(m_tempAlloc)
        , m_bbInNaturalOrder(m_resultAlloc)
    { }

    BasicBlockUpvalueInfo* WARN_UNUSED GetEntryBB()
    {
        TestAssert(m_bbInNaturalOrder.size() > 0);
        return m_bbInNaturalOrder[0];
    }

    BasicBlockUpvalueInfo* WARN_UNUSED TraverseAndGenerateBytecodeOffsetToBBMap(size_t bcPos)
    {
        TestAssert(bcPos < m_decoder.GetCurLength());
        ssize_t bbIdxTmp = std::lower_bound(m_bbStartOffsets.begin(), m_bbStartOffsets.end(), bcPos) - m_bbStartOffsets.begin();
        TestAssert(0 <= bbIdxTmp && bbIdxTmp + 1 < static_cast<ssize_t>(m_bbStartOffsets.size()));
        size_t bbIdx = static_cast<size_t>(bbIdxTmp);
        TestAssert(m_bbStartOffsets[bbIdx] == bcPos);
        if (m_bbForStartOffsets[bbIdx] != nullptr)
        {
            return m_bbForStartOffsets[bbIdx];
        }

        BasicBlockUpvalueInfo* bb = m_resultAlloc.AllocateObject<BasicBlockUpvalueInfo>();
        m_bbForStartOffsets[bbIdx] = bb;
        m_bbInNaturalOrder.push_back(bb);

        bb->m_ord = static_cast<uint32_t>(m_bbInNaturalOrder.size() - 1);
        bb->m_bytecodeOffset = SafeIntegerCast<uint32_t>(bcPos);
        bb->m_bytecodeIndex = static_cast<uint32_t>(-1);            // we'll populate it at the end of the pass

        size_t bcPosEnd = m_bbStartOffsets[bbIdx + 1];
        while (true)
        {
            size_t nextBcPos = m_decoder.GetNextBytecodePosition(bcPos);
            TestAssert(nextBcPos <= bcPosEnd);
            if (nextBcPos == bcPosEnd)
            {
                break;
            }
            TestAssert(!m_decoder.IsBytecodeBarrier(bcPos) && !m_decoder.BytecodeHasBranchOperand(bcPos));
            bcPos = nextBcPos;
        }
        bb->m_terminalNodeBcOffset = SafeIntegerCast<uint32_t>(bcPos);

        bool mayBranch = m_decoder.BytecodeHasBranchOperand(bcPos);
        bool isBarrier = m_decoder.IsBytecodeBarrier(bcPos);

        // Our BB ends with an implicit fallthrough if our last instruction cannot branch and is fallthroughable
        //
        bb->m_isTerminalInstImplicitTrivialBranch = (!mayBranch && !isBarrier);

        // Figure out our BB's successors
        // Note that we are expected to push the fallthrough successor first
        //
        bb->m_numSuccessors = 0;
        if (!isBarrier)
        {
            bb->m_successors[bb->m_numSuccessors] = TraverseAndGenerateBytecodeOffsetToBBMap(bcPosEnd);
            bb->m_numSuccessors++;
        }
        if (mayBranch)
        {
            bb->m_successors[bb->m_numSuccessors] = TraverseAndGenerateBytecodeOffsetToBBMap(m_decoder.GetBranchTarget(bcPos));
            bb->m_numSuccessors++;
        }

        bb->m_isLocalCapturedAtHead.Reset(m_resultAlloc, m_codeBlock->m_stackFrameNumSlots);
        bb->m_isLocalCapturedAtTail.Reset(m_resultAlloc, m_codeBlock->m_stackFrameNumSlots);
        bb->m_isLocalCapturedInBB.Reset(m_resultAlloc, m_codeBlock->m_stackFrameNumSlots);

        return bb;
    }

    void GenerateBytecodeOffsetToBBMap()
    {
        m_bbStartOffsets.push_back(0);

        {
            size_t bcPos = 0;
            size_t bcPosEnd = m_decoder.GetCurLength();
            while (bcPos < bcPosEnd)
            {
                size_t nextBcPos = m_decoder.GetNextBytecodePosition(bcPos);
                bool isBarrier = m_decoder.IsBytecodeBarrier(bcPos);
                bool mayBranch = m_decoder.BytecodeHasBranchOperand(bcPos);
                if (isBarrier || mayBranch)
                {
                    // This is a terminal instruction, so this bytecode itself should be the end of a basic block
                    //
                    m_bbStartOffsets.push_back(nextBcPos);
                }
                if (mayBranch)
                {
                    // This bytecode can perform a branch, its branch target should be the start of the basic block
                    //
                    size_t branchTarget = m_decoder.GetBranchTarget(bcPos);
                    m_bbStartOffsets.push_back(branchTarget);
                }
                bcPos = nextBcPos;
            }
            TestAssert(bcPos == bcPosEnd);
        }

        std::sort(m_bbStartOffsets.begin(), m_bbStartOffsets.end());
        m_bbStartOffsets.erase(std::unique(m_bbStartOffsets.begin(), m_bbStartOffsets.end()), m_bbStartOffsets.end());

        TestAssert(m_bbStartOffsets.size() > 1);
        TestAssert(m_bbStartOffsets[0] == 0 && m_bbStartOffsets.back() == m_decoder.GetCurLength());

        m_bbForStartOffsets.resize(m_bbStartOffsets.size() - 1, nullptr);
        m_bbInNaturalOrder.reserve(m_bbStartOffsets.size() - 1);

        BasicBlockUpvalueInfo* entryBlock = TraverseAndGenerateBytecodeOffsetToBBMap(0 /*bcPos*/);
        TestAssert(entryBlock == GetEntryBB());
        std::ignore = entryBlock;

        // Assert that the bytecode ranges of all the basic blocks are non-overlapping
        //
        if (x_isTestBuild)
        {
            TempVector<std::pair<size_t, size_t>> allRanges(m_tempAlloc);
            for (BasicBlockUpvalueInfo* bb : m_bbInNaturalOrder)
            {
                size_t start = bb->m_bytecodeOffset;
                size_t end = m_decoder.GetNextBytecodePosition(bb->m_terminalNodeBcOffset);
                TestAssert(start < end && end <= m_decoder.GetCurLength());
                allRanges.push_back(std::make_pair(start, end));
            }
            std::sort(allRanges.begin(), allRanges.end());
            TestAssert(allRanges.size() > 0);
            TestAssert(allRanges[0].first == 0);
            for (size_t i = 0; i + 1 < allRanges.size(); i++)
            {
                TestAssert(allRanges[i].second <= allRanges[i + 1].first);
            }
        }
    }

    // Computes m_isLocalCapturedAtHead and m_isLocalCapturedAtTail
    //
    void AnalyzeLocalCaptures()
    {
        TempVector<size_t> ucloseLimit(m_tempAlloc);    // At the end of BB i, all locals >= ucloseLimit[i] is closed

        size_t n = m_bbInNaturalOrder.size();

        ucloseLimit.resize(n);

        TempQueue<size_t> q(m_tempAlloc);
        bool* isInQueue = m_tempAlloc.AllocateArray<bool>(n);
        for (size_t i = 0; i < n; i++)
        {
            isInQueue[i] = false;
        }

        auto pushQueue = [&](size_t value) ALWAYS_INLINE
        {
            TestAssert(value < n);
            if (isInQueue[value])
            {
                return;
            }
            isInQueue[value] = true;
            q.push(value);
        };

        auto popQueue = [&]() ALWAYS_INLINE WARN_UNUSED -> size_t
        {
            TestAssert(!q.empty());
            size_t res = q.front();
            q.pop();
            TestAssert(isInQueue[res]);
            isInQueue[res] = false;
            return res;
        };

        for (size_t bbOrd = 0; bbOrd < n; bbOrd++)
        {
            BasicBlockUpvalueInfo* bb = m_bbInNaturalOrder[bbOrd];

            // Figure out if the BB ends with a UpvalueClose
            //
            ucloseLimit[bbOrd] = static_cast<size_t>(-1);
            if (m_decoder.IsBytecodeIntrinsic<BytecodeIntrinsicInfo::UpvalueClose>(bb->m_terminalNodeBcOffset))
            {
                BytecodeIntrinsicInfo::UpvalueClose info = m_decoder.GetBytecodeIntrinsicInfo<BytecodeIntrinsicInfo::UpvalueClose>(bb->m_terminalNodeBcOffset);
                TestAssert(info.start.IsLocal());
                ucloseLimit[bbOrd] = info.start.AsLocal();
            }

            // Figure out all the local variables captured inside the BB
            //
            {
                bool hasCapture = false;
                size_t cur = bb->m_bytecodeOffset;
                size_t end = bb->m_terminalNodeBcOffset;
                TempBitVector& isLocalCapturedInBB = bb->m_isLocalCapturedInBB;
                while (cur < end)
                {
                    if (m_decoder.IsBytecodeIntrinsic<BytecodeIntrinsicInfo::CreateClosure>(cur))
                    {
                        BytecodeIntrinsicInfo::CreateClosure info = m_decoder.GetBytecodeIntrinsicInfo<BytecodeIntrinsicInfo::CreateClosure>(cur);
                        TestAssert(info.proto.IsConstant());
                        UnlinkedCodeBlock* ucb = reinterpret_cast<UnlinkedCodeBlock*>(info.proto.AsConstant().m_value);
                        for (size_t i = 0; i < ucb->m_numUpvalues; i++)
                        {
                            UpvalueMetadata& uvmt = ucb->m_upvalueInfo[i];
                            if (!uvmt.m_isImmutable && uvmt.m_isParentLocal)
                            {
                                TestAssert(uvmt.m_slot < m_codeBlock->m_stackFrameNumSlots);
                                isLocalCapturedInBB.SetBit(uvmt.m_slot);
                                hasCapture = true;
                            }
                        }
                    }
                    cur = m_decoder.GetNextBytecodePosition(cur);
                }
                TestAssert(cur == end);

                if (hasCapture)
                {
                    pushQueue(bbOrd);
                }
            }
        }

        size_t bsVecLen = GetEntryBB()->m_isLocalCapturedAtHead.GetAllocLength();
        while (!q.empty())
        {
            size_t bbOrd = popQueue();
            BasicBlockUpvalueInfo* bb = m_bbInNaturalOrder[bbOrd];
            bool tailChanged = false;
            TempBitVector& atHead = bb->m_isLocalCapturedAtHead;
            TempBitVector& atTail = bb->m_isLocalCapturedAtTail;
            TempBitVector& inBB = bb->m_isLocalCapturedInBB;
            TestAssert(atHead.GetAllocLength() == bsVecLen && atTail.GetAllocLength() == bsVecLen && inBB.GetAllocLength() == bsVecLen);
            size_t uclimit = ucloseLimit[bbOrd];
            for (size_t i = 0; i < bsVecLen; i++)
            {
                uint64_t newValue = atHead.m_data[i] | inBB.m_data[i];
                if (uclimit <= i * 64)
                {
                    newValue = 0;
                }
                else if (uclimit < i * 64 + 64)
                {
                    size_t numHighBitsToClear = i * 64 + 64 - uclimit;
                    TestAssert(1 <= numHighBitsToClear && numHighBitsToClear <= 63);
                    newValue &= (static_cast<uint64_t>(1) << (64 - numHighBitsToClear)) - 1;
                }
                if (newValue != atTail.m_data[i])
                {
                    TestAssert((newValue & atTail.m_data[i]) == atTail.m_data[i]);
                    tailChanged = true;
                    atTail.m_data[i] = newValue;
                }
            }
            if (tailChanged)
            {
                for (size_t succIdx = 0; succIdx < bb->m_numSuccessors; succIdx++)
                {
                    BasicBlockUpvalueInfo* succ = bb->m_successors[succIdx];
                    TempBitVector& succAtHead = succ->m_isLocalCapturedAtHead;
                    TestAssert(succAtHead.GetAllocLength() == bsVecLen);
                    bool shouldPush = false;
                    for (size_t i = 0; i < bsVecLen; i++)
                    {
                        uint64_t newValue = succAtHead.m_data[i] | atTail.m_data[i];
                        if (newValue != succAtHead.m_data[i])
                        {
                            succAtHead.m_data[i] = newValue;
                            shouldPush = true;
                        }
                    }
                    if (shouldPush)
                    {
                        pushQueue(succ->m_ord);
                    }
                }
            }
        }

        // Assert that fixpoint is indeed reached
        //
#ifdef TESTBUILD
        {
            for (size_t i = 0; i < n; i++) { TestAssert(!isInQueue[i]); }
            std::unordered_map<BasicBlockUpvalueInfo*, std::vector<BasicBlockUpvalueInfo*>> predecessorMap;
            for (size_t bbOrd = 0; bbOrd < n; bbOrd++)
            {
                BasicBlockUpvalueInfo* bb = m_bbInNaturalOrder[bbOrd];
                for (size_t succIdx = 0; succIdx < bb->m_numSuccessors; succIdx++)
                {
                    BasicBlockUpvalueInfo* succ = bb->m_successors[succIdx];
                    predecessorMap[succ].push_back(bb);
                }
            }
            for (size_t bbOrd = 0; bbOrd < n; bbOrd++)
            {
                BasicBlockUpvalueInfo* bb = m_bbInNaturalOrder[bbOrd];
                for (size_t localOrd = 0; localOrd < m_codeBlock->m_stackFrameNumSlots; localOrd++)
                {
                    bool isCapturedAtHead = bb->m_isLocalCapturedAtHead.IsSet(localOrd);
                    bool isCapturedAtTail = bb->m_isLocalCapturedAtTail.IsSet(localOrd);
                    bool isCapturedInBB = bb->m_isLocalCapturedInBB.IsSet(localOrd);
                    if (localOrd < ucloseLimit[bbOrd])
                    {
                        TestAssertIff(isCapturedAtHead || isCapturedInBB, isCapturedAtTail);
                    }
                    else
                    {
                        TestAssert(!isCapturedAtTail);
                    }
                }
                for (size_t succIdx = 0; succIdx < bb->m_numSuccessors; succIdx++)
                {
                    BasicBlockUpvalueInfo* succ = bb->m_successors[succIdx];
                    for (size_t localOrd = 0; localOrd < m_codeBlock->m_stackFrameNumSlots; localOrd++)
                    {
                        TestAssertImp(bb->m_isLocalCapturedAtTail.IsSet(localOrd), succ->m_isLocalCapturedAtHead.IsSet(localOrd));
                    }
                }
                for (size_t localOrd = 0; localOrd < m_codeBlock->m_stackFrameNumSlots; localOrd++)
                {
                    if (bb->m_isLocalCapturedAtHead.IsSet(localOrd))
                    {
                        bool found = false;
                        for (BasicBlockUpvalueInfo* pred : predecessorMap[bb])
                        {
                            if (pred->m_isLocalCapturedAtTail.IsSet(localOrd))
                            {
                                found = true;
                                break;
                            }
                        }
                        TestAssert(found);
                    }
                }
            }
        }
#endif

        // Assert that each basic block with no successors must have closed all upvalues at the end
        //
#ifdef TESTBUILD
        {
            for (size_t bbOrd = 0; bbOrd < n; bbOrd++)
            {
                BasicBlockUpvalueInfo* bb = m_bbInNaturalOrder[bbOrd];
                if (bb->m_numSuccessors == 0)
                {
                    for (size_t localOrd = 0; localOrd < m_codeBlock->m_stackFrameNumSlots; localOrd++)
                    {
                        bool isCapturedAtTail = bb->m_isLocalCapturedAtTail.IsSet(localOrd);
                        TestAssert(!isCapturedAtTail && "A basic block with no successors must have closed all upvalues at end!");
                    }
                }
            }
        }
#endif

        // Populate m_bytecodeIndex and m_numBytecodesInBB field
        //
        {
            BaselineCodeBlock* bcb = m_codeBlock->m_baselineCodeBlock;
            TestAssert(bcb != nullptr);
            for (size_t bbOrd = 0; bbOrd < n; bbOrd++)
            {
                BasicBlockUpvalueInfo* bb = m_bbInNaturalOrder[bbOrd];
                bb->m_bytecodeIndex = SafeIntegerCast<uint32_t>(bcb->GetBytecodeIndexFromBytecodePtr(m_codeBlock->GetBytecodeStream() + bb->m_bytecodeOffset));
                TestAssert(bb->m_bytecodeIndex < bcb->m_numBytecodes);
                TestAssert(bcb->GetBytecodeOffsetFromBytecodeIndex(bb->m_bytecodeIndex) == bb->m_bytecodeOffset);

                size_t terminalBytecodeIndex = bcb->GetBytecodeIndexFromBytecodePtr(m_codeBlock->GetBytecodeStream() + bb->m_terminalNodeBcOffset);
                TestAssert(terminalBytecodeIndex < bcb->m_numBytecodes);
                TestAssert(bcb->GetBytecodeOffsetFromBytecodeIndex(terminalBytecodeIndex) == bb->m_terminalNodeBcOffset);
                TestAssert(terminalBytecodeIndex >= bb->m_bytecodeIndex);
                bb->m_numBytecodesInBB = static_cast<uint32_t>(terminalBytecodeIndex - bb->m_bytecodeIndex + 1);
            }
        }
    }

    static constexpr bool x_debugDump = false;
    static constexpr bool x_dumpInstructionsInBB = false;

    void DumpInformation()
    {
        fprintf(stderr, "=== DFG Control Flow & Upvalue Analysis Debug Dump ===\n");
        fprintf(stderr, "Function with %d BasicBlocks and %d locals.\n",
                static_cast<int>(m_bbInNaturalOrder.size()), static_cast<int>(m_codeBlock->m_stackFrameNumSlots));

        for (size_t bbOrd = 0; bbOrd < m_bbInNaturalOrder.size(); bbOrd++)
        {
            BasicBlockUpvalueInfo* bb = m_bbInNaturalOrder[bbOrd];
            fprintf(stderr, "BasicBlock #%d:\n", static_cast<int>(bbOrd));
            fprintf(stderr, "    Bytecode Range: [%d, %d)\n",
                    static_cast<int>(bb->m_bytecodeOffset), static_cast<int>(m_decoder.GetNextBytecodePosition(bb->m_terminalNodeBcOffset)));

            fprintf(stderr, "    Captured locals at head:");
            bool printedSomething = false;
            for (size_t localOrd = 0; localOrd < m_codeBlock->m_stackFrameNumSlots; localOrd++)
            {
                if (bb->m_isLocalCapturedAtHead.IsSet(localOrd))
                {
                    fprintf(stderr, " %d", static_cast<int>(localOrd));
                    printedSomething = true;
                }
            }
            if (!printedSomething) { fprintf(stderr, " (none)"); }
            fprintf(stderr, "\n");

            fprintf(stderr, "    Successors:");
            for (size_t succIdx = 0; succIdx < bb->m_numSuccessors; succIdx++)
            {
                BasicBlockUpvalueInfo* succ = bb->m_successors[succIdx];
                fprintf(stderr, " BB #%d", static_cast<int>(succ->m_ord));
            }
            if (bb->m_numSuccessors == 0) { fprintf(stderr, " (none)"); }
            fprintf(stderr, "\n");

            if (x_dumpInstructionsInBB)
            {
                fprintf(stderr, "    Bytecodes:\n");
                size_t pos = bb->m_bytecodeOffset;
                while (pos <= bb->m_terminalNodeBcOffset)
                {
                    fprintf(stderr, "        %s (offset=%d)\n", m_decoder.GetBytecodeKindName(pos), static_cast<int>(pos));
                    pos = m_decoder.GetNextBytecodePosition(pos);
                }
            }
            fprintf(stderr, "\n");
        }
    }

    static DfgControlFlowAndUpvalueAnalysisResult Run(TempArenaAllocator& resultAlloc, CodeBlock* codeBlock)
    {
        PassImpl impl(resultAlloc, codeBlock);
        impl.GenerateBytecodeOffsetToBBMap();
        impl.AnalyzeLocalCaptures();

        TestAssert(impl.m_bbInNaturalOrder.size() > 0);

        if (x_debugDump)
        {
            impl.DumpInformation();
        }

        return {
            .m_basicBlocks = std::move(impl.m_bbInNaturalOrder)
        };
    }
};

}   // anonymous namespace

namespace dfg {

DfgControlFlowAndUpvalueAnalysisResult WARN_UNUSED RunControlFlowAndUpvalueAnalysis(TempArenaAllocator& alloc, CodeBlock* codeBlock)
{
    return PassImpl::Run(alloc, codeBlock);
}

}   // namespace dfg
