#pragma once

#include "common.h"
#include "misc_math_helper.h"
#include "constexpr_power_helper.h"

static constexpr size_t x_tempArenaAllocatorPageSize = 32768;

static_assert(x_tempArenaAllocatorPageSize % 4096 == 0);

class GlobalTempArenaAllocatorMemoryPool
{
    MAKE_NONCOPYABLE(GlobalTempArenaAllocatorMemoryPool);
    MAKE_NONMOVABLE(GlobalTempArenaAllocatorMemoryPool);

public:
    GlobalTempArenaAllocatorMemoryPool()
        : m_freeListSize(0)
        , m_freeList(0)
    { }

    // The maximum number of memory chunks we keep in the codegen memory pool
    // We don't give memory back to OS before we reach the threshold
    //
    static constexpr size_t x_maxChunksInMemoryPool = 32;

    uintptr_t GetMemoryChunk()
    {
        uintptr_t result = TryGetMemoryChunk();
        if (result != 0)
        {
            return result;
        }

        void* mmapResult = mmap(nullptr, x_tempArenaAllocatorPageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
        VM_FAIL_WITH_ERRNO_IF(mmapResult == MAP_FAILED, "Failed to allocate memory of size %llu", static_cast<unsigned long long>(x_tempArenaAllocatorPageSize));

        if (x_isTestBuild)
        {
            // Clobber contents in test build
            //
            memset(mmapResult, 0xcd, x_tempArenaAllocatorPageSize);
        }

        return reinterpret_cast<uintptr_t>(mmapResult);
    }

    void FreeMemoryChunk(uintptr_t address)
    {
        TestAssert(address != 0 && address % 4096 == 0);

        if (x_isTestBuild)
        {
            // In test build, clobber contents
            //
            memset(reinterpret_cast<void*>(address), 0xcd, x_tempArenaAllocatorPageSize);
        }

        {
            // std::lock_guard<std::mutex> guard(m_lock);
            if (m_freeListSize < x_maxChunksInMemoryPool)
            {
                m_freeListSize++;
                *reinterpret_cast<uintptr_t*>(address) = m_freeList;
                m_freeList = address;
                return;
            }
        }

        // The free list is large enough, just unmap the memory
        //
        int ret = munmap(reinterpret_cast<void*>(address), x_tempArenaAllocatorPageSize);
        LOG_WARNING_WITH_ERRNO_IF(ret != 0, "Failed to free memory of size %llu", static_cast<unsigned long long>(x_tempArenaAllocatorPageSize));
    }

private:
    // Return 0 on failure
    //
    uintptr_t WARN_UNUSED TryGetMemoryChunk()
    {
        // std::lock_guard<std::mutex> guard(m_lock);

        if (m_freeList == 0)
        {
            return 0;
        }

        TestAssert(m_freeListSize > 0);
        m_freeListSize--;
        uintptr_t result = m_freeList;
        m_freeList = *reinterpret_cast<uintptr_t*>(m_freeList);
        return result;
    }

    // std::mutex m_lock;
    size_t m_freeListSize;
    uintptr_t m_freeList;
};

extern GlobalTempArenaAllocatorMemoryPool g_globalTempArenaAllocMemPool;

template<class T>
struct StlAdaptorForTempArenaAlloc;

class TempArenaAllocator
{
    MAKE_NONCOPYABLE(TempArenaAllocator);

public:
    TempArenaAllocator()
        : m_listHead(0)
        , m_customSizeListHead(0)
        , m_currentAddress(8)
        , m_currentAddressEnd(0)
    { }

    TempArenaAllocator(TempArenaAllocator&& other)
    {
        m_listHead = other.m_listHead;
        m_customSizeListHead = other.m_customSizeListHead;
        m_currentAddress = other.m_currentAddress;
        m_currentAddressEnd = other.m_currentAddressEnd;
        other.m_listHead = 0;
        other.m_customSizeListHead = 0;
        other.m_currentAddress = 8;
        other.m_currentAddressEnd = 0;
    }

