#pragma once

#include "common.h"
#include "constexpr_power_helper.h"
#include "misc_type_helper.h"

// A simple memory allocator for JIT memory allocation, using a segregated allocator for small allocations
// and mmap directly for large allocations.
//
// Note that for simplicity, currently we are disabling PIC and PIE and put all executable code (AOT or JIT)
// in the first 2GB address space.
//
// TODO: think about supporting ASLR later..
//

// The exponentially-growing stepping array for the segregated allocator
//
constexpr size_t x_jit_mem_alloc_stepping_array[] = {
// cellSize      num cells       remain
    32,     //      511            32
    64,     //      255            64
    96,     //      170            64
    144,    //      113            112
    192,    //      85             64
    272,    //      60             64
    368,    //      44             192
    496,    //      33             16
    704,    //      23             192
    1008,   //      16             256
    1488,   //      11             16
    2032,   //      8              128
    2720,   //      6              64
    3264,   //      5              64
    4080,   //      4              64
    5456,   //      3              16
    8176    //      2              32
};

constexpr size_t x_jit_mem_alloc_total_steppings = std::extent_v<decltype(x_jit_mem_alloc_stepping_array)>;

static_assert([]() {
    for (size_t i = 0; i < x_jit_mem_alloc_total_steppings; i++)
    {
        ReleaseAssert(x_jit_mem_alloc_stepping_array[i] % 16 == 0);
        ReleaseAssertImp(i > 0, x_jit_mem_alloc_stepping_array[i] > x_jit_mem_alloc_stepping_array[i - 1]);
    }
    return true;
}(), "All stepping values must be a multiple of 16 and in increasing order");

// Caller is responsible for checking 'smallAllocSize <= x_jit_mem_alloc_stepping_array.back()'
//
constexpr uint8_t WARN_UNUSED GetJitMemoryAllocatorSteppingFromSmallAllocationSize(size_t smallAllocSize)
{
    assert(smallAllocSize <= x_jit_mem_alloc_stepping_array[x_jit_mem_alloc_total_steppings - 1]);
    size_t left = 0;
    size_t right = x_jit_mem_alloc_total_steppings - 1;
    while (left != right)
    {
        // Invariant: 'right' is always an valid answer
        //
        assert(smallAllocSize <= x_jit_mem_alloc_stepping_array[right]);

        size_t mid = (left + right) / 2;
        if (smallAllocSize <= x_jit_mem_alloc_stepping_array[mid])
        {
            right = mid;
        }
        else
        {
            left = mid + 1;
        }
    }
    assert(smallAllocSize <= x_jit_mem_alloc_stepping_array[right]);
    AssertImp(right > 0, smallAllocSize > x_jit_mem_alloc_stepping_array[right - 1]);
    return static_cast<uint8_t>(right);
}

class JitMemoryPageHeader;
class JitMemoryLargeAllocationHeader;
class JitMemoryAllocator;

// The header base class for segregated allocator memory pages or large allocator allocations
// This always resides at a 16KB-boundary
//
struct JitMemoryPageHeaderBase
{
    MAKE_NONCOPYABLE(JitMemoryPageHeaderBase);
    MAKE_NONMOVABLE(JitMemoryPageHeaderBase);

    static constexpr size_t x_pageSize = 16 * 1024;
    static_assert(is_power_of_2(x_pageSize));

    // If m_cellSize == 0, this is the header for a large allocation
    // Otherwise, this is the page header for a segregated allocator page, and its value is the fixed allocation size for this page
    //
    uint16_t m_cellSize;

    bool IsLargeAllocation() const
    {
        assert(reinterpret_cast<uint64_t>(this) % x_pageSize == 0);
        assert(m_cellSize % 16 == 0);
        return m_cellSize == 0;
    }

    // Cast to segregated allocator memory page header
    //
    JitMemoryPageHeader* WARN_UNUSED AsSAHeader();

    // Cast to large allocation header
    //
    JitMemoryLargeAllocationHeader* WARN_UNUSED AsLAHeader();

    // Get the header from an allocation
    //
    static JitMemoryPageHeaderBase* WARN_UNUSED Get(const void* addr);
};
static_assert(sizeof(JitMemoryPageHeaderBase) == 2);

// Describes a memory page used by the segregated allocator for small allocations.
// Each memory page is always 16KB, always 16KB-aligned, and always used to allocate objects of a fixed size.
//
// This header always resides at the start of the 16KB page.
//
class JitMemoryPageHeader final : public JitMemoryPageHeaderBase
{
    MAKE_NONCOPYABLE(JitMemoryPageHeader);
    MAKE_NONMOVABLE(JitMemoryPageHeader);

    // The offset into the head of the free block list, 0 if the free list is empty
    //
    uint16_t m_freeListHead;

