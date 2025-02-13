#pragma once

#include "common_utils.h"
#include "dfg_arena.h"
#include "temp_arena_allocator.h"

namespace dfg {

// Utility to build the OSR exit information in one basic block
//
// The model is the following:
// 1. There is an uint16_t array A of length M, where M also fits in uint16_t.
// 2. There is a stream of events, where each event <idx, val> does A[idx] = val
// 3. There are some checkpoints in the event stream, and we want to build a
//    data structure, so at runtime, we can replay the event stream until a given
//    checkpoint, and return the resulted array A.
//
// One can think the model as below:
// A[i] tells how one can recover the value that should be stored in interpreter slot i.
// How one can recover the value from A[i] is not the business of this class (the value of A[i] may
// represent a register, a stack location, a constant, or some other stuffs, but we don't care).
//
// This class offers one higher-level abstraction for convenience:
// Whenever an SSA value changes its architectual location (being moved to another register or spilled
// to the stack), we must update all slots in A that stores this value to the new location.
// Therefore, this class allows each slot optionally be associated with an SSA node, and provides an
// API to update all slots associated with a given SSA node.
// To achieve this, each SSA value is required to provide a doubly-linked list head node, and we chain
// all slots that are currently associated with a certain SSA value onto that doubly-linked list.
//
struct DfgOsrExitMapBuilder
{
    MAKE_NONCOPYABLE(DfgOsrExitMapBuilder);
    MAKE_NONMOVABLE(DfgOsrExitMapBuilder);

    // All slots that stores the same SSA value are chained into a doubly linked list,
    // with the SSA value holding the head node.
    // The nodes for all interpreter slots belong to this class, but the head node held by the SSA value does not
    //
    // Note that this node must be allocated in the DfgArena, so offsets can always be represented by int32_t
    //
    struct DoublyLinkedListNode
    {
        DoublyLinkedListNode()
        {
            Reset();
        }

        void Reset()
        {
            m_prev = 0;
            m_next = 0;
        }

        DoublyLinkedListNode* Prev()
        {
            return reinterpret_cast<DoublyLinkedListNode*>(reinterpret_cast<uint8_t*>(this) + m_prev);
        }

        DoublyLinkedListNode* Next()
        {
            return reinterpret_cast<DoublyLinkedListNode*>(reinterpret_cast<uint8_t*>(this) + m_next);
        }

        bool IsIsolated()
        {
            TestAssertImp(m_prev == 0 || m_next == 0, m_prev == 0 && m_next == 0);
            return m_prev == 0;
        }

        void SetPrev(DoublyLinkedListNode* prev)
        {
            int64_t diff = reinterpret_cast<uint8_t*>(prev) - reinterpret_cast<uint8_t*>(this);
            TestAssert(IntegerCanBeRepresentedIn<int32_t>(diff));
            m_prev = static_cast<int32_t>(diff);
        }

        void SetNext(DoublyLinkedListNode* next)
        {
            int64_t diff = reinterpret_cast<uint8_t*>(next) - reinterpret_cast<uint8_t*>(this);
            TestAssert(IntegerCanBeRepresentedIn<int32_t>(diff));
            m_next = static_cast<int32_t>(diff);
        }

        int32_t m_prev;
        int32_t m_next;
    };

    DfgOsrExitMapBuilder(TempArenaAllocator& alloc, uint16_t numSlots)
        : m_alloc(alloc)
    {
        TestAssert(numSlots <= 65534);
        m_numSlots = numSlots;
        m_curOsrExitOrd = 0;
        m_slots = DfgAlloc()->AllocateArray<Info>(m_numSlots, alloc);
    }

    void Reset()
    {
        Info* start = m_slots;
        Info* end = start + m_numSlots;
        while (start < end)
        {
            start->Reset();
            start->m_info.clear();
            start++;
        }
        m_curOsrExitOrd = 0;
    }

    // Return the ordinal of the newly-added OSR exit point
    //
    uint16_t WARN_UNUSED AddOsrExitPoint()
    {
        if (unlikely(m_curOsrExitOrd == 65534))
        {
            fprintf(stderr, "[DFG][TODO] Too many (>65534) OSR exit points in one basic block! "
                            "This needs to be gracefully handled, but it's not done yet, so abort now.\n");
            abort();
        }
        uint16_t res = m_curOsrExitOrd;
        m_curOsrExitOrd++;
        return res;
    }

