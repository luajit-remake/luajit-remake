#include "mmap_utils.h"
#include "misc_math_helper.h"
#include "constexpr_power_helper.h"

void do_munmap(void* ptr, size_t size)
{
    int r = munmap(ptr, size);
    LOG_WARNING_WITH_ERRNO_IF(r != 0, "Cannot unmap allocation memory of size %llu", static_cast<unsigned long long>(size));
}

static size_t WARN_UNUSED RoundUpToPowerOfTwoAlignment(size_t value, size_t alignment)
{
    assert(is_power_of_2(alignment));
    value += alignment - 1;
    size_t mask = ~(alignment - 1);
    return value & mask;
}

void* WARN_UNUSED do_mmap_with_custom_alignment(size_t alignment, size_t length, int prot_flags, int map_flags)
{
    assert(is_power_of_2(alignment));
    assert(alignment > 4096);   // no reason to use this function if alignment is smaller than default alignment of mmap

    length = RoundUpToMultipleOf<4096>(length);

    size_t mmapLength = length + alignment;
    void* ptrVoid = mmap(nullptr, mmapLength, prot_flags, map_flags, -1, 0);
    VM_FAIL_WITH_ERRNO_IF(ptrVoid == MAP_FAILED, "Failed to allocate memory of size %llu", static_cast<unsigned long long>(mmapLength));

    assert(ptrVoid != nullptr);

    uint64_t ptrU64 = reinterpret_cast<uint64_t>(ptrVoid);
    uint64_t mmapEnd = ptrU64 + mmapLength;

    uint64_t alignedPtr = RoundUpToPowerOfTwoAlignment(ptrU64, alignment);
    assert(ptrU64 <= alignedPtr && alignedPtr % alignment == 0);

    uint64_t rangeEnd = alignedPtr + length;
    assert(rangeEnd <= mmapEnd);

    // Unmap the unnecessary ranges
    //
    assert(ptrU64 % 4096 == 0 && (alignedPtr - ptrU64) % 4096 == 0);
    if (ptrU64 < alignedPtr)
    {
        do_munmap(reinterpret_cast<void*>(ptrU64), alignedPtr - ptrU64);
    }

    assert(rangeEnd % 4096 == 0 && (mmapEnd - rangeEnd) % 4096 == 0);
    if (rangeEnd < mmapEnd)
    {
        do_munmap(reinterpret_cast<void*>(rangeEnd), mmapEnd - rangeEnd);
    }

    return reinterpret_cast<void*>(alignedPtr);
}