    // Number of already-allocated cells in this page
    //
    uint16_t m_numAllocatedCells;

    // The cell size stepping index
    //
    uint8_t m_cellSizeStepping;

    uint8_t m_unused1;

    // The next page of same cell size with available cells, nullptr if none
    //
    JitMemoryPageHeader* m_nextPage;

public:
    void Initialize(uint8_t cellSizeStepping, JitMemoryPageHeader* nextPage)
    {
        assert(reinterpret_cast<uint64_t>(this) % x_pageSize == 0);
        assert(cellSizeStepping < x_jit_mem_alloc_total_steppings);
        size_t cellSize = x_jit_mem_alloc_stepping_array[cellSizeStepping];

        m_cellSize = static_cast<uint16_t>(cellSize);
        m_freeListHead = static_cast<uint16_t>(sizeof(JitMemoryPageHeader));
        m_numAllocatedCells = 0;
        m_cellSizeStepping = cellSizeStepping;
        m_unused1 = 0;
        m_nextPage = nextPage;

        // Initialize the free list
        //
        {
            uint64_t addr = reinterpret_cast<uint64_t>(this) + sizeof(JitMemoryPageHeader);
            uint64_t addrEnd = reinterpret_cast<uint64_t>(this) + x_pageSize;
            while (true)
            {
                uint64_t curCellPtr = addr;
                addr += cellSize;
                bool hasNextCell = (addr + cellSize <= addrEnd);
                if (hasNextCell)
                {
                    *reinterpret_cast<uint16_t*>(curCellPtr) = addr & (x_pageSize - 1);
                }
                else
                {
                    *reinterpret_cast<uint16_t*>(curCellPtr) = 0;
                    break;
                }
            }
        }

        assert(!IsLargeAllocation() && AsSAHeader() == this);
    }

    uint8_t GetCellSizeStepping() { return m_cellSizeStepping; }

    bool WARN_UNUSED HasFreeCell()
    {
        return m_freeListHead != 0;
    }

    JitMemoryPageHeader* GetNextPage() { return m_nextPage; }

    void SetNextPage(JitMemoryPageHeader* nextPage)
    {
        assert(reinterpret_cast<uint64_t>(nextPage) % x_pageSize == 0);
        m_nextPage = nextPage;
    }

    void* WARN_UNUSED AllocateCell()
    {
        assert(HasFreeCell());
        assert(reinterpret_cast<uint64_t>(this) % x_pageSize == 0);

        void* res = reinterpret_cast<void*>(reinterpret_cast<uint64_t>(this) + m_freeListHead);
        uint16_t nextFreeCell = *reinterpret_cast<uint16_t*>(res);
        assert(nextFreeCell == 0 || (sizeof(JitMemoryPageHeader) <= nextFreeCell && nextFreeCell + m_cellSize <= x_pageSize));
        m_freeListHead = nextFreeCell;
        m_numAllocatedCells++;
        return res;
    }

    // Return true if the # of free cells in this page growed from 0 to 1,
    // which means the caller shall add this page to the available page list
    //
    bool WARN_UNUSED FreeCell(void* addr)
    {
        assert(reinterpret_cast<uint64_t>(this) % x_pageSize == 0);
        assert(reinterpret_cast<uint64_t>(this) + sizeof(JitMemoryPageHeader) <= reinterpret_cast<uint64_t>(addr));
        assert(reinterpret_cast<uint64_t>(addr) + m_cellSize <= reinterpret_cast<uint64_t>(this) + x_pageSize);
        assert(m_numAllocatedCells > 0);
        m_numAllocatedCells--;
        bool res = (m_freeListHead == 0);
        *reinterpret_cast<uint16_t*>(addr) = m_freeListHead;
        m_freeListHead = static_cast<uint16_t>(reinterpret_cast<uint64_t>(addr) & (x_pageSize - 1));
        return res;
    }
};
static_assert(offsetof_base_v<JitMemoryPageHeaderBase, JitMemoryPageHeader> == 0);
static_assert(sizeof(JitMemoryPageHeader) == 16);

class JitMemoryLargeAllocationHeader final : public JitMemoryPageHeaderBase
{
public:
    MAKE_NONCOPYABLE(JitMemoryLargeAllocationHeader);
    MAKE_NONMOVABLE(JitMemoryLargeAllocationHeader);

    uint16_t m_unused1;
    uint16_t m_unused2;
    uint16_t m_unused3;
    size_t m_size;

    struct DoublyLink
    {
        DoublyLink* prev;
        DoublyLink* next;
    };

    DoublyLink m_link;

    static JitMemoryLargeAllocationHeader* WARN_UNUSED GetFromLinkNode(DoublyLink* linkNode)
    {
        return reinterpret_cast<JitMemoryLargeAllocationHeader*>(reinterpret_cast<uint64_t>(linkNode) - offsetof_member_v<&JitMemoryLargeAllocationHeader::m_link>);
    }

