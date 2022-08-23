#pragma once

#include "memory_ptr.h"
#include "vm_string.h"

enum ThreadKind : uint8_t
{
    ExecutionThread,
    CompilerThread,
    GCThread
};

inline thread_local ThreadKind t_threadKind = ExecutionThread;

inline bool IsCompilerThread() { return t_threadKind == CompilerThread; }
inline bool IsExecutionThread() { return t_threadKind == ExecutionThread; }
inline bool IsGCThread() { return t_threadKind == GCThread; }

#define METAMETHOD_NAME_LIST            \
    /* Enum Name,    String Name */     \
    (Call,           __call)            \
  , (Add,            __add)             \
  , (Sub,            __sub)             \
  , (Mul,            __mul)             \
  , (Div,            __div)             \
  , (Mod,            __mod)             \
  , (Pow,            __pow)             \
  , (Unm,            __unm)             \
  , (Concat,         __concat)          \
  , (Len,            __len)             \
  , (Eq,             __eq)              \
  , (Lt,             __lt)              \
  , (Le,             __le)              \
  , (Index,          __index)           \
  , (NewIndex,       __newindex)        \
  , (ProtectedMt,    __metatable)

enum class LuaMetamethodKind
{
#define macro(e) PP_TUPLE_GET_1(e),
    PP_FOR_EACH(macro, METAMETHOD_NAME_LIST)
#undef macro
};

#define macro(e) +1
constexpr size_t x_totalLuaMetamethodKind = 0 PP_FOR_EACH(macro, METAMETHOD_NAME_LIST);
#undef macro

constexpr const char* x_luaMetatableStringName[x_totalLuaMetamethodKind] = {
#define macro(e) PP_STRINGIFY(PP_TUPLE_GET_2(e)),
    PP_FOR_EACH(macro, METAMETHOD_NAME_LIST)
#undef macro
};

using LuaMetamethodBitVectorT = std::conditional_t<x_totalLuaMetamethodKind <= 8, uint8_t,
                                std::conditional_t<x_totalLuaMetamethodKind <= 16, uint16_t,
                                std::conditional_t<x_totalLuaMetamethodKind <= 32, uint32_t,
                                std::conditional_t<x_totalLuaMetamethodKind <= 64, uint64_t,
                                void /*fire static_assert below*/>>>>;
static_assert(!std::is_same_v<LuaMetamethodBitVectorT, void>);

constexpr LuaMetamethodBitVectorT x_luaMetamethodBitVectorFullMask = static_cast<LuaMetamethodBitVectorT>(static_cast<uint64_t>(-1) >> (64 - x_totalLuaMetamethodKind));

// This corresponds to the m_high field in corresponding HeapString
//
constexpr std::array<uint16_t, x_totalLuaMetamethodKind> x_luaMetamethodHashes = {
#define macro(e) constexpr_xxh3::XXH3_64bits_const( std::string_view { PP_STRINGIFY(PP_TUPLE_GET_2(e)) } ) >> 48,
        PP_FOR_EACH(macro, METAMETHOD_NAME_LIST)
#undef macro
};

constexpr std::array<uint8_t, 64> x_luaMetamethodNamesSimpleHashTable = []() {
    for (size_t i = 0; i < x_totalLuaMetamethodKind; i++)
    {
        for (size_t j = i + 1; j < x_totalLuaMetamethodKind; j++)
        {
            assert(x_luaMetamethodHashes[i] != x_luaMetamethodHashes[j]);
        }
    }

    constexpr size_t htSize = 64;
    std::array<uint8_t, htSize> result;
    for (size_t i = 0; i < htSize; i++) { result[i] = 255; }
    for (size_t i = 0; i < x_totalLuaMetamethodKind; i++)
    {
        uint16_t slot = static_cast<uint16_t>((x_luaMetamethodHashes[i] >> 8) % htSize);
        while (result[slot] != 255)
        {
            slot = (slot + 1) % htSize;
        }
        result[slot] = static_cast<uint8_t>(i);
    }
    return result;
}();

// Return -1 if not found
//
constexpr int WARN_UNUSED GetLuaMetamethodOrdinalFromStringHash(uint16_t hashHigh)
{
    constexpr size_t htSize = std::size(x_luaMetamethodNamesSimpleHashTable);
    uint16_t slot = static_cast<uint16_t>((hashHigh >> 8) % htSize);
    while (true)
    {
        uint8_t entry = x_luaMetamethodNamesSimpleHashTable[slot];
        if (entry == 255) { return -1; }
        if (x_luaMetamethodHashes[entry] == hashHigh) { return entry; }
        slot = (slot + 1) % htSize;
    }
}

// Normally for each class type, we use one free list for compiler thread and one free list for execution thread.
// However, some classes may be allocated on the compiler thread but freed on the execution thread.
// If that is the case, we should use a lockfree freelist to make sure the freelist is effective
// (since otherwise a lot of freed objects would be on the execution thread free list but the compiler thread cannot grab them).
//
#define SPDS_ALLOCATABLE_CLASS_LIST             \
  /* C++ class name   Use lockfree freelist */  \
    (WatchpointSet,             false)

#define SPDS_CPP_NAME(e) PP_TUPLE_GET_1(e)
#define SPDS_USE_LOCKFREE_FREELIST(e) PP_TUPLE_GET_2(e)

#define macro(e) class SPDS_CPP_NAME(e);
PP_FOR_EACH(macro, SPDS_ALLOCATABLE_CLASS_LIST)
#undef macro

#define macro(e) + ((SPDS_USE_LOCKFREE_FREELIST(e)) ? 1 : 0)
constexpr size_t x_numSpdsAllocatableClassUsingLfFreelist = PP_FOR_EACH(macro, SPDS_ALLOCATABLE_CLASS_LIST);
#undef macro

#define macro(e) + ((SPDS_USE_LOCKFREE_FREELIST(e)) ? 0 : 1)
constexpr size_t x_numSpdsAllocatableClassNotUsingLfFreelist = PP_FOR_EACH(macro, SPDS_ALLOCATABLE_CLASS_LIST);
#undef macro

