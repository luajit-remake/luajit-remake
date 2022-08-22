#pragma once

#include "memory_ptr.h"
#include "heap_object_common.h"
#include "tvalue.h"

class alignas(8) HeapString
{
public:
    static constexpr uint32_t x_stringStructure = 0x8;
    // Common object header
    //
    uint32_t m_structure;   // always x_StringStructure
    HeapEntityType m_type;            // always TypeEnumForHeapObject<HeapString>
    GcCellState m_cellState;

    // This is the high 16 bits of the XXHash64 value, for quick comparison
    //
    uint16_t m_hashHigh;
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
        m_structure = x_stringStructure;
        m_type = TypeEnumForHeapObject<HeapString>;
        m_cellState = x_defaultCellState;

        m_hashHigh = static_cast<uint16_t>(slah.m_hashValue >> 48);
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
};
static_assert(sizeof(HeapString) == 16);

// In Lua all strings are hash-consed
//
template<typename CRTP>
class GlobalStringHashConser
{
private:
    // The hash table stores GeneralHeapPointer
    // We know that they must be UserHeapPointer, so the below values should never appear as valid values
    //
    static constexpr int32_t x_nonexistentValue = 0;
    static constexpr int32_t x_deletedValue = 4;

    static bool WARN_UNUSED IsNonExistentOrDeleted(GeneralHeapPointer<HeapString> ptr)
    {
        AssertIff(ptr.m_value >= 0, ptr.m_value == x_nonexistentValue || ptr.m_value == x_deletedValue);
        return ptr.m_value >= 0;
    }

    static bool WARN_UNUSED IsNonExistent(GeneralHeapPointer<HeapString> ptr)
    {
        return ptr.m_value == x_nonexistentValue;
    }

    // max load factor is x_loadfactor_numerator / (2^x_loadfactor_denominator_shift)
    //
    static constexpr uint32_t x_loadfactor_denominator_shift = 1;
    static constexpr uint32_t x_loadfactor_numerator = 1;

