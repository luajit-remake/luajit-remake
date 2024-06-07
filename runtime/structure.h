#pragma once

#include "common_utils.h"
#include "tvalue.h"
#include "memory_ptr.h"
#include "heap_object_common.h"
#include "vm.h"
#include "array_type.h"
#include "butterfly.h"

// We want to solve the following problem. Given a tree of size n and max depth D with a value on each node, we want to support:
// (1) Insert a new leaf.
// (2) Given a node, query if a value appeared on the path from node to root. If yes, at which depth.
//
// Trivially this problem can be solved via persistent data structure (a persistent trie or treap)
// with O(log D) insert, O(log D) query and O(n log D) space, but the constant hidden in big O is too large
// to be useful in our use case, and also we want faster query.
//
// We instead use a O(sqrt D) insert (amortized), O(1) query and O(n sqrt D) space algorithm.
//
// Let S = sqrt D, for each node x in the tree satisfying both of the following:
// (1) The depth of x is a multiple of S.
// (2) The subtree rooted by x has a maximum depth of >= S (thanks Yinzhan!).
// For each such x, we build a hash table containing all elements from root to x. We call such x 'anchors'.
//
// Lemma 1. There are at most n/S anchors in the tree.
// Proof. For each anchor x, there exists a chain of length S starting at x and going downwards.
//        Let chain(x) denote one such chain for x. All chain(x) must be disjoint from each other. QED
//
// So the total size of the anchor hash tables is bounded by O(D * n / S) = O(n sqrt D).
//
// For each node, we also build a hash table containing nodes from itself to the nearest anchor upwards.
// Then each query can be answered in O(1) by checking its own hash table and the anchor's hash table.
//
// Each node's individual hash table clearly has size O(S), so the total size is O(nS) = O(n sqrt D).
//
// By the way, this scheme can be extended to yield O(D^(1/c)) insert, O(c) query, O(n * D^(1/c)) space for any c,
// but likely the constant is too large for them to be useful, so we just use the sqrt n version.
//
// Below is the constant of S (in our use case, we are targeting a tree of maximum depth 254).
//
constexpr uint32_t x_log2_hiddenClassBlockSize = 4;
constexpr uint32_t x_hiddenClassBlockSize = (1 << x_log2_hiddenClassBlockSize);

class Structure;

class alignas(8) StructureTransitionTable
{
public:
    struct HashTableEntry
    {
        int32_t m_key;
        SystemHeapPointer<Structure> m_value;
    };

    static constexpr int32_t x_key_invalid = 0;
    static constexpr int32_t x_key_deleted = 1;

    // Special keys for operations other than AddProperty
    //
    // First time we add a metatable, this key points to the Structure with new metatable
    // Next time we add a metatable, if we found the metatable is different from the existing one,
    // we create a new Structure with PolyMetatable, and replace the value corresponding to
    // x_key_add_or_to_poly_metatable to the PolyMetatable structure
    //
    static constexpr int32_t x_key_add_or_to_poly_metatable = 2;
    static constexpr int32_t x_key_remove_metatable = 3;
    // For ChangeArrayType operations, the key is tag | newArrayType
    // Note that string property always correspond to negative keys, so there's no collision possible
    //
    static constexpr int32_t x_key_change_array_type_tag = 256;

    static constexpr uint32_t x_initialHashTableSize = 4;

    static uint32_t ComputeAllocationSize(uint32_t hashTableSize)
    {
        assert(is_power_of_2(hashTableSize));
        size_t allocationSize = x_trailingArrayOffset + hashTableSize * sizeof(HashTableEntry);
        return static_cast<uint32_t>(allocationSize);
    }

    static StructureTransitionTable* AllocateUninitialized(VM* vm, uint32_t hashTableSize)
    {
        uint32_t allocationSize = ComputeAllocationSize(hashTableSize);
        StructureTransitionTable* result = TranslateToRawPointer(vm, vm->AllocFromSystemHeap(allocationSize).As<StructureTransitionTable>());
        ConstructInPlace(result);
        return result;
    }

    static StructureTransitionTable* AllocateInitialTable(VM* vm, int32_t key, SystemHeapPointer<Structure> value)
    {
        StructureTransitionTable* r = AllocateUninitialized(vm, x_initialHashTableSize);
        r->m_hashTableMask = x_initialHashTableSize - 1;
        r->m_numElementsInHashTable = 1;
        memset(r->m_hashTable, 0, sizeof(HashTableEntry) * x_initialHashTableSize);
        bool found;
        HashTableEntry* e = Find(SystemHeapPointer<StructureTransitionTable>(r).As(), key, found /*out*/);
        assert(!found);
        e->m_key = key;
        e->m_value = value;
        return r;
    }

    bool ShouldResizeForThisInsertion()
    {
        return m_numElementsInHashTable >= m_hashTableMask / 2 + 1;
    }

    static ReinterpretCastPreservingAddressSpaceType<HashTableEntry*, StructureTransitionTable*> WARN_UNUSED Find(StructureTransitionTable* self, int32_t key, bool& found)
    {
        assert(key != x_key_invalid && key != x_key_deleted);
        uint32_t hashMask = self->m_hashTableMask;
        uint32_t slot = static_cast<uint32_t>(HashPrimitiveTypes(key)) & hashMask;
        while (true)
        {
            int32_t slotKey = self->m_hashTable[slot].m_key;
            if (slotKey == key)
            {
                found = true;
                return &self->m_hashTable[slot];
            }
            if (slotKey == x_key_invalid || slotKey == x_key_deleted)
            {
                found = false;
                return &self->m_hashTable[slot];
            }
            slot = (slot + 1) & hashMask;
        }
    }

    StructureTransitionTable* WARN_UNUSED Expand(VM* vm)
    {
        assert(ShouldResizeForThisInsertion());
        uint32_t newHashMask = m_hashTableMask * 2 + 1;
        StructureTransitionTable* newTable = AllocateUninitialized(vm, newHashMask + 1);
        newTable->m_hashTableMask = newHashMask;
        newTable->m_numElementsInHashTable = m_numElementsInHashTable;
        static_assert(x_key_invalid == 0);
        memset(newTable->m_hashTable, 0, sizeof(HashTableEntry) * (newHashMask + 1));

        for (uint32_t i = 0; i <= m_hashTableMask; i++)
        {
            HashTableEntry entry = m_hashTable[i];
            if (entry.m_key != x_key_invalid && entry.m_key != x_key_deleted)
            {
                uint32_t slot = static_cast<uint32_t>(HashPrimitiveTypes(entry.m_key)) & newHashMask;
                while (newTable->m_hashTable[slot].m_key != x_key_invalid)
                {
                    slot = (slot + 1) & newHashMask;
                }
                newTable->m_hashTable[slot] = entry;
            }
        }
        return newTable;
    }

    uint32_t m_hashTableMask;
    uint32_t m_numElementsInHashTable;
    HashTableEntry m_hashTable[0];

    static constexpr size_t x_trailingArrayOffset = offsetof_member_v<&StructureTransitionTable::m_hashTable>;
};

struct StructureKeyHashHelper
{
    static uint32_t GetHashValueForStringKey(UserHeapPointer<HeapString> stringKey)
    {
        HeapString* s = stringKey.As<HeapString>();
        assert(s->m_type == HeapEntityType::String);
        return s->m_hashLow;
    }

    static uint32_t GetHashValueForMaybeNonStringKey(UserHeapPointer<void> key)
    {
        UserHeapGcObjectHeader* hdr = key.As<UserHeapGcObjectHeader>();
        if (hdr->m_type == HeapEntityType::String)
        {
            return GetHashValueForStringKey(UserHeapPointer<HeapString>(key.As()));
        }
        else
        {
            return static_cast<uint32_t>(HashPrimitiveTypes(key.m_value));
        }
    }
};

// The structure for the anchor hash tables as described in the algorithm.
//
// [ hash table ] [ header ] [ block pointers ]
//                ^
//                pointer to object
//
class alignas(8) StructureAnchorHashTable final : public SystemHeapGcObjectHeader
{
public:
    struct HashTableEntry
    {
        uint8_t m_ordinal;
        uint8_t m_checkHash;
    };
    static_assert(sizeof(HashTableEntry) == 2);

    static constexpr uint8_t x_hashTableEmptyValue = 0xff;

    static constexpr size_t OffsetOfTrailingVarLengthArray()
    {
        return offsetof_member_v<&StructureAnchorHashTable::m_blockPointers>;
    }

    static GeneralHeapPointer<void> GetPropertyNameAtSlot(StructureAnchorHashTable* self, uint8_t ordinal)
    {
        assert(ordinal < self->m_numBlocks * x_hiddenClassBlockSize);
        uint8_t blockOrd = ordinal >> x_log2_hiddenClassBlockSize;
        uint8_t offset = ordinal & static_cast<uint8_t>(x_hiddenClassBlockSize - 1);
        SystemHeapPointer<GeneralHeapPointer<void>> p = self->m_blockPointers[blockOrd];
        return p.As()[offset];
    }

    static StructureAnchorHashTable* WARN_UNUSED Create(VM* vm, Structure* shc);

    static ReinterpretCastPreservingAddressSpaceType<HashTableEntry*, StructureAnchorHashTable*> GetHashTableBegin(StructureAnchorHashTable* self)
    {
        uint32_t hashTableSize = GetHashTableSizeFromHashTableMask(self->m_hashTableMask);
        return GetHashTableEnd(self) - hashTableSize;
    }

    static ReinterpretCastPreservingAddressSpaceType<HashTableEntry*, StructureAnchorHashTable*> GetHashTableEnd(StructureAnchorHashTable* self)
    {
        return ReinterpretCastPreservingAddressSpace<HashTableEntry*>(self);
    }

    void CloneHashTableTo(HashTableEntry* htStart, uint32_t htSize);

    static bool WARN_UNUSED GetSlotOrdinalFromPropertyNameAndHash(StructureAnchorHashTable* self, GeneralHeapPointer<void> key, uint32_t hashValue, uint32_t& result /*out*/)
    {
        int64_t hashTableMask = static_cast<int64_t>(self->m_hashTableMask);
        uint8_t checkHash = static_cast<uint8_t>(hashValue);
        int64_t hashSlot = static_cast<int64_t>(hashValue >> 8) | hashTableMask;
        auto hashTableEnd = GetHashTableEnd(self);
        DEBUG_ONLY(uint32_t hashTableSize = GetHashTableSizeFromHashTableMask(self->m_hashTableMask);)
        assert(-static_cast<int64_t>(hashTableSize) <= hashSlot && hashSlot < 0);

        while (true)
        {
            uint8_t htSlotOrdinal = hashTableEnd[hashSlot].m_ordinal;
            uint8_t htSlotCheckHash = hashTableEnd[hashSlot].m_checkHash;
            if (htSlotOrdinal == x_hashTableEmptyValue)
            {
                return false;
            }
            if (htSlotCheckHash == checkHash)
            {
                GeneralHeapPointer<void> p = GetPropertyNameAtSlot(self, htSlotOrdinal);
                if (p == key)
                {
                    result = htSlotOrdinal;
                    return true;
                }
            }

            hashSlot = (hashSlot - 1) | hashTableMask;
            assert(-static_cast<int64_t>(hashTableSize) <= hashSlot && hashSlot < 0);
        }
    }

    static uint32_t GetHashTableSizeFromHashTableMask(int32_t mask)
    {
        assert(mask < 0);
        mask = ~mask;
        assert(mask > 0 && mask < std::numeric_limits<int32_t>::max());
        mask++;
        assert(is_power_of_2(mask));
        return static_cast<uint32_t>(mask);
    }

    uint8_t m_numBlocks;
    uint8_t m_numTotalSlots;    // just m_numBlocks * x_hiddenClassBlockSize

    // This is a negative mask! use GetHashTableSizeFromHashTableMask() to recover hash table size
    //
    int32_t m_hashTableMask;

    // These block pointer point at the beginning of each x_hiddenClassBlockSize block
    //
    SystemHeapPointer<GeneralHeapPointer<void>> m_blockPointers[0];
};
static_assert(sizeof(StructureAnchorHashTable) == 8);

struct ButterflyNamedStorageGrowthPolicy
{
    static constexpr uint8_t x_initialMinimumButterflyCapacity = 4;
    static constexpr uint8_t x_butterflyCapacityFromInlineCapacityFactor = 2;
    static constexpr uint32_t x_butterflyNamedStorageCapacityGrowthFactor = 2;

    // TODO: refine growth strategy
    //
    static uint8_t WARN_UNUSED ComputeInitialButterflyCapacityForDictionary(uint8_t inlineNamedStorageCapacity)
    {
        uint8_t capacity = inlineNamedStorageCapacity / x_butterflyCapacityFromInlineCapacityFactor;
        capacity = std::max(capacity, x_initialMinimumButterflyCapacity);
        return capacity;
    }

    static uint32_t WARN_UNUSED ComputeNextButterflyCapacityImpl(uint32_t curButterflyCapacity)
    {
        assert(curButterflyCapacity > 0);
        uint32_t capacity = curButterflyCapacity * x_butterflyNamedStorageCapacityGrowthFactor;
        return capacity;
    }

    // Will not grow over 'x_maxNamedStorageCapacity'
    // Fail VM if the old capacity is already 'x_maxNamedStorageCapacity'
    //
    static uint32_t WARN_UNUSED ComputeNextButterflyCapacityForDictionaryOrFail(uint32_t curButterflyCapacity)
    {
        uint32_t capacity = ComputeNextButterflyCapacityImpl(curButterflyCapacity);
        if (unlikely(capacity > Butterfly::x_maxNamedStorageCapacity))
        {
            // If the old capacity is already 'x_maxNamedStorageCapacity', it means we are unable to grow
            // the vector to accommodate the new element, so fail the VM
            //
            VM_FAIL_IF(curButterflyCapacity >= Butterfly::x_maxNamedStorageCapacity,
                       "too many (>%llu) named properties in table object!", static_cast<unsigned long long>(Butterfly::x_maxNamedStorageCapacity));

            // Otherwise, do not grow over Butterfly::x_maxNamedStorageCapacity
            //
            capacity = Butterfly::x_maxNamedStorageCapacity;
        }
        assert(capacity > curButterflyCapacity);
        return capacity;
    }