constexpr size_t x_numSpdsAllocatableClasses = x_numSpdsAllocatableClassUsingLfFreelist + x_numSpdsAllocatableClassNotUsingLfFreelist;

namespace internal
{

template<typename T> struct is_spds_allocatable_class : std::false_type { };

#define macro(e) template<> struct is_spds_allocatable_class<SPDS_CPP_NAME(e)> : std::true_type { };
PP_FOR_EACH(macro, SPDS_ALLOCATABLE_CLASS_LIST)
#undef macro

template<typename T> struct spds_use_lockfree_freelist { };

#define macro(e) template<> struct spds_use_lockfree_freelist<SPDS_CPP_NAME(e)> : std::integral_constant<bool, SPDS_USE_LOCKFREE_FREELIST(e)> { };
PP_FOR_EACH(macro, SPDS_ALLOCATABLE_CLASS_LIST)
#undef macro

constexpr bool x_spds_class_use_lf_freelist_by_enumid[] = {
#define macro(e) SPDS_USE_LOCKFREE_FREELIST(e),
PP_FOR_EACH(macro, SPDS_ALLOCATABLE_CLASS_LIST)
#undef macro
    false
};

// <useLFFreelist, ord> uniquely identifies the class
//
template<typename T> struct spds_class_ord { };

#define macro(ord, e)                                                                                           \
    template<> struct spds_class_ord<SPDS_CPP_NAME(e)> {                                                        \
        constexpr static uint32_t GetOrd() {                                                                    \
            uint32_t ret = 0;                                                                                   \
            for (uint32_t i = 0; i < ord; i++) {                                                                \
                if (x_spds_class_use_lf_freelist_by_enumid[i] == x_spds_class_use_lf_freelist_by_enumid[ord]) { \
                    ret++;                                                                                      \
                }                                                                                               \
            }                                                                                                   \
            return ret;                                                                                         \
        }                                                                                                       \
    };
PP_FOR_EACH_UNPACK_TUPLE(macro, PP_ZIP_TWO_LISTS((PP_NATURAL_NUMBERS_LIST), (SPDS_ALLOCATABLE_CLASS_LIST)))
#undef macro

}   // namespace internal

template<typename T>
constexpr bool x_isSpdsAllocatableClass = internal::is_spds_allocatable_class<T>::value;

template<typename T>
constexpr bool x_spdsAllocatableClassUseLfFreelist = internal::spds_use_lockfree_freelist<T>::value;

// Those classes that use LfFreelist and those do not have a different set of ordinals
//
template<typename T>
constexpr uint32_t x_spdsAllocatableClassOrdinal = internal::spds_class_ord<T>::GetOrd();

constexpr size_t x_spdsAllocationPageSize = 4096;
static_assert(is_power_of_2(x_spdsAllocationPageSize));

// If 'isTempAlloc' is true, we give the memory pages back to VM when the struct is destructed
// Singlethread use only.
// Currently chunk size is always 4KB, so allocate small objects only.
//
template<typename Host, bool isTempAlloc>
class SpdsAllocImpl
{
    MAKE_NONCOPYABLE(SpdsAllocImpl);
    MAKE_NONMOVABLE(SpdsAllocImpl);

public:
    SpdsAllocImpl(Host* host)
        : m_host(host)
        , m_curChunk(0)
        , m_lastChunkInTheChain(0)
    { }

    SpdsAllocImpl()
        : SpdsAllocImpl(nullptr)
    { }

    ~SpdsAllocImpl()
    {
        if constexpr(isTempAlloc)
        {
            ReturnMemory();
        }
    }

    void SetHost(Host* host)
    {
        m_host = host;
    }

    // If collectedByFreeList == true, we allocate at least 4 bytes to make sure it can hold a free list pointer
    //
    template<typename T, bool collectedByFreeList = false>
    SpdsPtr<T> ALWAYS_INLINE WARN_UNUSED Alloc()
    {
        static_assert(sizeof(T) <= x_spdsAllocationPageSize - RoundUpToMultipleOf<alignof(T)>(isTempAlloc ? 4ULL : 0ULL));
        constexpr uint32_t objectSize = static_cast<uint32_t>(sizeof(T));
        constexpr uint32_t allocationSize = collectedByFreeList ? std::max(objectSize, 4U) : objectSize;
        return SpdsPtr<T> { AllocMemory<alignof(T)>(allocationSize) };
    }

private:
    template<size_t alignment>
    int32_t ALWAYS_INLINE WARN_UNUSED AllocMemory(uint32_t length)
    {
        static_assert(is_power_of_2(alignment) && alignment <= 32);
        assert(m_curChunk <= 0 && length > 0 && length % alignment == 0);

        m_curChunk &= ~static_cast<int>(alignment - 1);
        assert(m_curChunk % alignment == 0);
        if (likely((static_cast<uint32_t>(m_curChunk) & (x_spdsAllocationPageSize - 1)) >= length))
        {
            m_curChunk -= length;
            return m_curChunk;
        }
        else
        {
            int32_t oldChunk = (m_curChunk & (~static_cast<int>(x_spdsAllocationPageSize - 1))) + static_cast<int>(x_spdsAllocationPageSize);
            m_curChunk = m_host->SpdsAllocatePage();
            assert(m_curChunk <= 0);
            if (oldChunk > 0)
            {
                m_lastChunkInTheChain = m_curChunk;
            }
            assert(m_curChunk % x_spdsAllocationPageSize == 0);
            if constexpr(isTempAlloc)
            {
                m_curChunk -= 4;
                *reinterpret_cast<int32_t*>(reinterpret_cast<intptr_t>(m_host) + m_curChunk) = oldChunk;
                m_curChunk &= ~static_cast<int>(alignment - 1);
            }
            m_curChunk -= length;
            return m_curChunk;
        }
    }

