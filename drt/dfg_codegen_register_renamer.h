#pragma once

#include "common_utils.h"
#include "heap_ptr_utils.h"

namespace dfg {

struct RuntimeStencilRegRenamingPatchItem
{
    // bit [0, 3): the bit offset within byte offset
    // bit [3, 9): the composite register ordinal (higher 3 bit = class, lower 3 bit = ordinal in class)
    // bit [9, 15): the byte offset
    // bit 15: 1 if the register value should be flipped
    //
    size_t byteOffset;
    size_t bitOffset;
    size_t regClass;
    size_t ordInClass;
    bool shouldFlip;

    static constexpr size_t x_bytesPerChunk = 64;

    // Called by Deegen at build time only, never at DFG runtime
    //
    uint16_t WARN_UNUSED Crunch()
    {
        ReleaseAssert(byteOffset < x_bytesPerChunk);
        ReleaseAssert(bitOffset <= 5);
        ReleaseAssert(regClass < 7);
        ReleaseAssert(ordInClass < 8);
        uint16_t compositeValue = static_cast<uint16_t>(
            (bitOffset) |
            (ordInClass << 3) |
            (regClass << 6) |
            (byteOffset << 9) |
            ((shouldFlip ? 1ULL : 0ULL) << 15));

        // Check round-tripping works
        //
        {
            RuntimeStencilRegRenamingPatchItem other = Parse(compositeValue);
            ReleaseAssert(byteOffset == other.byteOffset);
            ReleaseAssert(bitOffset == other.bitOffset);
            ReleaseAssert(regClass == other.regClass);
            ReleaseAssert(ordInClass == other.ordInClass);
            ReleaseAssert(shouldFlip == other.shouldFlip);
        }

        return compositeValue;
    }

    void ALWAYS_INLINE Apply(uint8_t* codeFragmentStart, uint8_t regSubOrdinal)
    {
        uint8_t reg = regSubOrdinal;
        TestAssert(reg < 8);
        reg ^= (shouldFlip ? 7 : 0);
        reg <<= bitOffset;
        TestAssert((codeFragmentStart[byteOffset] & (7 << bitOffset)) == 0);
        codeFragmentStart[byteOffset] |= reg;
    }

    static RuntimeStencilRegRenamingPatchItem WARN_UNUSED ALWAYS_INLINE Parse(uint16_t cv)
    {
        return {
            .byteOffset = static_cast<size_t>((cv >> 9) & 63),
            .bitOffset = static_cast<size_t>(cv & 7),
            .regClass = static_cast<size_t>((cv >> 6) & 7),
            .ordInClass = static_cast<size_t>((cv >> 3) & 7),
            .shouldFlip = ((cv & (1 << 15)) != 0)
        };
    }
};

struct RegAllocStateForCodeGen;

// To fit one patch into 2 bytes (see above), we only have 6 bits to record the byteOffset
// Therefore, the code snippet is divided into 64 byte chunks, and a list of patches is used to describe each chunk
//
// So the patch stream is encoded as follows:
// [ #chunks ]
// [ #patches in 1st chunk ] [ patches in 1st chunk ]...
// [ #patches in 2nd chunk ] [ patches in 2nd chunk ]...
// ...
//
void RunStencilRegisterPatchingPhase(RestrictPtr<uint8_t> code,
                                     RestrictPtr<RegAllocStateForCodeGen> regInfo,
                                     ConstRestrictPtr<uint16_t> patches);

}   // namespace dfg
