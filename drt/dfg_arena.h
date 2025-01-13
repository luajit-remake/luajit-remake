#pragma once

#include "common.h"
#include "constexpr_power_helper.h"
#include "misc_math_helper.h"
#include "misc_type_helper.h"
#include "mmap_utils.h"

namespace dfg
{

// All the temporary data structures in DFG resides in an arena of 2GB
// This allows us to represent all the pointers using uint32
//
template<typename T>
struct ArenaPtr
{
    ArenaPtr() = default;
    ArenaPtr(std::nullptr_t) : m_value(0) { }
    ArenaPtr(T* value);
    explicit ArenaPtr(uint32_t value) : m_value(value) { }

    operator T*() const;

    bool IsNull() const { return m_value == 0; }

    bool operator==(std::nullptr_t) const { return IsNull(); }
    bool operator==(const ArenaPtr<T>& other) const { return m_value == other.m_value; }

    bool operator==(T* other) const
    {
        if (other == nullptr)
        {
            return IsNull();
        }
        else
        {
            T* val = *this;
            return val == other;
        }
    }

    uint32_t m_value;
};
static_assert(sizeof(ArenaPtr<void>) == 4);

// The 4GB-aligned 2GB arena
// This struct always resides at the base address of the arena
//
class Arena
{
    MAKE_NONCOPYABLE(Arena);
    MAKE_NONMOVABLE(Arena);

public:
    bool WARN_UNUSED IsValidPtr(void* ptr)
    {
        uintptr_t ptrv = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t base = reinterpret_cast<uintptr_t>(this);
        Assert(static_cast<uint32_t>(base) == 0);
        return base + sizeof(Arena) <= ptrv && ptrv < base + 0x80000000ULL;
    }

    template<typename T>
    T* WARN_UNUSED GetPtr(ArenaPtr<T> ptr)
    {
        Assert(!ptr.IsNull());
        T* res = reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(this) + ptr.m_value);
        Assert(IsValidPtr(res));
        return res;
    }

    template<typename T>
    ArenaPtr<T> WARN_UNUSED GetArenaPtr(T* ptr)
    {
        Assert(IsValidPtr(ptr));
        ArenaPtr<T> res { static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ptr)) };
        Assert(GetPtr(res) == ptr);
        return res;
    }

    template<typename T>
    __attribute__((__malloc__)) T* WARN_UNUSED ALWAYS_INLINE AllocateArrayUninitialized(size_t num)
    {
        static_assert(is_power_of_2(alignof(T)) && sizeof(T) % alignof(T) == 0);
        size_t allocSize = sizeof(T) * num;
        allocSize = RoundUpToPO2Alignment(allocSize, x_minimum_alignment);
        Assert(allocSize % x_minimum_alignment == 0);
        return reinterpret_cast<T*>(AllocateWithAlignment<alignof(T)>(allocSize));
    }

    // This assumes that the constructor will never throw
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

    template<typename T>
    __attribute__((__malloc__)) T* WARN_UNUSED ALWAYS_INLINE AllocateObjectUninitialized()
    {
        static_assert(is_power_of_2(alignof(T)) && sizeof(T) % alignof(T) == 0);
        return AllocateArrayUninitialized<T>(1 /*num*/);
    }

    template<typename T, typename... Args>
    __attribute__((__malloc__)) T* WARN_UNUSED ALWAYS_INLINE AllocateObject(Args&&... args)
    {
        T* ptr = AllocateObjectUninitialized<T>();
        new (static_cast<void*>(ptr)) T(std::forward<Args>(args)...);
        return ptr;
    }

    template<typename T>
    __attribute__((__malloc__)) T* WARN_UNUSED ALWAYS_INLINE AllocateObjectUninitializedWithTrailingBuffer(size_t trailingBufferSize)
    {
        static_assert(is_power_of_2(alignof(T)));
        size_t allocSize = sizeof(T) + trailingBufferSize;
        allocSize = RoundUpToPO2Alignment(allocSize, x_minimum_alignment /*alignment*/);
        return reinterpret_cast<T*>(AllocateWithAlignment<alignof(T)>(allocSize));
    }

    template<typename T, typename... Args>
    __attribute__((__malloc__)) T* WARN_UNUSED ALWAYS_INLINE AllocateObjectWithTrailingBuffer(size_t trailingBufferSize, Args&&... args)
    {
        T* ptr = AllocateObjectUninitializedWithTrailingBuffer<T>(trailingBufferSize);
        new (static_cast<void*>(ptr)) T(std::forward<Args>(args)...);
        return ptr;
    }

    __attribute__((__malloc__)) uint8_t* WARN_UNUSED ALWAYS_INLINE AllocateUninitializedMemoryWithAlignment(size_t size, size_t alignment)
    {
        Assert(is_power_of_2(alignment));
        size = RoundUpToPO2Alignment(size, x_minimum_alignment);
        Assert(m_curPtr % x_minimum_alignment == 0);
        Assert(size < (1ULL << 31));
        m_curPtr = RoundUpToPO2Alignment(m_curPtr, alignment);
        Assert(m_curPtr % x_minimum_alignment == 0 && m_curPtr % alignment == 0);
        void* res = AllocateImpl(size);
        Assert(reinterpret_cast<uintptr_t>(res) % alignment == 0);
        return reinterpret_cast<uint8_t*>(res);
    }

    // Free everything in the arena
    //
    void Reset()
    {
        ResetImpl(true /*freeMemoryToOS*/);
    }

    static Arena* WARN_UNUSED Create()
    {
        void* base = do_mmap_with_custom_alignment(1ULL << 32 /*alignment*/, 1ULL << 31 /*length*/, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE);
        size_t allocSize = RoundUpToPO2Alignment(sizeof(Arena), 4096);
        void* tmp = mmap(
            base,
            allocSize,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_FIXED,
            -1, 0);
        VM_FAIL_WITH_ERRNO_IF(tmp == MAP_FAILED, "Failed to allocate memory of size %llu", static_cast<unsigned long long>(allocSize));
        Assert(tmp == base);
        Arena* res = reinterpret_cast<Arena*>(base);
        new (static_cast<void*>(res)) Arena();
        return res;
    }