    void ReturnMemory()
    {
        int32_t chunk = (m_curChunk & (~static_cast<int>(x_spdsAllocationPageSize - 1))) + static_cast<int>(x_spdsAllocationPageSize);
        if (chunk != static_cast<int>(x_spdsAllocationPageSize))
        {
            m_host->SpdsPutAllocatedPagesToFreeList(chunk, m_lastChunkInTheChain);
        }
    }

    Host* m_host;
    int32_t m_curChunk;
    int32_t m_lastChunkInTheChain;
};

namespace internal
{

constexpr uint32_t GetLeastFitCellSizeInSlots(uint32_t slotToFit)
{
    // TODO: we haven't implemented the segregated allocator yet, so this function is dummy
    //
    return RoundUpToPowerOfTwo(slotToFit);
}

constexpr uint32_t x_maxInlineCapacity = 253;

// If we want the inline storage to hold at least 'elementToHold' elements, the optimal capacity is not 'elementToHold',
// as having exactly 'elementToHold' capacity can cause unnecessary internal fragmentation in our segregated allocator.
// This function computes the optimal capacity.
//
constexpr uint8_t ComputeOptimalInlineStorageCapacity(uint8_t elementToHold)
{
    assert(elementToHold <= x_maxInlineCapacity);
    // The TableObject has a header of 2 slots (16 bytes)
    //
    uint32_t minimalSlotsNeeded = elementToHold + 2;
    uint32_t r32 = GetLeastFitCellSizeInSlots(minimalSlotsNeeded);
    r32 = std::min(r32, x_maxInlineCapacity);
    return static_cast<uint8_t>(r32);
}

constexpr std::array<uint8_t, x_maxInlineCapacity + 1> ComputeOptimalInlineStorageCapacityArray()
{
    std::array<uint8_t, x_maxInlineCapacity + 1> r;
    for (uint8_t i = 0; i <= x_maxInlineCapacity; i++)
    {
        r[i] = ComputeOptimalInlineStorageCapacity(i);
        AssertImp(i > 0, r[i] >= r[i-1]);
    }
    return r;
}

constexpr std::array<uint8_t, x_maxInlineCapacity + 1> x_optimalInlineCapacityArray = ComputeOptimalInlineStorageCapacityArray();

constexpr std::array<uint8_t, x_maxInlineCapacity + 1> ComputeInlineStorageCapacitySteppingArray()
{
    std::array<uint8_t, 256> v;
    for (size_t i = 0; i < 256; i++) { v[i] = 0; }
    for (size_t i = 0; i <= x_maxInlineCapacity; i++) { v[x_optimalInlineCapacityArray[i]] = 1; }
    for (size_t i = 1; i < 256; i++) { v[i] += v[i-1]; }

    std::array<uint8_t, x_maxInlineCapacity + 1> r;
    for (size_t i = 0; i <= x_maxInlineCapacity; i++) { r[i] = v[x_optimalInlineCapacityArray[i]] - 1; }
    return r;
}

constexpr std::array<uint8_t, x_maxInlineCapacity + 1> x_optimalInlineCapacitySteppingArray = ComputeInlineStorageCapacitySteppingArray();

}   // namespace internal

constexpr size_t x_numInlineCapacitySteppings = internal::x_optimalInlineCapacitySteppingArray[internal::x_maxInlineCapacity] + 1;

namespace internal
{

constexpr std::array<uint8_t, x_numInlineCapacitySteppings> ComputeInlineStorageSizeForSteppingArray()
{
    std::array<uint8_t, x_numInlineCapacitySteppings> r;
    for (size_t i = 0; i <= x_maxInlineCapacity; i++) { r[x_optimalInlineCapacitySteppingArray[i]] = x_optimalInlineCapacityArray[i]; }
    return r;
}

constexpr std::array<uint8_t, x_numInlineCapacitySteppings> x_inlineStorageSizeForSteppingArray = ComputeInlineStorageSizeForSteppingArray();

}   // namespace internal

// [ 12GB user heap ] [ 2GB padding ] [ 2GB short-pointer data structures ] [ 2GB system heap ]
//                                                                          ^
//     userheap                                   SPDS region     32GB aligned baseptr   systemheap
//
template<typename CRTP>
class VMMemoryManager
{
public:
    static constexpr size_t x_pageSize = 4096;
    static constexpr size_t x_vmLayoutLength = 18ULL << 30;
    // The start address of the VM is always at 16GB % 32GB, this makes sure the VM base is aligned at 32GB
    //
    static constexpr size_t x_vmLayoutAlignment = 32ULL << 30;
    static constexpr size_t x_vmLayoutAlignmentOffset = 16ULL << 30;
    static constexpr size_t x_vmBaseOffset = 16ULL << 30;
    static constexpr size_t x_vmUserHeapSize = 12ULL << 30;

    static_assert((1ULL << x_vmBasePtrLog2Alignment) == x_vmLayoutAlignment, "the constants must match");

