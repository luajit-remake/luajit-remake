#include "gtest/gtest.h"

#include "drt/jit_memory_allocator.h"
#include "misc_math_helper.h"

TEST(JITMemoryAllocator, Sanity)
{
    JitMemoryAllocator alloc;

    struct AllocationDesc
    {
        uint8_t* ptr;
        size_t size;
        // A pattern to be rewritten into the allocated area, for validation
        //
        std::array<uint8_t, 8> pattern;

        static AllocationDesc Create()
        {
            int size;
            int dice = rand() % 100;
            if (dice < 40)
            {
                size = rand() % 100 + 1;
            }
            else if (dice < 70)
            {
                size = rand() % 2000 + 1;
            }
            else if (dice < 98)
            {
                size = rand() % 10000 + 1;
            }
            else
            {
                size = rand() % 100000 + 1;
            }

            AllocationDesc r;
            r.ptr = nullptr;
            r.size = static_cast<size_t>(size);
            for (size_t i = 0; i < 8; i++)
            {
                r.pattern[i] = static_cast<uint8_t>(rand() % 256);
            }
            return r;
        }
    };

    std::vector<AllocationDesc> allDesc;
    for (size_t i = 0; i < 5000; i++)
    {
        allDesc.push_back(AllocationDesc::Create());
    }

    size_t totalMem = 0;
    for (AllocationDesc& desc : allDesc)
    {
        uint8_t* ptr = reinterpret_cast<uint8_t*>(alloc.AllocateGivenSize(desc.size));
        desc.ptr = ptr;

        std::array<uint8_t, 8> pattern = desc.pattern;
        for (size_t i = 0; i < desc.size; i++)
        {
            ptr[i] = pattern[i % 8];
        }

        {
            size_t s = RoundUpToMultipleOf<16>(desc.size);
            if (s < 32) { s = 32; }
            totalMem += s;
        }
    }

    size_t reportedUseSize = alloc.GetTotalJITCodeSize();
    ReleaseAssert(reportedUseSize >= totalMem);
    ReleaseAssert(reportedUseSize <= totalMem * 3 / 2);

    size_t reportedOsSize = alloc.GetMemorySizeAllocatedFromOs();

    size_t unusedMemory = 0;
    for (size_t i = 0; i < x_jit_mem_alloc_total_steppings; i++)
    {
        if (alloc.m_freeList[i] != nullptr) {
            unusedMemory += (x_jit_mem_alloc_stepping_cell_array[i] - alloc.m_freeList[i]->m_numAllocatedCells) * x_jit_mem_alloc_stepping_array[i];
        }
    }

    ReleaseAssert(reportedOsSize >= reportedUseSize);
    ReleaseAssert(reportedOsSize == reportedUseSize + alloc.GetTotalMemorySpilled() + unusedMemory);

    for (AllocationDesc& desc : allDesc)
    {
        uint8_t* ptr = desc.ptr;
        std::array<uint8_t, 8> pattern = desc.pattern;
        for (size_t i = 0; i < desc.size; i++)
        {
            ReleaseAssert(ptr[i] == pattern[i % 8]);
        }
    }

    // Free everything in random order
    //
    std::vector<size_t> rndOrder;
    for (size_t i = 0; i < allDesc.size(); i++)
    {
        rndOrder.push_back(i);
    }

    for (size_t i = 0; i < rndOrder.size(); i++)
    {
        std::swap(rndOrder[i], rndOrder[static_cast<size_t>(rand()) % (i + 1)]);
    }

    for (size_t i = 0; i < rndOrder.size(); i++)
    {
        uint8_t* ptr = allDesc[rndOrder[i]].ptr;
        alloc.Free(ptr);
    }

    ReleaseAssert(alloc.GetTotalJITCodeSize() == 0);

    // Do the same allocation again, but in random order
    //
    for (size_t i = 0; i < rndOrder.size(); i++)
    {
        std::swap(rndOrder[i], rndOrder[static_cast<size_t>(rand()) % (i + 1)]);
    }

    for (AllocationDesc& desc : allDesc)
    {
        for (size_t i = 0; i < 8; i++)
        {
            desc.pattern[i] = static_cast<uint8_t>(rand() % 256);
        }
    }

    for (size_t i = 0; i < rndOrder.size(); i++)
    {
        AllocationDesc& desc = allDesc[rndOrder[i]];
        uint8_t* ptr = reinterpret_cast<uint8_t*>(alloc.AllocateGivenSize(desc.size));
        desc.ptr = ptr;

        std::array<uint8_t, 8> pattern = desc.pattern;
        for (size_t k = 0; k < desc.size; k++)
        {
            ptr[k] = pattern[k % 8];
        }
    }

    // We should not be allocating anything more from OS
    //
    ReleaseAssert(alloc.GetMemorySizeAllocatedFromOs() == reportedOsSize);
    ReleaseAssert(alloc.GetTotalJITCodeSize() == reportedUseSize);

    for (AllocationDesc& desc : allDesc)
    {
        uint8_t* ptr = desc.ptr;
        std::array<uint8_t, 8> pattern = desc.pattern;
        for (size_t i = 0; i < desc.size; i++)
        {
            ReleaseAssert(ptr[i] == pattern[i % 8]);
        }
    }
}