    // Compare if 's' is equal to the abstract multi-piece string represented by 'iterator'
    //
    // The iterator should provide two methods:
    // (1) bool HasMore() returns true if it has not yet reached the end
    // (2) std::pair<const void*, uint32_t> GetAndAdvance() returns the current string piece and advance the iterator
    //
    template<typename Iterator>
    static bool WARN_UNUSED CompareMultiPieceStringEqual(Iterator iterator, const HeapString* s)
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
    HeapString* WARN_UNUSED MaterializeMultiPieceString(Iterator iterator, StringLengthAndHash slah)
    {
        size_t allocationLength = HeapString::ComputeAllocationLengthForString(slah.m_length);
        VM_FAIL_IF(!IntegerCanBeRepresentedIn<uint32_t>(allocationLength),
            "Cannot create a string longer than 4GB (attempted length: %llu bytes).", static_cast<unsigned long long>(allocationLength));

        HeapPtrTranslator translator = static_cast<CRTP*>(this)->GetHeapPtrTranslator();
        UserHeapPointer<void> uhp = static_cast<CRTP*>(this)->AllocFromUserHeap(static_cast<uint32_t>(allocationLength));

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

    static void ReinsertDueToResize(GeneralHeapPointer<HeapString>* hashTable, uint32_t hashTableSizeMask, GeneralHeapPointer<HeapString> e)
    {
        uint32_t slot = e.As<HeapString>()->m_hashLow & hashTableSizeMask;
        while (hashTable[slot].m_value != x_nonexistentValue)
        {
            slot = (slot + 1) & hashTableSizeMask;
        }
        hashTable[slot] = e;
    }

    // TODO: when we have GC thread we need to figure out how this interacts with GC
    //
    void ExpandHashTableIfNeeded()
    {
        if (likely(m_elementCount <= (m_hashTableSizeMask >> x_loadfactor_denominator_shift) * x_loadfactor_numerator))
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

        static_assert(x_nonexistentValue == 0, "we are relying on this to do memset");
        memset(newHt, 0, sizeof(GeneralHeapPointer<HeapString>) * newSize);

        GeneralHeapPointer<HeapString>* cur = m_hashTable;
        GeneralHeapPointer<HeapString>* end = m_hashTable + m_hashTableSizeMask + 1;
        while (cur < end)
        {
            if (!IsNonExistentOrDeleted(cur->m_value))
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
    UserHeapPointer<HeapString> WARN_UNUSED InsertMultiPieceString(Iterator iterator)
    {
        HeapPtrTranslator translator = static_cast<CRTP*>(this)->GetHeapPtrTranslator();

        StringLengthAndHash lenAndHash = HashMultiPieceString(iterator);
        uint64_t hash = lenAndHash.m_hashValue;
        size_t length = lenAndHash.m_length;
        uint16_t expectedHashHigh = static_cast<uint16_t>(hash >> 48);
        uint32_t expectedHashLow = BitwiseTruncateTo<uint32_t>(hash);

        uint32_t slotForInsertion = static_cast<uint32_t>(-1);
        uint32_t slot = static_cast<uint32_t>(hash) & m_hashTableSizeMask;
        while (true)
        {
            {
                GeneralHeapPointer<HeapString> ptr = m_hashTable[slot];
                if (IsNonExistentOrDeleted(ptr))
                {
                    // If this string turns out to be non-existent, this can be a slot to insert the string
                    //
                    if (slotForInsertion == static_cast<uint32_t>(-1))
                    {
                        slotForInsertion = slot;
                    }
                    if (IsNonExistent(ptr))
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
        assert(IsNonExistentOrDeleted(m_hashTable[slotForInsertion]));

        m_elementCount++;
        HeapString* element = MaterializeMultiPieceString(iterator, lenAndHash);
        m_hashTable[slotForInsertion] = translator.TranslateToGeneralHeapPtr(element);

        ExpandHashTableIfNeeded();

        return translator.TranslateToUserHeapPtr(element);
    }

public:
    // Create a string by concatenating start[0] ~ start[len-1]
    // Each TValue must be a string
    //
    UserHeapPointer<HeapString> WARN_UNUSED CreateStringObjectFromConcatenation(TValue* start, size_t len)
    {
#ifndef NDEBUG
        for (size_t i = 0; i < len; i++)
        {
            assert(start[i].IsPointer(TValue::x_mivTag));
            assert(start[i].AsPointer().As<UserHeapGcObjectHeader>()->m_type == HeapEntityType::STRING);
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
            .m_translator = static_cast<CRTP*>(this)->GetHeapPtrTranslator()
        });
    }

    // Create a string by concatenating str1 .. start[0] ~ start[len-1]
    // str1 and each TValue must be a string
    //
    UserHeapPointer<HeapString> WARN_UNUSED CreateStringObjectFromConcatenation(UserHeapPointer<HeapString> str1, TValue* start, size_t len)
    {
#ifndef NDEBUG
        assert(str1.As()->m_type == HeapEntityType::STRING);
        for (size_t i = 0; i < len; i++)
        {
            assert(start[i].IsPointer(TValue::x_mivTag));
            assert(start[i].AsPointer().As<UserHeapGcObjectHeader>()->m_type == HeapEntityType::STRING);
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

        return InsertMultiPieceString(Iterator(str1, start, len, static_cast<CRTP*>(this)->GetHeapPtrTranslator()));
    }

    UserHeapPointer<HeapString> WARN_UNUSED CreateStringObjectFromRawString(const void* str, uint32_t len)
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

    uint32_t GetGlobalStringHashConserCurrentHashTableSize() const
    {
        return m_hashTableSizeMask + 1;
    }

    uint32_t GetGlobalStringHashConserCurrentElementCount() const
    {
        return m_elementCount;
    }

    bool WARN_UNUSED Initialize()
    {
        static constexpr uint32_t x_initialSize = 1024;
        m_hashTable = new (std::nothrow) GeneralHeapPointer<HeapString>[x_initialSize];
        CHECK_LOG_ERROR(m_hashTable != nullptr, "Failed to allocate space for initial hash table");

        static_assert(x_nonexistentValue == 0, "required for memset");
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
            HeapPtrTranslator translator = static_cast<CRTP*>(this)->GetHeapPtrTranslator();
            UserHeapPointer<void> uhp = static_cast<CRTP*>(this)->AllocFromUserHeap(static_cast<uint32_t>(allocationLength));

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

    void Cleanup()
    {
        if (m_hashTable != nullptr)
        {
            delete [] m_hashTable;
        }
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
        return offsetof_base_v<GlobalStringHashConser, CRTP> + offsetof_member_v<&GlobalStringHashConser::m_specialKeyForBooleanIndex>;
    }

private:
    uint32_t m_hashTableSizeMask;
    uint32_t m_elementCount;
    // use GeneralHeapPointer because it's 4 bytes
    // All pointers are actually always HeapPtr<HeapString>
    //
    GeneralHeapPointer<HeapString>* m_hashTable;

    // In PolyMetatable mode, the metatable is stored in a property slot
    // For simplicity, we always assign this special key (which is used exclusively for this purpose) to this slot
    //
    UserHeapPointer<HeapString> m_specialKeyForMetatableSlot;

    // These two special keys are used for 'false' and 'true' respectively
    //
    UserHeapPointer<HeapString> m_specialKeyForBooleanIndex[2];
};
