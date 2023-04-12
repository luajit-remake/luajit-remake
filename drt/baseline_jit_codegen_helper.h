#pragma once

#include "common.h"
#include "heap_ptr_utils.h"

// This struct name and member names are hardcoded as they are used by generated C++ code!
//
struct BytecodeBaselineJitTraits
{
    uint16_t m_fastPathCodeLen;
    uint16_t m_slowPathCodeLen;
    uint16_t m_dataSectionCodeLen;
    uint16_t m_dataSectionAlignment;
    uint16_t m_numCondBrLatePatches;
    uint16_t m_slowPathDataLen;
    uint16_t m_unused[2];
};
// Make sure the size of this struct is a power of 2 to make addressing cheap
//
static_assert(sizeof(BytecodeBaselineJitTraits) == 16);

// The max possible m_dataSectionAlignment
// We assert this at build time, so we know this must be true at runtime
//
constexpr size_t x_baselineJitMaxPossibleDataSectionAlignment = 16;

enum class BaselineJitCondBrLatePatchKind : uint32_t
{
    // *(uint32_t*)ptr += dstAddr
    //
    Int32,
    // ((uint32_t*)ptr)[0] = dstAddr
    // ((uint32_t*)ptr)[1] = dstBytecodeOrd
    //
    SlowPathData,
    // *(uint64_t*)ptr += dstAddr
    //
    Int64
};

struct BaselineJitCondBrLatePatchRecord
{
    uint8_t* m_ptr;
    uint32_t m_dstBytecodePtrLow32bits;
    BaselineJitCondBrLatePatchKind m_patchKind;

    uint64_t ALWAYS_INLINE WARN_UNUSED GetTrueBytecodePtr(uint64_t bytecodePtrBase)
    {
        uint32_t offset = m_dstBytecodePtrLow32bits - static_cast<uint32_t>(bytecodePtrBase);
        assert(offset < static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));
        return bytecodePtrBase + static_cast<uint64_t>(offset);
    }

    void ALWAYS_INLINE Patch(uint64_t jitAddr, uint32_t bytecodeOrd)
    {
        switch (m_patchKind)
        {
        case BaselineJitCondBrLatePatchKind::Int32:
        {
            UnalignedStore<uint32_t>(m_ptr, UnalignedLoad<uint32_t>(m_ptr) + static_cast<uint32_t>(jitAddr));
            break;
        }
        case BaselineJitCondBrLatePatchKind::SlowPathData:
        {
            UnalignedStore<uint32_t>(m_ptr, static_cast<uint32_t>(jitAddr));
            UnalignedStore<uint32_t>(m_ptr + 4, static_cast<uint32_t>(bytecodeOrd));
            break;
        }
        case BaselineJitCondBrLatePatchKind::Int64: [[unlikely]]
        {
            UnalignedStore<uint64_t>(m_ptr, UnalignedLoad<uint64_t>(m_ptr) + jitAddr);
            break;
        }
        }   /* switch m_patchKind */
    }
};
static_assert(sizeof(BaselineJitCondBrLatePatchRecord) == 16);

class CodeBlock;

struct DeegenBaselineJitCodegenControlStruct
{
    // Where outputs are written to
    // Caller is responsible for correctly doing all the size computations and allocate enough space!
    //
    uint8_t* m_jitFastPathAddr;
    uint8_t* m_jitSlowPathAddr;
    uint8_t* m_jitDataSecAddr;
    BaselineJitCondBrLatePatchRecord* m_condBrPatchesArray;
    uint8_t* m_slowPathDataPtr;
    uint32_t* m_slowPathDataIndexArray;

    // Input CodeBlock
    //
    CodeBlock* m_codeBlock;

    // Assertions: in debug mode, the codegen function will populate these fields after codegen completes,
    // so the caller can assert that no buffer overflow has happened (which should never happen as long
    // as all the size computations are done correctly).
    //
    uint8_t* m_expectedJitFastPathEnd;
    uint8_t* m_expectedJitSlowPathEnd;
    uint8_t* m_expectedJitDataSecEnd;
    BaselineJitCondBrLatePatchRecord* m_expectedCondBrPatchesArrayEnd;
    uint8_t* m_expectedSlowPathDataEnd;
    uint32_t* m_expectedSlowPathDataIndexArrayEnd;
};

class BaselineCodeBlock;

extern "C" BaselineCodeBlock* deegen_baseline_jit_do_codegen(CodeBlock* cb);