    // Set A[slot] to 'value', ignoring all fancy management.
    // 'slot' must not be associated with any SSA node
    //
    void SetUnassociated(uint16_t slot, uint16_t value)
    {
        TestAssert(slot < m_numSlots);
        TestAssert(m_slots[slot].IsIsolated());
        AddEventImpl(m_slots + slot, value);
    }

    // Set A[slot] to 'value', and associate 'slot' with 'ssaNode'
    //
    void SetAndAssociate(DoublyLinkedListNode* ssaNode, uint16_t slot, uint16_t value)
    {
        TestAssert(!IsSlotNode(ssaNode));
        TestAssert(slot < m_numSlots);
        Info* slotNode = m_slots + slot;
        UnlinkSlotFromList(slotNode);
        AddSlotToList(ssaNode, slotNode);
        AddEventImpl(slotNode, value);
    }

    // Set A[slot] to 'value', and make 'slot' no longer associated with any SSA node
    //
    void SetAndDeassociate(uint16_t slot, uint16_t value)
    {
        TestAssert(slot < m_numSlots);
        Info* slotNode = m_slots + slot;
        UnlinkSlotFromList(slotNode);
        slotNode->Reset();
        AddEventImpl(slotNode, value);
    }

    // Set the value of all slots associated with 'ssaNode' to the given value
    // Note that this will generate an event for every slot associated with the SSA node,
    // so to make the total data size linear with respect to program size,
    // this function should be called at most O(1) times for each SSA node.
    //
    void SetForAllSlotsAssociatedWith(DoublyLinkedListNode* ssaNode, uint16_t value)
    {
        TestAssert(!IsSlotNode(ssaNode));
        DoublyLinkedListNode* cur = ssaNode->Next();
        while (cur != ssaNode)
        {
            TestAssert(IsSlotNode(cur));
            AddEventImpl(static_cast<Info*>(cur), value);
            cur = cur->Next();
        }
    }

    // This is very slow and is prone to iterator invalidation bugs, so should only be used for assertion purpose
    //
    template<typename Func>
    void ALWAYS_INLINE ForEachSlotAssociatedWith(DoublyLinkedListNode* ssaNode, const Func& func)
    {
        TestAssert(!IsSlotNode(ssaNode));
        DoublyLinkedListNode* cur = ssaNode->Next();
        while (cur != ssaNode)
        {
            TestAssert(IsSlotNode(cur));
            Info* curSlot = static_cast<Info*>(cur);
            TestAssert((reinterpret_cast<uint64_t>(curSlot) - reinterpret_cast<uint64_t>(m_slots)) % sizeof(Info) == 0);
            size_t ord = (reinterpret_cast<uint64_t>(curSlot) - reinterpret_cast<uint64_t>(m_slots)) / sizeof(Info);
            TestAssert(curSlot == m_slots + ord);
            func(SafeIntegerCast<uint16_t>(ord));
            cur = cur->Next();
        }
    }

    // Assert that 'slot' is associated with 'ssaNode', that is, it is on the circular doubly-linked list of 'ssaNode'
    //
    void AssertSlotIsAssociatedWithValue([[maybe_unused]] DoublyLinkedListNode* ssaNode, [[maybe_unused]] uint16_t slot)
    {
#ifdef TESTBUILD
        TestAssert(!IsSlotNode(ssaNode));
        DoublyLinkedListNode* cur = ssaNode->Next();
        while (cur != ssaNode)
        {
            TestAssert(IsSlotNode(cur));
            Info* curSlot = static_cast<Info*>(cur);
            if (curSlot == m_slots + slot)
            {
                return;
            }
            cur = cur->Next();
        }
        TestAssert(false);
#endif
    }

    // For assertion purpose only, query the current value in the slot.
    // It is a bug to query a slot which hasn't been stored a value.
    //
    uint16_t WARN_UNUSED QueryCurrentValue(uint16_t slot)
    {
        TestAssert(slot < m_numSlots);
        Info* slotNode = m_slots + slot;
        TestAssert(slotNode->m_info.size() > 0);
        return slotNode->m_info.back().second;
    }

