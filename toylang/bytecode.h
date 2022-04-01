#pragma once

#include "common_utils.h"

namespace ToyLang {

using namespace CommonUtils;

template<typename T>
using RestrictPtr = T* __restrict__;

template<typename T>
using ConstRestrictPtr = RestrictPtr<const T>;

#define CLANG_GS_ADDRESS_SPACE_IDENTIFIER 256
#define CLANG_FS_ADDRESS_SPACE_IDENTIFIER 257

template<typename T>
using HeapPtr = T __attribute__((address_space(CLANG_GS_ADDRESS_SPACE_IDENTIFIER))) *;

template<typename P, typename T>
inline constexpr bool IsPtrOrHeapPtr = std::is_same_v<P, T*> || std::is_same_v<P, HeapPtr<T>>;

// Equivalent to memcpy, but asserts that the address range does not overlap
//
inline void ALWAYS_INLINE SafeMemcpy(void* dst, const void* src, size_t len)
{
    assert(reinterpret_cast<uintptr_t>(dst) + len <= reinterpret_cast<uintptr_t>(src) || reinterpret_cast<uintptr_t>(src) + len <= reinterpret_cast<uintptr_t>(dst));
    memcpy(dst, src, len);
}

enum class Type : uint8_t
{
    NIL,
    BOOLEAN,
    DOUBLE,
    STRING,
    FUNCTION,
    USERDATA,
    THREAD,
    OBJECT,
    X_END_OF_ENUM
};

class FunctionObject;

class HashConsedString
{

};

struct UserHeapPointer
{
    UserHeapPointer() : m_value(0) { }
    UserHeapPointer(int64_t value)
        : m_value(value)
    {
        assert(x_minValue <= m_value && m_value <= x_maxValue && (m_value & 7LL) == 0);
    }

    template<typename T>
    UserHeapPointer(HeapPtr<T> value)
    {
        m_value = reinterpret_cast<intptr_t>(value);
        assert(x_minValue <= m_value && m_value <= x_maxValue && (m_value & 7LL) == 0);
        assert(As<T>() == value);
    }

    template<typename T>
    HeapPtr<T> WARN_UNUSED ALWAYS_INLINE As() const
    {
        return reinterpret_cast<HeapPtr<T>>(m_value);
    }

    bool WARN_UNUSED ALWAYS_INLINE operator==(const UserHeapPointer& rhs) const
    {
        return m_value == rhs.m_value;
    }

    static constexpr int64_t x_minValue = static_cast<int64_t>(0xFFFFFFFE00000000ULL);
    static constexpr int64_t x_maxValue = static_cast<int64_t>(0xFFFFFFFEFFFFFFFFULL);

    intptr_t m_value;
};

struct SystemHeapPointer
{
    SystemHeapPointer() : m_value(0) { }
    SystemHeapPointer(uint32_t value)
        : m_value(value)
    {
        assert((m_value & 7U) == 0);
    }

    template<typename T>
    SystemHeapPointer(HeapPtr<T> value)
    {
        intptr_t addr = reinterpret_cast<intptr_t>(value);
        m_value = SafeIntegerCast<uint32_t>(addr);
        assert((m_value & 7U) == 0);
        assert(As<T>() == value);
    }

    template<typename T>
    HeapPtr<T> WARN_UNUSED ALWAYS_INLINE As() const
    {
        return reinterpret_cast<HeapPtr<T>>(static_cast<uint64_t>(m_value));
    }

    bool WARN_UNUSED ALWAYS_INLINE operator==(const SystemHeapPointer& rhs) const
    {
        return m_value == rhs.m_value;
    }

    uint32_t m_value;
};

struct GeneralHeapPointer
{
    GeneralHeapPointer() : m_value(0) { }
    GeneralHeapPointer(int32_t value)
        : m_value(value)
    {
        assert((m_value & 1) == 0);
        AssertImp(m_value < 0, x_negMinValue <= m_value && m_value <= x_negMaxValue);
        AssertImp(m_value >= 0, m_value <= x_posMaxValue);
    }

    template<typename T>
    GeneralHeapPointer(HeapPtr<T> value)
    {
        intptr_t addr = reinterpret_cast<intptr_t>(value);
        assert((addr & 7LL) == 0);
        addr = ArithmeticShiftRight(addr, 2);
        m_value = SafeIntegerCast<int32_t>(addr);
        AssertImp(m_value < 0, x_negMinValue <= m_value && m_value <= x_negMaxValue);
        AssertImp(m_value >= 0, m_value <= x_posMaxValue);
        assert(As<T>() == value);
    }

    template<typename T>
    HeapPtr<T> WARN_UNUSED ALWAYS_INLINE As() const
    {
        return reinterpret_cast<HeapPtr<T>>(ArithmeticShiftLeft(static_cast<int64_t>(m_value), 2));
    }

    bool WARN_UNUSED ALWAYS_INLINE operator==(const GeneralHeapPointer& rhs) const
    {
        return m_value == rhs.m_value;
    }

    static constexpr int32_t x_negMinValue = static_cast<int32_t>(0x80000000);
    static constexpr int32_t x_negMaxValue = static_cast<int32_t>(0xBFFFFFFF);
    static constexpr int32_t x_posMaxValue = static_cast<int32_t>(0x3FFFFFFF);