    TempArenaAllocator& WARN_UNUSED operator=(TempArenaAllocator&& other)
    {
        FreeAllMemoryChunks();
        m_listHead = other.m_listHead;
        m_customSizeListHead = other.m_customSizeListHead;
        m_currentAddress = other.m_currentAddress;
        m_currentAddressEnd = other.m_currentAddressEnd;
        other.m_listHead = 0;
        other.m_customSizeListHead = 0;
        other.m_currentAddress = 8;
        other.m_currentAddressEnd = 0;
        return *this;
    }

    ~TempArenaAllocator()
    {
        FreeAllMemoryChunks();
    }

    template<typename T>
    operator StlAdaptorForTempArenaAlloc<T>();

    template<typename T>
    operator const StlAdaptorForTempArenaAlloc<T>() const;

    void Reset()
    {
        FreeAllMemoryChunks();
    }

    class Mark
    {
        friend TempArenaAllocator;
        uintptr_t m_markListHead;
        uintptr_t m_markCustomSizeListHead;
        uintptr_t m_markCurrentAddress;
        uintptr_t m_markCurrentAddressEnd;
    };

    Mark WARN_UNUSED TakeMark()
    {
        Mark res;
        res.m_markListHead = m_listHead;
        res.m_markCustomSizeListHead = m_customSizeListHead;
        res.m_markCurrentAddress = m_currentAddress;
        res.m_markCurrentAddressEnd = m_currentAddressEnd;
        return res;
    }

    // Deallocate everything allocated after the mark is taken
    //
    void NO_INLINE ResetToMark(const Mark& mark)
    {
        while (m_listHead != mark.m_markListHead)
        {
            TestAssert(m_listHead != 0);
            uintptr_t next = *reinterpret_cast<uintptr_t*>(m_listHead);
            g_globalTempArenaAllocMemPool.FreeMemoryChunk(m_listHead);
            m_listHead = next;
        }
        while (m_customSizeListHead != mark.m_markCustomSizeListHead)
        {
            TestAssert(m_customSizeListHead != 0);
            uintptr_t next = reinterpret_cast<uintptr_t*>(m_customSizeListHead)[0];
            size_t size = reinterpret_cast<uintptr_t*>(m_customSizeListHead)[1];
            int ret = munmap(reinterpret_cast<void*>(m_customSizeListHead), size);
            LOG_WARNING_WITH_ERRNO_IF(ret != 0, "Failed to free memory of size %llu", static_cast<unsigned long long>(size));
            m_customSizeListHead = next;
        }
        m_currentAddress = mark.m_markCurrentAddress;
        m_currentAddressEnd = mark.m_markCurrentAddressEnd;
    }

    __attribute__((__malloc__)) void* WARN_UNUSED ALWAYS_INLINE AllocateWithAlignment(size_t alignment, size_t size)
    {
        Assert(alignment <= 4096 && is_power_of_2(alignment));
        AlignCurrentAddress(alignment);
        if (likely(m_currentAddress + size <= m_currentAddressEnd))
        {
            uintptr_t result = m_currentAddress;
            m_currentAddress += size;
            TestAssert(m_currentAddress <= m_currentAddressEnd);
            TestAssert(result % alignment == 0);
            return reinterpret_cast<void*>(result);
        }
        else
        {
            return AllocateWithAlignmentSlowPath(alignment, size);
        }
    }

    template<typename T>
    __attribute__((__malloc__)) T* WARN_UNUSED ALWAYS_INLINE AllocateArrayUninitialized(size_t num)
    {
        static_assert(is_power_of_2(alignof(T)) && sizeof(T) % alignof(T) == 0);
        size_t allocSize = sizeof(T) * num;
        return reinterpret_cast<T*>(AllocateWithAlignment(alignof(T), allocSize));
    }