    template<typename T>
    static uint8_t WARN_UNUSED ClampCapacityForStructure(T capacity, uint8_t inlineNamedStorageCapacity, uint8_t maxPropertySlots)
    {
        // It needs to grow to up to maxPropertySlots + 1: that's the real max capacity considering PolyMetatable
        //
        assert(inlineNamedStorageCapacity <= maxPropertySlots);
        uint8_t capacityLimit = maxPropertySlots + 1 - inlineNamedStorageCapacity;
        if (capacity > capacityLimit)
        {
            return capacityLimit;
        }
        else
        {
            return static_cast<uint8_t>(capacity);
        }
    }

    static uint8_t WARN_UNUSED ComputeInitialButterflyCapacityForStructure(uint8_t inlineNamedStorageCapacity, uint8_t maxPropertySlots)
    {
        uint16_t capacity = ComputeInitialButterflyCapacityForDictionary(inlineNamedStorageCapacity);
        return ClampCapacityForStructure(capacity, inlineNamedStorageCapacity, maxPropertySlots);
    }

    static uint8_t WARN_UNUSED ComputeNextButterflyCapacityForStructure(uint8_t inlineNamedStorageCapacity, uint8_t curButterflyCapacity, uint8_t maxPropertySlots)
    {
        uint32_t capacity = ComputeNextButterflyCapacityImpl(curButterflyCapacity);
        return ClampCapacityForStructure(capacity, inlineNamedStorageCapacity, maxPropertySlots);
    }
};

// A structure hidden class
//
// Future work: we can use one structure to store a chain of PropertyAdd transitions (and user
// use pointer tag to distinguish which node in the chain it is referring to). This should further reduce memory consumption.
//
// [ hash table ] [ header ] [ non-full block elements ] [ optional last full block pointer ]
//                ^
//                pointer to object
//
// The LastFullBlockPointer points at the END of the x_hiddenClassBlockSize block (unlike StructureAnchorHashTable which points at the beginning)
// This is really ugly, but I don't want to change this code any more..
//
class alignas(8) Structure final : public SystemHeapGcObjectHeader
{
public:
    enum class TransitionKind : uint8_t
    {
        BadTransitionKind,
        AddProperty,
        AddPropertyAndGrowPropertyStorageCapacity,
        AddMetaTable,
        TransitToPolyMetaTable,
        TransitToPolyMetaTableAndGrowPropertyStorageCapacity,
        RemoveMetaTable,
        UpdateArrayType
    };

    static constexpr size_t OffsetOfTrailingVarLengthArray()
    {
        return offsetof_member_v<&Structure::m_values>;
    }

    static uint8_t ComputeNonFullBlockLength(uint8_t numSlots)
    {
        if (numSlots == 0) { return 0; }
        uint8_t nonFullBlockLen = static_cast<uint8_t>(((static_cast<uint32_t>(numSlots) - 1) & (x_hiddenClassBlockSize - 1)) + 1);
        return nonFullBlockLen;
    }

    static constexpr int8_t x_inlineHashTableEmptyValue = 0x7f;

    struct InlineHashTableEntry
    {
        uint8_t m_checkHash;
        int8_t m_ordinal;
    };
    static_assert(sizeof(InlineHashTableEntry) == 2);

    static ReinterpretCastPreservingAddressSpaceType<InlineHashTableEntry*, Structure*> GetInlineHashTableBegin(Structure* self)
    {
        uint32_t hashTableSize = ComputeHashTableSizeFromHashTableMask(self->m_inlineHashTableMask);
        return GetInlineHashTableEnd(self) - hashTableSize;
    }

    static ReinterpretCastPreservingAddressSpaceType<InlineHashTableEntry*, Structure*> GetInlineHashTableEnd(Structure* self)
    {
        return ReinterpretCastPreservingAddressSpace<InlineHashTableEntry*>(self);
    }

    static uint32_t ComputeHashTableSizeFromHashTableMask(uint8_t mask)
    {
        uint32_t v = static_cast<uint32_t>(mask) + 1;
        assert(is_power_of_2(v));
        return v;
    }

    static GeneralHeapPointer<void> GetLastAddedKey(Structure* self)
    {
        assert(self->m_parentEdgeTransitionKind == TransitionKind::AddProperty ||
               self->m_parentEdgeTransitionKind == TransitionKind::AddPropertyAndGrowPropertyStorageCapacity);
        assert(self->m_numSlots > 0);
        uint8_t nonFullBlockLen = ComputeNonFullBlockLength(self->m_numSlots);
        assert(nonFullBlockLen > 0);
        return self->m_values[nonFullBlockLen - 1];
    }

    static UserHeapPointer<void> WARN_UNUSED GetKeyForSlotOrdinal(Structure* self, uint8_t slotOrdinal)
    {
        assert(slotOrdinal < self->m_numSlots);

        SystemHeapPointer<StructureAnchorHashTable> p = TCGet(self->m_anchorHashTable);
        if (p.m_value != 0 && slotOrdinal < p.As()->m_numTotalSlots)
        {
            return StructureAnchorHashTable::GetPropertyNameAtSlot(p.As(), slotOrdinal).As();
        }

        uint8_t lim = self->m_numSlots - self->m_nonFullBlockLen;
        if (slotOrdinal >= lim)
        {
            uint8_t ord = slotOrdinal - lim;
            assert(ord < x_hiddenClassBlockSize);
            return self->m_values[ord].As();
        }
        else
        {
            assert(lim >= x_hiddenClassBlockSize && lim - x_hiddenClassBlockSize <= slotOrdinal);
            int32_t ord = static_cast<int32_t>(slotOrdinal) - static_cast<int32_t>(lim);
            assert(-static_cast<int32_t>(x_hiddenClassBlockSize) <= ord && ord < 0);
            assert(HasFinalFullBlockPointer(self->m_numSlots));
            SystemHeapPointer<GeneralHeapPointer<void>> u = GetFinalFullBlockPointer(self);
            return u.As()[ord].As();
        }
    }

    static constexpr uint8_t x_maxNumSlots = 253;
    static_assert(internal::x_maxInlineCapacity == Structure::x_maxNumSlots);

    struct AddNewPropertyResult
    {
        // If true, the Structure just transitioned into dictionary mode
        // When this is true, all other fields in this struct are NOT filled
        //
        bool m_shouldTransitionToDictionaryMode;

        // If true, the caller should grow the object's butterfly property part
        //
        bool m_shouldGrowButterfly;

        // The slot ordinal to write into
        //
        uint32_t m_slotOrdinal;

        void* m_newStructure;
    };

    // Perform an AddProperty transition, the added property must be non-existent
    //
    void AddNonExistentProperty(VM* vm, UserHeapPointer<void> key, AddNewPropertyResult& result /*out*/);

    struct AddMetatableResult
    {
        // If true, the Structure is in PolyMetatable mode,
        // and the user should fill the metatable into m_slotOrdinal
        //
        bool m_shouldInsertMetatable;

        // If true, the caller should grow the object's butterfly property part
        // Only relavent if m_shouldInsertMetatable == true
        //
        bool m_shouldGrowButterfly;

        uint32_t m_slotOrdinal;
        SystemHeapPointer<Structure> m_newStructure;
    };

    // Perform a SetMetatable transition
    //
    void SetMetatable(VM* vm, UserHeapPointer<void> key, AddMetatableResult& result /*out*/);

    struct RemoveMetatableResult
    {
        // If true, the Structure is in PolyMetatable mode,
        // and the user should fill 'nil' into m_slotOrdinal
        //
        bool m_shouldInsertMetatable;
        uint32_t m_slotOrdinal;
        SystemHeapPointer<Structure> m_newStructure;
    };

    // Perform a RemoveMetatable transition
    //
    void RemoveMetatable(VM* vm, RemoveMetatableResult& result /*out*/);

    // Perform an UpdateArrayType transition, returns the new structure.
    // The array type MUST be different from what we have currently.
    //
    Structure* WARN_UNUSED UpdateArrayType(VM* vm, ArrayType newArrayType);

    static bool WARN_UNUSED IsAnchorTableContainsFinalBlock(Structure* self)
    {
        SystemHeapPointer<StructureAnchorHashTable> p = self->m_anchorHashTable;
        if (p.m_value == 0)
        {
            return false;
        }
        uint8_t lim = self->m_numSlots & (~static_cast<uint8_t>(x_hiddenClassBlockSize - 1));
        assert(p.As()->m_numTotalSlots == lim || p.As()->m_numTotalSlots == lim - x_hiddenClassBlockSize);
        return p.As()->m_numTotalSlots == lim;
    }

    enum class SlotAdditionKind
    {
        AddSlotForProperty,
        AddSlotForPolyMetatable,
        NoSlotAdded
    };

    // Create the structure obtained after a transition
    // If slotAdditionKind is 'AddSlotForProperty' or 'AddSlotForPolyMetatable':
    //   A slot for 'key' or metatable is added, and the created structure is fully initialized.
    // Otherwise:
    //   1. Parameter 'key' is useless.
    //   2. We only perform a clone of the structure, except that the new structure's 'm_parentEdgeTransitionType' is not filled.
    //   3. Caller should actually perform the transition by modifying the returned structure.
    //
    // In either case, the caller is responsible for updating the parent's transition table.
    //
    Structure* WARN_UNUSED CreateStructureForTransitionImpl(VM* vm, SlotAdditionKind slotAdditionKind, UserHeapPointer<void> key);

    Structure* WARN_UNUSED CreateStructureForAddPropertyTransition(VM* vm, UserHeapPointer<void> key)
    {
        assert(key.m_value != 0);
        return CreateStructureForTransitionImpl(vm, SlotAdditionKind::AddSlotForProperty, key);
    }

    Structure* WARN_UNUSED CreateStructureForPolyMetatableTransition(VM* vm)
    {
        assert(m_metatable <= 0);
        return CreateStructureForTransitionImpl(vm, SlotAdditionKind::AddSlotForPolyMetatable, vm->GetSpecialKeyForMetadataSlot().As<void>());
    }

    // Create a child by making a clone of 'this'. That is, the structure is cloned, except that:
    // 1. m_transitionTable is empty
    // 2. m_parent is set to 'this'
    // 3. m_parentEdgeTransitionType is BadTransitionKind: caller is responsible to set it
    //
    Structure* WARN_UNUSED CloneStructure(VM* vm)
    {
        return CreateStructureForTransitionImpl(vm, SlotAdditionKind::NoSlotAdded, UserHeapPointer<void>());
    }

    Structure* WARN_UNUSED CreateStructureForMonomorphicMetatableTransition(VM* vm, GeneralHeapPointer<void> metatable)
    {
        assert(m_metatable == 0);
        assert(metatable.m_value < 0);
        Structure* newStructure = CloneStructure(vm);
        newStructure->m_parentEdgeTransitionKind = TransitionKind::AddMetaTable;
        newStructure->m_metatable = metatable.m_value;
        assert(!newStructure->m_arrayType.MayHaveMetatable());
        newStructure->m_arrayType.SetMayHaveMetatable(true);
        return newStructure;
    }

    Structure* WARN_UNUSED CreateStructureForRemoveMetatableTransition(VM* vm)
    {
        assert(HasMonomorphicMetatable(this));
        Structure* newStructure = CloneStructure(vm);
        newStructure->m_parentEdgeTransitionKind = TransitionKind::RemoveMetaTable;
        newStructure->m_metatable = 0;
        assert(newStructure->m_arrayType.MayHaveMetatable());
        newStructure->m_arrayType.SetMayHaveMetatable(false);
        return newStructure;
    }

    // You should not call this function directly. Call GetInitialStructureForInlineCapacity instead. This function is exposed only for unit test.
    //
    static Structure* WARN_UNUSED CreateInitialStructure(VM* vm, uint8_t initialInlineCapacity);

    static SystemHeapPointer<StructureAnchorHashTable> WARN_UNUSED BuildNewAnchorTableIfNecessary(Structure* self);

    static bool WARN_UNUSED GetSlotOrdinalFromStringProperty(Structure* self, UserHeapPointer<HeapString> stringKey, uint32_t& result /*out*/)
    {
        return GetSlotOrdinalFromPropertyNameAndHash(self, stringKey.As<void>(), StructureKeyHashHelper::GetHashValueForStringKey(stringKey), result /*out*/);
    }

    static bool WARN_UNUSED GetSlotOrdinalFromMaybeNonStringProperty(Structure* self, UserHeapPointer<void> key, uint32_t& result /*out*/)
    {
        return GetSlotOrdinalFromPropertyNameAndHash(self, key, StructureKeyHashHelper::GetHashValueForMaybeNonStringKey(key), result /*out*/);
    }

    static bool WARN_UNUSED GetSlotOrdinalFromPropertyNameAndHash(Structure* self, UserHeapPointer<void> key, uint32_t hashvalue, uint32_t& result /*out*/)
    {
        GeneralHeapPointer<void> keyG = GeneralHeapPointer<void> { key.As<void>() };
        if (QueryInlineHashTable(self, keyG, static_cast<uint16_t>(hashvalue), result /*out*/))
        {
            return true;
        }
        SystemHeapPointer<StructureAnchorHashTable> anchorHt = self->m_anchorHashTable;
        if (likely(anchorHt.m_value == 0))
        {
            return false;
        }
        return StructureAnchorHashTable::GetSlotOrdinalFromPropertyNameAndHash(anchorHt.As(), keyG, hashvalue, result /*out*/);
    }

    static bool WARN_UNUSED QueryInlineHashTable(Structure* self, GeneralHeapPointer<void> key, uint16_t hashvalue, uint32_t& result /*out*/)
    {
        int64_t hashMask = ~ZeroExtendTo<int64_t>(self->m_inlineHashTableMask);
        assert(hashMask < 0);
        int64_t hashSlot = ZeroExtendTo<int64_t>(hashvalue >> 8) | hashMask;

        DEBUG_ONLY(uint32_t hashTableSize = ComputeHashTableSizeFromHashTableMask(self->m_inlineHashTableMask);)
        assert(-static_cast<int64_t>(hashTableSize) <= hashSlot && hashSlot < 0);

        uint8_t checkHash = static_cast<uint8_t>(hashvalue);
        auto hashTableEnd = GetInlineHashTableEnd(self);

        while (true)
        {
            int8_t slotOrdinal = hashTableEnd[hashSlot].m_ordinal;
            uint8_t slotCheckHash = hashTableEnd[hashSlot].m_checkHash;
            if (slotOrdinal == x_inlineHashTableEmptyValue)
            {
                return false;
            }
            if (slotCheckHash == checkHash)
            {
                GeneralHeapPointer<void> prop = GetPropertyNameFromInlineHashTableOrdinal(self, slotOrdinal);
                if (prop == key)
                {
                    result = GetPropertySlotFromInlineHashTableOrdinal(self, slotOrdinal);
                    return true;
                }
            }
            hashSlot = (hashSlot - 1) | hashMask;
            assert(-static_cast<int64_t>(hashTableSize) <= hashSlot && hashSlot < 0);
        }
    }

