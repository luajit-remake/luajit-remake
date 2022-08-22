#include <unistd.h>
#include <asm/prctl.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/auxv.h>
#include <elf.h>
#include "x64_segmentation_register_utils.h"

// Everything below are Linux specific, figure out portability issues later...
//
// https://www.kernel.org/doc/html/latest/x86/x86_64/fsgs.html
//
/* Will be eventually in asm/hwcap.h */
#ifndef HWCAP2_FSGSBASE
#define HWCAP2_FSGSBASE        (1 << 1)
#endif

static bool WARN_UNUSED X64_DetectFsgsbaseInstructionAvailability()
{
    unsigned long int val = getauxval(AT_HWCAP2);
    return (val & HWCAP2_FSGSBASE);
}

const bool x_isFsgsbaseInstructionSetAvailable = X64_DetectFsgsbaseInstructionAvailability();

void X64_SetSegmentationRegisterBySyscall(X64SegmentationRegisterKind kind, uint64_t value)
{
    int param1 = (kind == X64SegmentationRegisterKind::FS) ? ARCH_SET_FS : ARCH_SET_GS;
    long ret = syscall(__NR_arch_prctl, param1, value);
    if (unlikely(ret != 0))
    {
        int e = errno;
        fprintf(stderr, "[ERROR] System call to set up %s register failed with return value %d error %d(%s).",
                (kind == X64SegmentationRegisterKind::FS) ? "FS" : "GS", static_cast<int>(ret), e, strerror(e));
        ReleaseAssert(false);
    }
}

uint64_t X64_GetSegmentationRegisterBySyscall(X64SegmentationRegisterKind kind)
{
    int param1 = (kind == X64SegmentationRegisterKind::FS) ? ARCH_GET_FS : ARCH_GET_GS;
    uint64_t output;
    long ret = syscall(__NR_arch_prctl, param1, &output /*out*/);
    if (unlikely(ret != 0))
    {
        int e = errno;
        fprintf(stderr, "[ERROR] System call to read %s register failed with return value %d error %d(%s).",
                (kind == X64SegmentationRegisterKind::FS) ? "FS" : "GS", static_cast<int>(ret), e, strerror(e));
        ReleaseAssert(false);
    }
    return output;
}