    // This assumes that the constructor will never throw
    // Behaves like 'new T[num]'. Note that this is *not* a value initialization,
    // so e.g., the array is not zeroed out if the array element is a primitive type (same as how 'new int[10]' behaves)
    //
    template<typename T, typename... Args>
    __attribute__((__malloc__)) T* WARN_UNUSED ALWAYS_INLINE AllocateArray(size_t num)
    {
        T* ptr = AllocateArrayUninitialized<T>(num);
        for (size_t i = 0; i < num; i++)
        {
            // Not T(), which would do value-initialization (e.g., zeroes out primitive types)
            //
            new (static_cast<void*>(ptr + i)) T;
        }
        return ptr;
    }

    // This assumes that the constructor will never throw
    // Note that if 'args' is empty, the AllocateArray() specialization will be chosen, which does not do value-initialization.
    // To do explicit value initialization (i.e., 'new T[num]()'), use 'AllocateArrayWithValueInitialization' instead
    //
    template<typename T, typename... Args>
    __attribute__((__malloc__)) T* WARN_UNUSED ALWAYS_INLINE AllocateArray(size_t num, Args&&... args)
    {
        T* ptr = AllocateArrayUninitialized<T>(num);
        for (size_t i = 0; i < num; i++)
        {
            new (static_cast<void*>(ptr + i)) T(std::forward<Args>(args)...);
        }
        return ptr;
    }

    // This assumes that the constructor will never throw
    // Behaves like 'new T[num]()', that is, it does a value-initialization so it zeroes out primitive-typed array etc.
    //
    template<typename T, typename... Args>
    __attribute__((__malloc__)) T* WARN_UNUSED ALWAYS_INLINE AllocateArrayWithValueInitialization(size_t num)
    {
        T* ptr = AllocateArrayUninitialized<T>(num);
        for (size_t i = 0; i < num; i++)
        {
            new (static_cast<void*>(ptr + i)) T();
        }
        return ptr;
    }

    template<typename T>
    __attribute__((__malloc__)) T* WARN_UNUSED ALWAYS_INLINE AllocateObjectUninitialized()
    {
        static_assert(is_power_of_2(alignof(T)) && sizeof(T) % alignof(T) == 0);
        return AllocateArrayUninitialized<T>(1 /*num*/);
    }

    // Behaves like 'new T', that is, it does not zero-initialize if T is a primitive type or a class with default constructor
    //
    template<typename T, typename... Args>
    __attribute__((__malloc__)) T* WARN_UNUSED ALWAYS_INLINE AllocateObject()
    {
        T* ptr = AllocateObjectUninitialized<T>();
        new (static_cast<void*>(ptr)) T;
        return ptr;
    }

    template<typename T, typename... Args>
    __attribute__((__malloc__)) T* WARN_UNUSED ALWAYS_INLINE AllocateObject(Args&&... args)
    {
        T* ptr = AllocateObjectUninitialized<T>();
        new (static_cast<void*>(ptr)) T(std::forward<Args>(args)...);
        return ptr;
    }

    // Behaves like 'new T()'
    //
    template<typename T, typename... Args>
    __attribute__((__malloc__)) T* WARN_UNUSED ALWAYS_INLINE AllocateObjectWithValueInitialization()
    {
        T* ptr = AllocateObjectUninitialized<T>();
        new (static_cast<void*>(ptr)) T();
        return ptr;
    }

    // Note that if the trailing array is not at the end of the object due to alignment padding, e.g.,
    //     struct S { int32_t x; int16_t y; int16_t z[0]; }
    // We will allocate more memory than needed. But this is safe anyway, and if the caller cares about this
    // it can do allocation by AllocateUninitialized directly.
    //
    template<typename T>
    __attribute__((__malloc__)) T* WARN_UNUSED ALWAYS_INLINE AllocateObjectUninitializedWithTrailingBuffer(size_t trailingBufferSize)
    {
        static_assert(is_power_of_2(alignof(T)));
        size_t allocSize = sizeof(T) + trailingBufferSize;
        return reinterpret_cast<T*>(AllocateWithAlignment(alignof(T), allocSize));
    }