private:
    Arena()
    {
        ResetImpl(false /*freeMemoryToOS*/);
    }

    static constexpr size_t x_minimum_alignment = 8;
    static constexpr size_t x_allocation_chunk_size = x_isDebugBuild ? 4096 : 131072;

    template<size_t alignment>
    void* WARN_UNUSED ALWAYS_INLINE AllocateWithAlignment(size_t size)
    {
        static_assert(is_power_of_2(alignment));
        Assert(size % x_minimum_alignment == 0);
        Assert(m_curPtr % x_minimum_alignment == 0);
        Assert(size < (1ULL << 31));
        if constexpr(alignment > x_minimum_alignment)
        {
            m_curPtr = RoundUpToPO2Alignment(m_curPtr, alignment);
        }
        Assert(m_curPtr % x_minimum_alignment == 0 && m_curPtr % alignment == 0);
        void* res = AllocateImpl(size);
        Assert(reinterpret_cast<uintptr_t>(res) % alignment == 0);
        return res;
    }

    void* WARN_UNUSED ALWAYS_INLINE AllocateImpl(size_t size)
    {
        size_t res = m_curPtr;
        m_curPtr += size;
        if (unlikely(m_curPtr > m_boundaryPtr))
        {
            Grow();
        }
        return reinterpret_cast<void*>(res);
    }

    uintptr_t WARN_UNUSED ArenaEndAddr()
    {
        return reinterpret_cast<uintptr_t>(this) + (static_cast<uint64_t>(1) << 31);
    }

    void NO_INLINE __attribute__((__preserve_most__)) Grow()
    {
        Assert(m_curPtr > m_boundaryPtr);
        Assert(m_boundaryPtr % 4096 == 0);
        size_t sizeToAllocate = m_curPtr - m_boundaryPtr;
        sizeToAllocate = RoundUpToPO2Alignment(sizeToAllocate, x_allocation_chunk_size);
        VM_FAIL_IF(m_boundaryPtr + sizeToAllocate > ArenaEndAddr(), "DFG arena overflowed 2GB limit!");
        void* r = mmap(
            reinterpret_cast<void*>(m_boundaryPtr),
            sizeToAllocate,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_FIXED,
            -1, 0);
        VM_FAIL_WITH_ERRNO_IF(r == MAP_FAILED, "Failed to allocate memory of size %llu", static_cast<unsigned long long>(sizeToAllocate));
        Assert(r == reinterpret_cast<void*>(m_boundaryPtr));
        m_boundaryPtr += sizeToAllocate;
        Assert(m_curPtr <= m_boundaryPtr);
        Assert(m_boundaryPtr % 4096 == 0);
    }

    void NO_INLINE ResetImpl(bool freeMemoryToOS)
    {
        uintptr_t base = reinterpret_cast<uintptr_t>(this);
        Assert(static_cast<uint32_t>(base) == 0);
        size_t oldBoundaryPtr = m_boundaryPtr;
        m_curPtr = base + RoundUpToPO2Alignment(sizeof(Arena), x_minimum_alignment);
        m_boundaryPtr = base + RoundUpToPO2Alignment(sizeof(Arena), 4096);
        Assert(m_curPtr <= m_boundaryPtr);
        if (freeMemoryToOS && oldBoundaryPtr > m_boundaryPtr)
        {
            void* ptrVoid = mmap(
                reinterpret_cast<void*>(m_boundaryPtr),
                ArenaEndAddr() - m_boundaryPtr,
                PROT_NONE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED,
                -1, 0);
            LOG_WARNING_WITH_ERRNO_IF(ptrVoid == MAP_FAILED, "Failed to deallocate DFG arena");
            Assert(ptrVoid == reinterpret_cast<void*>(m_boundaryPtr));
        }
    }

    static size_t WARN_UNUSED ALWAYS_INLINE RoundUpToPO2Alignment(size_t value, size_t alignment)
    {
        Assert(is_power_of_2(alignment));
        value += alignment - 1;
        size_t mask = ~(alignment - 1);
        return value & mask;
    }

    size_t m_curPtr;
    size_t m_boundaryPtr;
};

