#pragma once

#include "memory_ptr.h"
#include "tvalue.h"
#include "array_type.h"
#include "jit_memory_allocator.h"

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
constexpr std::array<uint8_t, x_totalLuaMetamethodKind> x_luaMetamethodHashes = {
#define macro(e) constexpr_xxh3::XXH3_64bits_const( std::string_view { PP_STRINGIFY(PP_TUPLE_GET_2(e)) } ) >> 56,
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
        size_t slot = x_luaMetamethodHashes[i] % htSize;
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
constexpr int WARN_UNUSED GetLuaMetamethodOrdinalFromStringHash(uint8_t hashHigh)
{
    constexpr size_t htSize = std::size(x_luaMetamethodNamesSimpleHashTable);
    size_t slot = hashHigh % htSize;
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
    (WatchpointSet,                 false)      \
  , (JitCallInlineCacheEntry,       false)      \
  , (JitGenericInlineCacheEntry,    false)

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
        assert(m_curChunk % static_cast<int32_t>(alignment) == 0);
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
            assert(m_curChunk % static_cast<int32_t>(x_spdsAllocationPageSize) == 0);
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
    // A simple heuristic:
    // If the object contains at least one (but not zero) property, then it's likely more are coming.
    // So give it at least a few more inline slots
    //
    if (elementToHold > 0)
    {
        elementToHold = std::max(elementToHold, static_cast<uint8_t>(4));
    }
    // The TableObject has a header of 2 slots (16 bytes)
    //
    uint32_t minimalSlotsNeeded = elementToHold + 2;
    uint32_t r32 = GetLeastFitCellSizeInSlots(minimalSlotsNeeded);
    // Deduce the 2-slot header, that's the true inline capacity we have
    //
    r32 -= 2;
    r32 = std::min(r32, x_maxInlineCapacity);
    assert(r32 >= elementToHold);
    return static_cast<uint8_t>(r32);
}

constexpr std::array<uint8_t, x_maxInlineCapacity + 1> ComputeOptimalInlineStorageCapacityArray()
{
    std::array<uint8_t, x_maxInlineCapacity + 1> r;
    for (uint8_t i = 0; i <= x_maxInlineCapacity; i++)
    {
        r[i] = ComputeOptimalInlineStorageCapacity(i);
        assert(r[i] >= i);
        AssertImp(i > 0, r[i] >= r[i-1]);
    }
    for (uint8_t i = 0; i <= x_maxInlineCapacity; i++)
    {
        assert(r[r[i]] == r[i]);
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

class alignas(8) HeapString
{
public:
    static constexpr uint32_t x_stringStructure = 0x8;
    // Common object header
    //
    uint32_t m_hiddenClass;         // always x_StringStructure
    HeapEntityType m_type;          // always TypeEnumForHeapObject<HeapString>
    GcCellState m_cellState;

    // This is the high 8 bits of the XXHash64 value, for quick comparison
    //
    uint8_t m_hashHigh;
    // The ArrayType::x_invalidArrayType bit msut always be set.
    // Note that we also use the lower bits to represent if this string is a reserved word.
    // This is fine as long as we have bit 7 (invalidArraType bit) set and bit 6 (isCoroutine bit) unset.
    //
    uint8_t m_invalidArrayType;

    // This is the low 32 bits of the XXHash64 value, for hash table indexing and quick comparison
    //
    uint32_t m_hashLow;
    // The length of the string
    //
    uint32_t m_length;
    // The string itself
    //
    uint8_t m_string[0];

    static constexpr size_t TrailingArrayOffset()
    {
        return offsetof_member_v<&HeapString::m_string>;
    }

    void PopulateHeader(StringLengthAndHash slah)
    {
        m_hiddenClass = x_stringStructure;
        m_type = TypeEnumForHeapObject<HeapString>;
        m_cellState = x_defaultCellState;

        m_hashHigh = static_cast<uint8_t>(slah.m_hashValue >> 56);
        m_invalidArrayType = ArrayType::x_invalidArrayType;
        m_hashLow = BitwiseTruncateTo<uint32_t>(slah.m_hashValue);
        m_length = SafeIntegerCast<uint32_t>(slah.m_length);
    }

    // Returns the allocation length to store a string of length 'length'
    //
    static size_t ComputeAllocationLengthForString(size_t length)
    {
        // Nonsense: despite Lua string can contain '\0', Lua nevertheless requires string to end with '\0'.
        //
        length++;
        static_assert(TrailingArrayOffset() == sizeof(HeapString));
        size_t beforeAlignment = TrailingArrayOffset() + length;
        return RoundUpToMultipleOf<8>(beforeAlignment);
    }

    // Compare the string represented by 'this' and the string represented by 'other'
    // -1 if <, 0 if ==, 1 if >
    //
    int WARN_UNUSED Compare(HeapString* other)
    {
        if (this == other)
        {
            return 0;
        }
        uint8_t* selfStr = m_string;
        uint32_t selfLength = m_length;
        uint8_t* otherStr = other->m_string;
        uint32_t otherLength = other->m_length;
        uint32_t minLen = std::min(selfLength, otherLength);
        // Note that Lua string may contain '\0', so we must use memcmp, not strcmp.
        //
        int cmpResult = memcmp(selfStr, otherStr, minLen);
        if (cmpResult != 0)
        {
            return cmpResult;
        }
        if (selfLength > minLen)
        {
            return 1;
        }
        if (otherLength > minLen)
        {
            return -1;
        }
        else
        {
            return 0;
        }
    }

    static void SetReservedWord(HeapString* self, uint8_t reservedId)
    {
        assert(reservedId + 1 <= 63);
        uint8_t arrTy = ArrayType::x_invalidArrayType + reservedId + 1;
        self->m_invalidArrayType = arrTy;
    }

    static bool WARN_UNUSED IsReservedWord(HeapString* self)
    {
        return self->m_invalidArrayType != ArrayType::x_invalidArrayType;
    }

    static uint8_t WARN_UNUSED GetReservedWordOrdinal(HeapString* self)
    {
        assert(IsReservedWord(self));
        assert(self->m_invalidArrayType > ArrayType::x_invalidArrayType);
        uint8_t ord = static_cast<uint8_t>(self->m_invalidArrayType - ArrayType::x_invalidArrayType - 1);
        assert(ord + 1 <= 63);
        return ord;
    }
};
static_assert(sizeof(HeapString) == 16);

class ScriptModule;

// [ 12GB user heap ] [ 2GB padding ] [ 2GB short-pointer data structures ] [ 2GB system heap ]
//                                                                          ^
//     userheap                                   SPDS region     32GB aligned baseptr   systemheap
//
class VM
{
public:
    static VM* WARN_UNUSED Create();
    void Destroy();

    static VM* GetActiveVMForCurrentThread()
    {
        return VM_GetActiveVMForCurrentThread();
    }

    HeapPtrTranslator GetHeapPtrTranslator() const
    {
        return HeapPtrTranslator { };
    }

    void SetUpSegmentationRegister()
    {
        X64_SetSegmentationRegister<X64SegmentationRegisterKind::GS>(VMBaseAddress());
    }

    SpdsAllocImpl<VM, true /*isTempAlloc*/> WARN_UNUSED CreateSpdsArenaAlloc()
    {
        return SpdsAllocImpl<VM, true /*isTempAlloc*/>(this);
    }

    SpdsAllocImpl<VM, false /*isTempAlloc*/>& WARN_UNUSED GetCompilerThreadSpdsAlloc()
    {
        return m_compilerThreadSpdsAlloc;
    }

    SpdsAllocImpl<VM, false /*isTempAlloc*/>& WARN_UNUSED GetExecutionThreadSpdsAlloc()
    {
        return m_executionThreadSpdsAlloc;
    }

    SpdsAllocImpl<VM, false /*isTempAlloc*/>& WARN_UNUSED GetSpdsAllocForCurrentThread()
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
        return UserHeapPointer<void> { VM_OffsetToPointer<void>(static_cast<uintptr_t>(m_userHeapCurPtr)) };
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
                void* result = freelist.AsPtr();
                freelist = *reinterpret_cast<SpdsPtr<void>*>(result);
                return reinterpret_cast<T*>(result);
            }
            else
            {
                SpdsPtr<T> result = GetSpdsAllocForCurrentThread().template Alloc<T, true /*collectedByFreeList*/>();
                return result.AsPtr();
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
            freelist = GetHeapPtrTranslator().TranslateToSpdsPtr<void>(object);
        }
        else
        {
            ReleaseAssert(false);
        }
    }

    // Create a string by concatenating start[0] ~ start[len-1]
    // Each TValue must be a string
    //
    UserHeapPointer<HeapString> WARN_UNUSED CreateStringObjectFromConcatenation(TValue* start, size_t len);

    // Create a string by concatenating start[0] ~ start[len-1]
    // Each item is a string described by <ptr, len>
    //
    UserHeapPointer<HeapString> WARN_UNUSED CreateStringObjectFromConcatenation(std::pair<const void*, size_t>* start, size_t len);

    // Create a string by concatenating str1 .. start[0] ~ start[len-1]
    // str1 and each TValue must be a string
    //
    UserHeapPointer<HeapString> WARN_UNUSED CreateStringObjectFromConcatenation(UserHeapPointer<HeapString> str1, TValue* start, size_t len);
    UserHeapPointer<HeapString> WARN_UNUSED CreateStringObjectFromRawString(const void* str, uint32_t len);

    HeapString* WARN_UNUSED CreateStringObjectFromRawCString(const char* str)
    {
        return CreateStringObjectFromRawString(str, static_cast<uint32_t>(strlen(str))).As();
    }

    // Create a string by concatenating n copies of 'str'
    //
    UserHeapPointer<HeapString> WARN_UNUSED CreateStringObjectFromConcatenationOfSameString(const char* ptr, uint32_t len, size_t n);

    uint32_t GetGlobalStringHashConserCurrentHashTableSize() const
    {
        return m_hashTableSizeMask + 1;
    }

    uint32_t GetGlobalStringHashConserCurrentElementCount() const
    {
        return m_elementCount;
    }

    UserHeapPointer<HeapString> GetSpecialKeyForMetadataSlot()
    {
        return m_specialKeyForMetatableSlot;
    }

    UserHeapPointer<HeapString> GetSpecialKeyForBoolean(bool v)
    {
        return m_specialKeyForBooleanIndex[static_cast<size_t>(v)];
    }

    static constexpr size_t OffsetofSpecialKeyForBooleanIndex()
    {
        return offsetof_member_v<&VM::m_specialKeyForBooleanIndex>;
    }

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

    static CoroutineRuntimeContext* VM_GetRootCoroutine()
    {
        constexpr size_t offset = offsetof_member_v<&VM::m_rootCoroutine>;
        using T = typeof_member_t<&VM::m_rootCoroutine>;
        return *VM_OffsetToPointer<T>(offset);
    }

    TableObject* GetRootGlobalObject();

    // The VM structure also stores several 'true' library functions that doesn't change even if the respective user-exposed
    // global value is overwritten.
    // This is mainly a convenience feature, as use of this feature could have been avoided by using upvalues instead,
    // but by not using upvalue, our global environment initialization logic can be simpler.
    //
    enum class LibFn
    {
        BaseNext,
        BaseError,
        BaseIPairsIter,
        BaseToString,
        BaseLoad,
        IoLinesIter,
        // A special object denoting that the 'is_next' validation of a key-value for-loop has passed
        //
        BaseNextValidationOk,
        // must be last member
        //
        X_END_OF_ENUM
    };

    enum class LibFnProto
    {
        CoroutineWrapCall,
        // must be last member
        //
        X_END_OF_ENUM
    };

    template<LibFn fn>
    void ALWAYS_INLINE InitializeLibFn(TValue val)
    {
        static_assert(fn != LibFn::X_END_OF_ENUM);
        assert(val.Is<tFunction>() || val.Is<tTable>());
        m_vmLibFunctionObjects[static_cast<size_t>(fn)] = val;
    }

    template<LibFn fn>
    TValue WARN_UNUSED ALWAYS_INLINE GetLibFn()
    {
        static_assert(fn != LibFn::X_END_OF_ENUM);
        return m_vmLibFunctionObjects[static_cast<size_t>(fn)];
    }

    template<LibFnProto fn>
    void ALWAYS_INLINE InitializeLibFnProto(SystemHeapPointer<ExecutableCode> val)
    {
        static_assert(fn != LibFnProto::X_END_OF_ENUM);
        m_vmLibFnProtos[static_cast<size_t>(fn)] = val;
    }

    template<LibFnProto fn>
    SystemHeapPointer<ExecutableCode> WARN_UNUSED ALWAYS_INLINE GetLibFnProto()
    {
        static_assert(fn != LibFnProto::X_END_OF_ENUM);
        return m_vmLibFnProtos[static_cast<size_t>(fn)];
    }

    UserHeapPointer<HeapString> GetStringNameForMetatableKind(LuaMetamethodKind kind)
    {
        return m_stringNameForMetatableKind[static_cast<size_t>(kind)];
    }

    // return -1 if not found, otherwise return the corresponding LuaMetamethodKind
    //
    int WARN_UNUSED GetMetamethodOrdinalFromStringName(HeapString* stringName)
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

    static std::mt19937* WARN_UNUSED ALWAYS_INLINE GetUserPRNG()
    {
        constexpr size_t offset = offsetof_member_v<&VM::m_usrPRNG>;
        using T = typeof_member_t<&VM::m_usrPRNG>;
        std::mt19937* res = *VM_OffsetToPointer<T>(offset);
        if (likely(res != nullptr))
        {
            return res;
        }
        return GetUserPRNGSlow();
    }

    static constexpr size_t OffsetofStringNameForMetatableKind()
    {
        return offsetof_member_v<&VM::m_stringNameForMetatableKind>;
    }

    static TValue* ALWAYS_INLINE VM_LibFnArrayAddr()
    {
        return VM_OffsetToPointer<TValue>(offsetof_member_v<&VM::m_vmLibFunctionObjects>);
    }

    static HeapString* ALWAYS_INLINE VM_GetEmptyString()
    {
        constexpr size_t offset = offsetof_member_v<&VM::m_emptyString>;
        using T = typeof_member_t<&VM::m_emptyString>;
        return *VM_OffsetToPointer<T>(offset);
    }

    std::pair<TValue* /*retStart*/, uint64_t /*numRet*/> LaunchScript(ScriptModule* module);

    // Determines the starting tier of the Lua functions when a new CodeBlock is created
    // (which happens either when a Lua chunk is parsed, or when an existing Lua chunk is
    // instantiated with an unseen global object)
    //
    // When 'BaselineJIT' is selected, any newly-created CodeBlock will be compiled to baseline JIT code immediately.
    //
    enum class EngineStartingTier
    {
        Interpreter,
        BaselineJIT
    };

    // Only affects CodeBlocks created after this call.
    //
    void SetEngineStartingTier(EngineStartingTier tier)
    {
        m_isEngineStartingTierBaselineJit = (tier == EngineStartingTier::BaselineJIT);
    }

    EngineStartingTier GetEngineStartingTier() const
    {
        return m_isEngineStartingTierBaselineJit ? EngineStartingTier::BaselineJIT : EngineStartingTier::Interpreter;
    }

    bool IsEngineStartingTierBaselineJit() const
    {
        return GetEngineStartingTier() == EngineStartingTier::BaselineJIT;
    }

    // Must be ordered from lower tier to higher tier
    //
    enum class EngineMaxTier : uint8_t
    {
        Interpreter,
        BaselineJIT,
        Unrestricted    // same effect as specifying the last tier of the above list
    };

    // Only affects CodeBlocks created or tiered-up after this call.
    //
    void SetEngineMaxTier(EngineMaxTier tier) { m_engineMaxTier = tier; }

    // Return true if interpreter may tier up to a higher tier
    //
    bool WARN_UNUSED InterpreterCanTierUpFurther() { return m_engineMaxTier > EngineMaxTier::Interpreter; }

    // Return true if baseline JIT may tier up to a higher tier
    //
    bool WARN_UNUSED BaselineJitCanTierUpFurther() { return false; }

    JitMemoryAllocator* GetJITMemoryAlloc()
    {
        return &m_jitMemoryAllocator;
    }

    uint32_t GetNumTotalBaselineJitCompilations() { return m_totalBaselineJitCompilations; }
    void IncrementNumTotalBaselineJitCompilations() { m_totalBaselineJitCompilations++; }

    static constexpr size_t x_pageSize = 4096;

private:
    static constexpr size_t x_vmLayoutLength = 18ULL << 30;
    // The start address of the VM is always at 16GB % 32GB, this makes sure the VM base is aligned at 32GB
    //
    static constexpr size_t x_vmLayoutAlignment = 32ULL << 30;
    static constexpr size_t x_vmLayoutAlignmentOffset = 16ULL << 30;
    static constexpr size_t x_vmBaseOffset = 16ULL << 30;
    static constexpr size_t x_vmUserHeapSize = 12ULL << 30;

    static_assert((1ULL << x_vmBasePtrLog2Alignment) == x_vmLayoutAlignment, "the constants must match");

    uintptr_t VMBaseAddress() const
    {
        uintptr_t result = reinterpret_cast<uintptr_t>(this);
        assert(result == m_self);
        return result;
    }

    void __attribute__((__preserve_most__)) BumpUserHeap();
    void BumpSystemHeap();

    bool WARN_UNUSED SpdsAllocateTryGetFreeListPage(int32_t* out)
    {
        uint64_t taggedValue = m_spdsPageFreeList.load(std::memory_order_acquire);
        while (true)
        {
            int32_t head = BitwiseTruncateTo<int32_t>(taggedValue);
            assert(head % static_cast<int32_t>(x_spdsAllocationPageSize) == 0);
            if (head == x_spdsAllocationPageSize)
            {
                return false;
            }
            uint32_t tag = BitwiseTruncateTo<uint32_t>(taggedValue >> 32);

            std::atomic<int32_t>* addr = reinterpret_cast<std::atomic<int32_t>*>(VMBaseAddress() + SignExtendTo<uint64_t>(head) - 4);
            int32_t newHead = addr->load(std::memory_order_relaxed);
            assert(newHead % static_cast<int32_t>(x_spdsAllocationPageSize) == 0);
            tag++;
            uint64_t newTaggedValue = (static_cast<uint64_t>(tag) << 32) | ZeroExtendTo<uint64_t>(newHead);

            if (m_spdsPageFreeList.compare_exchange_weak(taggedValue /*expected, inout*/, newTaggedValue /*desired*/, std::memory_order_release, std::memory_order_acquire))
            {
                *out = head;
                return true;
            }
        }
    }

    int32_t WARN_UNUSED SpdsAllocatePageSlowPath();

    // Allocate a chunk of memory, return one of the pages, and put the rest into free list
    //
    int32_t WARN_UNUSED SpdsAllocatePageSlowPathImpl();

    // In Lua all strings are hash-consed
    // The global string conser implementation
    //

    // The hash table stores GeneralHeapPointer
    // We know that they must be UserHeapPointer, so the below values should never appear as valid values
    //
    static constexpr int32_t x_stringConserHtNonexistentValue = 0;
    static constexpr int32_t x_stringConserHtDeletedValue = 4;

    static bool WARN_UNUSED StringHtCellValueIsNonExistentOrDeleted(GeneralHeapPointer<HeapString> ptr)
    {
        AssertIff(ptr.m_value >= 0, ptr.m_value == x_stringConserHtNonexistentValue || ptr.m_value == x_stringConserHtDeletedValue);
        return ptr.m_value >= 0;
    }

    static bool WARN_UNUSED StringHtCellValueIsNonExistent(GeneralHeapPointer<HeapString> ptr)
    {
        return ptr.m_value == x_stringConserHtNonexistentValue;
    }

    // max load factor is x_stringht_loadfactor_numerator / (2^x_stringht_loadfactor_denominator_shift)
    //
    static constexpr uint32_t x_stringht_loadfactor_denominator_shift = 1;
    static constexpr uint32_t x_stringht_loadfactor_numerator = 1;

    static void ReinsertDueToResize(GeneralHeapPointer<HeapString>* hashTable, uint32_t hashTableSizeMask, GeneralHeapPointer<HeapString> e);

    // TODO: when we have GC thread we need to figure out how this interacts with GC
    //
    void ExpandStringConserHashTableIfNeeded();

    // Insert an abstract multi-piece string into the hash table if it does not exist
    // Return the HeapString
    //
    template<typename Iterator>
    UserHeapPointer<HeapString> WARN_UNUSED InsertMultiPieceString(Iterator iterator);

    static std::mt19937* WARN_UNUSED NO_INLINE GetUserPRNGSlow()
    {
        VM* vm = VM::GetActiveVMForCurrentThread();
        assert(vm->m_usrPRNG == nullptr);
        vm->m_usrPRNG = new std::mt19937();
        return vm->m_usrPRNG;
    }

    bool WARN_UNUSED InitializeVMBase();
    bool WARN_UNUSED InitializeVMStringManager();
    void CleanupVMStringManager();
    bool WARN_UNUSED InitializeVMGlobalData();
    bool WARN_UNUSED Initialize();
    void Cleanup();
    void CreateRootCoroutine();

    // The data members
    //

    // must be first member, stores the value of static_cast<CRTP*>(this)
    //
    uintptr_t m_self;

    bool m_isEngineStartingTierBaselineJit;
    EngineMaxTier m_engineMaxTier;

    alignas(64) SpdsAllocImpl<VM, false /*isTempAlloc*/> m_executionThreadSpdsAlloc;

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

    JitMemoryAllocator m_jitMemoryAllocator;

    uint32_t m_totalBaselineJitCompilations;

    alignas(64) std::mutex m_spdsAllocationMutex;

    // SPDS region grows from high address to low address
    //
    std::atomic<uint64_t> m_spdsPageFreeList;
    int32_t m_spdsPageAllocLimit;

    alignas(64) SpdsAllocImpl<VM, false /*isTempAlloc*/> m_compilerThreadSpdsAlloc;

    SpdsPtr<void> m_spdsCompilerThreadFreeList[x_numSpdsAllocatableClassNotUsingLfFreelist];

    uint32_t m_hashTableSizeMask;
    uint32_t m_elementCount;
    // use GeneralHeapPointer because it's 4 bytes
    //
    GeneralHeapPointer<HeapString>* m_hashTable;

    // In PolyMetatable mode, the metatable is stored in a property slot
    // For simplicity, we always assign this special key (which is used exclusively for this purpose) to this slot
    //
    UserHeapPointer<HeapString> m_specialKeyForMetatableSlot;

    // These two special keys are used for 'false' and 'true' respectively
    //
    UserHeapPointer<HeapString> m_specialKeyForBooleanIndex[2];

    CoroutineRuntimeContext* m_rootCoroutine;

    std::array<UserHeapPointer<HeapString>, x_totalLuaMetamethodKind> m_stringNameForMetatableKind;

    std::array<SystemHeapPointer<Structure>, x_numInlineCapacitySteppings> m_initialStructureForDifferentInlineCapacity;

    TValue m_vmLibFunctionObjects[static_cast<size_t>(LibFn::X_END_OF_ENUM)];
    SystemHeapPointer<ExecutableCode> m_vmLibFnProtos[static_cast<size_t>(LibFnProto::X_END_OF_ENUM)];

    // The PRNG exposed to the user program. Internal VM logic must not use this PRNG.
    //
    std::mt19937* m_usrPRNG;

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

    // The string ""
    //
    HeapString* m_emptyString;

    // The string 'tostring'
    //
    UserHeapPointer<HeapString> m_toStringString;

    // The string '__tostring'
    //
    UserHeapPointer<HeapString> m_stringNameForToStringMetamethod;

    // In Lua, the 'string' type initially has a non-nil metatable. This is the only per-type metatable that exists by default in Lua.
    // This value records the initial hidden class for 'm_metatableForString', which allows us to quickly rule out existence
    // of certain fields in that metatable. (Specifically, if the hidden class has not been changed, then we know for sure that
    // the only string field in the metatable is '__index').
    //
    SystemHeapPointer<void> m_initialHiddenClassOfMetatableForString;
};

inline UserHeapPointer<HeapString> VM_GetSpecialKeyForBoolean(bool v)
{
    constexpr size_t offset = VM::OffsetofSpecialKeyForBooleanIndex();
    return VM_OffsetToPointer<UserHeapPointer<HeapString>>(offset)[static_cast<size_t>(v)];
}

inline UserHeapPointer<HeapString> VM_GetStringNameForMetatableKind(LuaMetamethodKind kind)
{
    constexpr size_t offset = VM::OffsetofStringNameForMetatableKind();
    return VM_OffsetToPointer<UserHeapPointer<HeapString>>(offset)[static_cast<size_t>(kind)];
}

template<VM::LibFn fn>
inline TValue ALWAYS_INLINE VM_GetLibFunctionObject()
{
    static_assert(fn != VM::LibFn::X_END_OF_ENUM);
    TValue* arrayStart = VM::VM_LibFnArrayAddr();
    return arrayStart[static_cast<size_t>(fn)];
}