    static bool HasFinalFullBlockPointer(uint8_t numSlots)
    {
        return numSlots >= x_hiddenClassBlockSize && numSlots % x_hiddenClassBlockSize != 0;
    }

    static ReinterpretCastPreservingAddressSpaceType<SystemHeapPointer<GeneralHeapPointer<void>>*, Structure*> GetFinalFullBlockPointerAddress(Structure* self)
    {
        assert(HasFinalFullBlockPointer(self->m_numSlots));
        return ReinterpretCastPreservingAddressSpace<SystemHeapPointer<GeneralHeapPointer<void>>*>(self->m_values + self->m_nonFullBlockLen);
    }

    static SystemHeapPointer<GeneralHeapPointer<void>> GetFinalFullBlockPointer(Structure* self)
    {
        return *GetFinalFullBlockPointerAddress(self);
    }

    static SystemHeapPointer<Structure> GetHiddenClassOfFullBlockPointer(Structure* self)
    {
        // The full block pointer points at one past the end of the block
        //
        uint32_t addr = GetFinalFullBlockPointer(self).m_value;

        // So subtracting the length of the block gives us the m_values pointer
        //
        addr -= static_cast<uint32_t>(sizeof(GeneralHeapPointer<void>)) * static_cast<uint32_t>(x_hiddenClassBlockSize);

        // Finally, subtract the offset of m_values to get the class pointer
        //
        addr -= static_cast<uint32_t>(OffsetOfTrailingVarLengthArray());

        return SystemHeapPointer<Structure> { addr };
    }

    static GeneralHeapPointer<void> GetPropertyNameFromInlineHashTableOrdinal(Structure* self, int8_t ordinal)
    {
        if (ordinal >= 0)
        {
            // A non-negative offset denote the offset into the non-full block
            //
            assert(ordinal < self->m_nonFullBlockLen);
            return self->m_values[ordinal];
        }
        else
        {
            // Ordinal [-x_hiddenClassBlockSize, 0) denote the offset into the final full block pointer
            // Note that the final full block pointer points at one past the end of the block, so we can simply index using the ordinal
            //
            assert(HasFinalFullBlockPointer(self->m_numSlots));
            assert(-static_cast<int8_t>(x_hiddenClassBlockSize) <= ordinal);
            SystemHeapPointer<GeneralHeapPointer<void>> u = GetFinalFullBlockPointer(self);
            return u.As()[ordinal];
        }
    }

    static uint32_t GetPropertySlotFromInlineHashTableOrdinal(Structure* self, int8_t ordinal)
    {
        assert(-static_cast<int8_t>(x_hiddenClassBlockSize) <= ordinal && ordinal < self->m_nonFullBlockLen);
        AssertImp(ordinal < 0, HasFinalFullBlockPointer(self->m_numSlots));
        int result = static_cast<int>(self->m_numSlots - self->m_nonFullBlockLen) + static_cast<int>(ordinal);
        assert(result >= 0);
        return static_cast<uint32_t>(result);
    }

    static uint32_t ComputeTrailingVarLengthArrayLengthBytes(uint8_t numSlots)
    {
        uint32_t result = ComputeNonFullBlockLength(numSlots) * static_cast<uint32_t>(sizeof(GeneralHeapPointer<void>));
        if (HasFinalFullBlockPointer(numSlots))
        {
            result += static_cast<uint32_t>(sizeof(SystemHeapPointer<GeneralHeapPointer<void>>));
        }
        return result;
    }

    // Get the key that transitioned from parent to self
    //
    static int32_t WARN_UNUSED GetParentEdgeTransitionKey(Structure* self)
    {
        assert(self->m_parentEdgeTransitionKind != TransitionKind::BadTransitionKind);

        switch (self->m_parentEdgeTransitionKind)
        {
        case TransitionKind::AddProperty: [[fallthrough]];
        case TransitionKind::AddPropertyAndGrowPropertyStorageCapacity:
        {
            int32_t transitionKey = GetLastAddedKey(self).m_value;
            assert(transitionKey < 0);
            return transitionKey;
        }
        case TransitionKind::AddMetaTable: [[fallthrough]];
        case TransitionKind::TransitToPolyMetaTable: [[fallthrough]];
        case TransitionKind::TransitToPolyMetaTableAndGrowPropertyStorageCapacity:
        {
            return StructureTransitionTable::x_key_add_or_to_poly_metatable;
        }
        case TransitionKind::RemoveMetaTable:
        {
            return StructureTransitionTable::x_key_remove_metatable;
        }
        case TransitionKind::UpdateArrayType:
        {
            ArrayType arrayType = TCGet(self->m_arrayType);
            return StructureTransitionTable::x_key_change_array_type_tag | static_cast<int32_t>(arrayType.m_asValue);
        }
        case TransitionKind::BadTransitionKind:
        {
            __builtin_unreachable();
        }
        }   // end switch
    }

    StructureTransitionTable* WARN_UNUSED InitializeOutlinedTransitionTable(VM* vm, int32_t keyForOnlyChild, SystemHeapPointer<Structure> onlyChild)
    {
        assert(m_transitionTable.IsType<Structure>());
        assert(m_transitionTable.As<Structure>() == onlyChild.As());
        assert(GetParentEdgeTransitionKey(m_transitionTable.As<Structure>()) == keyForOnlyChild);
        StructureTransitionTable* newTable = StructureTransitionTable::AllocateInitialTable(vm, keyForOnlyChild, onlyChild);
        m_transitionTable.Store(SystemHeapPointer<StructureTransitionTable> { newTable });
        return newTable;
    }

    template<bool isInsert, typename Func>
    Structure* WARN_UNUSED ALWAYS_INLINE QueryTransitionTableAndInsertOrUpsertImpl(VM* vm, int32_t transitionKey, const Func& insertOrUpsertStructureFunc)
    {
        auto getNewStructureForNotFoundCase = [&]() ALWAYS_INLINE -> Structure*
        {
            if constexpr(isInsert)
            {
                return insertOrUpsertStructureFunc();
            }
            else
            {
                return insertOrUpsertStructureFunc(nullptr /*curStructure*/);
            }
        };

        if (unlikely(m_transitionTable.IsNullPtr()))
        {
            Structure* newStructure = getNewStructureForNotFoundCase();
            m_transitionTable.Store(SystemHeapPointer<Structure>(newStructure));
            return newStructure;
        }
        else if (likely(m_transitionTable.IsType<Structure>()))
        {
            Structure* onlyChild = m_transitionTable.As<Structure>();
            assert(onlyChild->m_parent == SystemHeapPointer<Structure>(this));
            int32_t keyForOnlyChild = GetParentEdgeTransitionKey(onlyChild);
            if (keyForOnlyChild == transitionKey)
            {
                // Found. For insert, just return. For upsert, call upsert function and update value
                //
                Structure* curStructure = TranslateToRawPointer(vm, onlyChild);
                if constexpr(isInsert)
                {
                    return curStructure;
                }
                else
                {
                    Structure* newStructure = insertOrUpsertStructureFunc(curStructure);
                    assert(newStructure != nullptr);
                    m_transitionTable.Store(SystemHeapPointer<Structure> { newStructure });
                    return newStructure;
                }
            }
            else
            {
                // We need to create an outlined transition table
                //
                StructureTransitionTable* newTable = InitializeOutlinedTransitionTable(vm, keyForOnlyChild, onlyChild);
                bool found;
                StructureTransitionTable::HashTableEntry* e = StructureTransitionTable::Find(newTable, transitionKey, found /*out*/);
                assert(!found);

                Structure* newStructure = getNewStructureForNotFoundCase();
                e->m_key = transitionKey;
                e->m_value = newStructure;
                newTable->m_numElementsInHashTable++;

                // InitializeOutlinedTransitionTable updates m_transitionTable, but just to make sure
                //
                assert(m_transitionTable.IsType<StructureTransitionTable>() &&
                       TranslateToRawPointer(vm, m_transitionTable.As<StructureTransitionTable>()) == newTable);

                return newStructure;
            }
        }
        else
        {
            assert(m_transitionTable.IsType<StructureTransitionTable>());
            StructureTransitionTable* table = TranslateToRawPointer(vm, m_transitionTable.As<StructureTransitionTable>());
            bool found;
            StructureTransitionTable::HashTableEntry* e = StructureTransitionTable::Find(table, transitionKey, found /*out*/);
            if (!found)
            {
                Structure* newStructure = getNewStructureForNotFoundCase();
                e->m_key = transitionKey;
                e->m_value = newStructure;
                table->m_numElementsInHashTable++;

                if (unlikely(table->ShouldResizeForThisInsertion()))
                {
                    StructureTransitionTable* newTable = table->Expand(vm);
                    m_transitionTable.Store(SystemHeapPointer<StructureTransitionTable>(newTable));
                    assert(!newTable->ShouldResizeForThisInsertion());
                    // FIXME: delete old table!
                }

                return newStructure;
            }
            else
            {
                assert(e->m_key == transitionKey);
                // Found. For insert, just return. For upsert, call upsert function and update value
                //
                Structure* curStructure = TranslateToRawPointer(vm, e->m_value.As());
                assert(TranslateToRawPointer(vm, curStructure->m_parent.As()) == this);
                if constexpr(isInsert)
                {
                    return curStructure;
                }
                else
                {
                    Structure* newStructure = insertOrUpsertStructureFunc(curStructure);
                    assert(newStructure != nullptr);
                    e->m_value = newStructure;
                    return newStructure;
                }
            }
        }
    }

    // 'insertStructureFunc' should take no parameter and return Structure*
    //
    // Query if the transition key exists in the transition table
    // If not, call 'insertStructureFunc' and insert the returned Structure into transition table
    // In either case, returns the structure after the transition
    //
    template<typename Func>
    Structure* WARN_UNUSED ALWAYS_INLINE QueryTransitionTableAndInsert(VM* vm, int32_t transitionKey, const Func& insertStructureFunc)
    {
        return QueryTransitionTableAndInsertOrUpsertImpl<true /*isInsert*/>(vm, transitionKey, insertStructureFunc);
    }

    // 'upsertStructureFunc' should take Structure* and return Structure*
    //
    // Query if the transition key exists in the transition table
    // If yes, call 'upsertStructureFunc' with the current value, and replace it with the returned value
    // If not, call 'upsertStructureFunc' with 'nullptr', and insert the returned Structure into transition table
    // In either case, returns the structure returned by 'upsertStructureFunc'
    //
    template<typename Func>
    Structure* WARN_UNUSED ALWAYS_INLINE QueryTransitionTableAndUpsert(VM* vm, int32_t transitionKey, const Func& upsertStructureFunc)
    {
        return QueryTransitionTableAndInsertOrUpsertImpl<false /*isInsert*/>(vm, transitionKey, upsertStructureFunc);
    }

    static bool WARN_UNUSED IsPolyMetatable(Structure* self)
    {
        return self->m_metatable > 0;
    }

    static bool WARN_UNUSED HasNoMetatable(Structure* self)
    {
        return self->m_metatable == 0;
    }

    static bool WARN_UNUSED HasMonomorphicMetatable(Structure* self)
    {
        return self->m_metatable < 0;
    }

    static uint32_t WARN_UNUSED GetPolyMetatableSlot(Structure* self)
    {
        assert(IsPolyMetatable(self));
        return static_cast<uint32_t>(self->m_metatable - 1);
    }

    static HeapPtr<TableObject> WARN_UNUSED GetMonomorphicMetatable(Structure* self)
    {
        assert(HasMonomorphicMetatable(self));
        return GeneralHeapPointer<TableObject>(self->m_metatable).As();
    }

    static bool WARN_UNUSED IsSlotUsedByPolyMetatable(Structure* self, uint32_t slot)
    {
        return self->m_metatable == static_cast<int32_t>(slot) + 1;
    }

    static SystemHeapPointer<Structure> WARN_UNUSED GetInitialStructureForInlineCapacity(VM* vm, uint32_t inlineCapacity)
    {
        uint8_t stepping = GetInitialStructureSteppingForInlineCapacity(inlineCapacity);
        return GetInitialStructureForStepping(vm, stepping);
    }

    static uint8_t WARN_UNUSED GetInitialStructureSteppingForInlineCapacity(uint32_t inlineCapacity)
    {
        if (inlineCapacity > Structure::x_maxNumSlots)
        {
            inlineCapacity = Structure::x_maxNumSlots;
        }

        uint8_t stepping = internal::x_optimalInlineCapacitySteppingArray[inlineCapacity];
        assert(stepping < x_numInlineCapacitySteppings);
        assert(internal::x_inlineStorageSizeForSteppingArray[stepping] == internal::x_optimalInlineCapacityArray[inlineCapacity]);
        assert(internal::x_optimalInlineCapacityArray[internal::x_inlineStorageSizeForSteppingArray[stepping]] == internal::x_optimalInlineCapacityArray[inlineCapacity]);
        return stepping;
    }

    static SystemHeapPointer<Structure> WARN_UNUSED GetInitialStructureForStepping(VM* vm, uint8_t stepping)
    {
        std::array<SystemHeapPointer<Structure>, x_numInlineCapacitySteppings>& arr = vm->GetInitialStructureForDifferentInlineCapacityArray();
        assert(stepping < x_numInlineCapacitySteppings);
        if (likely(arr[stepping].m_value != 0))
        {
            return arr[stepping];
        }

        uint8_t optimalInlineCapacity = internal::x_inlineStorageSizeForSteppingArray[stepping];
        Structure* s = Structure::CreateInitialStructure(vm, optimalInlineCapacity);
        arr[stepping] = s;
        return arr[stepping];
    }