    void Initialize(size_t size, DoublyLink* anchor)
    {
        assert(reinterpret_cast<uint64_t>(this) % x_pageSize == 0);
        assert(size % 4096 == 0);
        m_cellSize = 0;
        m_unused1 = 0;
        m_unused2 = 0;
        m_unused3 = 0;
        m_size = size;
        m_link.prev = anchor;
        m_link.next = anchor->next;
        anchor->next->prev = &m_link;
        anchor->next = &m_link;
        assert(IsLargeAllocation() && AsLAHeader() == this);
    }

    // Free underlying memory to OS
    //
    void Destroy();

    // Get the size of the memory region
    //
    size_t GetSize() { return m_size; }

    void* GetAllocatedObject()
    {
        assert(reinterpret_cast<uint64_t>(this) % x_pageSize == 0);
        return reinterpret_cast<uint8_t*>(this) + sizeof(JitMemoryLargeAllocationHeader);
    }
};
static_assert(offsetof_base_v<JitMemoryPageHeaderBase, JitMemoryLargeAllocationHeader> == 0);
static_assert(sizeof(JitMemoryLargeAllocationHeader) == 32);

// We assume the JIT memory returned from allocator is always 16-byte aligned.
// So the size of the header structs needs to be a multiple of 16 as well.
//
static_assert(sizeof(JitMemoryPageHeader) % 16 == 0);
static_assert(sizeof(JitMemoryLargeAllocationHeader) % 16 == 0);

inline JitMemoryPageHeader* WARN_UNUSED JitMemoryPageHeaderBase::AsSAHeader()
{
    assert(!IsLargeAllocation());
    return static_cast<JitMemoryPageHeader*>(this);
}

inline JitMemoryLargeAllocationHeader* WARN_UNUSED JitMemoryPageHeaderBase::AsLAHeader()
{
    assert(IsLargeAllocation());
    return static_cast<JitMemoryLargeAllocationHeader*>(this);
}

inline JitMemoryPageHeaderBase* WARN_UNUSED JitMemoryPageHeaderBase::Get(const void* addr)
{
    uint64_t addr64 = reinterpret_cast<uint64_t>(addr);
    addr64 &= ~static_cast<uint64_t>(x_pageSize - 1);
    JitMemoryPageHeaderBase* res = reinterpret_cast<JitMemoryPageHeaderBase*>(addr64);

    AssertImp(res->IsLargeAllocation(), (reinterpret_cast<uint64_t>(addr) & (x_pageSize - 1)) == sizeof(JitMemoryLargeAllocationHeader));
    AssertImp(!res->IsLargeAllocation(), (reinterpret_cast<uint64_t>(addr) & (x_pageSize - 1)) >= sizeof(JitMemoryPageHeader));
    AssertImp(!res->IsLargeAllocation(), ((reinterpret_cast<uint64_t>(addr) & (x_pageSize - 1)) - sizeof(JitMemoryPageHeader)) % res->m_cellSize == 0);

    return res;
}

class JitMemoryAllocator
{
public:
    JitMemoryAllocator()
    {
        for (size_t i = 0; i < x_jit_mem_alloc_total_steppings; i++)
        {
            m_freeList[i] = nullptr;
        }
        m_totalUsedMemory = 0;
        m_totalOsMemoryUsage = 0;
        m_reservedRangeCur = 0;
        m_reservedRangeEnd = 0;
        m_laAnchor.prev = &m_laAnchor;
        m_laAnchor.next = &m_laAnchor;
    }

    ~JitMemoryAllocator()
    {
        Shutdown();
    }

    // Allocate a piece of memory with size x_jit_mem_alloc_stepping_array[wantedStepping]
    // Directly responsible for 'm_totalUsedMemory' accounting
    //
    void* WARN_UNUSED AllocateGivenStepping(uint8_t wantedStepping)
    {
        assert(wantedStepping < x_jit_mem_alloc_total_steppings);
        JitMemoryPageHeader* freelist = m_freeList[wantedStepping];
        if (unlikely(freelist == nullptr))
        {
            freelist = AllocateNewPageForStepping(wantedStepping);
        }
        assert(freelist == m_freeList[wantedStepping]);

        assert(freelist != nullptr && freelist->HasFreeCell());
        void* res = freelist->AllocateCell();
        if (unlikely(!freelist->HasFreeCell()))
        {
            JitMemoryPageHeader* nextPage = freelist->GetNextPage();
            freelist->SetNextPage(nullptr);
            m_freeList[wantedStepping] = nextPage;
        }
        AssertImp(m_freeList[wantedStepping] != nullptr, m_freeList[wantedStepping]->HasFreeCell());

        m_totalUsedMemory += x_jit_mem_alloc_stepping_array[wantedStepping];

        assert(reinterpret_cast<uint64_t>(res) % 16 == 0);
        return res;
    }