    // 'size_t' is the # of uint16_t elements, not the byte size!
    //
    // The built stream format is the following:
    // For each *non-empty* vector, [ slotOrd ] [ #elements ] [ osrExitOrds... ] [ values... ]
    //
    // TODO: I am very doubious if this is the right design to have. It seems to me that in many cases
    // simply replaying the log until the checkpoint will be faster.
    //
    std::pair<uint16_t*, size_t> WARN_UNUSED BuildEventStream()
    {
        size_t numElements = 1;
        {
            Info* start = m_slots;
            Info* end = m_slots + m_numSlots;
            while (start < end)
            {
                if (start->m_info.size() > 0)
                {
                    auto& back = start->m_info.back();
                    TestAssert(back.first <= m_curOsrExitOrd);
                    // The valid OsrExitOrdinals are [0, m_curOsrExitOrd).
                    // If there is an event after the last valid m_curOsrExitOrd, it is never useful and can be deleted
                    //
                    if (back.first == m_curOsrExitOrd)
                    {
                        start->m_info.pop_back();
                    }
                }
                if (start->m_info.size() > 0)
                {
                    numElements += 2 + start->m_info.size() * 2;
                }
                start++;
            }
        }
        uint16_t* dataStart = m_alloc.AllocateArray<uint16_t>(numElements);
        {
            uint16_t* curData = dataStart;
            Info* start = m_slots;
            Info* end = m_slots + m_numSlots;
            uint16_t slotOrd = 0;
            while (start < end)
            {
                auto& v = start->m_info;
                if (v.size() > 0)
                {
                    *curData = slotOrd;
                    curData++;
                    TestAssert(v.size() <= 65535);
                    *curData = static_cast<uint16_t>(v.size());
                    curData++;

#ifdef TESTBUILD
                    for (size_t i = 1; i < v.size(); i++)
                    {
                        TestAssert(v[i].first > v[i - 1].first);
                    }
#endif

                    uint16_t* p = curData + v.size();
                    for (auto& it : v)
                    {
                        *curData = it.first;
                        curData++;
                        *p = it.second;
                        p++;
                    }
                    curData = p;
                }
                start++;
                slotOrd++;
            }
            TestAssert(slotOrd == m_numSlots);

            // The stream ends with a sentry slot value 65535
            //
            TestAssert(m_numSlots < 65535);
            *curData = 65535;
            curData++;
            TestAssert(curData == dataStart + numElements);
        }
        return std::make_pair(dataStart, numElements);
    }

private:
    bool IsSlotNode(DoublyLinkedListNode* node)
    {
        return m_slots <= node && node < m_slots + m_numSlots;
    }

    // Note that this function does not reset the links in 'node'
    //
    void UnlinkSlotFromList(DoublyLinkedListNode* node)
    {
        TestAssert(IsSlotNode(node));
        DoublyLinkedListNode* prev = node->Prev();
        DoublyLinkedListNode* next = node->Next();
        prev->SetNext(next);
        next->SetPrev(prev);
    }

    void AddSlotToList(DoublyLinkedListNode* listNode, DoublyLinkedListNode* nodeToAdd)
    {
        TestAssert(IsSlotNode(nodeToAdd));
        DoublyLinkedListNode* next = listNode->Next();
        listNode->SetNext(nodeToAdd);
        next->SetPrev(nodeToAdd);
        nodeToAdd->SetNext(next);
        nodeToAdd->SetPrev(listNode);
    }

    struct Info : DoublyLinkedListNode
    {
        Info(TempArenaAllocator& alloc)
            : DoublyLinkedListNode()
            , m_info(alloc)
        { }

        TempVector<std::pair<uint16_t /*osrExitOrd*/, uint16_t /*value*/>> m_info;
    };