    template<typename... Args>
    static CRTP* WARN_UNUSED Create(Args&&... args)
    {
        constexpr size_t x_mmapLength = x_vmLayoutLength + x_vmLayoutAlignment * 2;
        void* ptrVoid = mmap(nullptr, x_mmapLength, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        CHECK_LOG_ERROR_WITH_ERRNO(ptrVoid != MAP_FAILED, "Failed to reserve VM address range, please make sure overcommit is allowed");

        // cut out the desired properly-aligned space, and unmap the remaining
        //
        {
            uintptr_t ptr = reinterpret_cast<uintptr_t>(ptrVoid);
            uintptr_t alignedPtr = RoundUpToMultipleOf<x_vmLayoutAlignment>(ptr);
            assert(alignedPtr >= ptr && alignedPtr % x_vmLayoutAlignment == 0 && alignedPtr - ptr < x_vmLayoutAlignment);

            uintptr_t vmRangeStart = alignedPtr + x_vmLayoutAlignmentOffset;

            // If any unmap failed, log a warning, but continue execution.
            //
            if (vmRangeStart > ptr)
            {
                int r = munmap(reinterpret_cast<void*>(ptr), vmRangeStart - ptr);
                LOG_WARNING_WITH_ERRNO_IF(r != 0, "Failed to unmap unnecessary VM address range");
            }

            {
                uintptr_t vmRangeEnd = vmRangeStart + x_vmLayoutLength;
                uintptr_t originalMapEnd = ptr + x_mmapLength;
                assert(vmRangeEnd <= originalMapEnd);
                if (originalMapEnd > vmRangeEnd)
                {
                    int r = munmap(reinterpret_cast<void*>(vmRangeEnd), originalMapEnd - vmRangeEnd);
                    LOG_WARNING_WITH_ERRNO_IF(r != 0, "Failed to unmap unnecessary VM address range");
                }
            }

            ptrVoid = reinterpret_cast<void*>(vmRangeStart);
        }

        assert(reinterpret_cast<uintptr_t>(ptrVoid) % x_vmLayoutAlignment == x_vmLayoutAlignmentOffset);

        bool success = false;
        void* unmapPtrOnFailure = ptrVoid;
        size_t unmapLengthOnFailure = x_vmLayoutLength;

        Auto(
            if (!success)
            {
                int r = munmap(unmapPtrOnFailure, unmapLengthOnFailure);
                LOG_WARNING_WITH_ERRNO_IF(r != 0, "Cannot unmap VM on failure cleanup");
            }
        );

        // Map memory and initialize the VM struct
        //
        void* vmVoid = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptrVoid) + x_vmBaseOffset);
        assert(reinterpret_cast<uintptr_t>(vmVoid) % x_vmLayoutAlignment == 0);
        constexpr size_t sizeToMap = RoundUpToMultipleOf<x_pageSize>(sizeof(CRTP));
        {
            void* r = mmap(vmVoid, sizeToMap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_FIXED, -1, 0);
            CHECK_LOG_ERROR_WITH_ERRNO(r != MAP_FAILED, "Failed to allocate VM struct");
            TestAssert(vmVoid == r);
        }

        CRTP* vm = new (vmVoid) CRTP();
        assert(vm == vmVoid);
        Auto(
            if (!success)
            {
                vm->~CRTP();
            }
        );

        CHECK_LOG_ERROR(vm->Initialize(std::forward<Args>(args)...));
        Auto(
            if (!success)
            {
                vm->Cleanup();
            }
        );