    static SystemHeapPointer<Structure> WARN_UNUSED GetInitialStructureForSteppingKnowingAlreadyBuilt(VM* vm, uint8_t stepping)
    {
        assert(stepping < x_numInlineCapacitySteppings);
        SystemHeapPointer<Structure> r = vm->GetInitialStructureForDifferentInlineCapacityArray()[stepping];
        assert(r.m_value != 0);
        return r;
    }

    // DEVNOTE: If you add a field, make sure to update both CreateInitialStructure
    // and CreateStructureForTransitionImpl to initialize the field!
    //

    // The total number of in-use slots in the represented object (this is different from the capacity!)
    //
    uint8_t m_numSlots;

    // The length for the non-full block
    // Can be computed from m_numSlots but we store it for simplicity since we have a byte to spare here
    //
    uint8_t m_nonFullBlockLen;

    // The anchor hash table, 0 if not exist
    // Anchor hash table only exists if there are at least 2 * x_hiddenClassBlockSize entries,
    // so hidden class smaller than that doesn't query anchor hash table
    //
    SystemHeapPointer<StructureAnchorHashTable> m_anchorHashTable;

    // For now we don't consider delete (so we need to check for nil if metatable may be non-nil)
    // If we need to support delete, we need to use a special record to denote an entry is deleted
    // by the inline hash table, we probably will also need a free list to recycle the deleted slots
    //

    ArrayType m_arrayType;

    // TODO: replace by actual implementation
    //
    uint8_t m_lock;

    static_assert(std::is_same_v<LuaMetamethodBitVectorT, uint16_t>, "you should reorder the members to minimize padding in this structure");
    LuaMetamethodBitVectorT m_knownNonexistentMetamethods;

    // The hash mask of the inline hash table
    // The inline hash table contains entries for the last full block and all entries in the non-full block
    //
    uint8_t m_inlineHashTableMask;

    // The object's inline named property storage capacity
    // slot [0, m_inlineNamedStorageCapacity) goes in the inline storage
    //
    uint8_t m_inlineNamedStorageCapacity;

    // The object's butterfly named property storage capacity
    // slot >= m_inlineNamedStorageCapacity goes here
    //
    uint8_t m_butterflyNamedStorageCapacity;

    // The kind of the transition that transitioned from the parent to this node
    //
    TransitionKind m_parentEdgeTransitionKind;

    // The parent of this node
    //
    SystemHeapPointer<Structure> m_parent;

    static constexpr int32_t x_noMetaTable = 0;

    // The metatable of the object
    // If < 0, this should be interpreted as a GeneralHeapPointer, denoting the metatable shared by all objects with this structure.
    // If = 0, it means all objects with this structure has no metatable.
    // If > 0, it means the structure is in PolyMetatable mode: objects with this structure do not all have the identical metatable,
    // and 'm_metatable - 1' is the slot ordinal where the metatable is stored in the object.
    //
    int32_t m_metatable;

    // If it is a Structure pointer, it means that currently there exists only one transition, which is to that pointer.
    //
    using TransitionTableType = SystemHeapPointerTaggedUnion<Structure, StructureTransitionTable>;
    TransitionTableType m_transitionTable;

    // The value var-length array contains the non-full block, and,
    // if m_numSlots >= x_hiddenClassBlockSize && m_numSlot % x_hiddenClassBlockSize != 0, the pointer to the last full block
    //
    GeneralHeapPointer<void> m_values[0];
};
static_assert(sizeof(Structure) == 32);

// WARNING: if the structure is in PolyMetatable mode, this iterator will return the key 'vm->GetSpecialKeyForPolyMetatable()' for the
// PolyMetatable slot! User should special check this case themselves if this behavior is undesired
//
class StructureIterator
{
public:
    StructureIterator(SystemHeapPointer<Structure> hc)
    {
        Structure* hiddenClass = hc.As();
        assert(hiddenClass->m_type == HeapEntityType::Structure);
        SystemHeapPointer<StructureAnchorHashTable> aht = TCGet(hiddenClass->m_anchorHashTable);

        m_hiddenClass = hc;
        m_anchorTable = aht;
        m_ord = 0;
        m_maxOrd = hiddenClass->m_numSlots;

        if (aht.m_value != 0)
        {
            StructureAnchorHashTable* anchorHashTable = aht.As();
            assert(anchorHashTable->m_numBlocks > 0);
            m_curPtr = anchorHashTable->m_blockPointers[0];
            m_anchorTableMaxOrd = anchorHashTable->m_numTotalSlots;
        }
        else
        {
            m_anchorTableMaxOrd = 0;
            if (Structure::HasFinalFullBlockPointer(hiddenClass->m_numSlots))
            {
                m_curPtr = Structure::GetFinalFullBlockPointer(hiddenClass).As() - x_hiddenClassBlockSize;
            }
            else
            {
                m_curPtr = m_hiddenClass.As<uint8_t>() + Structure::OffsetOfTrailingVarLengthArray();
            }
        }
    }

    StructureIterator(Structure* hiddenClass)
        : StructureIterator(SystemHeapPointer<Structure> { hiddenClass })
    { }

    bool HasMore()
    {
        return m_ord < m_maxOrd;
    }

    GeneralHeapPointer<void> GetCurrentKey()
    {
        assert(m_ord < m_maxOrd);
        return TCGet(*m_curPtr.As());
    }

    uint8_t GetCurrentSlotOrdinal()
    {
        assert(m_ord < m_maxOrd);
        return m_ord;
    }

    void Advance()
    {
        m_ord++;
        if (unlikely((m_ord & static_cast<uint8_t>(x_hiddenClassBlockSize - 1)) == 0))
        {
            if (m_ord < m_maxOrd)
            {
                Structure* hiddenClass = m_hiddenClass.As<Structure>();
                if (m_ord == m_anchorTableMaxOrd)
                {
                    if (Structure::HasFinalFullBlockPointer(m_maxOrd) &&
                        !Structure::IsAnchorTableContainsFinalBlock(hiddenClass))
                    {
                        m_curPtr = Structure::GetFinalFullBlockPointer(hiddenClass).As() - x_hiddenClassBlockSize;
                    }
                    else
                    {
                        m_curPtr = m_hiddenClass.m_value + static_cast<uint32_t>(Structure::OffsetOfTrailingVarLengthArray());
                    }
                }
                else if (m_ord > m_anchorTableMaxOrd)
                {
                    assert(m_ord == m_anchorTableMaxOrd + x_hiddenClassBlockSize);
                    assert(Structure::HasFinalFullBlockPointer(hiddenClass->m_numSlots) &&
                           !Structure::IsAnchorTableContainsFinalBlock(hiddenClass));
                    assert(m_curPtr.As() == Structure::GetFinalFullBlockPointer(hiddenClass).As() - 1);
                    m_curPtr = m_hiddenClass.As<uint8_t>() + Structure::OffsetOfTrailingVarLengthArray();
                }
                else
                {
                    StructureAnchorHashTable* anchorTable = m_anchorTable.As<StructureAnchorHashTable>();
                    m_curPtr = anchorTable->m_blockPointers[m_ord >> x_log2_hiddenClassBlockSize];
                }
            }
        }
        else
        {
            m_curPtr = m_curPtr.As() + 1;
        }
    }

private:
    SystemHeapPointer<Structure> m_hiddenClass;
    // It's important to store m_anchorTable here since it may change
    //
    SystemHeapPointer<StructureAnchorHashTable> m_anchorTable;
    SystemHeapPointer<GeneralHeapPointer<void>> m_curPtr;
    uint8_t m_ord;
    uint8_t m_maxOrd;
    uint8_t m_anchorTableMaxOrd;
};
static_assert(sizeof(StructureIterator) == 16);

UserHeapPointer<void> WARN_UNUSED GetPolyMetatableFromObjectWithStructureHiddenClass(TableObject* obj, uint32_t slot, uint32_t inlineCapacity);

class CacheableDictionary final : public SystemHeapGcObjectHeader
{
public:
    ~CacheableDictionary()
    {
        if (m_hashTable != nullptr)
        {
            delete [] m_hashTable;
        }
    }

    struct HashTableEntry
    {
        GeneralHeapPointer<void> m_key;
        uint32_t m_slot;
    };
    static_assert(sizeof(HashTableEntry) == 8);

    // Create an empty CacheableDictionary with expected 'numSlots' properties and specified inline storage capacity
    //
    static CacheableDictionary* WARN_UNUSED CreateEmptyDictionary(VM* vm, uint32_t anticipatedNumSlots, uint8_t inlineCapacity, bool shouldNeverTransitToUncacheableDictionary)
    {
        CacheableDictionary* r = TranslateToRawPointer(vm, vm->AllocFromSystemHeap(sizeof(CacheableDictionary)).AsNoAssert<CacheableDictionary>());
        SystemHeapGcObjectHeader::Populate(r);
        r->m_shouldNeverTransitToUncacheableDictionary = shouldNeverTransitToUncacheableDictionary;
        r->m_inlineNamedStorageCapacity = inlineCapacity;
        r->m_butterflyNamedStorageCapacity = 0;
        uint32_t hashTableMask = RoundUpToPowerOfTwo(anticipatedNumSlots) * 2 - 1;
        hashTableMask = std::max(hashTableMask, 127U);
        r->m_hashTableMask = hashTableMask;
        r->m_slotCount = 0;
        r->m_hashTable = new HashTableEntry[hashTableMask + 1];
        r->m_metatable.m_value = 0;
        memset(r->m_hashTable, 0, sizeof(HashTableEntry) * (hashTableMask + 1));
        return r;
    }

    // FIXME: we need to think about the GC story and interaction with IC here
    //
    CacheableDictionary* WARN_UNUSED RelocateForAddingOrRemovingMetatable(VM* vm)
    {
        CacheableDictionary* r = TranslateToRawPointer(vm, vm->AllocFromSystemHeap(sizeof(CacheableDictionary)).AsNoAssert<CacheableDictionary>());
        // m_metatable field is intentionally not populated because it shall be populated by our caller
        //
        SystemHeapGcObjectHeader::Populate(r);
        r->m_shouldNeverTransitToUncacheableDictionary = m_shouldNeverTransitToUncacheableDictionary;
        r->m_inlineNamedStorageCapacity = m_inlineNamedStorageCapacity;
        r->m_butterflyNamedStorageCapacity = m_butterflyNamedStorageCapacity;
        r->m_hashTableMask = m_hashTableMask;
        r->m_slotCount = m_slotCount;
        r->m_hashTable = m_hashTable;
        // Since CacheableDictionary and object is 1-on-1, 'this' will never be used anymore, so just have the new dictionary steal our hash table
        //
        m_hashTable = nullptr;
        return r;
    }

    CacheableDictionary* WARN_UNUSED Clone(VM* vm)
    {
        CacheableDictionary* r = TranslateToRawPointer(vm, vm->AllocFromSystemHeap(sizeof(CacheableDictionary)).AsNoAssert<CacheableDictionary>());
        SystemHeapGcObjectHeader::Populate(r);
        r->m_shouldNeverTransitToUncacheableDictionary = m_shouldNeverTransitToUncacheableDictionary;
        r->m_inlineNamedStorageCapacity = m_inlineNamedStorageCapacity;
        r->m_butterflyNamedStorageCapacity = m_butterflyNamedStorageCapacity;
        r->m_hashTableMask = m_hashTableMask;
        r->m_slotCount = m_slotCount;
        r->m_hashTable = new HashTableEntry[m_hashTableMask + 1];
        memcpy(r->m_hashTable, m_hashTable, sizeof(HashTableEntry) * (m_hashTableMask + 1));
        r->m_metatable = m_metatable;
        return r;
    }

    struct CreateFromStructureResult
    {
        CacheableDictionary* m_dictionary;
        // The slot to put the value for the newly inserted prop
        //
        uint32_t m_slot;
        bool m_shouldGrowButterfly;
    };

    static uint32_t WARN_UNUSED GetInitOrNextButterflyCapacity(CacheableDictionary* self)
    {
        if (self->m_butterflyNamedStorageCapacity == 0)
        {
            uint32_t newCapacity = ButterflyNamedStorageGrowthPolicy::ComputeInitialButterflyCapacityForDictionary(self->m_inlineNamedStorageCapacity);
            assert(newCapacity > 0);
            return newCapacity;
        }
        else
        {
            uint32_t oldCapacity = self->m_butterflyNamedStorageCapacity;
            uint32_t newCapacity = ButterflyNamedStorageGrowthPolicy::ComputeNextButterflyCapacityForDictionaryOrFail(oldCapacity);
            assert(newCapacity > oldCapacity);
            return newCapacity;
        }
    }

