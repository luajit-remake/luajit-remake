#pragma once

#include "common.h"
#include <immintrin.h>

namespace CommonUtils
{

enum class X64SegmentationRegisterKind
{
    FS,
    GS
};

extern const bool x_isFsgsbaseInstructionSetAvailable;

uint64_t X64_GetSegmentationRegisterBySyscall(X64SegmentationRegisterKind kind);
void X64_SetSegmentationRegisterBySyscall(X64SegmentationRegisterKind kind, uint64_t value);

template<X64SegmentationRegisterKind kind>
void X64_SetSegmentationRegister(uint64_t value)
{
    if (x_isFsgsbaseInstructionSetAvailable)
    {
        // Just to be safe: stop the compiler from hoisting this instruction,
        // it may cause invalid instruction on architectures not supporting it
        //
        COMPILER_REORDERING_BARRIER;
        if constexpr(kind == X64SegmentationRegisterKind::FS)
        {
            _writefsbase_u64(value);
        }
        else
        {
            _writegsbase_u64(value);
        }
    }
    else
    {
        X64_SetSegmentationRegisterBySyscall(kind, value);
    }
}

template<X64SegmentationRegisterKind kind>
uint64_t X64_GetSegmentationRegister()
{
    if (x_isFsgsbaseInstructionSetAvailable)
    {
        // Just to be safe: stop the compiler from hoisting this instruction,
        // it may cause invalid instruction on architectures not supporting it
        //
        COMPILER_REORDERING_BARRIER;
        if constexpr(kind == X64SegmentationRegisterKind::FS)
        {
            return _readfsbase_u64();
        }
        else
        {
            return _readgsbase_u64();
        }
    }
    else
    {
        return X64_GetSegmentationRegisterBySyscall(kind);
    }
}

}   // namespace CommonUtils