        success = true;
        return vm;
    }

    void Destroy()
    {
        CRTP* ptr = static_cast<CRTP*>(this);
        ptr->Cleanup();
        ptr->~CRTP();

        void* unmapAddr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptr) - x_vmBaseOffset);
        int r = munmap(unmapAddr, x_vmLayoutLength);
        LOG_WARNING_WITH_ERRNO_IF(r != 0, "Cannot unmap VM");
    }

    static CRTP* GetActiveVMForCurrentThread()
    {
        return reinterpret_cast<CRTP*>(reinterpret_cast<HeapPtr<CRTP>>(0)->m_self);
    }

    HeapPtrTranslator GetHeapPtrTranslator() const
    {
        return HeapPtrTranslator { VMBaseAddress() };
    }

    void SetUpSegmentationRegister()
    {
        X64_SetSegmentationRegister<X64SegmentationRegisterKind::GS>(VMBaseAddress());
    }

    SpdsAllocImpl<CRTP, true /*isTempAlloc*/> WARN_UNUSED CreateSpdsArenaAlloc()
    {
        return SpdsAllocImpl<CRTP, true /*isTempAlloc*/>(static_cast<CRTP*>(this));
    }

    SpdsAllocImpl<CRTP, false /*isTempAlloc*/>& WARN_UNUSED GetCompilerThreadSpdsAlloc()
    {
        return m_compilerThreadSpdsAlloc;
    }

    SpdsAllocImpl<CRTP, false /*isTempAlloc*/>& WARN_UNUSED GetExecutionThreadSpdsAlloc()
    {
        return m_executionThreadSpdsAlloc;
    }

    SpdsAllocImpl<CRTP, false /*isTempAlloc*/>& WARN_UNUSED GetSpdsAllocForCurrentThread()
    {
        assert(!IsGCThread());
        if (IsCompilerThread())
        {
            return GetCompilerThreadSpdsAlloc();
        }
        else
        {
            return GetExecutionThreadSpdsAlloc();
        }
    }

    // Grab one 4K page from the SPDS region.
    // The page can be given back via SpdsPutAllocatedPagesToFreeList()
    // NOTE: if the page is [begin, end), this returns 'end', not 'begin'! SpdsReturnMemoryFreeList also expects 'end', not 'begin'
    //
    int32_t WARN_UNUSED ALWAYS_INLINE SpdsAllocatePage()
    {
        {
            int32_t out;
            if (SpdsAllocateTryGetFreeListPage(&out))
            {
                return out;
            }
        }
        return SpdsAllocatePageSlowPath();
    }

    void SpdsPutAllocatedPagesToFreeList(int32_t firstPage, int32_t lastPage)
    {
        std::atomic<int32_t>* addr = reinterpret_cast<std::atomic<int32_t>*>(VMBaseAddress() + SignExtendTo<uint64_t>(lastPage) - 4);
        uint64_t taggedValue = m_spdsPageFreeList.load(std::memory_order_relaxed);
        while (true)
        {
            uint32_t tag = BitwiseTruncateTo<uint32_t>(taggedValue >> 32);
            int32_t head = BitwiseTruncateTo<int32_t>(taggedValue);
            addr->store(head, std::memory_order_relaxed);

            tag++;
            uint64_t newTaggedValue = (static_cast<uint64_t>(tag) << 32) | ZeroExtendTo<uint64_t>(firstPage);
            if (m_spdsPageFreeList.compare_exchange_weak(taggedValue /*expected, inout*/, newTaggedValue /*desired*/, std::memory_order_release, std::memory_order_relaxed))
            {
                break;
            }
        }
    }

    // Allocate a chunk of memory from the user heap
    // Only execution thread may do this
    //
    UserHeapPointer<void> WARN_UNUSED AllocFromUserHeap(uint32_t length)
    {
        assert(length > 0 && length % 8 == 0);
        // TODO: we currently do not have GC, so it's only a bump allocator..
        //
        m_userHeapCurPtr -= static_cast<int64_t>(length);
        if (unlikely(m_userHeapCurPtr < m_userHeapPtrLimit))
        {
            BumpUserHeap();
        }
        return UserHeapPointer<void> { reinterpret_cast<HeapPtr<void>>(m_userHeapCurPtr) };
    }

    // Allocate a chunk of memory from the system heap
    // Only execution thread may do this
    //
    SystemHeapPointer<void> WARN_UNUSED AllocFromSystemHeap(uint32_t length)
    {
        assert(length > 0 && length % 8 == 0);
        // TODO: we currently do not have GC, so it's only a bump allocator..
        //
        uint32_t result = m_systemHeapCurPtr;
        VM_FAIL_IF(AddWithOverflowCheck(m_systemHeapCurPtr, length, &m_systemHeapCurPtr),
            "Resource limit exceeded: system heap overflowed 4GB memory limit.");

        if (unlikely(m_systemHeapCurPtr > m_systemHeapPtrLimit))
        {
            BumpSystemHeap();
        }
        return SystemHeapPointer<void> { result };
    }

    // Note: the memory returned matches the alignment and size of T, but is NOT initialized! One must call constructor of T manually.
    //
    template<typename T>
    T* WARN_UNUSED AllocateFromSpdsRegionUninitialized()
    {
        static_assert(x_isSpdsAllocatableClass<T>, "T is not registered as a SPDS allocatable class!");
        static_assert(!x_spdsAllocatableClassUseLfFreelist<T>, "unimplemented yet");
        if constexpr(!x_spdsAllocatableClassUseLfFreelist<T>)
        {
            assert(!IsGCThread());
            SpdsPtr<void>& freelist = IsCompilerThread() ?
                        m_spdsCompilerThreadFreeList[x_spdsAllocatableClassOrdinal<T>] :
                        m_spdsExecutionThreadFreeList[x_spdsAllocatableClassOrdinal<T>];
            if (likely(freelist.m_value != 0))
            {
                HeapPtr<void> result = freelist.AsPtr();
                freelist = TCGet(*reinterpret_cast<HeapPtr<SpdsPtr<void>>>(result));
                return GetHeapPtrTranslator().TranslateToRawPtr(result);
            }
            else
            {
                SpdsPtr<T> result = GetSpdsAllocForCurrentThread().template Alloc<T, true /*collectedByFreeList*/>();
                return GetHeapPtrTranslator().TranslateToRawPtr(result.Get());
            }
        }
        else
        {
            ReleaseAssert(false);
        }
    }

    // Deallocate an object returned by AllocateFromSpdsRegionUninitialized
    // Call destructor and put back to free list
    //
    template<typename T, bool callDestructor = true>
    void DeallocateSpdsRegionObject(T* object)
    {
        static_assert(x_isSpdsAllocatableClass<T>, "T is not registered as a SPDS allocatable class!");
        static_assert(!x_spdsAllocatableClassUseLfFreelist<T>, "unimplemented yet");
        if (callDestructor)
        {
            object->~T();
        }
        if constexpr(!x_spdsAllocatableClassUseLfFreelist<T>)
        {
            assert(!IsGCThread());
            SpdsPtr<void>& freelist = IsCompilerThread() ?
                        m_spdsCompilerThreadFreeList[x_spdsAllocatableClassOrdinal<T>] :
                        m_spdsExecutionThreadFreeList[x_spdsAllocatableClassOrdinal<T>];

            UnalignedStore<int32_t>(object, freelist.m_value);
            freelist = GetHeapPtrTranslator().TranslateToSpdsPtr(object);
        }
        else
        {
            ReleaseAssert(false);
        }
    }

    bool WARN_UNUSED Initialize()
    {
        static_assert(std::is_base_of_v<VMMemoryManager, CRTP>, "wrong use of CRTP pattern");
        // These restrictions might not be necessary, but just to make things safe and simple
        //
        static_assert(!std::is_polymorphic_v<CRTP>, "must be not polymorphic");
        static_assert(offsetof_base_v<VMMemoryManager, CRTP> == 0, "VM must inherit VMMemoryManager as the first inherited class");

        m_self = reinterpret_cast<uintptr_t>(static_cast<CRTP*>(this));

        SetUpSegmentationRegister();

        m_userHeapPtrLimit = -static_cast<int64_t>(x_vmBaseOffset - x_vmUserHeapSize);
        m_userHeapCurPtr = -static_cast<int64_t>(x_vmBaseOffset - x_vmUserHeapSize);

        static_assert(sizeof(CRTP) >= x_minimum_valid_heap_address);
        m_systemHeapPtrLimit = static_cast<uint32_t>(RoundUpToMultipleOf<x_pageSize>(sizeof(CRTP)));
        m_systemHeapCurPtr = sizeof(CRTP);

        m_spdsPageFreeList.store(static_cast<uint64_t>(x_spdsAllocationPageSize));
        m_spdsPageAllocLimit = -static_cast<int32_t>(x_pageSize);

        m_executionThreadSpdsAlloc.SetHost(static_cast<CRTP*>(this));
        m_compilerThreadSpdsAlloc.SetHost(static_cast<CRTP*>(this));

        for (size_t i = 0; i < x_numSpdsAllocatableClassNotUsingLfFreelist; i++)
        {
            m_spdsCompilerThreadFreeList[i] = SpdsPtr<void> { 0 };
        }

        for (size_t i = 0; i < x_numSpdsAllocatableClassNotUsingLfFreelist; i++)
        {
            m_spdsExecutionThreadFreeList[i] = SpdsPtr<void> { 0 };
        }
        return true;
    }

    void Cleanup() { }

