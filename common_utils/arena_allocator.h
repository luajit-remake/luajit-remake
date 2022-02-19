#pragma once

#include "common.h"
#include "global_arena_memory_pool.h"
#include "constexpr_power_helper.h"

namespace CommonUtils
{

inline GlobalArenaMemoryPool g_arenaMemoryPool;

class TempArenaAllocator
{
public:
    TempArenaAllocator()
        : m_listHead(0)
        , m_currentAddress(8)
        , m_currentAddressEnd(0)
    { }

    ~TempArenaAllocator()
    {
        FreeAllMemoryChunks();
    }

    void Reset()
    {
        FreeAllMemoryChunks();
    }

    void* WARN_UNUSED Allocate(size_t alignment, size_t size)
    {
        // TODO: support large size allocation
        //
        TestAssert(size <= g_arenaMemoryPool.x_memoryChunkSize - 4096);
        AlignCurrentAddress(alignment);
        if (m_currentAddress + size > m_currentAddressEnd)
        {
            GetNewMemoryChunk();
            AlignCurrentAddress(alignment);
            TestAssert(m_currentAddress + size <= m_currentAddressEnd);
        }
        TestAssert(m_currentAddress % alignment == 0);
        uintptr_t result = m_currentAddress;
        m_currentAddress += size;
        TestAssert(m_currentAddress <= m_currentAddressEnd);
        return reinterpret_cast<void*>(result);
    }

private:
    void GetNewMemoryChunk()
    {
        uintptr_t address = g_arenaMemoryPool.GetMemoryChunk();
        AppendToList(address);
        // the first 8 bytes of the region is used as linked list
        //
        m_currentAddress = address + 8;
        m_currentAddressEnd = address + g_arenaMemoryPool.x_memoryChunkSize;
    }

    void AlignCurrentAddress(size_t alignment)
    {
        TestAssert(alignment <= 4096 && is_power_of_2(static_cast<int>(alignment)));
        size_t mask = alignment - 1;
        m_currentAddress += mask;
        m_currentAddress &= ~mask;
    }

    void AppendToList(uintptr_t address)
    {
        *reinterpret_cast<uintptr_t*>(address) = m_listHead;
        m_listHead = address;
    }

    void FreeAllMemoryChunks()
    {
        while (m_listHead != 0)
        {
            uintptr_t next = *reinterpret_cast<uintptr_t*>(m_listHead);
            g_arenaMemoryPool.FreeMemoryChunk(m_listHead);
            m_listHead = next;
        }
        m_currentAddress = 8;
        m_currentAddressEnd = 0;
    }

    uintptr_t m_listHead;
    uintptr_t m_currentAddress;
    uintptr_t m_currentAddressEnd;
};

static_assert(is_power_of_2(__STDCPP_DEFAULT_NEW_ALIGNMENT__), "std default new alignment is not a power of 2");

}   // namespace CommonUtils

inline void* operator new(std::size_t count, CommonUtils::TempArenaAllocator& taa)
{
    return taa.Allocate(__STDCPP_DEFAULT_NEW_ALIGNMENT__, count);
}

inline void* operator new[](std::size_t count, CommonUtils::TempArenaAllocator& taa)
{
    return taa.Allocate(__STDCPP_DEFAULT_NEW_ALIGNMENT__, count);
}

inline void* operator new(std::size_t count, std::align_val_t al, CommonUtils::TempArenaAllocator& taa)
{
    TestAssert(CommonUtils::is_power_of_2(static_cast<int>(al)));
    return taa.Allocate(static_cast<size_t>(al), count);
}

inline void* operator new[](std::size_t count, std::align_val_t al, CommonUtils::TempArenaAllocator& taa)
{
    TestAssert(CommonUtils::is_power_of_2(static_cast<int>(al)));
    return taa.Allocate(static_cast<size_t>(al), count);
}