    template<typename T, typename... Args>
    __attribute__((__malloc__)) T* WARN_UNUSED ALWAYS_INLINE AllocateObjectWithTrailingBuffer(size_t trailingBufferSize, Args&&... args)
    {
        T* ptr = AllocateObjectUninitializedWithTrailingBuffer<T>(trailingBufferSize);
        new (static_cast<void*>(ptr)) T(std::forward<Args>(args)...);
        return ptr;
    }

private:
    void AllocMemoryChunk()
    {
        uintptr_t address = g_globalTempArenaAllocMemPool.GetMemoryChunk();
        AppendToList(address);
        // the first 8 bytes of the region is used as linked list
        //
        m_currentAddress = address + 8;
        m_currentAddressEnd = address + x_tempArenaAllocatorPageSize;
    }

    void* WARN_UNUSED AllocLargeMemoryChunk(size_t alignment, size_t size)
    {
        size_t headerSize = 16;
        headerSize = std::max(alignment, headerSize);
        size_t allocate_size = size + headerSize;
        allocate_size = RoundUpToMultipleOf<4096>(allocate_size);

        void* mmapResult = mmap(nullptr, allocate_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
        VM_FAIL_WITH_ERRNO_IF(mmapResult == MAP_FAILED, "Failed to allocate memory of size %llu", static_cast<unsigned long long>(allocate_size));

        if (x_isTestBuild)
        {
            // Clobber contents in test build
            //
            memset(mmapResult, 0xcd, allocate_size);
        }

        uintptr_t* headerPtr = reinterpret_cast<uintptr_t*>(mmapResult);
        headerPtr[0] = m_customSizeListHead;
        headerPtr[1] = allocate_size;
        m_customSizeListHead = reinterpret_cast<uintptr_t>(mmapResult);
        uintptr_t result = reinterpret_cast<uintptr_t>(mmapResult) + headerSize;
        TestAssert(result % alignment == 0);
        return reinterpret_cast<void*>(result);
    }

    void* WARN_UNUSED NO_INLINE __attribute__((__preserve_most__)) AllocateWithAlignmentSlowPath(size_t alignment, size_t size)
    {
        Assert(alignment <= 4096 && is_power_of_2(alignment) && size % alignment == 0);
        if (size > x_tempArenaAllocatorPageSize - 4096)
        {
            // For large allocations that cannot be supported by the memory pool, directly allocate it.
            //
            return AllocLargeMemoryChunk(alignment, size);
        }
        else
        {
            AllocMemoryChunk();
            AlignCurrentAddress(alignment);
            TestAssert(m_currentAddress + size <= m_currentAddressEnd);
            TestAssert(m_currentAddress % alignment == 0);
            uintptr_t result = m_currentAddress;
            m_currentAddress += size;
            TestAssert(m_currentAddress <= m_currentAddressEnd);
            return reinterpret_cast<void*>(result);
        }
    }

    void ALWAYS_INLINE AlignCurrentAddress(size_t alignment)
    {
        Assert(alignment <= 4096 && is_power_of_2(alignment));
        size_t mask = alignment - 1;
        m_currentAddress += mask;
        m_currentAddress &= ~mask;
    }

    void ALWAYS_INLINE AppendToList(uintptr_t address)
    {
        *reinterpret_cast<uintptr_t*>(address) = m_listHead;
        m_listHead = address;
    }

    void NO_INLINE FreeAllMemoryChunks()
    {
        while (m_listHead != 0)
        {
            uintptr_t next = *reinterpret_cast<uintptr_t*>(m_listHead);
            g_globalTempArenaAllocMemPool.FreeMemoryChunk(m_listHead);
            m_listHead = next;
        }
        while (m_customSizeListHead != 0)
        {
            uintptr_t next = reinterpret_cast<uintptr_t*>(m_customSizeListHead)[0];
            size_t size = reinterpret_cast<uintptr_t*>(m_customSizeListHead)[1];
            int ret = munmap(reinterpret_cast<void*>(m_customSizeListHead), size);
            LOG_WARNING_WITH_ERRNO_IF(ret != 0, "Failed to free memory of size %llu", static_cast<unsigned long long>(size));
            m_customSizeListHead = next;
        }
        m_currentAddress = 8;
        m_currentAddressEnd = 0;
    }

    uintptr_t m_listHead;
    uintptr_t m_customSizeListHead;
    uintptr_t m_currentAddress;
    uintptr_t m_currentAddressEnd;
};

static_assert(is_power_of_2(__STDCPP_DEFAULT_NEW_ALIGNMENT__), "std default new alignment is not a power of 2");

inline void* ALWAYS_INLINE operator new(std::size_t count, TempArenaAllocator& taa)
{
    return taa.AllocateWithAlignment(__STDCPP_DEFAULT_NEW_ALIGNMENT__, count);
}

inline void* ALWAYS_INLINE operator new[](std::size_t count, TempArenaAllocator& taa)
{
    return taa.AllocateWithAlignment(__STDCPP_DEFAULT_NEW_ALIGNMENT__, count);
}

inline void* ALWAYS_INLINE operator new(std::size_t count, std::align_val_t al, TempArenaAllocator& taa)
{
    TestAssert(is_power_of_2(static_cast<int>(al)));
    return taa.AllocateWithAlignment(static_cast<size_t>(al), count);
}

inline void* ALWAYS_INLINE operator new[](std::size_t count, std::align_val_t al, TempArenaAllocator& taa)
{
    TestAssert(is_power_of_2(static_cast<int>(al)));
    return taa.AllocateWithAlignment(static_cast<size_t>(al), count);
}

template<class T>
struct StlAdaptorForTempArenaAlloc
{
    typedef T value_type;