    int32_t m_value;
};

struct SpdsRegionPointer
{
    int32_t m_value;
};

template<typename T>
struct SpdsPtr
{
    SpdsPtr() : m_value(0) { }
    SpdsPtr(int32_t value)
        : m_value(value)
    {
        assert(m_value < 0);
    }

    HeapPtr<T> WARN_UNUSED ALWAYS_INLINE Get() const
    {
        return reinterpret_cast<HeapPtr<T>>(static_cast<int64_t>(m_value));
    }

    int32_t m_value;
};

constexpr size_t x_spdsAllocationPageSize = 4096;
static_assert(is_power_of_2(x_spdsAllocationPageSize));

// If 'isTempAlloc' is true, we give the memory pages back to VM when the struct is destructed
// Singlethread use only.
// Currently chunk size is always 4KB, so allocate small objects only.
//
template<typename Host, bool isTempAlloc>
class SpdsAllocImpl : NonCopyable, NonMovable
{
public:
    SpdsAllocImpl(Host* host)
        : m_host(host)
        , m_curChunk(0)
        , m_lastChunkInTheChain(0)
    { }

    ~SpdsAllocImpl()
    {
        if constexpr(isTempAlloc)
        {
            ReturnMemory();
        }
    }

    template<typename T>
    SpdsPtr<T> ALWAYS_INLINE WARN_UNUSED Alloc()
    {
        static_assert(sizeof(T) <= x_spdsAllocationPageSize - RoundUpToMultipleOf<alignof(T)>(isTempAlloc ? 4ULL : 0ULL));
        return SpdsPtr<T> { AllocMemory<alignof(T)>(static_cast<uint32_t>(sizeof(T))) };
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

struct MiscImmediateValue
{
    // All misc immediate values must be between [0, 127]
    // 0 is more efficient to use than the others in that a XOR instruction is not needed to create the value
    //
    static constexpr uint64_t x_nil = 0;
    static constexpr uint64_t x_false = 2;
    static constexpr uint64_t x_true = 3;

    MiscImmediateValue() : m_value(0) { }
    MiscImmediateValue(uint64_t value)
        : m_value(value)
    {
        assert(m_value == x_nil || m_value == x_true || m_value == x_false);
    }

    bool ALWAYS_INLINE IsNil() const
    {
        return m_value == 0;
    }

    bool ALWAYS_INLINE IsBoolean() const
    {
        return m_value != 0;
    }

    bool ALWAYS_INLINE GetBooleanValue() const
    {
        assert(IsBoolean());
        return m_value & 1;
    }

    MiscImmediateValue WARN_UNUSED ALWAYS_INLINE FlipBooleanValue() const
    {
        assert(IsBoolean());
        return MiscImmediateValue { m_value ^ 1 };
    }

    static MiscImmediateValue WARN_UNUSED ALWAYS_INLINE CreateNil()
    {
        return MiscImmediateValue { x_nil };
    }

    static MiscImmediateValue WARN_UNUSED ALWAYS_INLINE CreateFalse()
    {
        return MiscImmediateValue { x_false };
    }

    static MiscImmediateValue WARN_UNUSED ALWAYS_INLINE CreateTrue()
    {
        return MiscImmediateValue { x_true };
    }

    uint64_t m_value;
};

struct TValue
{
    // We use the following NaN boxing scheme:
    //         / 0000 **** **** ****
    // double {          ...
    //         \ FFFA **** **** ****
    // int       FFFB FFFF **** ****
    // other     FFFC FFFF 0000 00** (00 - 7F only)
    // pointer   FFFF FFFE **** ****
    //
    // The FFFE word in pointer may carry tagging info (fine as long as it's not 0xFFFF), but currently we don't need it.
    //

    TValue() : m_value(0) { }
    TValue(uint64_t value) : m_value(value) { }

    static constexpr uint64_t x_int32Tag = 0xFFFBFFFF00000000ULL;
    static constexpr uint64_t x_mivTag = 0xFFFCFFFF0000007FULL;

    // Translates to a single ANDN instruction with BMI1 support
    //
    bool ALWAYS_INLINE IsInt32(uint64_t int32Tag) const
    {
        assert(int32Tag == x_int32Tag);
        bool result = (m_value & int32Tag) == int32Tag;
        AssertIff(result, static_cast<uint32_t>(m_value >> 32) == 0xFFFBFFFFU);
        return result;
    }

    // Translates to imm8 LEA instruction + ANDN instruction with BMI1 support
    //
    bool ALWAYS_INLINE IsMIV(uint64_t mivTag) const
    {
        assert(mivTag == x_mivTag);
        bool result = (m_value & (mivTag - 0x7F)) == (mivTag - 0x7F);
        AssertIff(result, x_mivTag - 0x7F <= m_value && m_value <= x_mivTag);
        return result;
    }

    bool ALWAYS_INLINE IsPointer(uint64_t mivTag) const
    {
        assert(mivTag == x_mivTag);
        bool result = (m_value > mivTag);
        AssertIff(result, static_cast<uint32_t>(m_value >> 32) == 0xFFFFFFFEU);
        return result;
    }

    bool ALWAYS_INLINE IsDouble(uint64_t int32Tag) const
    {
        assert(int32Tag == x_int32Tag);
        bool result = m_value < int32Tag;
        AssertIff(result, m_value <= 0xFFFAFFFFFFFFFFFFULL);
        return result;
    }

    double ALWAYS_INLINE AsDouble() const
    {
        assert(IsDouble(x_int32Tag) && !IsMIV(x_mivTag) && !IsPointer(x_mivTag) && !IsInt32(x_int32Tag));
        return cxx2a_bit_cast<double>(m_value);
    }

    int32_t ALWAYS_INLINE AsInt32() const
    {
        assert(IsInt32(x_int32Tag) && !IsMIV(x_mivTag) && !IsPointer(x_mivTag) && !IsDouble(x_int32Tag));
        return BitwiseTruncateTo<int32_t>(m_value);
    }

    UserHeapPointer ALWAYS_INLINE AsPointer() const
    {
        assert(IsPointer(x_mivTag) && !IsMIV(x_mivTag) && !IsDouble(x_int32Tag) && !IsInt32(x_int32Tag));
        return UserHeapPointer { static_cast<int64_t>(m_value) };
    }

    MiscImmediateValue ALWAYS_INLINE AsMIV(uint64_t mivTag) const
    {
        assert(mivTag == x_mivTag && IsMIV(x_mivTag) && !IsDouble(x_int32Tag) && !IsInt32(x_int32Tag) && !IsPointer(x_mivTag));
        return MiscImmediateValue { m_value ^ mivTag };
    }

    static TValue WARN_UNUSED ALWAYS_INLINE CreateInt32(int32_t value, uint64_t int32Tag)
    {
        assert(int32Tag == x_int32Tag);
        TValue result { int32Tag | ZeroExtendTo<uint64_t>(value) };
        assert(result.AsInt32() == value);
        return result;
    }

    static TValue WARN_UNUSED ALWAYS_INLINE CreateDouble(double value)
    {
        TValue result { cxx2a_bit_cast<uint64_t>(value) };
        SUPRESS_FLOAT_EQUAL_WARNING(
            AssertImp(!std::isnan(value), result.AsDouble() == value);
            AssertIff(std::isnan(value), std::isnan(result.AsDouble()));
        )
        return result;
    }

    static TValue WARN_UNUSED ALWAYS_INLINE CreatePointer(UserHeapPointer ptr)
    {
        TValue result { static_cast<uint64_t>(ptr.m_value) };
        assert(result.AsPointer() == ptr);
        return result;
    }

    static TValue WARN_UNUSED ALWAYS_INLINE CreateMIV(MiscImmediateValue miv, uint64_t mivTag)
    {
        assert(mivTag == x_mivTag);
        TValue result { miv.m_value ^ mivTag };
        assert(result.AsMIV(mivTag).m_value == miv.m_value);
        return result;
    }

    uint64_t m_value;
};

// [ 4GB user heap ] [ 2GB padding ] [ 2GB short-pointer data structures ] [ up to 4GB system heap ]
//                                                                         ^
//    userheap                                   SPDS region      4GB aligned baseptr     systemheap
//
template<typename CRTP>
class VMLayout
{
public:
    static constexpr size_t x_pageSize = 4096;
    static constexpr size_t x_vmLayoutLength = 12ULL << 30;
    static constexpr size_t x_vmLayoutAlignment = 4ULL << 30;
    static constexpr size_t x_vmBaseOffset = 8ULL << 30;

    template<typename... Args>
    static CRTP* WARN_UNUSED Create(Args&&... args)
    {
        void* ptrVoid = mmap(nullptr, x_vmLayoutLength + x_vmLayoutAlignment, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (ptrVoid == MAP_FAILED)
        {
            int e = errno;
            fprintf(stderr, "[ERROR] VMLayout::Create(): failed to reserve VM address range, error %d (%s)", e, strerror(e));
            return nullptr;
        }

        // cut out the 4GB-aligned 12GB space, and unmap the remaining
        //
        {
            uintptr_t ptr = reinterpret_cast<uintptr_t>(ptrVoid);
            uintptr_t alignedPtr = RoundUpToMultipleOf<x_vmLayoutAlignment>(ptr);
            assert(alignedPtr >= ptr && alignedPtr % x_vmLayoutAlignment == 0 && alignedPtr - ptr < x_vmLayoutAlignment);

            // If any unmap failed, log a warning, but continue execution.
            //
            if (alignedPtr > ptr)
            {
                int r = munmap(reinterpret_cast<void*>(ptr), alignedPtr - ptr);
                if (r != 0)
                {
                    int e = errno;
                    fprintf(stderr, "[WARN] VMLayout::Create(): failed to unmap unnecessary VM address range [1], error %d (%s)", e, strerror(e));
                }
            }

            {
                uintptr_t addr = alignedPtr + x_vmLayoutLength;
                int r = munmap(reinterpret_cast<void*>(addr), x_vmLayoutAlignment - (alignedPtr - ptr));
                if (r != 0)
                {
                    int e = errno;
                    fprintf(stderr, "[WARN] VMLayout::Create(): failed to unmap unnecessary VM address range [2], error %d (%s)", e, strerror(e));
                }
            }

            ptrVoid = reinterpret_cast<void*>(alignedPtr);
        }

        bool success = false;
        void* unmapPtrOnFailure = ptrVoid;
        size_t unmapLengthOnFailure = x_vmLayoutLength;

        Auto(
            if (!success)
            {
                int r = munmap(unmapPtrOnFailure, unmapLengthOnFailure);
                if (r != 0)
                {
                    int e = errno;
                    fprintf(stderr, "[WARN] VMLayout::Create(): cannot unmap VM on failure cleanup, error %d (%s)", e, strerror(e));
                }
            }
        );

        // Map memory and initialize the VM struct
        //
        void* vmVoid = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptrVoid) + x_vmBaseOffset);
        constexpr size_t sizeToMap = RoundUpToMultipleOf<x_pageSize>(sizeof(CRTP));
        {
            void* r = mmap(vmVoid, sizeToMap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_FIXED, -1, 0);
            if (r == MAP_FAILED)
            {
                int e = errno;
                fprintf(stderr, "[ERROR] VMLayout::Create(): failed to allocate VM struct, error %d (%s)", e, strerror(e));
                return nullptr;
            }
            TestAssert(vmVoid == r);
        }

        CRTP* vm = new (vmVoid) CRTP(std::forward<Args>(args)...);
        assert(vm == vmVoid);

        Auto(
            if (!success)
            {
                vm->~CRTP();
            }
        );

        success = true;
        return vm;
    }

    void Destroy()
    {
        CRTP* ptr = static_cast<CRTP*>(this);
        ptr->~CRTP();

        void* unmapAddr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptr) - x_vmBaseOffset);
        int r = munmap(unmapAddr, x_vmLayoutLength);
        if (r != 0)
        {
            int e = errno;
            fprintf(stderr, "[WARN] VMLayout::Destroy(): cannot unmap VM, error %d (%s)", e, strerror(e));
        }
    }