    // Handle the case that a structure transitions to a CacheableDictionary due to too many properties
    //
    static void CreateFromStructure(VM* vm, TableObject* obj, Structure* structure, UserHeapPointer<void> newProp, CreateFromStructureResult& result /*out*/)
    {
        uint32_t neededSlots = structure->m_numSlots;
        // One more slot for the newly added key
        //
        neededSlots += 1;
        if (Structure::IsPolyMetatable(structure))
        {
            // Now the metatable is stored in the dictionary, since dictionary is 1-to-1 with object
            // (but since we don't change dictionary pointer when we change metatable, the metatable is still not uncacheable, i.e. behaves like PolyMetatable)
            //
            neededSlots -= 1;
        }

        // Populate various fields
        //
        CacheableDictionary* r = CacheableDictionary::CreateEmptyDictionary(vm, neededSlots, structure->m_inlineNamedStorageCapacity, false /*shouldNeverTransitToUncacheableDictionary*/);
        r->m_butterflyNamedStorageCapacity = structure->m_butterflyNamedStorageCapacity;
        if (Structure::IsPolyMetatable(structure))
        {
            r->m_metatable = GetPolyMetatableFromObjectWithStructureHiddenClass(obj, Structure::GetPolyMetatableSlot(structure), structure->m_inlineNamedStorageCapacity);
        }
        else
        {
            r->m_metatable = GeneralHeapPointer<void>(structure->m_metatable).As();
        }

        result.m_dictionary = r;
        if (Structure::IsPolyMetatable(structure))
        {
            result.m_shouldGrowButterfly = false;
            result.m_slot = Structure::GetPolyMetatableSlot(structure);
        }
        else if (structure->m_inlineNamedStorageCapacity + structure->m_butterflyNamedStorageCapacity == structure->m_numSlots)
        {
            result.m_shouldGrowButterfly = true;
            r->m_butterflyNamedStorageCapacity = GetInitOrNextButterflyCapacity(r);
            result.m_slot = structure->m_numSlots;
        }
        else
        {
            result.m_shouldGrowButterfly = false;
            result.m_slot = structure->m_numSlots;
        }

        assert(r->m_butterflyNamedStorageCapacity + r->m_inlineNamedStorageCapacity >= neededSlots);
        assert(result.m_slot < neededSlots);
        AssertImp(result.m_shouldGrowButterfly, r->m_butterflyNamedStorageCapacity > structure->m_butterflyNamedStorageCapacity);
        AssertImp(!result.m_shouldGrowButterfly, r->m_butterflyNamedStorageCapacity == structure->m_butterflyNamedStorageCapacity);

        // Insert the properties into the hash table
        //
#ifndef NDEBUG
        std::unordered_set<int64_t> showedUpKeys;
        std::unordered_set<uint32_t> showedUpSlots;
#endif
        StructureIterator iterator(structure);
        while (iterator.HasMore())
        {
            UserHeapPointer<void> key = iterator.GetCurrentKey().As();
            uint32_t keySlot = iterator.GetCurrentSlotOrdinal();
            iterator.Advance();

            if (key == vm->GetSpecialKeyForMetadataSlot().As<void>())
            {
                continue;
            }
            assert(keySlot != result.m_slot);

#ifndef NDEBUG
            assert(!showedUpKeys.count(key.m_value));
            assert(!showedUpSlots.count(keySlot));
            showedUpKeys.insert(key.m_value);
            showedUpSlots.insert(keySlot);
#endif

            r->InsertNonExistentPropertyForInitOrResize(key, StructureKeyHashHelper::GetHashValueForMaybeNonStringKey(key), keySlot);
        }

        assert(!showedUpKeys.count(newProp.m_value));
        assert(!showedUpSlots.count(result.m_slot));
        assert(showedUpKeys.size() + 1 == neededSlots);

        r->InsertNonExistentPropertyForInitOrResize(newProp, StructureKeyHashHelper::GetHashValueForMaybeNonStringKey(newProp), result.m_slot);

        r->m_slotCount = neededSlots;
    }

    // Only used for initialization and resize, so this does not check for resize, and does not update slot count!
    //
    void InsertNonExistentPropertyForInitOrResize(UserHeapPointer<void> prop, uint32_t propHash, uint32_t slotOrdinal)
    {
        size_t htMask = m_hashTableMask;
        size_t slot = propHash & htMask;
        while (m_hashTable[slot].m_key.m_value != 0)
        {
            assert(TranslateToRawPointer(m_hashTable[slot].m_key.As()) != prop.As());
            slot = (slot + 1) & htMask;
        }
        m_hashTable[slot].m_key = prop.As();
        m_hashTable[slot].m_slot = slotOrdinal;
    }

    // After an insertion, resize the hash table if needed. Return true if the hash table is resized.
    //
    // If returned true, the caller should check if the CacheableDictionary should transit to UncacheableDictionary
    // due to the table containing too many elements but the values of most of them are nil
    //
    static bool WARN_UNUSED ResizeIfNeeded(CacheableDictionary* self)
    {
        if (likely(self->m_slotCount * 2 < self->m_hashTableMask))
        {
            return false;
        }

        self->ResizeImpl();
        return true;
    }

    void NO_INLINE ResizeImpl()
    {
        assert(is_power_of_2(m_hashTableMask + 1));
        uint32_t oldMask = m_hashTableMask;
        uint32_t newMask = oldMask * 2 + 1;
        ReleaseAssert(newMask < std::numeric_limits<uint32_t>::max());
        m_hashTableMask = newMask;

        HashTableEntry* oldHt = m_hashTable;
        m_hashTable = new HashTableEntry[newMask + 1];
        memset(m_hashTable, 0, sizeof(HashTableEntry) * (newMask + 1));

        DEBUG_ONLY(uint32_t cnt = 0;)
        HashTableEntry* oldHtEnd = oldHt + oldMask + 1;
        HashTableEntry* curEntry = oldHt;
        while (curEntry < oldHtEnd)
        {
            if (curEntry->m_key.m_value != 0)
            {
                UserHeapPointer<void> key = curEntry->m_key.As();
                uint32_t keySlot = curEntry->m_slot;
                InsertNonExistentPropertyForInitOrResize(key, StructureKeyHashHelper::GetHashValueForMaybeNonStringKey(key), keySlot);
                DEBUG_ONLY(cnt++;)
            }
            curEntry++;
        }
        assert(cnt == m_slotCount);

        delete [] oldHt;
    }

    // Query the slot for a property
    // Return false if the property is not found, the 'hashSlot' output can be used for insertion
    //
    static bool WARN_UNUSED ALWAYS_INLINE GetSlotOrdinalFromPropertyImpl(CacheableDictionary* self, UserHeapPointer<void> prop, uint32_t propHash, size_t& slotForInsertion /*out*/, uint32_t& slotOrdinal /*out*/)
    {
        size_t hashMask = self->m_hashTableMask;
        size_t slot = propHash & hashMask;
        GeneralHeapPointer<void> gprop = prop.As();
        while (true)
        {
            GeneralHeapPointer<void> key = self->m_hashTable[slot].m_key;
            if (key.m_value == 0)
            {
                slotForInsertion = slot;
                return false;
            }
            if (key == gprop)
            {
                slotOrdinal = self->m_hashTable[slot].m_slot;
                return true;
            }
            slot = (slot + 1) & hashMask;
        }
    }

    // Query the hash table slot for a property
    // This weird function is only used by the Lua 'next' slow path
    //
    static uint32_t WARN_UNUSED GetHashTableSlotNumberForProperty(CacheableDictionary* self, UserHeapPointer<void> prop)
    {
        size_t hashMask = self->m_hashTableMask;
        size_t slot = StructureKeyHashHelper::GetHashValueForMaybeNonStringKey(prop) & hashMask;
        GeneralHeapPointer<void> gprop = prop.As();
        while (true)
        {
            GeneralHeapPointer<void> key = self->m_hashTable[slot].m_key;
            if (key.m_value == 0)
            {
                return static_cast<uint32_t>(-1);
            }
            if (key == gprop)
            {
                return static_cast<uint32_t>(slot);
            }
            slot = (slot + 1) & hashMask;
        }
    }

    static bool WARN_UNUSED GetSlotOrdinalFromStringProperty(CacheableDictionary* self, UserHeapPointer<HeapString> prop, uint32_t& slotOrdinal /*out*/)
    {
        size_t slotForInsertionUnused;
        return GetSlotOrdinalFromPropertyImpl(self, prop.As<void>(), StructureKeyHashHelper::GetHashValueForStringKey(prop), slotForInsertionUnused /*out*/, slotOrdinal /*out*/);
    }

    static bool WARN_UNUSED GetSlotOrdinalFromMaybeNonStringProperty(CacheableDictionary* self, UserHeapPointer<void> prop, uint32_t& slotOrdinal /*out*/)
    {
        size_t slotForInsertionUnused;
        return GetSlotOrdinalFromPropertyImpl(self, prop, StructureKeyHashHelper::GetHashValueForMaybeNonStringKey(prop), slotForInsertionUnused /*out*/, slotOrdinal /*out*/);
    }

    struct PutByIdResult
    {
        uint32_t m_slot;
        uint32_t m_newButterflyCapacity;
        bool m_shouldGrowButterfly;
        bool m_shouldCheckForTransitionToUncacheableDictionary;
    };

    static void ALWAYS_INLINE PreparePutPropertyImpl(CacheableDictionary* self, UserHeapPointer<void> prop, uint32_t propHash, PutByIdResult& result /*out*/)
    {
        size_t slotForInsertion;
        if (GetSlotOrdinalFromPropertyImpl(self, prop, propHash, slotForInsertion /*out*/, result.m_slot /*out*/))
        {
            result.m_shouldGrowButterfly = false;
            result.m_shouldCheckForTransitionToUncacheableDictionary = false;
            return;
        }

        // Slot not found
        //
        result.m_slot = self->m_slotCount;
        assert(self->m_slotCount <= self->m_inlineNamedStorageCapacity + self->m_butterflyNamedStorageCapacity);
        if (self->m_slotCount == self->m_inlineNamedStorageCapacity + self->m_butterflyNamedStorageCapacity)
        {
            result.m_shouldGrowButterfly = true;
            result.m_newButterflyCapacity = GetInitOrNextButterflyCapacity(self);
        }
        else
        {
            result.m_shouldGrowButterfly = false;
        }

        // insert into hash table
        //
        self->m_hashTable[slotForInsertion].m_key = GeneralHeapPointer<void>(prop.As());
        self->m_hashTable[slotForInsertion].m_slot = result.m_slot;
        self->m_slotCount++;
        result.m_shouldCheckForTransitionToUncacheableDictionary = ResizeIfNeeded(self);
    }

    static void PreparePutById(CacheableDictionary* self, UserHeapPointer<HeapString> prop, PutByIdResult& result /*out*/)
    {
        PreparePutPropertyImpl(self, prop.As<void>(), StructureKeyHashHelper::GetHashValueForStringKey(prop), result);
    }

    static void PreparePutByMaybeNonStringKey(CacheableDictionary* self, UserHeapPointer<void> prop, PutByIdResult& result /*out*/)
    {
        PreparePutPropertyImpl(self, prop, StructureKeyHashHelper::GetHashValueForMaybeNonStringKey(prop), result);
    }

    // For the global object we should make it never transit to UncacheableDictionary, as that would destroy the performance for all global variable access
    //
    bool m_shouldNeverTransitToUncacheableDictionary;
    uint8_t m_inlineNamedStorageCapacity;
    uint32_t m_butterflyNamedStorageCapacity;
    uint32_t m_hashTableMask;
    uint32_t m_slotCount;
    HashTableEntry* m_hashTable;
    // Whenever this value is changed from zero to non-zero, or from non-zero to zero, we must relocate the structure, otherwise we would break the IC!
    //
    UserHeapPointer<void> m_metatable;
};

inline StructureAnchorHashTable* WARN_UNUSED StructureAnchorHashTable::Create(VM* vm, Structure* shc)
{
    uint8_t numElements = shc->m_numSlots;
    assert(numElements % x_hiddenClassBlockSize == 0);
    uint8_t numBlocks = numElements >> x_log2_hiddenClassBlockSize;

    // Do the space calculations and allocate the struct
    //
    uint32_t hashTableSize = RoundUpToPowerOfTwo(static_cast<uint32_t>(numElements)) * 2;
    assert(hashTableSize % 8 == 0);
    int64_t hashTableMask = ~static_cast<int64_t>(hashTableSize - 1);

    uint32_t hashTableLengthBytes = hashTableSize * static_cast<uint32_t>(sizeof(HashTableEntry));
    uint32_t trailingArrayLengthBytes = numBlocks * static_cast<uint32_t>(sizeof(SystemHeapPointer<GeneralHeapPointer<void>>));
    uint32_t allocationSize = hashTableLengthBytes + static_cast<uint32_t>(OffsetOfTrailingVarLengthArray()) + trailingArrayLengthBytes;
    allocationSize = RoundUpToMultipleOf<8>(allocationSize);
    SystemHeapPointer<void> objectAddressStart = vm->AllocFromSystemHeap(allocationSize);

    // First, fill in the header
    //
    HashTableEntry* hashTableStart = TranslateToRawPointer(vm, objectAddressStart.As<HashTableEntry>());
    HashTableEntry* hashTableEnd = hashTableStart + hashTableSize;
    StructureAnchorHashTable* r = reinterpret_cast<StructureAnchorHashTable*>(hashTableEnd);
    ConstructInPlace(r);
    SystemHeapGcObjectHeader::Populate(r);
    r->m_numBlocks = numBlocks;
    r->m_numTotalSlots = numElements;
    r->m_hashTableMask = static_cast<int32_t>(hashTableMask);
    assert(GetHashTableSizeFromHashTableMask(r->m_hashTableMask) == hashTableSize);

    SystemHeapPointer<StructureAnchorHashTable> oldAnchorTableV = shc->m_anchorHashTable;
    AssertImp(oldAnchorTableV.m_value == 0, numElements == x_hiddenClassBlockSize);
    AssertImp(oldAnchorTableV.m_value != 0, oldAnchorTableV.As()->m_numBlocks == numBlocks - 1);

    // Now, copy in the content of the existing anchor table
    //
    if (oldAnchorTableV.m_value != 0)
    {
        // Copy in the hash table
        //
        StructureAnchorHashTable* oldAnchorTable = TranslateToRawPointer(vm, oldAnchorTableV.As<StructureAnchorHashTable>());
        oldAnchorTable->CloneHashTableTo(hashTableStart, hashTableSize);

        // Copy in the block pointer list
        //
        SafeMemcpy(r->m_blockPointers, oldAnchorTable->m_blockPointers, sizeof(SystemHeapPointer<GeneralHeapPointer<void>>) * oldAnchorTable->m_numBlocks);
    }
    else
    {
        memset(hashTableStart, x_hashTableEmptyValue, sizeof(HashTableEntry) * hashTableSize);
    }

    // Now, insert the new full block into the hash table
    //
    for (uint32_t i = 0; i < x_hiddenClassBlockSize; i++)
    {
        GeneralHeapPointer<void> e  = shc->m_values[i];
        UserHeapPointer<void> eu { e.As<void>() };
        uint32_t hashValue = StructureKeyHashHelper::GetHashValueForMaybeNonStringKey(eu);

        uint8_t checkHash = static_cast<uint8_t>(hashValue);
        int64_t hashSlot = static_cast<int64_t>(hashValue >> 8) | hashTableMask;
        assert(-static_cast<int64_t>(hashTableSize) <= hashSlot && hashSlot < 0);

        while (hashTableEnd[hashSlot].m_ordinal != x_hashTableEmptyValue)
        {
            hashSlot = (hashSlot - 1) | hashTableMask;
            assert(-static_cast<int64_t>(hashTableSize) <= hashSlot && hashSlot < 0);
        }

        hashTableEnd[hashSlot].m_ordinal = static_cast<uint8_t>(numElements - x_hiddenClassBlockSize + i);
        hashTableEnd[hashSlot].m_checkHash = checkHash;
    }

    // And finally fill in the pointer for the new block
    //
    {
        SystemHeapPointer<uint8_t> base = vm->GetHeapPtrTranslator().TranslateToSystemHeapPtr(shc).As<uint8_t>();
        base = base.As() + static_cast<uint32_t>(Structure::OffsetOfTrailingVarLengthArray());
        r->m_blockPointers[numBlocks - 1] = base.As<GeneralHeapPointer<void>>();
    }

    // In debug mode, check that the anchor hash table contains all the expected elements, no more and no less
    //
#ifndef NDEBUG
    {
        // Make sure all element in the list are distinct and can be found in the hash table
        //
        ReleaseAssert(r->m_numTotalSlots == numElements);
        uint32_t elementCount = 0;
        std::set<int64_t> elementSet;
        for (uint8_t i = 0; i < r->m_numTotalSlots; i++)
        {
            GeneralHeapPointer<void> p = StructureAnchorHashTable::GetPropertyNameAtSlot(r, i);
            UserHeapPointer<void> key { p.As() };
            ReleaseAssert(!elementSet.count(key.m_value));
            elementSet.insert(key.m_value);
            elementCount++;

            uint32_t querySlot = static_cast<uint32_t>(-1);
            bool found = StructureAnchorHashTable::GetSlotOrdinalFromPropertyNameAndHash(
                        r, p, StructureKeyHashHelper::GetHashValueForMaybeNonStringKey(key), querySlot /*out*/);
            ReleaseAssert(found);
            ReleaseAssert(querySlot == i);
        }
        ReleaseAssert(elementCount == r->m_numTotalSlots && elementSet.size() == elementCount);

        // Make sure the hash table doesn't contain anything other than the elements in the list
        //
        uint32_t elementCountHt = 0;
        std::set<int64_t> elementSetHt;
        for (uint32_t i = 0; i < GetHashTableSizeFromHashTableMask(r->m_hashTableMask); i++)
        {
            HashTableEntry e = GetHashTableBegin(r)[i];
            if (e.m_ordinal != x_hashTableEmptyValue)
            {
                GeneralHeapPointer<void> p = StructureAnchorHashTable::GetPropertyNameAtSlot(r, e.m_ordinal);
                UserHeapPointer<void> key { p.As() };
                ReleaseAssert(elementSet.count(key.m_value));
                ReleaseAssert(!elementSetHt.count(key.m_value));
                elementSetHt.insert(key.m_value);
                elementCountHt++;
            }
        }
        ReleaseAssert(elementCountHt == elementCount);
        ReleaseAssert(elementSetHt.size() == elementCount);
    }
#endif

    return r;
}