    StlAdaptorForTempArenaAlloc(TempArenaAllocator& alloc)
        : m_alloc(alloc)
    { }

    template<class U>
    StlAdaptorForTempArenaAlloc(const StlAdaptorForTempArenaAlloc<U>& other) noexcept
        : m_alloc(other.m_alloc)
    { }

    T* WARN_UNUSED allocate(std::size_t n)
    {
        return m_alloc.AllocateArrayUninitialized<T>(n);
    }

    void deallocate(T* /*p*/, std::size_t /*n*/) noexcept { }

    TempArenaAllocator& m_alloc;
};

template<class T, class U>
bool operator==(const StlAdaptorForTempArenaAlloc<T>& a, const StlAdaptorForTempArenaAlloc<U>& b)
{
    return &a.m_alloc == &b.m_alloc;
}

template<class T, class U>
bool operator!=(const StlAdaptorForTempArenaAlloc<T>& a, const StlAdaptorForTempArenaAlloc<U>& b)
{
    return &a.m_alloc != &b.m_alloc;
}

template<typename T>
TempArenaAllocator::operator StlAdaptorForTempArenaAlloc<T>()
{
    return StlAdaptorForTempArenaAlloc<T>(*this);
}

template<typename T>
TempArenaAllocator::operator const StlAdaptorForTempArenaAlloc<T>() const
{
    return StlAdaptorForTempArenaAlloc<T>(*const_cast<TempArenaAllocator*>(this));
}

template<typename T>
using TempVector = std::vector<T, StlAdaptorForTempArenaAlloc<T>>;

template<typename K, typename Compare = std::less<K>>
using TempSet = std::set<K, Compare, StlAdaptorForTempArenaAlloc<K>>;

template<typename K, typename V, typename Compare = std::less<K>>
using TempMap = std::map<K, V, Compare, StlAdaptorForTempArenaAlloc<std::pair<const K, V>>>;

template<typename K, typename Hash = std::hash<K>, typename KeyEqual = std::equal_to<K>>
using TempUnorderedSet = std::unordered_set<K, Hash, KeyEqual, StlAdaptorForTempArenaAlloc<K>>;

template<typename K, typename V, typename Hash = std::hash<K>, typename KeyEqual = std::equal_to<K>>
using TempUnorderedMap = std::unordered_map<K, V, Hash, KeyEqual, StlAdaptorForTempArenaAlloc<std::pair<const K, V>>>;

template<typename T>
using TempQueue = std::queue<T, std::deque<T, StlAdaptorForTempArenaAlloc<T>>>;

// Calls the object destructor. Does not work for array!
//
template<typename T>
struct ArenaAllocatorDeleterForObjectWithNonTrivialDestructor
{
    static_assert(!std::is_array_v<T>);