    static CRTP* GetActiveVMForCurrentThread()
    {
        return reinterpret_cast<CRTP*>(reinterpret_cast<HeapPtr<CRTP>>(0)->m_self);
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

    // Grab one 4K page from the SPDS region.
    // The page can be given back via SpdsReturnMemoryFreeList()
    // NOTE: if the page is [begin, end), this returns 'end', not 'begin'! SpdsReturnMemoryFreeList also expects 'end', not 'begin'
    //
    int32_t WARN_UNUSED ALWAYS_INLINE SpdsAllocatePage()
    {
        int32_t out;
        if (SpdsAllocateTryGetFreeListPage(&out))
        {
            return out;
        }
        else
        {
            return SpdsAllocatePageSlowPath();
        }
    }

    void SpdsPutAllocatedPagesToFreeList(int32_t firstPage, int32_t lastPage)
    {
        std::atomic<int32_t>* addr = reinterpret_cast<std::atomic<int32_t>*>(m_self + SignExtendTo<uint64_t>(lastPage) - 4);
        while (true)
        {
            uint64_t taggedValue = m_spdsPageFreeList.load(std::memory_order_relaxed);
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

private:
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

            std::atomic<int32_t>* addr = reinterpret_cast<std::atomic<int32_t>*>(m_self + SignExtendTo<uint64_t>(head) - 4);
            int32_t newHead = addr->load(std::memory_order_relaxed);
            assert(newHead % x_spdsAllocationPageSize == 0);
            tag++;
            uint64_t newTaggedValue = (static_cast<uint64_t>(tag) << 32) | ZeroExtendTo<uint64_t>(newHead);

            if (m_spdsPageFreeList.compare_exchange_weak(taggedValue /*expect, inout*/, newTaggedValue /*desired*/, std::memory_order_release, std::memory_order_acquire))
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
        // We allocate 4K, 4K, 8K, 16K, 32K first
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

        if (unlikely(SubWithOverflowCheck(m_spdsPageAllocLimit, static_cast<int32_t>(lengthToAllocate), &m_spdsPageAllocLimit)))
        {
            fprintf(stderr, "Resource limit exceeded: abort because SPDS region overflowed 2GB memory limit.\n");
            ReleaseAssert(false);
        }

        // Allocate memory
        //
        uintptr_t allocAddr = m_self + SignExtendTo<uint64_t>(m_spdsPageAllocLimit);
        assert(allocAddr % x_pageSize == 0 && allocAddr % x_spdsAllocationPageSize == 0);
        void* r = mmap(reinterpret_cast<void*>(allocAddr), static_cast<size_t>(lengthToAllocate), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_FIXED, -1, 0);
        if (r == MAP_FAILED)
        {
            int e = errno;
            fprintf(stderr, "Out of Memory: Allocation of length %d failed with error %d (%s), aborting.\n", static_cast<int>(lengthToAllocate), e, strerror(e));
            ReleaseAssert(false);
        }
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
                std::atomic<int32_t>* addr = reinterpret_cast<std::atomic<int32_t>*>(m_self + SignExtendTo<uint64_t>(cur) - 4);
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
    VMLayout()
        : m_executionThreadSpdsAlloc(static_cast<CRTP*>(this))
        , m_spdsPageFreeList(static_cast<uint64_t>(x_spdsAllocationPageSize))
        , m_compilerThreadSpdsAlloc(static_cast<CRTP*>(this))
    {
        static_assert(std::is_base_of_v<VMLayout, CRTP>, "wrong use of CRTP pattern");
        // These restrictions might not be necessary, but just to make things safe and simple
        //
        static_assert(!std::is_polymorphic_v<CRTP>, "must be not polymorphic");
        static_assert(offsetof_base_v<VMLayout, CRTP> == 0, "VM must inherit VMLayout as the first inherited class");

        m_self = reinterpret_cast<uintptr_t>(static_cast<CRTP*>(this));

        m_systemHeapPtr = static_cast<uint32_t>(RoundUpToMultipleOf<x_pageSize>(sizeof(CRTP)));

        m_spdsPageAllocLimit = 0;
    }

    // must be first member
    //
    uintptr_t m_self;

    alignas(64) SpdsAllocImpl<CRTP, false /*isTempAlloc*/> m_executionThreadSpdsAlloc;

    // user heap region grows from low address to high address
    // lowest physically unmapped address of the user heap region (offsets from m_self)
    //
    int64_t m_userHeapPtrLimit;

    // lowest logically available address of the user heap region (offsets from m_self)
    //
    int64_t m_userHeapCurPtr;

    // system heap region grows from low address to high address
    // lowest physically unmapped address of the system heap region (offsets from m_self)
    //
    uint32_t m_systemHeapPtr;

    // lowest logically available address of the system heap region (offsets from m_self)
    //
    uint32_t m_systemHeapCurPtr;

    alignas(64) std::mutex m_spdsAllocationMutex;

    // SPDS region grows from high address to low address
    //
    std::atomic<uint64_t> m_spdsPageFreeList;
    int32_t m_spdsPageAllocLimit;

    alignas(64) SpdsAllocImpl<CRTP, false /*isTempAlloc*/> m_compilerThreadSpdsAlloc;
};

class VM : public VMLayout<VM>
{
public:

};

struct alignas(8) HeapObjectHeader
{
    Type m_type;
    uint8_t m_padding1;
    uint8_t m_padding2;
    uint8_t m_padding3;
    SystemHeapPointer m_structure;
};
static_assert(sizeof(HeapObjectHeader) == 8);

class IRNode
{
public:
    virtual ~IRNode() { }

};

class IRLogicalVariable
{
public:

};

class IRBasicBlock
{
public:
    std::vector<IRNode*> m_nodes;
    std::vector<IRNode*> m_varAtHead;
    std::vector<IRNode*> m_varAvailableAtTail;
};

class IRConstant : public IRNode
{
public:

};

class IRGetLocal : public IRNode
{
public:
    int m_slot;
    IRLogicalVariable* m_vinfo;
};

class IRSetLocal : public IRNode
{
public:
    IRNode* m_value;
    int m_slot;
    IRLogicalVariable* m_vinfo;
};

class IRAdd : public IRNode
{
public:
    IRNode* m_lhs;
    IRNode* m_rhs;
};

class IRReturn : public IRNode
{
public:
    IRNode* m_value;
};

class IRCheckIsConstant : public IRNode
{
public:
    IRNode* m_value;
    TValue m_constant;
};

class BytecodeSlot
{
public:
    constexpr BytecodeSlot() : m_value(x_invalidValue) { }

    static constexpr BytecodeSlot WARN_UNUSED Local(int ord)
    {
        assert(ord >= 0);
        return BytecodeSlot(ord);
    }
    static constexpr BytecodeSlot WARN_UNUSED Constant(int ord)
    {
        assert(ord < 0);
        return BytecodeSlot(ord);
    }

    bool IsInvalid() const { return m_value == x_invalidValue; }
    bool IsLocal() const { assert(!IsInvalid()); return m_value >= 0; }
    bool IsConstant() const { assert(!IsInvalid()); return m_value < 0; }

    int WARN_UNUSED LocalOrd() const { assert(IsLocal()); return m_value; }
    int WARN_UNUSED ConstantOrd() const { assert(IsConstant()); return m_value; }

    explicit operator int() const { return m_value; }

private:
    constexpr BytecodeSlot(int value) : m_value(value) { }

    static constexpr int x_invalidValue = 0x7fffffff;
    int m_value;
};

enum class CodeBlockType
{
    INTERPRETER,
    BASELINE,
    FIRST_LEVEL_OPT
};

class CodeBlock
{
public:
    virtual ~CodeBlock() = default;
    virtual CodeBlockType GetCodeBlockType() const = 0;

};

class GlobalObject;
class alignas(64) CoroutineRuntimeContext
{
public:
    // The constant table of the current function, if interpreter
    //
    uint64_t* m_constants;

    // The global object, if interpreter
    //
    GlobalObject* m_globalObject;

    // slot [m_variadicRetSlotBase + ord] holds variadic return value 'ord'
    //
    uint32_t m_numVariadicRets;
    uint32_t m_variadicRetSlotBegin;

    // The stack object
    //
    uint64_t* m_stackObject;


};

using InterpreterFn = void(*)(CoroutineRuntimeContext* /*rc*/, RestrictPtr<void> /*stackframe*/, ConstRestrictPtr<uint8_t> /*instr*/, uint64_t /*unused*/);

class BaselineCodeBlock;
class FLOCodeBlock;

class InterpreterCodeBlock final : public CodeBlock
{
public:
    virtual CodeBlockType GetCodeBlockType() const override final
    {
        return CodeBlockType::INTERPRETER;
    }

    uint32_t m_stackFrameNumSlots;
    uint32_t m_numFixedArguments;
    uint32_t m_numUpValues;
    uint32_t m_bytecodeLength;

    bool m_hasVariadicArguments;

    // The entry point of the function, can be interpreter or jit
    //
    InterpreterFn m_functionEntryPoint;

    BaselineCodeBlock* m_baselineCodeBlock;
    FLOCodeBlock* m_floCodeBlock;

    uint8_t m_bytecode[0];
};


class FunctionObject : HeapObjectHeader
{
public:
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, FunctionObject>>>
    static InterpreterCodeBlock* ALWAYS_INLINE GetInterpreterCodeBlock(T self)
    {
        return reinterpret_cast<InterpreterCodeBlock*>(self->m_bytecode) - 1;
    }

    uint8_t* m_bytecode;
    TValue m_upValues[0];
};

// stack frame format:
//     [... VarArgs ...] [Header] [... Locals ...]
//                                ^
//                                stack frame pointer (sfp)
//
class alignas(8) StackFrameHeader
{
public:
    // The address of the caller stack frame
    //
    StackFrameHeader* m_caller;
    // The return address
    //
    void* m_retAddr;
    // The function corresponding to this stack frame
    //
    HeapPtr<FunctionObject> m_func;
    // If the function is calling (i.e. not topmost frame), denotes the offset of the bytecode that performed the call
    //
    uint32_t m_callerBytecodeOffset;
    // Total number of variadic arguments passed to the function
    //
    uint32_t m_numVariadicArguments;

    static StackFrameHeader* GetStackFrameHeader(void* sfp)
    {
        return reinterpret_cast<StackFrameHeader*>(sfp) - 1;
    }

    static TValue* GetLocalAddr(void* sfp, BytecodeSlot slot)
    {
        assert(slot.IsLocal());
        int ord = slot.LocalOrd();
        return reinterpret_cast<TValue*>(sfp) + ord;
    }

    static TValue GetLocal(void* sfp, BytecodeSlot slot)
    {
        return *GetLocalAddr(sfp, slot);
    }
};

static_assert(sizeof(StackFrameHeader) % sizeof(TValue) == 0);
static constexpr size_t x_sizeOfStackFrameHeaderInTermsOfTValue = sizeof(StackFrameHeader) / sizeof(TValue);

// The varg part of each inlined function can always
// be represented as a list of locals plus a suffix of the original function's varg
//
class InlinedFunctionVarArgRepresentation
{
public:
    // The prefix ordinals
    //
    std::vector<int> m_prefix;
    // The suffix of the original function's varg beginning at that ordinal (inclusive)
    //
    int m_suffix;
};

class InliningStackEntry
{
public:
    // The base ordinal of stack frame header
    //
    int m_baseOrd;
    // Number of fixed arguments for this function
    //
    int m_numArguments;
    // Number of locals for this function
    //
    int m_numLocals;
    // Varargs of this function
    //
    InlinedFunctionVarArgRepresentation m_varargs;

};

class BytecodeToIRTransformer
{
public:
    // Remap a slot in bytecode to the physical slot for the interpreter/baseline JIT
    //
    void RemapSlot(BytecodeSlot /*slot*/)
    {

    }

    void TransformFunctionImpl(IRBasicBlock* /*bb*/)
    {

    }

    std::vector<InliningStackEntry> m_inlineStack;
};

enum class Opcode
{
    BcReturn,
    BcCall,
    BcAddVV,
    BcSubVV,
    BcIsLTVV,
    X_END_OF_ENUM
};

extern const InterpreterFn x_interpreter_dispatches[static_cast<size_t>(Opcode::X_END_OF_ENUM)];

#define Dispatch(rc, stackframe, instr)                                                                                          \
    do {                                                                                                                         \
        uint8_t dispatch_nextopcode = *reinterpret_cast<const uint8_t*>(instr);                                                  \
        assert(dispatch_nextopcode < static_cast<size_t>(Opcode::X_END_OF_ENUM));                                                \
_Pragma("clang diagnostic push")                                                                                                 \
_Pragma("clang diagnostic ignored \"-Wuninitialized\"")                                                                          \
        uint64_t dispatch_unused;                                                                                                \
        [[clang::musttail]] return x_interpreter_dispatches[dispatch_nextopcode]((rc), (stackframe), (instr), dispatch_unused);  \
_Pragma("clang diagnostic pop")                                                                                                  \
    } while (false)

// The return statement is required to fill nil up to x_minNilFillReturnValues values even if it returns less than that many values
//
constexpr uint32_t x_minNilFillReturnValues = 3;

class BcReturn
{
public:
    uint8_t m_opcode;
    bool m_isVariadicRet;
    uint16_t m_numReturnValues;
    BytecodeSlot m_slotBegin;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcReturn* bc = reinterpret_cast<const BcReturn*>(bcu);
        assert(bc->m_opcode == static_cast<uint8_t>(Opcode::BcReturn));
        assert(bc->m_slotBegin.IsLocal());
        TValue* pbegin = StackFrameHeader::GetLocalAddr(sfp, bc->m_slotBegin);
        uint32_t numRetValues = bc->m_numReturnValues;
        if (bc->m_isVariadicRet)
        {
            assert(rc->m_numVariadicRets != static_cast<uint32_t>(-1));
            TValue* pdst = pbegin + bc->m_numReturnValues;
            TValue* psrc = reinterpret_cast<TValue*>(sfp) + rc->m_variadicRetSlotBegin;
            numRetValues += rc->m_numVariadicRets;
            SafeMemcpy(pdst, psrc, sizeof(TValue) * rc->m_numVariadicRets);
        }
        // No matter we consumed variadic ret or not, it is no longer valid after the return
        //
        DEBUG_ONLY(rc->m_numVariadicRets = static_cast<uint32_t>(-1);)

        // Fill nil up to x_minNilFillReturnValues values
        // TODO: we can also just do a vectorized write
        //
        {
            uint32_t idx = numRetValues;
            while (idx < x_minNilFillReturnValues)
            {
                pbegin[idx] = TValue::CreateMIV(MiscImmediateValue::CreateNil(), TValue::x_mivTag);
                idx++;
            }
        }

        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
        using RetFn = void(*)(CoroutineRuntimeContext* /*rc*/, void* /*sfp*/, uint8_t* /*retValuesStart*/, uint64_t /*numRetValues*/);
        RetFn retAddr = reinterpret_cast<RetFn>(hdr->m_retAddr);
        StackFrameHeader* callerSf = hdr->m_caller;
        [[clang::musttail]] return retAddr(rc, static_cast<void*>(callerSf), reinterpret_cast<uint8_t*>(pbegin), numRetValues);
    }
} __attribute__((__packed__));

class BcCall
{
public:
    uint8_t m_opcode;
    bool m_keepVariadicRet;
    bool m_passVariadicRetAsParam;
    uint32_t m_numFixedParams;
    uint32_t m_numFixedRets;    // only used when m_keepVariadicRet == false
    BytecodeSlot m_funcSlot;   // params are [m_funcSlot + 1, ... m_funcSlot + m_numFixedParams]
    BytecodeSlot m_retSlotBegin;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcCall* bc = reinterpret_cast<const BcCall*>(bcu);
        assert(bc->m_opcode == static_cast<uint8_t>(Opcode::BcCall));
        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
        hdr->m_callerBytecodeOffset = SafeIntegerCast<uint32_t>(reinterpret_cast<const uint8_t*>(bc) - hdr->m_func->m_bytecode);

        assert(bc->m_funcSlot.IsLocal());
        TValue* begin = StackFrameHeader::GetLocalAddr(sfp, bc->m_funcSlot);
        TValue func = *begin;
        begin++;

        if (func.IsPointer(TValue::x_mivTag))
        {
            if (func.AsPointer().As<HeapObjectHeader>()->m_type == Type::FUNCTION)
            {
                HeapPtr<FunctionObject> target = func.AsPointer().As<FunctionObject>();

                TValue* sfEnd = reinterpret_cast<TValue*>(sfp) + FunctionObject::GetInterpreterCodeBlock(hdr->m_func)->m_stackFrameNumSlots;
                TValue* baseForNextFrame = sfEnd + x_sizeOfStackFrameHeaderInTermsOfTValue;

                uint32_t numFixedArgsToPass = bc->m_numFixedParams;
                uint32_t totalArgs = numFixedArgsToPass;
                if (bc->m_passVariadicRetAsParam)
                {
                    totalArgs += rc->m_numVariadicRets;
                }

                uint32_t numCalleeExpectingArgs = FunctionObject::GetInterpreterCodeBlock(target)->m_numFixedArguments;
                bool calleeTakesVarArgs = FunctionObject::GetInterpreterCodeBlock(target)->m_hasVariadicArguments;

                // If the callee takes varargs and it is not empty, set up the varargs
                //
                if (unlikely(calleeTakesVarArgs))
                {
                    uint32_t actualNumVarArgs = 0;
                    if (totalArgs > numCalleeExpectingArgs)
                    {
                        actualNumVarArgs = totalArgs - numCalleeExpectingArgs;
                        baseForNextFrame += actualNumVarArgs;
                    }

                    // First, if we need to pass varret, move the whole varret to the correct position
                    //
                    if (bc->m_passVariadicRetAsParam)
                    {
                        TValue* varRetbegin = reinterpret_cast<TValue*>(sfp) + rc->m_variadicRetSlotBegin;
                        // TODO: over-moving is fine
                        memmove(baseForNextFrame + numFixedArgsToPass, varRetbegin, sizeof(TValue) * rc->m_numVariadicRets);
                    }

                    // Now, copy the fixed args to the correct position
                    //
                    SafeMemcpy(baseForNextFrame, begin, sizeof(TValue) * numFixedArgsToPass);

                    // Now, set up the vararg part
                    //
                    if (totalArgs > numCalleeExpectingArgs)
                    {
                        SafeMemcpy(sfEnd, baseForNextFrame + numCalleeExpectingArgs, sizeof(TValue) * (totalArgs - numCalleeExpectingArgs));
                    }

                    // Finally, set up the numVarArgs field in the frame header
                    //
                    StackFrameHeader* sfh = reinterpret_cast<StackFrameHeader*>(baseForNextFrame) - 1;
                    sfh->m_numVariadicArguments = actualNumVarArgs;
                }
                else
                {
                    // First, if we need to pass varret, move the whole varret to the correct position, up to the number of args the callee accepts
                    //
                    if (bc->m_passVariadicRetAsParam)
                    {
                        if (numCalleeExpectingArgs > numFixedArgsToPass)
                        {
                            TValue* varRetbegin = reinterpret_cast<TValue*>(sfp) + rc->m_variadicRetSlotBegin;
                            // TODO: over-moving is fine
                            memmove(baseForNextFrame + numFixedArgsToPass, varRetbegin, sizeof(TValue) * std::min(rc->m_numVariadicRets, numCalleeExpectingArgs - numFixedArgsToPass));
                        }
                    }

                    // Now, copy the fixed args to the correct position, up to the number of args the callee accepts
                    //
                    SafeMemcpy(baseForNextFrame, begin, sizeof(TValue) * std::min(numFixedArgsToPass, numCalleeExpectingArgs));
                }

                // Finally, pad in nils if necessary
                //
                if (totalArgs < numCalleeExpectingArgs)
                {
                    TValue* p = baseForNextFrame + totalArgs;
                    TValue* end = baseForNextFrame + numCalleeExpectingArgs;
                    while (p < end)
                    {
                        *p = TValue::CreateMIV(MiscImmediateValue::CreateNil(), TValue::x_mivTag);
                        p++;
                    }
                }

                // Set up the stack frame header
                //
                StackFrameHeader* sfh = reinterpret_cast<StackFrameHeader*>(baseForNextFrame) - 1;
                sfh->m_caller = hdr;
                sfh->m_retAddr = reinterpret_cast<void*>(OnReturn);
                sfh->m_func = target;

                _Pragma("clang diagnostic push")
                _Pragma("clang diagnostic ignored \"-Wuninitialized\"")
                uint64_t unused;
                [[clang::musttail]] return FunctionObject::GetInterpreterCodeBlock(target)->m_functionEntryPoint(rc, reinterpret_cast<RestrictPtr<void>>(baseForNextFrame), target->m_bytecode, unused);
                _Pragma("clang diagnostic pop")
            }
        }
        else
        {
            assert(false && "unimplemented");
        }
    }

    static void OnReturn(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> retValuesU, uint64_t numRetValues)
    {
        const TValue* retValues = reinterpret_cast<const TValue*>(retValuesU);
        StackFrameHeader* hdr = reinterpret_cast<StackFrameHeader*>(stackframe);
        ConstRestrictPtr<uint8_t> bcu = hdr->m_func->m_bytecode + hdr->m_callerBytecodeOffset;
        const BcCall* bc = reinterpret_cast<const BcCall*>(bcu);
        assert(static_cast<Opcode>(bc->m_opcode) == Opcode::BcCall);
        if (bc->m_keepVariadicRet)
        {
            rc->m_numVariadicRets = SafeIntegerCast<uint32_t>(numRetValues);
            rc->m_variadicRetSlotBegin = SafeIntegerCast<uint32_t>(retValues - reinterpret_cast<TValue*>(stackframe));
        }
        else
        {
            if (bc->m_numFixedRets <= x_minNilFillReturnValues)
            {
                SafeMemcpy(StackFrameHeader::GetLocalAddr(stackframe, bc->m_funcSlot), retValues, sizeof(TValue) * bc->m_numFixedRets);
            }
            else
            {
                TValue* dst = StackFrameHeader::GetLocalAddr(stackframe, bc->m_funcSlot);
                if (numRetValues < bc->m_numFixedRets)
                {
                    SafeMemcpy(dst, retValues, sizeof(TValue) * numRetValues);
                    while (numRetValues < bc->m_numFixedRets)
                    {
                        dst[numRetValues] = TValue::CreateMIV(MiscImmediateValue::CreateNil(), TValue::x_mivTag);
                        numRetValues++;
                    }
                }
                else
                {
                    SafeMemcpy(dst, retValues, sizeof(TValue) * bc->m_numFixedRets);
                }
            }
        }
        Dispatch(rc, stackframe, bcu + sizeof(BcCall));
    }
} __attribute__((__packed__));

class BcAddVV
{
public:
    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    BytecodeSlot m_result;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcAddVV* bc = reinterpret_cast<const BcAddVV*>(bcu);
        assert(bc->m_opcode == static_cast<uint8_t>(Opcode::BcAddVV));
        TValue lhs = StackFrameHeader::GetLocal(stackframe, bc->m_lhs);
        TValue rhs = StackFrameHeader::GetLocal(stackframe, bc->m_rhs);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(lhs.AsDouble() + rhs.AsDouble());
            Dispatch(rc, stackframe, bcu + sizeof(BcAddVV));
        }
        else
        {
            assert(false && "unimplemented");
        }
    }
} __attribute__((__packed__));

