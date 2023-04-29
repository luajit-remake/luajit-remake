#include "jit_memory_allocator.h"
#include "misc_math_helper.h"

static void do_munmap(void* ptr, size_t size)
{
    int r = munmap(ptr, size);
    LOG_WARNING_WITH_ERRNO_IF(r != 0, "Cannot unmap JIT allocation memory of size %llu", static_cast<unsigned long long>(size));
}

void JitMemoryLargeAllocationHeader::Destroy()
{
    DoublyLink* next = m_link.next;
    DoublyLink* prev = m_link.prev;
    next->prev = prev;
    prev->next = next;
    assert(reinterpret_cast<uint64_t>(this) % x_pageSize == 0);
    do_munmap(this, GetSize());
}

static size_t WARN_UNUSED RoundUpToPowerOfTwoAlignment(size_t value, size_t alignment)
{
    assert(is_power_of_2(alignment));
    value += alignment - 1;
    size_t mask = ~(alignment - 1);
    return value & mask;
}

static void* WARN_UNUSED do_mmap_with_custom_alignment(size_t alignment, size_t length, int prot_flags, int map_flags)
{
    assert(is_power_of_2(alignment));
    assert(alignment > 4096);   // no reason to use this function if alignment is smaller than default alignment of mmap

    length = RoundUpToMultipleOf<4096>(length);

    size_t mmapLength = length + alignment;
    void* ptrVoid = mmap(nullptr, mmapLength, prot_flags, map_flags, -1, 0);
    VM_FAIL_WITH_ERRNO_IF(ptrVoid == MAP_FAILED, "Failed to allocate JIT memory of size %llu", static_cast<unsigned long long>(mmapLength));

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

JitMemoryPageHeader* WARN_UNUSED JitMemoryAllocator::AllocateUninitalizedPage()
{
    constexpr size_t x_pageSize = JitMemoryPageHeaderBase::x_pageSize;
    if (unlikely(m_reservedRangeCur == m_reservedRangeEnd))
    {
        void* reservedRange = do_mmap_with_custom_alignment(x_pageSize /*alignment*/, x_reserveRangeSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_32BIT);
        m_reservedRangeCur = reinterpret_cast<uint64_t>(reservedRange);
        assert(m_reservedRangeCur % x_pageSize == 0);
        m_reservedRangeEnd = m_reservedRangeCur + x_reserveRangeSize;

        m_unmapList.push_back(reservedRange);
    }

    assert(m_reservedRangeCur + x_pageSize <= m_reservedRangeEnd);
    assert(m_reservedRangeCur % x_pageSize == 0);
    void* pageAddr = reinterpret_cast<void*>(m_reservedRangeCur);
    m_reservedRangeCur += x_pageSize;

    void* r = mmap(pageAddr, x_pageSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_FIXED, -1, 0);
    VM_FAIL_WITH_ERRNO_IF(r == MAP_FAILED, "Failed to allocate JIT memory of size %llu", static_cast<unsigned long long>(x_pageSize));
    assert(pageAddr == r);

    m_totalOsMemoryUsage += x_pageSize;

    return reinterpret_cast<JitMemoryPageHeader*>(pageAddr);
}

void* WARN_UNUSED JitMemoryAllocator::DoLargeAllocation(size_t size)
{
    constexpr size_t x_pageSize = JitMemoryPageHeaderBase::x_pageSize;

    size = RoundUpToMultipleOf<4096>(size + sizeof(JitMemoryLargeAllocationHeader));

    void* ptrVoid = do_mmap_with_custom_alignment(x_pageSize /*alignment*/,
                                                  size /*length*/,
                                                  PROT_READ | PROT_WRITE | PROT_EXEC,
                                                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_32BIT);

    JitMemoryLargeAllocationHeader* hdr = reinterpret_cast<JitMemoryLargeAllocationHeader*>(ptrVoid);
    hdr->Initialize(size, &m_laAnchor);

    m_totalUsedMemory += size;
    m_totalOsMemoryUsage += size;

    void* res = hdr->GetAllocatedObject();
    assert(reinterpret_cast<uint64_t>(res) % 16 == 0);
    return res;
}

void JitMemoryAllocator::Shutdown()
{
    while (m_laAnchor.next != &m_laAnchor)
    {
        JitMemoryLargeAllocationHeader* hdr = JitMemoryLargeAllocationHeader::GetFromLinkNode(m_laAnchor.next);
        Free(hdr->GetAllocatedObject());
    }

    assert(m_laAnchor.prev == &m_laAnchor);
    assert(m_laAnchor.next == &m_laAnchor);

    m_totalOsMemoryUsage += m_reservedRangeEnd - m_reservedRangeCur;
    for (void* ptr : m_unmapList)
    {
        do_munmap(ptr, x_reserveRangeSize);
        m_totalOsMemoryUsage -= x_reserveRangeSize;
    }

    assert(m_totalOsMemoryUsage == 0);
}