    void operator()(T* p)
    {
        p->~T();
    }
};

// The only expected use case of this class is to declare this as a local variable.
//
template<typename T>
using arena_unique_ptr = std::unique_ptr<T, ArenaAllocatorDeleterForObjectWithNonTrivialDestructor<T>>;

// We sometimes use a TempVector<T> as a resizable buffer
// This function increases the capacity of the buffer 'desiredCapacity', but still uses exponential growth,
// so multiple calls to this function still always results in linear total allocation
//
// Note that this function does not change the size of the buffer, only the capacity
//
template<typename T, typename TAlloc>
void ALWAYS_INLINE ReserveCapacityForVectorWithExponentialGrowth(std::vector<T, TAlloc>& buffer, size_t desiredCapacity)
{
    size_t oldCapacity = buffer.capacity();
    if (desiredCapacity > oldCapacity)
    {
        // Allocate more memory with exponential growth
        //
        size_t newCapacity = std::max(oldCapacity * 3 / 2, desiredCapacity);
        buffer.reserve(newCapacity);
    }
    Assert(buffer.capacity() >= desiredCapacity);
}

// Make the size of the dynamic buffer to at least 'desiredSize', while using exponential growth to grow the capacity of the buffer
//
// if 'desiredSize' is smaller than the current size, nothing happens
//
// Note that this is required, since the clear() + resize() pattern for std::vector will by default result in always allocating
// the exact size requested by 'resize', resulting in O(n^2) total allocation instead of O(n).
//
// As an example, the following program:
//
//     std::vector<int> a;
//     for (size_t i = 0; i < 1000; i++)
//     {
//         a.resize(i);
//         printf("size = %llu, capacity = %llu\n", a.size(), a.capacity());
//         a.clear();
//         printf("size = %llu, capacity = %llu\n", a.size(), a.capacity());
//     }
//
// will output:
//     ....
//     size = 600, capacity = 600
//     size = 0, capacity = 600
//     size = 601, capacity = 601
//     size = 0, capacity = 601
//     size = 602, capacity = 602
//     size = 0, capacity = 602
//     ....
//
template<typename T, typename TAlloc, typename U>
void ALWAYS_INLINE GrowVectorToAtLeast(std::vector<T, TAlloc>& buffer, size_t desiredSize, U&& initValue)
{
    if (desiredSize > buffer.size())
    {
        ReserveCapacityForVectorWithExponentialGrowth(buffer, desiredSize);
        buffer.resize(desiredSize, std::forward<U>(initValue));
    }
    Assert(buffer.size() >= desiredSize);
}

// Resizes the buffer to the given size, using exponential growth to grow the capacity of the buffer
//
template<typename T, typename TAlloc, typename U>
void ALWAYS_INLINE ResizeVectorTo(std::vector<T, TAlloc>& buffer, size_t desiredSize, U&& initValue)
{
    ReserveCapacityForVectorWithExponentialGrowth(buffer, desiredSize);
    buffer.resize(desiredSize, std::forward<U>(initValue));
    Assert(buffer.size() == desiredSize);
}