    // Allocate a piece of memory with size 'wantedSize'
    //
    void* WARN_UNUSED AllocateGivenSize(size_t wantedSize)
    {
        if (wantedSize > x_jit_mem_alloc_stepping_array[x_jit_mem_alloc_total_steppings - 1])
        {
            return DoLargeAllocation(wantedSize);
        }
        else
        {
            uint8_t wantedStepping = GetJitMemoryAllocatorSteppingFromSmallAllocationSize(wantedSize);
            assert(wantedStepping < x_jit_mem_alloc_total_steppings && x_jit_mem_alloc_stepping_array[wantedStepping] >= wantedSize);
            return AllocateGivenStepping(wantedStepping);
        }
    }

    // Free an allocated piece of memory
    // Directly responsible for 'm_totalUsedMemory' and 'm_totalOsMemoryUsage' accounting
    //
    void Free(void* addr)
    {
        JitMemoryPageHeaderBase* hb = JitMemoryPageHeaderBase::Get(addr);
        if (unlikely(hb->IsLargeAllocation()))
        {
            JitMemoryLargeAllocationHeader* hdr = hb->AsLAHeader();
            assert(m_totalUsedMemory >= hdr->GetSize());
            m_totalUsedMemory -= hdr->GetSize();
            assert(m_totalOsMemoryUsage >= hdr->GetSize());
            m_totalOsMemoryUsage -= hdr->GetSize();
            hdr->Destroy();
        }
        else
        {
            JitMemoryPageHeader* hdr = hb->AsSAHeader();
            assert(m_totalUsedMemory >= hdr->m_cellSize);
            m_totalUsedMemory -= hdr->m_cellSize;

            bool shouldInsertToFreeList = hdr->FreeCell(addr);
            if (unlikely(shouldInsertToFreeList))
            {
                assert(hdr->HasFreeCell());
                uint8_t stepping = hdr->GetCellSizeStepping();
                assert(stepping < x_jit_mem_alloc_total_steppings);
                assert(hdr->GetNextPage() == nullptr);
                hdr->SetNextPage(m_freeList[stepping]);
                m_freeList[stepping] = hdr;
            }
        }
    }

    // Includes internal fragmentation
    //
    size_t GetTotalJITCodeSize()
    {
        return m_totalUsedMemory;
    }

    size_t GetMemorySizeAllocatedFromOs()
    {
        return m_totalOsMemoryUsage;
    }

private:
    // Returns the new free list head
    //
    JitMemoryPageHeader* AllocateNewPageForStepping(uint8_t stepping)
    {
        assert(stepping < x_jit_mem_alloc_total_steppings);
        assert(m_freeList[stepping] == nullptr);

        JitMemoryPageHeader* newPage = AllocateUninitalizedPage();
        newPage->Initialize(stepping, nullptr /*nextPage*/);

        m_freeList[stepping] = newPage;
        return newPage;
    }

    // Directly responsible for 'm_totalUsedMemory' and 'm_totalOsMemoryUsage' accounting
    //
    void* WARN_UNUSED DoLargeAllocation(size_t size);

    // Directly responsible for 'm_totalOsMemoryUsage' accounting
    //
    JitMemoryPageHeader* WARN_UNUSED AllocateUninitalizedPage();

    // Deallocate everything and free all memory to OS.
    //
    void Shutdown();

    JitMemoryPageHeader* m_freeList[x_jit_mem_alloc_total_steppings];

    // The current size of memory the user has used.
    // This statastic includes internal fragmentation.
    //
    size_t m_totalUsedMemory;

    // The current size of memory we allocated from OS
    // i.e., m_totalUsedMemory + memory of available cells + page header overhead
    //
    size_t m_totalOsMemoryUsage;

    // mmap returns 4KB-aligned memory but we want 16KB-aligned memory.
    // Of course one can do this by allocate 32KB with one mmap then cut off the unaligned parts with two munmap,
    // but to make things better, we reserve (not allocate) x_reserveRangeSize memory range from OS once,
    // then use MAP_FIXED to turn them into usable memory as needed
    //
    static constexpr size_t x_reserveRangeSize = 16 * 1024 * 1024;
    static_assert(x_reserveRangeSize % JitMemoryPageHeaderBase::x_pageSize == 0);

    uint64_t m_reservedRangeCur;
    uint64_t m_reservedRangeEnd;

    // A circular doubly-linked list chaining all the large allocations, for clean shutdown
    //
    JitMemoryLargeAllocationHeader::DoublyLink m_laAnchor;

    // For clean shutdown
    // It's ugly to use std::vector, but for now...
    //
    std::vector<void*> m_unmapList;
};