inline void StructureAnchorHashTable::CloneHashTableTo(StructureAnchorHashTable::HashTableEntry* htStart, uint32_t htSize)
{
    uint32_t selfHtSize = GetHashTableSizeFromHashTableMask(m_hashTableMask);
    assert(htSize >= selfHtSize);

    if (htSize == selfHtSize)
    {
        // If the target hash table has equal size, a memcpy is sufficient
        //
        SafeMemcpy(htStart, GetHashTableBegin(this), sizeof(HashTableEntry) * htSize);
    }
    else
    {
        // Otherwise, we must insert every element into the new hash table
        //
        memset(htStart, x_hashTableEmptyValue, sizeof(HashTableEntry) * htSize);

        assert(is_power_of_2(htSize));
        HashTableEntry* htEnd = htStart + htSize;
        int64_t htMask = ~ZeroExtendTo<int64_t>(htSize - 1);
        assert(htMask < 0);
        for (uint8_t blockOrd = 0; blockOrd < m_numBlocks; blockOrd++)
        {
            GeneralHeapPointer<void>* p = m_blockPointers[blockOrd].As();
            for (uint8_t offset = 0; offset < x_hiddenClassBlockSize; offset++)
            {
                GeneralHeapPointer<void> e = p[offset];
                UserHeapPointer<void> eu { e.As() };
                uint32_t hashValue = StructureKeyHashHelper::GetHashValueForMaybeNonStringKey(eu);

                uint8_t checkHash = static_cast<uint8_t>(hashValue);
                int64_t hashSlot = static_cast<int64_t>(hashValue >> 8) | htMask;
                assert(-static_cast<int64_t>(htSize) <= hashSlot && hashSlot < 0);

                while (htEnd[hashSlot].m_ordinal != x_hashTableEmptyValue)
                {
                    hashSlot = (hashSlot - 1) | htMask;
                    assert(-static_cast<int64_t>(htSize) <= hashSlot && hashSlot < 0);
                }

                htEnd[hashSlot].m_ordinal = static_cast<uint8_t>((blockOrd << x_log2_hiddenClassBlockSize) | offset);
                htEnd[hashSlot].m_checkHash = checkHash;
            }
        }
    }
}

inline SystemHeapPointer<StructureAnchorHashTable> WARN_UNUSED Structure::BuildNewAnchorTableIfNecessary(Structure* self)
{
    assert(self->m_nonFullBlockLen == x_hiddenClassBlockSize);
    assert(self->m_numSlots > 0 && self->m_numSlots % x_hiddenClassBlockSize == 0);
    SystemHeapPointer<StructureAnchorHashTable> anchorHt = TCGet(self->m_anchorHashTable);
    AssertIff(!IsAnchorTableContainsFinalBlock(self), anchorHt.m_value == 0 || anchorHt.As()->m_numTotalSlots != self->m_numSlots);
    if (!IsAnchorTableContainsFinalBlock(self))
    {
        AssertImp(anchorHt.m_value != 0, anchorHt.As()->m_numTotalSlots == self->m_numSlots - static_cast<uint8_t>(x_hiddenClassBlockSize));
        // The updated anchor hash table has not been built, we need to build it now
        //
        VM* vm = VM::GetActiveVMForCurrentThread();
        Structure* selfRaw = TranslateToRawPointer(vm, self);
        StructureAnchorHashTable* newAnchorTable = StructureAnchorHashTable::Create(vm, selfRaw);
        selfRaw->m_anchorHashTable = vm->GetHeapPtrTranslator().TranslateToSystemHeapPtr(newAnchorTable);
        anchorHt = selfRaw->m_anchorHashTable;

        // Once the updated anchor hash table is built, we don't need the inline hash table any more, empty it out
        //
        InlineHashTableEntry* ht = GetInlineHashTableBegin(selfRaw);
        size_t htLengthBytes = ComputeHashTableSizeFromHashTableMask(self->m_inlineHashTableMask) * sizeof(InlineHashTableEntry);
        memset(ht, x_inlineHashTableEmptyValue, htLengthBytes);
    }

    assert(anchorHt.As()->m_numTotalSlots == self->m_numSlots);
    return anchorHt;
}