    void AddEventImpl(Info* info, uint16_t value)
    {
        auto& v = info->m_info;
        if (v.empty())
        {
            v.push_back(std::make_pair(m_curOsrExitOrd, value));
        }
        else
        {
            auto& back = v.back();
            TestAssert(back.first <= m_curOsrExitOrd);
            if (value != back.second)
            {
                if (back.first == m_curOsrExitOrd)
                {
                    back.second = value;
                }
                else
                {
                    v.push_back(std::make_pair(m_curOsrExitOrd, value));
                }
            }
        }
    }

    TempArenaAllocator& m_alloc;
    uint16_t m_numSlots;
    uint16_t m_curOsrExitOrd;
    // The doubly-linked list node and event vector for each slot in A
    // This array is allocated in DFG arena
    //
    Info* m_slots;
};

// Note that we require 10 bytes after end of stream to be accessible,
// so that we can always safely load 16 bytes of data into a xmm regardless of how large the actual array is
//
struct DfgOsrExitEventStreamReplayer
{
    DfgOsrExitEventStreamReplayer(uint16_t* stream, uint16_t osrExitOrd)
        : m_data(stream)
        , m_osrExitOrd(osrExitOrd)
        , m_curSlotOrd(0)
    { }

    uint16_t GetCurSlotOrd() { return m_curSlotOrd; }

    // Return false if there is no visible write event to m_curSlotOrd at the given osrExitOrd
    //
    bool ALWAYS_INLINE WARN_UNUSED GetAndAdvance(uint16_t& value /*out*/)
    {
        TestAssert(m_curSlotOrd < 65535);
        TestAssert(m_curSlotOrd <= *m_data);
        if (m_curSlotOrd < *m_data)
        {
            m_curSlotOrd++;
            return false;
        }

        TestAssert(m_curSlotOrd == *m_data);
        uint16_t numElements = m_data[1];
        TestAssert(numElements > 0);

        uint16_t* keys = m_data + 2;
        uint16_t* values = keys + numElements;
        m_data = values + numElements;

        if (m_osrExitOrd < keys[0])
        {
            m_curSlotOrd++;
            return false;
        }

        size_t idx = RunBinarySearch(keys, numElements, m_osrExitOrd);
        TestAssert(idx < numElements);

        value = values[idx];
        m_curSlotOrd++;
        return true;
    }

    // 'keys' are strictly ascending
    // Return the largest i such that keys[i] <= k, or -1 if keys[0] > k
    //
    static size_t ALWAYS_INLINE RunBinarySearch(uint16_t* keys, size_t numElements, uint16_t k)
    {
        TestAssert(numElements > 0);

        // Invariant: 'left' is always a valid answer or == 0, 'right' is never a valid answer
        //
        size_t left = 0;
        size_t right = numElements;
        while (left + 8 < right)
        {
            size_t mid = (left + right) / 2;
            bool criterion = (keys[mid] <= k);
            left = (criterion ? mid : left);
            right = (criterion ? right : mid);
        }

        __m128i data = _mm_loadu_si128(reinterpret_cast<__m128i_u*>(keys + left));
        __m128i test = _mm_set1_epi16(static_cast<short>(k));
        __m128i cmpRes = _mm_cmpgt_epi16(data, test);
        uint32_t mask = static_cast<uint32_t>(_mm_movemask_epi8(cmpRes));
        mask |= 1U << ((right - left) * 2);
        size_t ctz = CountTrailingZeros(mask);
        TestAssert(ctz % 2 == 0);
        TestAssertImp(left > 0, ctz >= 2);
        ctz = ctz / 2 - 1;
        TestAssertImp(left > 0, ctz < right - left);

        size_t idx = left + ctz;
        TestAssert(idx < numElements || idx == static_cast<size_t>(-1));
        TestAssertImp(idx < numElements, keys[idx] <= k);
        TestAssertImp(idx + 1 < numElements, keys[idx + 1] > k);
        return idx;
    }

    // We may load 16 bytes where only first 2 bytes is a valid key.
    // But the next 4 bytes must still be within stream (one value + one terminal sentry),
    // so we may access at most 10 bytes past end of stream
    //
    static constexpr size_t x_requiredExtraAccessibleBytesAtEnd = 10;

private:
    uint16_t* m_data;
    uint16_t m_osrExitOrd;
    uint16_t m_curSlotOrd;
};

}   // namespace dfg
