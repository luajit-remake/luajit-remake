#include "vm.h"
#include "runtime_utils.h"
#include "deegen_options.h"

void InitializeDfgAllocationArenaIfNeeded();

VM* WARN_UNUSED VM::Create()
{
    if (x_allow_baseline_jit_tier_up_to_optimizing_jit)
    {
        InitializeDfgAllocationArenaIfNeeded();
    }

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
    constexpr size_t sizeToMap = RoundUpToMultipleOf<x_pageSize>(sizeof(VM));
    {
        void* r = mmap(vmVoid, sizeToMap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_FIXED, -1, 0);
        CHECK_LOG_ERROR_WITH_ERRNO(r != MAP_FAILED, "Failed to allocate VM struct");
        TestAssert(vmVoid == r);
    }

    VM* vm = new (vmVoid) VM();
    assert(vm == vmVoid);
    Auto(
        if (!success)
        {
            vm->~VM();
        }
    );

    CHECK_LOG_ERROR(vm->Initialize());
    Auto(
        if (!success)
        {
            vm->Cleanup();
        }
    );

    success = true;
    return vm;
}

void VM::Destroy()
{
    VM* ptr = this;
    ptr->Cleanup();
    ptr->~VM();

    void* unmapAddr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptr) - x_vmBaseOffset);
    int r = munmap(unmapAddr, x_vmLayoutLength);
    LOG_WARNING_WITH_ERRNO_IF(r != 0, "Cannot unmap VM");
}

bool WARN_UNUSED VM::InitializeVMBase()
{
    // I'm not sure if there's any place where we expect the VM struct to not have a vtable,
    // but there is no reason it needs to have one any way
    //
    static_assert(!std::is_polymorphic_v<VM>, "must be not polymorphic");

    m_self = reinterpret_cast<uintptr_t>(this);

    SetUpSegmentationRegister();

    m_isEngineStartingTierBaselineJit = false;
    m_engineMaxTier = EngineMaxTier::Unrestricted;

    m_userHeapPtrLimit = -static_cast<int64_t>(x_vmBaseOffset - x_vmUserHeapSize);
    m_userHeapCurPtr = -static_cast<int64_t>(x_vmBaseOffset - x_vmUserHeapSize);

    static_assert(sizeof(VM) >= x_minimum_valid_heap_address);
    m_systemHeapPtrLimit = static_cast<uint32_t>(RoundUpToMultipleOf<x_pageSize>(sizeof(VM)));
    m_systemHeapCurPtr = sizeof(VM);

    m_spdsPageFreeList.store(static_cast<uint64_t>(x_spdsAllocationPageSize));
    m_spdsPageAllocLimit = -static_cast<int32_t>(x_pageSize);

    m_executionThreadSpdsAlloc.SetHost(this);
    m_compilerThreadSpdsAlloc.SetHost(this);

    for (size_t i = 0; i < x_numSpdsAllocatableClassNotUsingLfFreelist; i++)
    {
        m_spdsCompilerThreadFreeList[i] = SpdsPtr<void> { 0 };
    }

    for (size_t i = 0; i < x_numSpdsAllocatableClassNotUsingLfFreelist; i++)
    {
        m_spdsExecutionThreadFreeList[i] = SpdsPtr<void> { 0 };
    }

    m_totalBaselineJitCompilations = 0;

    return true;
}

void __attribute__((__preserve_most__)) VM::BumpUserHeap()
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

void VM::BumpSystemHeap()
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

int32_t WARN_UNUSED VM::SpdsAllocatePageSlowPath()
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

int32_t WARN_UNUSED VM::SpdsAllocatePageSlowPathImpl()
{
    constexpr int32_t x_allocationSize = 65536;

    // Compute how much memory we should allocate
    // We allocate 4K, 8K, 16K, 32K first (the highest 4K is not used to prevent all kinds of overflowing issue)
    // After that we allocate 64K each time
    //
    assert(m_spdsPageAllocLimit % static_cast<int32_t>(x_pageSize) == 0 && m_spdsPageAllocLimit % static_cast<int32_t>(x_spdsAllocationPageSize) == 0);
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
    int32_t result = m_spdsPageAllocLimit + static_cast<int32_t>(x_spdsAllocationPageSize);

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
        assert(lastPage == m_spdsPageAllocLimit + static_cast<int32_t>(lengthToAllocate));

        SpdsPutAllocatedPagesToFreeList(firstPage, lastPage);
    }
    return result;
}

