#include "gtest/gtest.h"

#include "temp_arena_allocator.h"
#include "misc_math_helper.h"

TEST(TempArenaAllocator, Sanity)
{
    TempArenaAllocator alloc;

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
            else if (dice < 95)
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

    std::vector<AllocationDesc> allDesc[3];
    for (size_t k = 0; k < 3; k++)
    {
        for (size_t i = 0; i < 5000; i++)
        {
            allDesc[k].push_back(AllocationDesc::Create());
        }
    }

    auto doAlloc = [&](AllocationDesc& desc)
    {
        uint8_t* ptr = reinterpret_cast<uint8_t*>(alloc.AllocateWithAlignment(1, desc.size));
        desc.ptr = ptr;

        std::array<uint8_t, 8> pattern = desc.pattern;
        for (size_t i = 0; i < desc.size; i++)
        {
            ptr[i] = pattern[i % 8];
        }
    };

    auto doCheck = [&](AllocationDesc& desc)
    {
        uint8_t* ptr = desc.ptr;
        std::array<uint8_t, 8> pattern = desc.pattern;
        for (size_t i = 0; i < desc.size; i++)
        {
            ReleaseAssert(ptr[i] == pattern[i % 8]);
        }
    };

    for (AllocationDesc& desc : allDesc[0])
    {
        doAlloc(desc);
    }

    TempArenaAllocator::Mark mark = alloc.TakeMark();

    for (AllocationDesc& desc : allDesc[1])
    {
        doAlloc(desc);
    }

    for (uint32_t k : { 0U, 1U })
    {
        for (AllocationDesc& desc : allDesc[k])
        {
            doCheck(desc);
        }
    }

    alloc.ResetToMark(mark);

    for (AllocationDesc& desc : allDesc[2])
    {
        doAlloc(desc);
    }

    for (uint32_t k : { 0U, 2U })
    {
        for (AllocationDesc& desc : allDesc[k])
        {
            doCheck(desc);
        }
    }
}

TEST(TempArenaAllocator, StlVector)
{
    TempArenaAllocator alloc;
    TempVector<int> vec(alloc);
    std::vector<int> gold;
    for (size_t i = 0; i < 100000; i++)
    {
        int value = rand();
        vec.push_back(value);
        gold.push_back(value);
    }
    ReleaseAssert(vec.size() == gold.size());
    for (size_t i = 0; i < vec.size(); i++)
    {
        ReleaseAssert(vec[i] == gold[i]);
    }
}