inline Structure* WARN_UNUSED Structure::CreateStructureForTransitionImpl(VM* vm, SlotAdditionKind slotAdditionKind, UserHeapPointer<void> key)
{
    bool shouldAddKey = (slotAdditionKind != SlotAdditionKind::NoSlotAdded);

    AssertIff(slotAdditionKind == SlotAdditionKind::AddSlotForPolyMetatable, key == vm->GetSpecialKeyForMetadataSlot().As<void>());

    // Doesn't make sense to transit to PolyMetatable mode if we are already in PolyMetatable mode
    //
    AssertImp(slotAdditionKind == SlotAdditionKind::AddSlotForPolyMetatable, m_metatable <= 0);

    AssertImp(shouldAddKey, m_numSlots < x_maxNumSlots || (m_numSlots == x_maxNumSlots && slotAdditionKind == SlotAdditionKind::AddSlotForPolyMetatable));
    assert(m_numSlots <= static_cast<uint32_t>(m_inlineNamedStorageCapacity) + m_butterflyNamedStorageCapacity);
    assert(m_nonFullBlockLen <= x_hiddenClassBlockSize);

    // Work out various properties of the new hidden class
    //
    SystemHeapPointer<StructureAnchorHashTable> anchorTableForNewNode;
    uint8_t nonFullBlockCopyLengthForNewNode;
    bool hasFinalBlockPointer;
    bool mayMemcpyOldInlineHashTable;
    bool inlineHashTableMustContainFinalBlock;
    SystemHeapPointer<GeneralHeapPointer<void>> finalBlockPointerValue;    // only filled if hasFinalBlockPointer

    if (shouldAddKey)
    {
        if (m_nonFullBlockLen == x_hiddenClassBlockSize - 1)
        {
            // We are about to fill our current non-full block to full capacity, so the previous full block's node now qualifies to become an anchor
            // If it has not become an anchor yet, build it.
            //
            AssertIff(m_numSlots >= x_hiddenClassBlockSize, HasFinalFullBlockPointer(m_numSlots));
            AssertImp(m_anchorHashTable.m_value == 0, m_numSlots < x_hiddenClassBlockSize * 2);
            if (m_numSlots >= x_hiddenClassBlockSize)
            {
                SystemHeapPointer<Structure> anchorTargetHiddenClass = GetHiddenClassOfFullBlockPointer(this);
                anchorTableForNewNode = BuildNewAnchorTableIfNecessary(anchorTargetHiddenClass.As());
                assert(anchorTableForNewNode.m_value != 0);
            }
            else
            {
                anchorTableForNewNode.m_value = 0;
            }
            nonFullBlockCopyLengthForNewNode = x_hiddenClassBlockSize - 1;
            hasFinalBlockPointer = false;
            inlineHashTableMustContainFinalBlock = false;
            // The new node's inline hash table should only contain the last x_hiddenClassBlockSize elements
            // If our hash table doesn't contain the final block, then our hash table only contains the last x_hiddenClassBlockSize - 1 elements, only in this case we can copy
            //
            mayMemcpyOldInlineHashTable = !IsAnchorTableContainsFinalBlock(this);
            goto end_setup;
        }
        else if (m_nonFullBlockLen == x_hiddenClassBlockSize)
        {
            // The current node's non-full block is full. The new node will have a new non-full block, and its final block pointer will point to us.
            //
            anchorTableForNewNode = m_anchorHashTable;
            nonFullBlockCopyLengthForNewNode = 0;
            hasFinalBlockPointer = true;
            SystemHeapPointer<uint8_t> val = vm->GetHeapPtrTranslator().TranslateToSystemHeapPtr(this).As<uint8_t>();
            val = val.As() + static_cast<uint32_t>(OffsetOfTrailingVarLengthArray());
            val = val.As() + x_hiddenClassBlockSize * static_cast<uint32_t>(sizeof(GeneralHeapPointer<void>));
            finalBlockPointerValue = val.As<GeneralHeapPointer<void>>();
            inlineHashTableMustContainFinalBlock = !IsAnchorTableContainsFinalBlock(this);
            mayMemcpyOldInlineHashTable = false;
            goto end_setup;
        }
    }

    {
        // General case:
        // Either we are adding a key, but the current node's non-full block will not reach full capcity after the transition,
        // or we are not adding a key so we just want to clone the current node
        //
        nonFullBlockCopyLengthForNewNode = m_nonFullBlockLen;
        // The new node has a final block pointer iff we do
        //
        hasFinalBlockPointer = HasFinalFullBlockPointer(m_numSlots);
        if (hasFinalBlockPointer)
        {
            finalBlockPointerValue = GetFinalFullBlockPointer(this);
        }

        AssertImp(nonFullBlockCopyLengthForNewNode == x_hiddenClassBlockSize, !hasFinalBlockPointer && !shouldAddKey);

        if (IsAnchorTableContainsFinalBlock(this))
        {
            // If our anchor table already contains the final block, this is the good case.
            // The new node's inline hash table doesn't need to contain the final block, and it may directly copy from our inline hash table
            //
            inlineHashTableMustContainFinalBlock = false;
            mayMemcpyOldInlineHashTable = true;
            anchorTableForNewNode = m_anchorHashTable;
        }
        else
        {
            // Otherwise, if we have a final block pointer, we want to check if that node has been promoted to an anchor.
            // If yes, then the new node can set its anchor to that node instead of our anchor so it doesn't have to contain the final block
            //
            bool useUpdatedAnchor = false;
            SystemHeapPointer<StructureAnchorHashTable> updatedAnchor;
            if (hasFinalBlockPointer)
            {
                Structure* anchorClass = GetHiddenClassOfFullBlockPointer(this).As<Structure>();
                assert(anchorClass->m_numSlots > 0 && anchorClass->m_numSlots % x_hiddenClassBlockSize == 0);
                assert(anchorClass->m_numSlots == m_numSlots - m_nonFullBlockLen);
                if (IsAnchorTableContainsFinalBlock(anchorClass))
                {
                    useUpdatedAnchor = true;
                    updatedAnchor = anchorClass->m_anchorHashTable;
                }
            }
            if (!useUpdatedAnchor)
            {
                anchorTableForNewNode = m_anchorHashTable;
                inlineHashTableMustContainFinalBlock = hasFinalBlockPointer;
                mayMemcpyOldInlineHashTable = true;
            }
            else
            {
                anchorTableForNewNode = updatedAnchor;
                inlineHashTableMustContainFinalBlock = false;
                mayMemcpyOldInlineHashTable = false;
            }
        }
    }

end_setup:
    AssertImp(shouldAddKey, nonFullBlockCopyLengthForNewNode < x_hiddenClassBlockSize);
    AssertImp(!shouldAddKey, nonFullBlockCopyLengthForNewNode <= x_hiddenClassBlockSize);
    AssertImp(inlineHashTableMustContainFinalBlock, hasFinalBlockPointer);

    // Check if a butterfly space expansion is needed
    //
    assert(m_inlineNamedStorageCapacity <= x_maxNumSlots);
    bool needButterflyExpansion = false;
    uint8_t expandedButterflyCapacity = 0;
    if (shouldAddKey)
    {
        if (m_butterflyNamedStorageCapacity == 0)
        {
            assert(m_numSlots <= m_inlineNamedStorageCapacity);
            if (m_numSlots == m_inlineNamedStorageCapacity)
            {
                needButterflyExpansion = true;
                expandedButterflyCapacity = ButterflyNamedStorageGrowthPolicy::ComputeInitialButterflyCapacityForStructure(m_inlineNamedStorageCapacity, x_maxNumSlots);
            }
        }
        else
        {
            assert(m_numSlots > m_inlineNamedStorageCapacity);
            uint8_t usedLen = m_numSlots - m_inlineNamedStorageCapacity;
            assert(usedLen <= m_butterflyNamedStorageCapacity);
            if (usedLen == m_butterflyNamedStorageCapacity)
            {
                needButterflyExpansion = true;
                expandedButterflyCapacity = ButterflyNamedStorageGrowthPolicy::ComputeNextButterflyCapacityForStructure(m_inlineNamedStorageCapacity, m_butterflyNamedStorageCapacity, x_maxNumSlots);
            }
        }
        AssertImp(needButterflyExpansion, static_cast<uint32_t>(expandedButterflyCapacity) + m_inlineNamedStorageCapacity > m_numSlots);
        AssertIff(!needButterflyExpansion, static_cast<uint32_t>(m_inlineNamedStorageCapacity) + m_butterflyNamedStorageCapacity > m_numSlots);
    }
    else
    {
        assert(static_cast<uint32_t>(m_inlineNamedStorageCapacity) + m_butterflyNamedStorageCapacity >= m_numSlots);
    }

    // Work out the space needed for the new hidden class and perform allocation
    //
    uint8_t numElementsInInlineHashTable;
    if (nonFullBlockCopyLengthForNewNode != x_hiddenClassBlockSize)
    {
        numElementsInInlineHashTable = nonFullBlockCopyLengthForNewNode + static_cast<uint8_t>(shouldAddKey);
        if (hasFinalBlockPointer && inlineHashTableMustContainFinalBlock)
        {
            numElementsInInlineHashTable += static_cast<uint8_t>(x_hiddenClassBlockSize);
        }
    }
    else
    {
        assert(!shouldAddKey);
        numElementsInInlineHashTable = 0;
        if (!IsAnchorTableContainsFinalBlock(this))
        {
            numElementsInInlineHashTable += static_cast<uint8_t>(x_hiddenClassBlockSize);
        }
    }

    uint32_t htSize = RoundUpToPowerOfTwo(numElementsInInlineHashTable) * 2;
    if (htSize < 8) { htSize = 8; }
    assert(htSize <= 256);

    uint8_t htMaskToStore = static_cast<uint8_t>(htSize - 1);
    int64_t htMask = ~ZeroExtendTo<int64_t>(htMaskToStore);
    assert(htSize == ComputeHashTableSizeFromHashTableMask(htMaskToStore));

    uint8_t newNumSlots = m_numSlots + static_cast<uint8_t>(shouldAddKey);
    AssertIff(hasFinalBlockPointer, HasFinalFullBlockPointer(newNumSlots));

    uint32_t hashTableLengthBytes = static_cast<uint32_t>(sizeof(InlineHashTableEntry)) * htSize;
    uint32_t trailingVarLenArrayLengthBytes = ComputeTrailingVarLengthArrayLengthBytes(newNumSlots);
    uint32_t totalObjectLengthBytes = hashTableLengthBytes + static_cast<uint32_t>(OffsetOfTrailingVarLengthArray()) + trailingVarLenArrayLengthBytes;
    totalObjectLengthBytes = RoundUpToMultipleOf<8>(totalObjectLengthBytes);

    SystemHeapPointer<void> objectAddressStart = vm->AllocFromSystemHeap(totalObjectLengthBytes);

    // Populate the header
    //
    InlineHashTableEntry* htBegin = objectAddressStart.As<InlineHashTableEntry>();
    InlineHashTableEntry* htEnd = htBegin + htSize;
    Structure* r = reinterpret_cast<Structure*>(htEnd);

    ConstructInPlace(r);

    SystemHeapGcObjectHeader::Populate(r);
    r->m_numSlots = newNumSlots;
    r->m_nonFullBlockLen = nonFullBlockCopyLengthForNewNode + static_cast<uint8_t>(shouldAddKey);
    assert(r->m_nonFullBlockLen == ComputeNonFullBlockLength(r->m_numSlots));
    r->m_arrayType = m_arrayType;
    if (slotAdditionKind == SlotAdditionKind::AddSlotForPolyMetatable)
    {
        r->m_arrayType.SetMayHaveMetatable(true);
    }
    r->m_anchorHashTable = anchorTableForNewNode;
    r->m_inlineHashTableMask = htMaskToStore;
    r->m_inlineNamedStorageCapacity = m_inlineNamedStorageCapacity;
    r->m_knownNonexistentMetamethods = m_knownNonexistentMetamethods;
    if (slotAdditionKind == SlotAdditionKind::AddSlotForProperty && key.As<UserHeapGcObjectHeader>()->m_type == HeapEntityType::String)
    {
        int metamethodOrd = vm->GetMetamethodOrdinalFromStringName(TranslateToRawPointer(key.As<HeapString>()));
        if (unlikely(metamethodOrd != -1))
        {
            LuaMetamethodBitVectorT mask = static_cast<LuaMetamethodBitVectorT>(static_cast<LuaMetamethodBitVectorT>(1) << metamethodOrd);
            assert(r->m_knownNonexistentMetamethods & mask);
            r->m_knownNonexistentMetamethods &= ~mask;
        }
    }

    if (needButterflyExpansion)
    {
        r->m_butterflyNamedStorageCapacity = expandedButterflyCapacity;
        if (slotAdditionKind == SlotAdditionKind::AddSlotForProperty)
        {
            r->m_parentEdgeTransitionKind = TransitionKind::AddPropertyAndGrowPropertyStorageCapacity;
        }
        else
        {
            assert(slotAdditionKind == SlotAdditionKind::AddSlotForPolyMetatable);
            r->m_parentEdgeTransitionKind = TransitionKind::TransitToPolyMetaTableAndGrowPropertyStorageCapacity;
        }
    }
    else
    {
        r->m_butterflyNamedStorageCapacity = m_butterflyNamedStorageCapacity;
        if (slotAdditionKind == SlotAdditionKind::NoSlotAdded)
        {
            // The caller is responsible for setting up the correct transition kind
            //
            r->m_parentEdgeTransitionKind = TransitionKind::BadTransitionKind;
        }
        else if (slotAdditionKind == SlotAdditionKind::AddSlotForProperty)
        {
            r->m_parentEdgeTransitionKind = TransitionKind::AddProperty;
        }
        else
        {
            assert(slotAdditionKind == SlotAdditionKind::AddSlotForPolyMetatable);
            r->m_parentEdgeTransitionKind = TransitionKind::TransitToPolyMetaTable;
        }
    }
    assert(static_cast<uint32_t>(r->m_inlineNamedStorageCapacity) + r->m_butterflyNamedStorageCapacity <= x_maxNumSlots + 1);

    r->m_parent = this;
    if (slotAdditionKind == SlotAdditionKind::AddSlotForPolyMetatable)
    {
        // Note that 'm_metatable - 1' is the slot ordinal storing the metatable
        //
        r->m_metatable = r->m_numSlots;
    }
    else
    {
        r->m_metatable = m_metatable;
    }
    r->m_transitionTable.m_value = 0;

    // Populate the element list
    //
    {
        // Copy everything
        //
        SafeMemcpy(r->m_values, m_values, sizeof(GeneralHeapPointer<void>) * nonFullBlockCopyLengthForNewNode);

        if (shouldAddKey)
        {
            // Insert the new element
            //
            r->m_values[nonFullBlockCopyLengthForNewNode] = key.As<void>();
        }

        // Write the final block pointer if needed
        //
        AssertIff(hasFinalBlockPointer, HasFinalFullBlockPointer(r->m_numSlots));
        if (hasFinalBlockPointer)
        {
            *GetFinalFullBlockPointerAddress(r) = finalBlockPointerValue;
        }
    }

    auto insertNonExistentElementIntoInlineHashTable = [&htEnd, &htMask, &htSize]
            (UserHeapPointer<void> element, int8_t ordinalOfElement) ALWAYS_INLINE
    {
        uint16_t hashOfElement = static_cast<uint16_t>(StructureKeyHashHelper::GetHashValueForMaybeNonStringKey(element));
        uint8_t checkHash = static_cast<uint8_t>(hashOfElement);
        int64_t hashSlot = ZeroExtendTo<int64_t>(hashOfElement >> 8) | htMask;

        assert(-static_cast<int64_t>(htSize) <= hashSlot && hashSlot < 0);
        while (htEnd[hashSlot].m_ordinal != x_inlineHashTableEmptyValue)
        {
            hashSlot = (hashSlot - 1) | htMask;
            assert(-static_cast<int64_t>(htSize) <= hashSlot && hashSlot < 0);
        }

        htEnd[hashSlot].m_ordinal = ordinalOfElement;
        htEnd[hashSlot].m_checkHash = checkHash;

        std::ignore = htSize;
    };

    // Populate the inline hash table
    //
    // If we are allowed to memcpy the hash table (i.e. the new table is always old table + potentially the new element), do it if possible.
    //
    if (mayMemcpyOldInlineHashTable && htMaskToStore == m_inlineHashTableMask)
    {
        assert(ComputeHashTableSizeFromHashTableMask(m_inlineHashTableMask) == htSize);
        SafeMemcpy(htBegin, GetInlineHashTableBegin(this), hashTableLengthBytes);

        if (shouldAddKey)
        {
            // Insert the newly-added element
            //
            insertNonExistentElementIntoInlineHashTable(key, static_cast<int8_t>(nonFullBlockCopyLengthForNewNode));
        }
    }
    else
    {
        // We must manually insert every element into the hash table
        //
        memset(htBegin, x_inlineHashTableEmptyValue, hashTableLengthBytes);

        // First insert the final block if needed
        //
        AssertImp(nonFullBlockCopyLengthForNewNode == x_hiddenClassBlockSize, !inlineHashTableMustContainFinalBlock);
        DEBUG_ONLY(uint32_t elementsInserted = 0;)
        if (inlineHashTableMustContainFinalBlock)
        {
            GeneralHeapPointer<void>* p = GetFinalFullBlockPointer(r).As();
            for (int8_t i = -static_cast<int8_t>(x_hiddenClassBlockSize); i < 0; i++)
            {
                GeneralHeapPointer<void> e = TCGet(p[i]);
                insertNonExistentElementIntoInlineHashTable(e.As(), i /*ordinalOfElement*/);
                DEBUG_ONLY(elementsInserted++;)
            }
        }

        // Then insert the non-full block
        //
        if (nonFullBlockCopyLengthForNewNode == x_hiddenClassBlockSize && IsAnchorTableContainsFinalBlock(this))
        {
            assert(!shouldAddKey && numElementsInInlineHashTable == 0);
        }
        else
        {
            size_t len = r->m_nonFullBlockLen;
            for (size_t i = 0; i < len; i++)
            {
                GeneralHeapPointer<void> e = r->m_values[i];
                insertNonExistentElementIntoInlineHashTable(e.As(), static_cast<int8_t>(i) /*ordinalOfElement*/);
                DEBUG_ONLY(elementsInserted++;)
            }
        }
        assert(elementsInserted == numElementsInInlineHashTable);
    }

    // In debug mode, check that the new node contains all the expected elements, no more and no less
    //
#ifndef NDEBUG
    {
        uint32_t elementCount = 0;
        std::set<int64_t> elementSet;
        StructureIterator iterator(r);
        while (iterator.HasMore())
        {
            GeneralHeapPointer<void> keyG = iterator.GetCurrentKey();
            UserHeapPointer<void> keyToLookup { keyG.As() };
            uint8_t slot = iterator.GetCurrentSlotOrdinal();
            iterator.Advance();

            // Check that the iterator returns each key once and exactly once
            //
            ReleaseAssert(!elementSet.count(keyToLookup.m_value));
            elementSet.insert(keyToLookup.m_value);
            elementCount++;
            ReleaseAssert(elementCount <= r->m_numSlots);

            // Check that the key exists in the new node and is at the expected place
            //
            {
                uint32_t querySlot = static_cast<uint32_t>(-1);
                bool found = GetSlotOrdinalFromMaybeNonStringProperty(r, keyToLookup, querySlot /*out*/);
                ReleaseAssert(found);
                ReleaseAssert(querySlot == slot);
            }

            // Check that the key exists in the old node iff it is not equal to the newly inserted key
            //
            {
                uint32_t querySlot = static_cast<uint32_t>(-1);
                bool found = GetSlotOrdinalFromMaybeNonStringProperty(this, keyToLookup, querySlot /*out*/);
                if (shouldAddKey && keyToLookup == key)
                {
                    ReleaseAssert(!found);
                }
                else
                {
                    ReleaseAssert(found);
                    ReleaseAssert(querySlot == slot);
                }
            }
        }

        // Check that the iterator returned exactly the expected number of keys, and the newly inserted key is inside it
        //
        ReleaseAssert(elementCount == r->m_numSlots && elementSet.size() == elementCount);
        if (shouldAddKey)
        {
            ReleaseAssert(elementSet.count(key.m_value));
        }

        // Check the inline hash table, make sure it contains nothing unexpected
        // We don't have to check the anchor hash table because it has its own self-check
        //
        uint32_t inlineHtElementCount = 0;
        std::set<int64_t> inlineHtElementSet;
        for (uint32_t i = 0; i < ComputeHashTableSizeFromHashTableMask(r->m_inlineHashTableMask); i++)
        {
            InlineHashTableEntry e = GetInlineHashTableBegin(r)[i];
            if (e.m_ordinal != x_inlineHashTableEmptyValue)
            {
                GeneralHeapPointer<void> keyG = GetPropertyNameFromInlineHashTableOrdinal(r, e.m_ordinal);
                UserHeapPointer<void> keyToLookup { keyG.As() };

                ReleaseAssert(!inlineHtElementSet.count(keyToLookup.m_value));
                inlineHtElementSet.insert(keyToLookup.m_value);
                inlineHtElementCount++;

                ReleaseAssert(elementSet.count(keyToLookup.m_value));

                if (r->m_anchorHashTable.m_value != 0)
                {
                    uint32_t querySlot = static_cast<uint32_t>(-1);
                    bool found = StructureAnchorHashTable::GetSlotOrdinalFromPropertyNameAndHash(
                                r->m_anchorHashTable.As<StructureAnchorHashTable>(),
                                keyG,
                                StructureKeyHashHelper::GetHashValueForMaybeNonStringKey(keyToLookup),
                                querySlot /*out*/);
                    ReleaseAssert(!found);
                }
            }
        }

        uint32_t expectedElementsInInlineHt;
        if (r->m_nonFullBlockLen != x_hiddenClassBlockSize)
        {
            expectedElementsInInlineHt = r->m_nonFullBlockLen;
            if (HasFinalFullBlockPointer(r->m_numSlots) && !IsAnchorTableContainsFinalBlock(r))
            {
                expectedElementsInInlineHt += x_hiddenClassBlockSize;
            }
        }
        else
        {
            expectedElementsInInlineHt = 0;
            if (!IsAnchorTableContainsFinalBlock(r))
            {
                expectedElementsInInlineHt += x_hiddenClassBlockSize;
            }
        }
        ReleaseAssert(inlineHtElementCount == expectedElementsInInlineHt);
        ReleaseAssert(inlineHtElementSet.size() == inlineHtElementCount);
        ReleaseAssert(numElementsInInlineHashTable == expectedElementsInInlineHt);
    }
#endif

    return r;
}