private:
    uintptr_t VMBaseAddress() const
    {
        uintptr_t result = reinterpret_cast<uintptr_t>(static_cast<const CRTP*>(this));
        assert(result == m_self);
        return result;
    }

    void NO_INLINE BumpUserHeap()
    {
        assert(m_userHeapCurPtr < m_userHeapPtrLimit);
        VM_FAIL_IF(m_userHeapCurPtr < -static_cast<intptr_t>(x_vmBaseOffset),
            "Resource limit exceeded: user heap overflowed %dGB memory limit.", static_cast<int>(x_vmUserHeapSize >> 30));

        constexpr size_t x_allocationSize = 65536;
        // TODO: consider allocating smaller sizes on the first few allocations
        //
        intptr_t newHeapLimit = m_userHeapCurPtr & (~static_cast<intptr_t>(x_allocationSize - 1));
        assert(newHeapLimit <= m_userHeapCurPtr && newHeapLimit % static_cast<int64_t>(x_pageSize) == 0 && newHeapLimit < m_userHeapPtrLimit);
        size_t lengthToAllocate = static_cast<size_t>(m_userHeapPtrLimit - newHeapLimit);
        assert(lengthToAllocate % x_pageSize == 0);

        uintptr_t allocAddr = VMBaseAddress() + static_cast<uint64_t>(newHeapLimit);
        void* r = mmap(reinterpret_cast<void*>(allocAddr), lengthToAllocate, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_FIXED, -1, 0);
        VM_FAIL_WITH_ERRNO_IF(r == MAP_FAILED,
            "Out of Memory: Allocation of length %llu failed", static_cast<unsigned long long>(lengthToAllocate));
        assert(r == reinterpret_cast<void*>(allocAddr));

        m_userHeapPtrLimit = newHeapLimit;
        assert(m_userHeapPtrLimit <= m_userHeapCurPtr);
        assert(m_userHeapPtrLimit >= -static_cast<intptr_t>(x_vmBaseOffset));
    }

    void NO_INLINE BumpSystemHeap()
    {
        assert(m_systemHeapCurPtr > m_systemHeapPtrLimit);
        constexpr uint32_t x_allocationSize = 65536;

        VM_FAIL_IF(m_systemHeapCurPtr > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) - x_allocationSize,
            "Resource limit exceeded: system heap overflowed 2GB memory limit.");

        // TODO: consider allocating smaller sizes on the first few allocations
        //
        uint32_t newHeapLimit = RoundUpToMultipleOf<x_allocationSize>(m_systemHeapCurPtr);
        assert(newHeapLimit >= m_systemHeapCurPtr && newHeapLimit % static_cast<int64_t>(x_pageSize) == 0 && newHeapLimit > m_systemHeapPtrLimit);

        size_t lengthToAllocate = static_cast<size_t>(newHeapLimit - m_systemHeapPtrLimit);
        assert(lengthToAllocate % x_pageSize == 0);

        uintptr_t allocAddr = VMBaseAddress() + static_cast<uint64_t>(m_systemHeapPtrLimit);
        void* r = mmap(reinterpret_cast<void*>(allocAddr), lengthToAllocate, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_FIXED, -1, 0);
        VM_FAIL_WITH_ERRNO_IF(r == MAP_FAILED,
            "Out of Memory: Allocation of length %llu failed", static_cast<unsigned long long>(lengthToAllocate));
        assert(r == reinterpret_cast<void*>(allocAddr));

        m_systemHeapPtrLimit = newHeapLimit;
        assert(m_systemHeapPtrLimit >= m_systemHeapCurPtr);
    }

    bool WARN_UNUSED SpdsAllocateTryGetFreeListPage(int32_t* out)
    {
        uint64_t taggedValue = m_spdsPageFreeList.load(std::memory_order_acquire);
        while (true)
        {
            int32_t head = BitwiseTruncateTo<int32_t>(taggedValue);
            assert(head % x_spdsAllocationPageSize == 0);
            if (head == x_spdsAllocationPageSize)
            {
                return false;
            }
            uint32_t tag = BitwiseTruncateTo<uint32_t>(taggedValue >> 32);

            std::atomic<int32_t>* addr = reinterpret_cast<std::atomic<int32_t>*>(VMBaseAddress() + SignExtendTo<uint64_t>(head) - 4);
            int32_t newHead = addr->load(std::memory_order_relaxed);
            assert(newHead % x_spdsAllocationPageSize == 0);
            tag++;
            uint64_t newTaggedValue = (static_cast<uint64_t>(tag) << 32) | ZeroExtendTo<uint64_t>(newHead);

            if (m_spdsPageFreeList.compare_exchange_weak(taggedValue /*expected, inout*/, newTaggedValue /*desired*/, std::memory_order_release, std::memory_order_acquire))
            {
                *out = head;
                return true;
            }
        }
    }

    int32_t NO_INLINE WARN_UNUSED SpdsAllocatePageSlowPath()
    {
        while (true)
        {
            if (m_spdsAllocationMutex.try_lock())
            {
                int32_t result = SpdsAllocatePageSlowPathImpl();
                m_spdsAllocationMutex.unlock();
                return result;
            }
            else
            {
                // Someone else has took the lock, we wait until they finish, and retry
                //
                {
                    std::lock_guard<std::mutex> blinkLock(m_spdsAllocationMutex);
                }
                int32_t out;
                if (SpdsAllocateTryGetFreeListPage(&out))
                {
                    return out;
                }
            }
        }
    }

    // Allocate a chunk of memory, return one of the pages, and put the rest into free list
    //
    int32_t WARN_UNUSED SpdsAllocatePageSlowPathImpl()
    {
        constexpr int32_t x_allocationSize = 65536;

        // Compute how much memory we should allocate
        // We allocate 4K, 8K, 16K, 32K first (the highest 4K is not used to prevent all kinds of overflowing issue)
        // After that we allocate 64K each time
        //
        assert(m_spdsPageAllocLimit % x_pageSize == 0 && m_spdsPageAllocLimit % x_spdsAllocationPageSize == 0);
        size_t lengthToAllocate = x_allocationSize;
        if (unlikely(m_spdsPageAllocLimit > -x_allocationSize))
        {
            if (m_spdsPageAllocLimit == 0)
            {
                lengthToAllocate = 4096;
            }
            else
            {
                assert(m_spdsPageAllocLimit < 0);
                lengthToAllocate = static_cast<size_t>(-m_spdsPageAllocLimit);
                assert(lengthToAllocate <= x_allocationSize);
            }
        }
        assert(lengthToAllocate > 0 && lengthToAllocate % x_pageSize == 0 && lengthToAllocate % x_spdsAllocationPageSize == 0);

        VM_FAIL_IF(SubWithOverflowCheck(m_spdsPageAllocLimit, static_cast<int32_t>(lengthToAllocate), &m_spdsPageAllocLimit),
            "Resource limit exceeded: SPDS region overflowed 2GB memory limit.");

        // We have some code that assumes the address std::numeric_limits<int32_t>::min() is not used
        //
        VM_FAIL_IF(m_spdsPageAllocLimit == std::numeric_limits<int32_t>::min(),
            "Resource limit exceeded: SPDS region overflowed 2GB memory limit.");

        // Allocate memory
        //
        uintptr_t allocAddr = VMBaseAddress() + SignExtendTo<uint64_t>(m_spdsPageAllocLimit);
        assert(allocAddr % x_pageSize == 0 && allocAddr % x_spdsAllocationPageSize == 0);
        void* r = mmap(reinterpret_cast<void*>(allocAddr), static_cast<size_t>(lengthToAllocate), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_FIXED, -1, 0);
        VM_FAIL_WITH_ERRNO_IF(r == MAP_FAILED,
            "Out of Memory: Allocation of length %llu failed", static_cast<unsigned long long>(lengthToAllocate));

        assert(r == reinterpret_cast<void*>(allocAddr));

        // The first page is returned to caller
        //
        int32_t result = m_spdsPageAllocLimit + x_spdsAllocationPageSize;

        // Insert the other pages, if any, into free list
        //
        size_t numPages = lengthToAllocate / x_spdsAllocationPageSize;
        if (numPages > 1)
        {
            int32_t cur = result + static_cast<int32_t>(x_spdsAllocationPageSize);
            int32_t firstPage = cur;
            for (size_t i = 1; i < numPages - 1; i++)
            {
                int32_t next = cur + static_cast<int32_t>(x_spdsAllocationPageSize);
                std::atomic<int32_t>* addr = reinterpret_cast<std::atomic<int32_t>*>(VMBaseAddress() + SignExtendTo<uint64_t>(cur) - 4);
                addr->store(next, std::memory_order_relaxed);
                cur = next;
            }

            int32_t lastPage = cur;
            assert(lastPage == m_spdsPageAllocLimit + lengthToAllocate);

            SpdsPutAllocatedPagesToFreeList(firstPage, lastPage);
        }
        return result;
    }