bool WARN_UNUSED VM::InitializeVMGlobalData()
{
    for (size_t i = 0; i < x_totalLuaMetamethodKind; i++)
    {
        m_stringNameForMetatableKind[i] = CreateStringObjectFromRawString(x_luaMetatableStringName[i], static_cast<uint32_t>(std::char_traits<char>::length(x_luaMetatableStringName[i])));
        assert(m_stringNameForMetatableKind[i].As()->m_hashHigh == x_luaMetamethodHashes[i]);
        assert(GetMetamethodOrdinalFromStringName(TranslateToRawPointer(m_stringNameForMetatableKind[i].As())) == static_cast<int>(i));
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

    m_emptyString = nullptr;
    m_toStringString.m_value = 0;
    m_stringNameForToStringMetamethod.m_value = 0;
    m_initialHiddenClassOfMetatableForString.m_value = 0;

    m_usrPRNG = nullptr;

    CreateRootCoroutine();
    return true;
}

bool WARN_UNUSED VM::InitializeVMStringManager()
{
    static constexpr uint32_t x_initialSize = 1024;
    m_hashTable = new (std::nothrow) GeneralHeapPointer<HeapString>[x_initialSize];
    CHECK_LOG_ERROR(m_hashTable != nullptr, "Failed to allocate space for initial hash table");

    static_assert(x_stringConserHtNonexistentValue == 0, "required for memset");
    memset(m_hashTable, 0, sizeof(GeneralHeapPointer<HeapString>) * x_initialSize);

    m_hashTableSizeMask = x_initialSize - 1;
    m_elementCount = 0;

    // Create a special key used as an exotic index into the table
    //
    // The content of the string and its hash value doesn't matter,
    // because we don't put this string into the global hash table thus it will never be found by others.
    //
    // We give it some content for debug purpose, but, we give it a fake hash value, to avoids unnecessary
    // collision with the real string of that value in the Structure's hash table.
    //
    auto createSpecialKey = [&](const char* debugName, uint64_t fakeHash) -> UserHeapPointer<HeapString>
    {
        StringLengthAndHash slah {
            .m_length = strlen(debugName),
            .m_hashValue = fakeHash
        };

        size_t allocationLength = HeapString::ComputeAllocationLengthForString(slah.m_length);
        HeapPtrTranslator translator = GetHeapPtrTranslator();
        UserHeapPointer<void> uhp = AllocFromUserHeap(static_cast<uint32_t>(allocationLength));

        HeapString* ptr = translator.TranslateToRawPtr(uhp.AsNoAssert<HeapString>());

        ptr->PopulateHeader(slah);
        // Copy the trailing '\0' as well
        //
        memcpy(ptr->m_string, debugName, slah.m_length + 1);

        return uhp.As<HeapString>();
    };

    // Create the special key used as the key for metatable slot in PolyMetatable mode
    //
    m_specialKeyForMetatableSlot = createSpecialKey("(hidden_mt_tbl)" /*debugName*/, 0x1F2E3D4C5B6A798LL /*specialHash*/);

    // Create the special keys for 'false' and 'true' index
    //
    m_specialKeyForBooleanIndex[0] = createSpecialKey("(hidden_false)" /*debugName*/, 0x897A6B5C4D3E2F1LL /*specialHash*/);
    m_specialKeyForBooleanIndex[1] = createSpecialKey("(hidden_true)" /*debugName*/, 0xC5B4D6A3E792F81LL /*specialHash*/);
    return true;
}

void VM::CleanupVMStringManager()
{
    if (m_hashTable != nullptr)
    {
        delete [] m_hashTable;
    }
}

bool WARN_UNUSED VM::Initialize()
{
    static_assert(x_segmentRegisterSelfReferencingOffset == offsetof_member_v<&VM::m_self>);

    bool success = false;
    CHECK_LOG_ERROR(InitializeVMBase());
    CHECK_LOG_ERROR(InitializeVMStringManager());
    Auto(if (!success) CleanupVMStringManager());
    CHECK_LOG_ERROR(InitializeVMGlobalData());
    success = true;
    return true;
}

void VM::Cleanup()
{
    CleanupVMStringManager();
}

namespace {

// Compare if 's' is equal to the abstract multi-piece string represented by 'iterator'
//
// The iterator should provide two methods:
// (1) bool HasMore() returns true if it has not yet reached the end
// (2) std::pair<const void*, uint32_t> GetAndAdvance() returns the current string piece and advance the iterator
//
template<typename Iterator>
bool WARN_UNUSED ALWAYS_INLINE CompareMultiPieceStringEqual(Iterator iterator, const HeapString* s)
{
    uint32_t length = s->m_length;
    const uint8_t* ptr = s->m_string;
    while (iterator.HasMore())
    {
        const void* curStr;
        uint32_t curLen;
        std::tie(curStr, curLen) = iterator.GetAndAdvance();

        if (curLen > length)
        {
            return false;
        }
        if (memcmp(ptr, curStr, curLen) != 0)
        {
            return false;
        }
        ptr += curLen;
        length -= curLen;
    }
    return length == 0;
}

template<typename Iterator>
HeapString* WARN_UNUSED ALWAYS_INLINE MaterializeMultiPieceString(VM* vm, Iterator iterator, StringLengthAndHash slah)
{
    size_t allocationLength = HeapString::ComputeAllocationLengthForString(slah.m_length);
    VM_FAIL_IF(!IntegerCanBeRepresentedIn<uint32_t>(allocationLength),
               "Cannot create a string longer than 4GB (attempted length: %llu bytes).", static_cast<unsigned long long>(allocationLength));

    HeapPtrTranslator translator = vm->GetHeapPtrTranslator();
    UserHeapPointer<void> uhp = vm->AllocFromUserHeap(static_cast<uint32_t>(allocationLength));

    HeapString* ptr = translator.TranslateToRawPtr(uhp.AsNoAssert<HeapString>());
    ptr->PopulateHeader(slah);

    uint8_t* curDst = ptr->m_string;
    while (iterator.HasMore())
    {
        const void* curStr;
        uint32_t curLen;
        std::tie(curStr, curLen) = iterator.GetAndAdvance();

        SafeMemcpy(curDst, curStr, curLen);
        curDst += curLen;
    }

    // Fill in the trailing '\0', as required by Lua
    // Note that ComputeAllocationLengthForString has already automatically reserved space for this '\0'
    //
    *curDst = 0;

    // Assert that the provided length and hash value matches reality
    //
    assert(curDst - ptr->m_string == static_cast<intptr_t>(slah.m_length));
    assert(HashString(ptr->m_string, ptr->m_length) == slah.m_hashValue);
    return ptr;
}

}   // anonymous namespace

void VM::ReinsertDueToResize(GeneralHeapPointer<HeapString>* hashTable, uint32_t hashTableSizeMask, GeneralHeapPointer<HeapString> e)
{
    uint32_t slot = e.As<HeapString>()->m_hashLow & hashTableSizeMask;
    while (hashTable[slot].m_value != x_stringConserHtNonexistentValue)
    {
        slot = (slot + 1) & hashTableSizeMask;
    }
    hashTable[slot] = e;
}

void VM::ExpandStringConserHashTableIfNeeded()
{
    if (likely(m_elementCount <= (m_hashTableSizeMask >> x_stringht_loadfactor_denominator_shift) * x_stringht_loadfactor_numerator))
    {
        return;
    }

    assert(m_hashTable != nullptr && is_power_of_2(m_hashTableSizeMask + 1));
    VM_FAIL_IF(m_hashTableSizeMask >= (1U << 29),
               "Global string hash table has grown beyond 2^30 slots");
    uint32_t newSize = (m_hashTableSizeMask + 1) * 2;
    uint32_t newMask = newSize - 1;
    GeneralHeapPointer<HeapString>* newHt = new (std::nothrow) GeneralHeapPointer<HeapString>[newSize];
    VM_FAIL_IF(newHt == nullptr,
               "Out of memory, failed to resize global string hash table to size %u", static_cast<unsigned>(newSize));

    static_assert(x_stringConserHtNonexistentValue == 0, "we are relying on this to do memset");
    memset(newHt, 0, sizeof(GeneralHeapPointer<HeapString>) * newSize);

    GeneralHeapPointer<HeapString>* cur = m_hashTable;
    GeneralHeapPointer<HeapString>* end = m_hashTable + m_hashTableSizeMask + 1;
    while (cur < end)
    {
        if (!StringHtCellValueIsNonExistentOrDeleted(cur->m_value))
        {
            ReinsertDueToResize(newHt, newMask, *cur);
        }
        cur++;
    }
    delete [] m_hashTable;
    m_hashTable = newHt;
    m_hashTableSizeMask = newMask;
}

// Insert an abstract multi-piece string into the hash table if it does not exist
// Return the HeapString
//
template<typename Iterator>
UserHeapPointer<HeapString> WARN_UNUSED VM::InsertMultiPieceString(Iterator iterator)
{
    HeapPtrTranslator translator = GetHeapPtrTranslator();

    StringLengthAndHash lenAndHash = HashMultiPieceString(iterator);
    uint64_t hash = lenAndHash.m_hashValue;
    size_t length = lenAndHash.m_length;
    uint8_t expectedHashHigh = static_cast<uint8_t>(hash >> 56);
    uint32_t expectedHashLow = BitwiseTruncateTo<uint32_t>(hash);

    uint32_t slotForInsertion = static_cast<uint32_t>(-1);
    uint32_t slot = static_cast<uint32_t>(hash) & m_hashTableSizeMask;
    while (true)
    {
        {
            GeneralHeapPointer<HeapString> ptr = m_hashTable[slot];
            if (StringHtCellValueIsNonExistentOrDeleted(ptr))
            {
                // If this string turns out to be non-existent, this can be a slot to insert the string
                //
                if (slotForInsertion == static_cast<uint32_t>(-1))
                {
                    slotForInsertion = slot;
                }
                if (StringHtCellValueIsNonExistent(ptr))
                {
                    break;
                }
                else
                {
                    goto next_slot;
                }
            }

            HeapPtr<HeapString> s = ptr.As<HeapString>();
            if (s->m_hashHigh != expectedHashHigh || s->m_hashLow != expectedHashLow || s->m_length != length)
            {
                goto next_slot;
            }

            HeapString* rawPtr = translator.TranslateToRawPtr(s);
            if (!CompareMultiPieceStringEqual(iterator, rawPtr))
            {
                goto next_slot;
            }

            // We found the string
            //
            return translator.TranslateToUserHeapPtr(rawPtr);
        }
next_slot:
        slot = (slot + 1) & m_hashTableSizeMask;
    }

    // The string is not found, insert it into the hash table
    //
    assert(slotForInsertion != static_cast<uint32_t>(-1));
    assert(StringHtCellValueIsNonExistentOrDeleted(m_hashTable[slotForInsertion]));

    m_elementCount++;
    HeapString* element = MaterializeMultiPieceString(this, iterator, lenAndHash);
    m_hashTable[slotForInsertion] = translator.TranslateToGeneralHeapPtr(element);

    ExpandStringConserHashTableIfNeeded();

    return translator.TranslateToUserHeapPtr(element);
}

UserHeapPointer<HeapString> WARN_UNUSED VM::CreateStringObjectFromConcatenation(TValue* start, size_t len)
{
#ifndef NDEBUG
    for (size_t i = 0; i < len; i++)
    {
        assert(start[i].IsPointer());
        assert(start[i].AsPointer().As<UserHeapGcObjectHeader>()->m_type == HeapEntityType::String);
    }
#endif
    struct Iterator
    {
        bool HasMore()
        {
            return m_cur < m_end;
        }

        std::pair<const uint8_t*, uint32_t> GetAndAdvance()
        {
            assert(m_cur < m_end);
            HeapString* e = m_translator.TranslateToRawPtr(m_cur->AsPointer().As<HeapString>());
            m_cur++;
            return std::make_pair(static_cast<const uint8_t*>(e->m_string), e->m_length);
        }

        TValue* m_cur;
        TValue* m_end;
        HeapPtrTranslator m_translator;
    };

    return InsertMultiPieceString(Iterator {
        .m_cur = start,
        .m_end = start + len,
        .m_translator = GetHeapPtrTranslator()
    });
}

UserHeapPointer<HeapString> WARN_UNUSED VM::CreateStringObjectFromConcatenation(std::pair<const void*, size_t>* start, size_t len)
{
    struct Iterator
    {
        bool HasMore()
        {
            return m_cur < m_end;
        }

        std::pair<const uint8_t*, uint32_t> GetAndAdvance()
        {
            assert(m_cur < m_end);
            const uint8_t* ptr = reinterpret_cast<const uint8_t*>(m_cur->first);
            uint32_t len = static_cast<uint32_t>(m_cur->second);
            m_cur++;
            return std::make_pair(ptr, len);
        }

        std::pair<const void*, size_t>* m_cur;
        std::pair<const void*, size_t>* m_end;
    };

    return InsertMultiPieceString(Iterator {
        .m_cur = start,
        .m_end = start + len
    });
}

UserHeapPointer<HeapString> WARN_UNUSED VM::CreateStringObjectFromConcatenation(UserHeapPointer<HeapString> str1, TValue* start, size_t len)
{
#ifndef NDEBUG
    assert(str1.As()->m_type == HeapEntityType::String);
    for (size_t i = 0; i < len; i++)
    {
        assert(start[i].IsPointer());
        assert(start[i].AsPointer().As<UserHeapGcObjectHeader>()->m_type == HeapEntityType::String);
    }
#endif

    struct Iterator
    {
        Iterator(UserHeapPointer<HeapString> str1, TValue* start, size_t len, HeapPtrTranslator translator)
            : m_isFirst(true)
            , m_firstString(str1)
            , m_cur(start)
            , m_end(start + len)
            , m_translator(translator)
        { }

        bool HasMore()
        {
            return m_isFirst || m_cur < m_end;
        }

        std::pair<const uint8_t*, uint32_t> GetAndAdvance()
        {
            HeapString* e;
            if (m_isFirst)
            {
                m_isFirst = false;
                e = m_translator.TranslateToRawPtr(m_firstString.As<HeapString>());
            }
            else
            {
                assert(m_cur < m_end);
                e = m_translator.TranslateToRawPtr(m_cur->AsPointer().As<HeapString>());
                m_cur++;
            }
            return std::make_pair(static_cast<const uint8_t*>(e->m_string), e->m_length);
        }

        bool m_isFirst;
        UserHeapPointer<HeapString> m_firstString;
        TValue* m_cur;
        TValue* m_end;
        HeapPtrTranslator m_translator;
    };

    return InsertMultiPieceString(Iterator(str1, start, len, GetHeapPtrTranslator()));
}

UserHeapPointer<HeapString> WARN_UNUSED VM::CreateStringObjectFromRawString(const void* str, uint32_t len)
{
    struct Iterator
    {
        Iterator(const void* str, uint32_t len)
            : m_str(str)
            , m_len(len)
            , m_isFirst(true)
        { }

        bool HasMore()
        {
            return m_isFirst;
        }

        std::pair<const void*, uint32_t> GetAndAdvance()
        {
            assert(m_isFirst);
            m_isFirst = false;
            return std::make_pair(m_str, m_len);
        }

        const void* m_str;
        uint32_t m_len;
        bool m_isFirst;
    };

    return InsertMultiPieceString(Iterator(str, len));
}

UserHeapPointer<HeapString> WARN_UNUSED VM::CreateStringObjectFromConcatenationOfSameString(const char* inputStringPtr, uint32_t inputStringLen, size_t n)
{
    if (unlikely(inputStringLen == 0 || n == 0))
    {
        return VM_GetEmptyString();
    }

    // Gracefully handle overflow edge case. Note that we cannot compute n*inputStringLen before
    // validating that it won't overflow, which is why we have two checks below.
    //
    VM_FAIL_IF(n >= std::numeric_limits<uint32_t>::max(),
               "Cannot create a string longer than 4GB (attempted length: %llu*%llu bytes).",
               static_cast<unsigned long long>(n), static_cast<unsigned long long>(inputStringLen));

    VM_FAIL_IF(n * inputStringLen >= std::numeric_limits<uint32_t>::max(),
               "Cannot create a string longer than 4GB (attempted length: %llu bytes).", static_cast<unsigned long long>(n * inputStringLen));

    // If we are making a lot of copies of small strings (which is actually the common case),
    // try to pre-coalesce them together to reduce the # of memcpy calls (and XXH streaming hash updates).
    //
    constexpr size_t x_directLimit = 2200;
    uint64_t buffer64[x_directLimit / sizeof(uint64_t) + 1];
    uint8_t* buf = reinterpret_cast<uint8_t*>(buffer64);

    uint8_t* coalescedStringPtr;
    uint32_t coalescedStringLen;

    if (inputStringLen == 1)
    {
        // For length == 1 input string, we can simply create a coalesced string by memset
        //
        coalescedStringPtr = buf;
        coalescedStringLen = static_cast<uint32_t>(std::min(x_directLimit, n));
        memset(buf, inputStringPtr[0], coalescedStringLen);
    }
    else if (n >= 4 && inputStringLen * 4 <= x_directLimit)
    {
        // Otherwise, we create a coalesced string by doubling until we reach the limit, if beneficial
        //
        coalescedStringPtr = buf;
        coalescedStringLen = inputStringLen;
        size_t numCopiesInCoalescedString = 1;
        memcpy(buf, inputStringPtr, inputStringLen);
        while (coalescedStringLen * 2 <= x_directLimit && numCopiesInCoalescedString * 2 <= n)
        {
            memcpy(buf + coalescedStringLen, buf, coalescedStringLen);
            coalescedStringLen *= 2;
            numCopiesInCoalescedString *= 2;
        }
    }
    else
    {
        // It is not beneficial or possible to create the coalesced string.
        //
        coalescedStringPtr = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(inputStringPtr));
        coalescedStringLen = inputStringLen;
    }

    assert(coalescedStringLen > 0 && coalescedStringLen % inputStringLen == 0);
#ifndef NDEBUG
    for (size_t i = 0; i < coalescedStringLen; i++)
    {
        assert(coalescedStringPtr[i] == static_cast<uint8_t>(inputStringPtr[i % inputStringLen]));
    }
#endif

    struct Iterator
    {
        Iterator(const uint8_t* ptr, uint32_t len, uint32_t totalLen)
            : m_ptr(ptr)
            , m_len(len)
            , m_totalLen(totalLen)
        { }

        bool HasMore()
        {
            return m_totalLen > 0;
        }

        std::pair<const void*, uint32_t> GetAndAdvance()
        {
            assert(m_totalLen > 0);
            uint32_t consume = std::min(m_len, m_totalLen);
            m_totalLen -= consume;
            return std::make_pair(m_ptr, consume);
        }

        const uint8_t* m_ptr;
        uint32_t m_len;
        uint32_t m_totalLen;
    };

    return InsertMultiPieceString(Iterator(coalescedStringPtr, coalescedStringLen, static_cast<uint32_t>(n * inputStringLen)));
}

void VM::CreateRootCoroutine()
{
    // Create global object
    //
    UserHeapPointer<TableObject> globalObject = CreateGlobalObject(this);
    m_rootCoroutine = CoroutineRuntimeContext::Create(this, globalObject, CoroutineRuntimeContext::x_rootCoroutineDefaultStackSlots);
    m_rootCoroutine->m_coroutineStatus.SetResumable(false);
    m_rootCoroutine->m_parent = nullptr;
}

TableObject* VM::GetRootGlobalObject()
{
    return TranslateToRawPointer(m_rootCoroutine->m_globalObject.As());
}
