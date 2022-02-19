#pragma once

#include "concurrent_queue.h"

namespace CommonUtils
{

class GlobalArenaMemoryPool
{
public:
    GlobalArenaMemoryPool()
        : m_freeListSizeApproximation(0)
        , m_freeList()
    { }

    // We allocate memory in 256KB chunks
    //
    static constexpr size_t x_memoryChunkSize = 256 * 1024;

    // The maximum number of memory chunks we keep in the codegen memory pool
    // We don't give memory back to OS before we reach the threshold
    //
    static constexpr size_t x_maxChunksInMemoryPool = 32 * 1024;

    uintptr_t WARN_UNUSED GetMemoryChunk()
    {
        uintptr_t result;
        bool success = m_freeList.try_dequeue(result /*out*/);
        if (success)
        {
            m_freeListSizeApproximation.fetch_sub(1);
            return result;
        }

        void* mmapResult = mmap(nullptr, x_memoryChunkSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
        if (mmapResult == MAP_FAILED)
        {
            ReleaseAssert(false && "Out Of Memory");
        }

        return reinterpret_cast<uintptr_t>(mmapResult);
    }

    void FreeMemoryChunk(uintptr_t address)
    {
        TestAssert(address % 4096 == 0);
        bool enqueued = false;
        // Important to cast x_maxChunksInMemoryPool to int, not the other way around:
        // the m_freeListSizeApproximation is just an approximation, it can be negative under concurrency
        //
        if (likely(m_freeListSizeApproximation.load() <= static_cast<int>(x_maxChunksInMemoryPool)))
        {
            // enqueue() returns false if we run OOM
            //
            enqueued = m_freeList.enqueue(address);
        }
        if (unlikely(!enqueued))
        {
            // Just unmap the memory
            //
            int ret = munmap(reinterpret_cast<void*>(address), x_memoryChunkSize);
            if (unlikely(ret != 0))
            {
                int err = errno;
                fprintf(stderr, "[WARNING] [Memory Pool] munmap failed with error %d(%s)\n", err, strerror(err));
            }
        }
        else
        {
            m_freeListSizeApproximation.fetch_add(1);
        }
    }

private:
    std::atomic<int> m_freeListSizeApproximation;
    ConcurrentQueue<uintptr_t> m_freeList;
};

}   // namespace CommonUtils