protected:
    VMMemoryManager() { }

    // must be first member, stores the value of static_cast<CRTP*>(this)
    //
    uintptr_t m_self;

    alignas(64) SpdsAllocImpl<CRTP, false /*isTempAlloc*/> m_executionThreadSpdsAlloc;

    // user heap region grows from high address to low address
    // highest physically mapped address of the user heap region (offsets from m_self)
    //
    int64_t m_userHeapPtrLimit;

    // lowest logically used address of the user heap region (offsets from m_self)
    //
    int64_t m_userHeapCurPtr;

    // system heap region grows from low address to high address
    // lowest physically unmapped address of the system heap region (offsets from m_self)
    //
    uint32_t m_systemHeapPtrLimit;

    // lowest logically available address of the system heap region (offsets from m_self)
    //
    uint32_t m_systemHeapCurPtr;

    SpdsPtr<void> m_spdsExecutionThreadFreeList[x_numSpdsAllocatableClassNotUsingLfFreelist];

    alignas(64) std::mutex m_spdsAllocationMutex;

    // SPDS region grows from high address to low address
    //
    std::atomic<uint64_t> m_spdsPageFreeList;
    int32_t m_spdsPageAllocLimit;

    alignas(64) SpdsAllocImpl<CRTP, false /*isTempAlloc*/> m_compilerThreadSpdsAlloc;

    SpdsPtr<void> m_spdsCompilerThreadFreeList[x_numSpdsAllocatableClassNotUsingLfFreelist];

};

template<typename CRTP>
class VMGlobalDataManager
{
public:
    bool WARN_UNUSED Initialize()
    {
        static_assert(std::is_base_of_v<VMGlobalDataManager, CRTP>, "wrong use of CRTP pattern");

        for (size_t i = 0; i < x_totalLuaMetamethodKind; i++)
        {
            m_stringNameForMetatableKind[i] = static_cast<CRTP*>(this)->CreateStringObjectFromRawString(x_luaMetatableStringName[i], static_cast<uint32_t>(std::char_traits<char>::length(x_luaMetatableStringName[i])));
            assert(m_stringNameForMetatableKind[i].As()->m_hashHigh == x_luaMetamethodHashes[i]);
            assert(GetMetamethodOrdinalFromStringName(m_stringNameForMetatableKind[i].As()) == static_cast<int>(i));
        }

        for (size_t i = 0; i < x_numInlineCapacitySteppings; i++)
        {
            m_initialStructureForDifferentInlineCapacity[i].m_value = 0;
        }
        m_filePointerForStdout = stdout;
        m_filePointerForStderr = stderr;

        m_metatableForNil = UserHeapPointer<void>();
        m_metatableForBoolean = UserHeapPointer<void>();
        m_metatableForNumber = UserHeapPointer<void>();
        m_metatableForString = UserHeapPointer<void>();
        m_metatableForFunction = UserHeapPointer<void>();
        m_metatableForCoroutine = UserHeapPointer<void>();

        CreateRootCoroutine();
        return true;
    }