inline Arena* g_arena;

// g_arena is not a constant, but never change after program initialization
// so we use attribute 'const' (LLVM attribute readnone) to mark that the return value of
// this function will never change, allowing compiler to do CSE appropriately
//
inline Arena* ALWAYS_INLINE WARN_UNUSED __attribute__((__const__)) DfgAlloc()
{
    return g_arena;
}

template<typename T>
ArenaPtr<T>::ArenaPtr(T* value)
    : ArenaPtr<T>(DfgAlloc()->GetArenaPtr(value))
{ }

template<typename T>
ArenaPtr<T>::operator T*() const
{
    return DfgAlloc()->GetPtr(*this);
}

// Adaptor class for STL allocator
//
template<class T>
struct StlAdaptorForDfgArenaAlloc
{
    typedef T value_type;

    StlAdaptorForDfgArenaAlloc() = default;

    template<class U>
    constexpr StlAdaptorForDfgArenaAlloc(const StlAdaptorForDfgArenaAlloc<U>&) noexcept { }

    T* WARN_UNUSED allocate(std::size_t n)
    {
        return DfgAlloc()->AllocateArrayUninitialized<T>(n);
    }

    void deallocate(T* /*p*/, std::size_t /*n*/) noexcept { }
};

template<class T, class U>
bool operator==(const StlAdaptorForDfgArenaAlloc<T>&, const StlAdaptorForDfgArenaAlloc<U>&) { return true; }

template<class T, class U>
bool operator!=(const StlAdaptorForDfgArenaAlloc<T>&, const StlAdaptorForDfgArenaAlloc<U>&) { return false; }

template<typename T>
using DVector = std::vector<T, StlAdaptorForDfgArenaAlloc<T>>;

template<typename K, typename Compare = std::less<K>>
using DSet = std::set<K, Compare, StlAdaptorForDfgArenaAlloc<K>>;

template<typename K, typename V, typename Compare = std::less<K>>
using DMap = std::map<K, V, Compare, StlAdaptorForDfgArenaAlloc<std::pair<const K, V>>>;

template<typename K, typename Hash = std::hash<K>, typename KeyEqual = std::equal_to<K>>
using DUnorderedSet = std::unordered_set<K, Hash, KeyEqual, StlAdaptorForDfgArenaAlloc<K>>;

template<typename K, typename V, typename Hash = std::hash<K>, typename KeyEqual = std::equal_to<K>>
using DUnorderedMap = std::unordered_map<K, V, Hash, KeyEqual, StlAdaptorForDfgArenaAlloc<std::pair<const K, V>>>;

template<typename T>
using DQueue = std::queue<T, std::deque<T, StlAdaptorForDfgArenaAlloc<T>>>;

}   // namespace dfg

void InitializeDfgAllocationArenaIfNeeded();