class BcSubVV
{
public:
    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    BytecodeSlot m_result;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcSubVV* bc = reinterpret_cast<const BcSubVV*>(bcu);
        assert(bc->m_opcode == static_cast<uint8_t>(Opcode::BcSubVV));
        TValue lhs = StackFrameHeader::GetLocal(stackframe, bc->m_lhs);
        TValue rhs = StackFrameHeader::GetLocal(stackframe, bc->m_rhs);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(lhs.AsDouble() - rhs.AsDouble());
            Dispatch(rc, stackframe, bcu + sizeof(BcSubVV));
        }
        else
        {
            assert(false && "unimplemented");
        }
    }
} __attribute__((__packed__));

class BcIsLTVV
{
public:
    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    int32_t m_offset;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcIsLTVV* bc = reinterpret_cast<const BcIsLTVV*>(bcu);
        assert(bc->m_opcode == static_cast<uint8_t>(Opcode::BcIsLTVV));
        TValue lhs = StackFrameHeader::GetLocal(stackframe, bc->m_lhs);
        TValue rhs = StackFrameHeader::GetLocal(stackframe, bc->m_rhs);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            if (lhs.AsDouble() < rhs.AsDouble())
            {
                Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
            }
            else
            {
                Dispatch(rc, stackframe, bcu + sizeof(BcIsLTVV));
            }
        }
        else
        {
            assert(false && "unimplemented");
        }
    }
} __attribute__((__packed__));

}   // namespace ToyLang
