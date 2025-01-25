#pragma once

#include "common_utils.h"

namespace dfg {

// In DFG, there is a consistent mapping from <InlinedCallFrame, FrameSlot> to DFG physical stack slot
// at all DFG basic block boundaries (except for interpreter slots that always store a statically-known
// constant, which is not mapped to a DFG physical stack slot).
//
// So at any DFG basic block boundary, the interpreter stack frames can be reconstructed from the DFG
// stack frames using solely this information.
//
// The data structure below records the relevant information for each InlinedCallFrame.
//
struct DfgInlinedCallFrameOsrInfo
{
    static constexpr size_t TrailingArrayOffset()
    {
        return offsetof_member_v<&DfgInlinedCallFrameOsrInfo::m_values>;
    }

    static constexpr size_t GetAllocationLength(size_t trailingArrayNumElements)
    {
        size_t len = TrailingArrayOffset() + sizeof(int16_t) * trailingArrayNumElements;
        return RoundUpToMultipleOf<alignof(DfgInlinedCallFrameOsrInfo)>(len);
    }

    // Return the array length the caller should allocate to hold all the reconstructed interpreter frames
    //
    size_t GetInterpreterFramesTotalNumSlots()
    {
        return m_frameStartSlot + m_frameFullLength;
    }

    bool HasParentFrame() { return m_parentFrameOsrInfo != 0; }

    DfgInlinedCallFrameOsrInfo* GetParentFrame()
    {
        TestAssert(HasParentFrame());
        uint8_t* addr = reinterpret_cast<uint8_t*>(this) + m_parentFrameOsrInfo;
        return reinterpret_cast<DfgInlinedCallFrameOsrInfo*>(addr);
    }

    // 'result' should be of length GetInterpreterFramesTotalNumSlots()
    //
    void ReconstructInterpreterFramesBaseInfo(uint64_t* constantTableEnd, uint64_t* dfgStackBase, uint64_t* result /*out*/)
    {
        ReconstructInterpreterFramesBaseInfoImpl(this, constantTableEnd, dfgStackBase, result);
    }

private:
    static void ReconstructInterpreterFramesBaseInfoImpl(DfgInlinedCallFrameOsrInfo* curFrame,
                                                         uint64_t* constantTableEnd,
                                                         uint64_t* dfgStackBase,
                                                         uint64_t* result /*out*/)
    {
        size_t frameStart = curFrame->m_frameStartSlot;
        size_t frameEnd = frameStart + curFrame->m_frameFullLength;
        while (true)
        {
            // Fill values [frameStart, frameEnd)
            //
            {
                TestAssert(frameStart <= frameEnd);

                uint64_t* dest = result + frameStart;
                size_t lenToFill = frameEnd - frameStart;
                TestAssert(lenToFill <= curFrame->m_frameFullLength);
                for (size_t i = 0; i < lenToFill; i++)
                {
                    int16_t val = curFrame->m_values[i];
                    if (val < 0)
                    {
                        dest[i] = constantTableEnd[val];
                    }
                    else
                    {
                        dest[i] = dfgStackBase[val];
                    }
                }
            }
            // Move up to parent frame and fill parent frames
            //
            if (frameStart == 0)
            {
                break;
            }
            TestAssert(curFrame->HasParentFrame());
            curFrame = curFrame->GetParentFrame();
            frameEnd = frameStart;
            frameStart = curFrame->m_frameStartSlot;
        }
    }

public:
    // The parent DfgInlinedCallFrameOsrInfo is at this byte offset from this struct
    //
    int32_t m_parentFrameOsrInfo;

    // The absolute interpreter slot ordinal for local 0 of this frame
    //
    uint16_t m_frameBaseSlot;
    // The absolute interpreter slot ordinal for the start of the this frame (start of varargs region,
    // note that the varargs may need to be moved after reconstruction)
    //
    uint16_t m_frameStartSlot;
    // This frame occupies interpreter slots [m_frameStartSlot, m_frameStartSlot + m_frameFullLength)
    //
    uint16_t m_frameFullLength;

    // An array of length m_frameFullLength, the base mapping described above
    // Each value should be interpreted as follows:
    //     < 0: the interpreter slot is a statically known constant with this ordinal in the constant table
    //     >=0: the interpreter slot should have the value of the DFG physical slot with this ordinal
    //
    // Note that at the start of a basic block, if an interpreter slot is bytecode-dead, the corresponding
    // DFG physical slot may hold any value including invalid boxed-value bit patterns, and may have been
    // reused to store something else. But this is fine since the slot is bytecode-dead anyway.
    //
    int16_t m_values[0];
};
// This class must be trivially copyable since we will construct it in a scratch space and copy it to the destination by memcpy
//
static_assert(std::is_trivially_copyable_v<DfgInlinedCallFrameOsrInfo>);

}   // namespace dfg