inline Structure* WARN_UNUSED Structure::CreateInitialStructure(VM* vm, uint8_t inlineCapacity)
{
    assert(inlineCapacity <= x_maxNumSlots + 1);

    // Work out the space needed for the new hidden class and perform allocation
    //
    uint32_t htSize = 8;
    uint8_t htMaskToStore = static_cast<uint8_t>(htSize - 1);
    assert(htSize == ComputeHashTableSizeFromHashTableMask(htMaskToStore));

    uint32_t hashTableLengthBytes = static_cast<uint32_t>(sizeof(InlineHashTableEntry)) * htSize;
    uint32_t trailingVarLenArrayLengthBytes = ComputeTrailingVarLengthArrayLengthBytes(0 /*numSlots*/);
    uint32_t totalObjectLengthBytes = hashTableLengthBytes + static_cast<uint32_t>(OffsetOfTrailingVarLengthArray()) + trailingVarLenArrayLengthBytes;
    totalObjectLengthBytes = RoundUpToMultipleOf<8>(totalObjectLengthBytes);

    SystemHeapPointer<void> objectAddressStart = vm->AllocFromSystemHeap(totalObjectLengthBytes);

    // Populate the structure
    //
    InlineHashTableEntry* htBegin = objectAddressStart.As<InlineHashTableEntry>();
    InlineHashTableEntry* htEnd = htBegin + htSize;
    Structure* r = reinterpret_cast<Structure*>(htEnd);

    memset(htBegin, x_inlineHashTableEmptyValue, hashTableLengthBytes);

    ConstructInPlace(r);

    SystemHeapGcObjectHeader::Populate(r);
    r->m_numSlots = 0;
    r->m_nonFullBlockLen = 0;
    r->m_arrayType = ArrayType::GetInitialArrayType();
    r->m_knownNonexistentMetamethods = x_luaMetamethodBitVectorFullMask;
    r->m_anchorHashTable.m_value = 0;
    r->m_inlineHashTableMask = htMaskToStore;
    r->m_inlineNamedStorageCapacity = inlineCapacity;
    r->m_butterflyNamedStorageCapacity = 0;
    r->m_parentEdgeTransitionKind = TransitionKind::BadTransitionKind;
    r->m_parent.m_value = 0;
    r->m_metatable = 0;
    r->m_transitionTable.m_value = 0;

    return r;
}

inline void Structure::AddNonExistentProperty(VM* vm, UserHeapPointer<void> key, AddNewPropertyResult& result /*out*/)
{
#ifndef NDEBUG
    // Confirm that the key doesn't exist
    //
    {
        uint32_t tmp;
        assert(GetSlotOrdinalFromMaybeNonStringProperty(this, key, tmp /*out*/) == false);
    }
#endif

    // m_numSlots == x_maxNumSlots + 1 is possible due to PolyMetatable
    //
    if (m_numSlots >= x_maxNumSlots)
    {
        // Transit to CacheableDictionary
        //
        result.m_shouldTransitionToDictionaryMode = true;
        return;
    }

    int32_t transitionKey = GeneralHeapPointer<void>(key.As()).m_value;
    assert(transitionKey < 0);

    // Get the structure after transition, creating it and insert it into transition table if needed
    //
    Structure* transitionStructure = QueryTransitionTableAndInsert(vm, transitionKey, [&]() -> Structure* {
        return CreateStructureForAddPropertyTransition(vm, key);
    });
    assert(transitionStructure != nullptr);
    // Make sure we didn't botch the transition: the exact structure should be returned if we query the same key again
    //
    assert(QueryTransitionTableAndInsert(vm, transitionKey, [&]() -> Structure* { ReleaseAssert(false); }) == transitionStructure);

    TransitionKind tkind = transitionStructure->m_parentEdgeTransitionKind;
    assert(tkind == TransitionKind::AddProperty || tkind == TransitionKind::AddPropertyAndGrowPropertyStorageCapacity);

    result.m_shouldTransitionToDictionaryMode = false;
    result.m_shouldGrowButterfly = (tkind == TransitionKind::AddPropertyAndGrowPropertyStorageCapacity);
    assert(transitionStructure->m_numSlots == m_numSlots + 1);
    result.m_slotOrdinal = m_numSlots;
    result.m_newStructure = transitionStructure;

#ifndef NDEBUG
    // Confirm that the key exists in the new structure
    //
    {
        uint32_t tmp;
        assert(GetSlotOrdinalFromMaybeNonStringProperty(transitionStructure, key, tmp /*out*/) == true);
        assert(tmp == result.m_slotOrdinal);
    }
#endif
}

inline void Structure::SetMetatable(VM* vm, UserHeapPointer<void> key, AddMetatableResult& result /*out*/)
{
    assert(key.As<UserHeapGcObjectHeader>()->m_type == HeapEntityType::Table);
    GeneralHeapPointer<void> metatable { key.As() };
    constexpr int32_t transitionKey = StructureTransitionTable::x_key_add_or_to_poly_metatable;
    Structure* transitionStructure;

    if (likely(m_metatable == 0))
    {
        // We currently have no metatable.
        // We should check if we already have a AddMetatable transition.
        // If yes, and the transition is for the same metatable as we are adding, we can just use it.
        // If yes, but the transition is for a different metatable, we should make it transit to PolyMetatable instead.
        // If no, we should create a transition that transit to monomorphic metatable.
        //
        transitionStructure = QueryTransitionTableAndUpsert(vm, transitionKey, [&](Structure* oldStructure) -> Structure* {
            if (unlikely(oldStructure == nullptr))
            {
                // Transit to monomorphic metatable
                //
                return CreateStructureForMonomorphicMetatableTransition(vm, metatable);
            }
            assert(oldStructure->m_metatable != 0);
            if (HasMonomorphicMetatable(oldStructure))
            {
                if (likely(oldStructure->m_metatable == metatable.m_value))
                {
                    // Monomorphic and same metatable, we should just transit to it
                    //
                    return oldStructure;
                }
                else
                {
                    // Monomorphic but different metatable, we should replace the transition to transit to PolyMetatable instead
                    //
                    return CreateStructureForPolyMetatableTransition(vm);
                }
            }
            else
            {
                assert(IsPolyMetatable(oldStructure));
                // Already PolyMetatable, we should just transit to it
                //
                return oldStructure;
            }
        });
        assert(QueryTransitionTableAndInsert(vm, transitionKey, [&]() -> Structure* { ReleaseAssert(false); }) == transitionStructure);
        assert(TranslateToRawPointer(vm, transitionStructure->m_parent.As()) == this);
    }
    else if (IsPolyMetatable(this))
    {
        // We are already in PolyMetatable mode, so the Structure isn't responsible for storing the metatable
        // Just ask the caller to update the slot value
        //
        result.m_newStructure = this;
        result.m_shouldInsertMetatable = true;
        result.m_shouldGrowButterfly = false;
        result.m_slotOrdinal = GetPolyMetatableSlot(this);
        return;
    }
    else
    {
        assert(HasMonomorphicMetatable(this));
        assert(m_parentEdgeTransitionKind != TransitionKind::TransitToPolyMetaTable);
        assert(m_parentEdgeTransitionKind != TransitionKind::TransitToPolyMetaTableAndGrowPropertyStorageCapacity);
        if (metatable.m_value == m_metatable)
        {
            // SetMetatable attempted to set the same monomorphic metatable as we already have, so no-op
            //
            result.m_newStructure = this;
            result.m_shouldInsertMetatable = false;
            result.m_shouldGrowButterfly = false;
            return;
        }

        if (m_parentEdgeTransitionKind == TransitionKind::AddMetaTable)
        {
            assert(HasMonomorphicMetatable(this));
            // Our parent transit to us by adding a different metatable
            // so we should make parent's AddMetatable operation transit to PolyMetatable instead
            //
            Structure* base = TranslateToRawPointer(vm, m_parent.As());
            transitionStructure = base->QueryTransitionTableAndUpsert(vm, transitionKey, [&](Structure* oldStructure) -> Structure* {
                assert(oldStructure != nullptr);
                if (oldStructure == this)
                {
                    assert(!IsPolyMetatable(oldStructure));
                    return base->CreateStructureForPolyMetatableTransition(vm);
                }
                else
                {
                    assert(IsPolyMetatable(oldStructure));
                    return oldStructure;
                }
            });
            assert(IsPolyMetatable(transitionStructure));
            assert(base->QueryTransitionTableAndInsert(vm, transitionKey, [&]() -> Structure* { ReleaseAssert(false); }) == transitionStructure);
            assert(TranslateToRawPointer(vm, transitionStructure->m_parent.As()) == base);
        }
        else
        {
            // We already have a different metatable, but this is not immediately added by our parent
            // so we just create a transition that transit to PolyMetatable
            //
            // Note that if the transition exists, it must be a PolyMetatable transition so we don't need to overwrite it
            //
            assert(HasMonomorphicMetatable(this));
            assert(m_metatable != metatable.m_value);
            transitionStructure = QueryTransitionTableAndInsert(vm, transitionKey, [&]() -> Structure* {
                return CreateStructureForPolyMetatableTransition(vm);
            });
            assert(IsPolyMetatable(transitionStructure));
            assert(QueryTransitionTableAndInsert(vm, transitionKey, [&]() -> Structure* { ReleaseAssert(false); }) == transitionStructure);
            assert(TranslateToRawPointer(vm, transitionStructure->m_parent.As()) == this);
        }
    }

    assert(transitionStructure != nullptr);
    TransitionKind transitionKind = transitionStructure->m_parentEdgeTransitionKind;
    assert(transitionKind == TransitionKind::AddMetaTable ||
           transitionKind == TransitionKind::TransitToPolyMetaTable ||
           transitionKind == TransitionKind::TransitToPolyMetaTableAndGrowPropertyStorageCapacity);

    result.m_newStructure = transitionStructure;
    if (transitionKind == TransitionKind::AddMetaTable)
    {
        result.m_shouldInsertMetatable = false;
        result.m_shouldGrowButterfly = false;
    }
    else
    {
        result.m_shouldInsertMetatable = true;
        result.m_shouldGrowButterfly = (transitionKind == TransitionKind::TransitToPolyMetaTableAndGrowPropertyStorageCapacity);
        result.m_slotOrdinal = GetPolyMetatableSlot(transitionStructure);
    }
}

inline void Structure::RemoveMetatable(VM* vm, RemoveMetatableResult& result /*out*/)
{
    if (m_metatable == 0)
    {
        // We don't have metatable, so no-op
        //
        result.m_shouldInsertMetatable = false;
        result.m_newStructure = this;
    }
    else if (m_parentEdgeTransitionKind == TransitionKind::AddMetaTable)
    {
        assert(HasMonomorphicMetatable(this) && HasNoMetatable(m_parent.As()));
        // We can simply transit back to parent. Note that we only do this for monomorphic case,
        // since PolyMetatable may have involved a butterfly expansion. Just make things simple for now.
        //
        result.m_shouldInsertMetatable = false;
        result.m_newStructure = TranslateToRawPointer(vm, m_parent.As());
    }
    else if (IsPolyMetatable(this))
    {
        // We are in poly metatable mode, ask the caller to overwrite the slot
        //
        result.m_shouldInsertMetatable = true;
        result.m_slotOrdinal = GetPolyMetatableSlot(this);
        result.m_newStructure = this;
    }
    else
    {
        assert(HasMonomorphicMetatable(this));
        // We are in monomorphic metatable mode, perform a RemoveMetatable transition
        //
        constexpr int32_t transitionKey = StructureTransitionTable::x_key_remove_metatable;
        Structure* transitionStructure = QueryTransitionTableAndInsert(vm, transitionKey, [&]() -> Structure* {
            return CreateStructureForRemoveMetatableTransition(vm);
        });
        assert(QueryTransitionTableAndInsert(vm, transitionKey, [&]() -> Structure* { ReleaseAssert(false); }) == transitionStructure);

        assert(TranslateToRawPointer(vm, transitionStructure->m_parent.As()) == this);
        assert(transitionStructure->m_metatable == 0);
        assert(transitionStructure->m_parentEdgeTransitionKind == TransitionKind::RemoveMetaTable);

        result.m_shouldInsertMetatable = false;
        result.m_newStructure = transitionStructure;
    }
}

inline Structure* WARN_UNUSED Structure::UpdateArrayType(VM* vm, ArrayType newArrayType)
{
    assert(m_arrayType.m_asValue <= ArrayType::x_usefulBitsMask);
    assert(newArrayType.m_asValue <= ArrayType::x_usefulBitsMask);
    assert(m_arrayType.m_asValue != newArrayType.m_asValue);

    int32_t transitionKey = StructureTransitionTable::x_key_change_array_type_tag | static_cast<int32_t>(newArrayType.m_asValue);
    Structure* transitionStructure = QueryTransitionTableAndInsert(vm, transitionKey, [&]() -> Structure* {
        Structure* newStructure = CloneStructure(vm);
        newStructure->m_parentEdgeTransitionKind = TransitionKind::UpdateArrayType;
        newStructure->m_arrayType.m_asValue = newArrayType.m_asValue;
        return newStructure;
    });
    assert(QueryTransitionTableAndInsert(vm, transitionKey, [&]() -> Structure* { ReleaseAssert(false); }) == transitionStructure);

    assert(TranslateToRawPointer(vm, transitionStructure->m_parent.As()) == this);
    assert(transitionStructure->m_parentEdgeTransitionKind == TransitionKind::UpdateArrayType);
    assert(transitionStructure->m_arrayType.m_asValue == newArrayType.m_asValue);
    return transitionStructure;
}