    void Cleanup() { }

    FILE* WARN_UNUSED GetStdout() { return m_filePointerForStdout; }
    FILE* WARN_UNUSED GetStderr() { return m_filePointerForStderr; }

    void RedirectStdout(FILE* newStdout) { m_filePointerForStdout = newStdout; }
    void RedirectStderr(FILE* newStderr) { m_filePointerForStderr = newStderr; }

    std::array<SystemHeapPointer<Structure>, x_numInlineCapacitySteppings>& GetInitialStructureForDifferentInlineCapacityArray()
    {
        return m_initialStructureForDifferentInlineCapacity;
    }

    CoroutineRuntimeContext* GetRootCoroutine()
    {
        return m_rootCoroutine;
    }

    HeapPtr<TableObject> GetRootGlobalObject();

    void InitLibBaseDotNextFunctionObject(TValue val)
    {
        assert(val.IsPointer());
        assert(val.AsPointer<UserHeapGcObjectHeader>().As()->m_type == HeapEntityType::Function);
        m_ljrLibBaseDotNextFunctionObject = val;
    }

    TValue GetLibBaseDotNextFunctionObject()
    {
        return m_ljrLibBaseDotNextFunctionObject;
    }

    UserHeapPointer<HeapString> GetStringNameForMetatableKind(LuaMetamethodKind kind)
    {
        return m_stringNameForMetatableKind[static_cast<size_t>(kind)];
    }

    // return -1 if not found, otherwise return the corresponding LuaMetamethodKind
    //
    int WARN_UNUSED GetMetamethodOrdinalFromStringName(HeapPtr<HeapString> stringName)
    {
        int ord = GetLuaMetamethodOrdinalFromStringHash(stringName->m_hashHigh);
        if (likely(ord == -1))
        {
            return -1;
        }
        assert(static_cast<size_t>(ord) < x_totalLuaMetamethodKind);
        if (likely(m_stringNameForMetatableKind[static_cast<size_t>(ord)] == stringName))
        {
            return ord;
        }
        return -1;
    }

    static constexpr size_t OffsetofStringNameForMetatableKind()
    {
        return offsetof_base_v<VMGlobalDataManager, CRTP> + offsetof_member_v<&VMGlobalDataManager::m_stringNameForMetatableKind>;
    }

private:
    void CreateRootCoroutine();

    CoroutineRuntimeContext* m_rootCoroutine;

    std::array<UserHeapPointer<HeapString>, x_totalLuaMetamethodKind> m_stringNameForMetatableKind;

    std::array<SystemHeapPointer<Structure>, x_numInlineCapacitySteppings> m_initialStructureForDifferentInlineCapacity;

    // The true Lua base library's 'next' function object ('next' is the name of the that function...)
    // Even if the global variable 'next' is overwritten, this will not be changed
    //
    TValue m_ljrLibBaseDotNextFunctionObject;

    // Allow unit test to hook stdout and stderr to a custom temporary file
    //
    FILE* m_filePointerForStdout;
    FILE* m_filePointerForStderr;

public:
    // Per-type Lua metatables
    //
    UserHeapPointer<void> m_metatableForNil;
    UserHeapPointer<void> m_metatableForBoolean;
    UserHeapPointer<void> m_metatableForNumber;
    UserHeapPointer<void> m_metatableForString;
    UserHeapPointer<void> m_metatableForFunction;
    UserHeapPointer<void> m_metatableForCoroutine;
};

class ScriptModule;

class VM : public VMMemoryManager<VM>, public GlobalStringHashConser<VM>, public VMGlobalDataManager<VM>
{
public:
    static_assert(x_segmentRegisterSelfReferencingOffset == offsetof_member_v<&VM::m_self>);

    bool WARN_UNUSED Initialize()
    {
        bool success = false;

        CHECK_LOG_ERROR(static_cast<VMMemoryManager<VM>*>(this)->Initialize());
        Auto(if (!success) static_cast<VMMemoryManager<VM>*>(this)->Cleanup());

        CHECK_LOG_ERROR(static_cast<GlobalStringHashConser<VM>*>(this)->Initialize());
        Auto(if (!success) static_cast<GlobalStringHashConser<VM>*>(this)->Cleanup());

        CHECK_LOG_ERROR(static_cast<VMGlobalDataManager<VM>*>(this)->Initialize());
        Auto(if (!success) static_cast<VMGlobalDataManager<VM>*>(this)->Cleanup());

        success = true;
        return true;
    }

    void Cleanup()
    {
        static_cast<GlobalStringHashConser<VM>*>(this)->Cleanup();
        static_cast<VMMemoryManager<VM>*>(this)->Cleanup();
        static_cast<VMGlobalDataManager<VM>*>(this)->Cleanup();
    }

    void LaunchScript(ScriptModule* module);
    static void LaunchScriptReturnEndpoint(CoroutineRuntimeContext* /*rc*/, void* /*sfp*/, uint8_t* /*retValuesStart*/, uint64_t /*numRetValues*/) { }
};

template<> void VMGlobalDataManager<VM>::CreateRootCoroutine();
template<> HeapPtr<TableObject> VMGlobalDataManager<VM>::GetRootGlobalObject();

inline UserHeapPointer<HeapString> VM_GetSpecialKeyForBoolean(bool v)
{
    constexpr size_t offset = VM::OffsetofSpecialKeyForBooleanIndex();
    return TCGet(reinterpret_cast<HeapPtr<UserHeapPointer<HeapString>>>(offset)[static_cast<size_t>(v)]);
}

inline UserHeapPointer<HeapString> VM_GetStringNameForMetatableKind(LuaMetamethodKind kind)
{
    constexpr size_t offset = VM::OffsetofStringNameForMetatableKind();
    return TCGet(reinterpret_cast<HeapPtr<UserHeapPointer<HeapString>>>(offset)[static_cast<size_t>(kind)]);
}
