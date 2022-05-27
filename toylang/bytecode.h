#pragma once

#include "common_utils.h"
#include "memory_ptr.h"
#include "vm.h"
#include "vm_string.h"

namespace ToyLang {

using namespace CommonUtils;

// An array is implemented by a vector part and a sparse map part
// This class describes the policy of when to use vector part and when to use sparse map part
//
struct ArrayGrowthPolicy
{
    // Lua has 1-based array
    //
    constexpr static int32_t x_arrayBaseOrd = 1;

    // When we need to grow array for an index, if the index is <= x_alwaysVectorCutoff,
    // we always grow the vector to accommodate the index, regardless of how sparse the array is
    //
    constexpr static int32_t x_alwaysVectorCutoff = 1000;

    // If the index is > x_sparseMapUnlessContinuousCutoff,
    // unless the array is continuous, we always store the index to sparse map
    //
    // Note that if a sparse map contains an integer index >= x_arrayBaseOrd, the vector part never grows.
    //
    constexpr static int32_t x_sparseMapUnlessContinuousCutoff = 100000;

    // If the index falls between the two cutoffs, we count the # of elements in the array,
    // and grow the vector if there are at least index / x_densityCutoff elements after the growth.
    //
    constexpr static uint32_t x_densityCutoff = 8;

    // If the index is greater than this cutoff, it unconditionally goes to the sparse map
    // to prevent potential arithmetic overflow in addressing
    //
    constexpr static int32_t x_unconditionallySparseMapCutoff = 1U << 27;

    // The growth factor for vector part
    //
    constexpr static double x_vectorGrowthFactor = 2;
};

struct ArrayType
{
    ArrayType() : m_asValue(0) { }
    ArrayType(uint8_t value) : m_asValue(value) { }

    enum Kind : uint8_t  // must fit in 2 bits
    {
        NoButterflyArrayPart,
        Int32,
        Double,
        Any
    };

    using BitFieldCarrierType = uint8_t;

    // The storage value is interpreted as the following:
    //
    // bit 0: bool m_isContinuous
    //   Whether the array is continuous, i.e., entry is non-nil iff index in [x_arrayBaseOrd, len). Notes:
    //   (1) If Kind is NoButterflyArrayPart, m_isContinuous is false
    //   (2) m_isContinuous = true also implies m_hasSparseMap = false
    //
    using BFM_isContinuous = BitFieldMember<BitFieldCarrierType, bool /*type*/, 0 /*start*/, 1 /*width*/>;
    bool IsContinuous() { return BFM_isContinuous::Get(m_asValue); }
    void SetIsContinuous(bool v) { return BFM_isContinuous::Set(m_asValue, v); }

    // bit 1-2: Kind m_kind
    //   Whether all non-hole values in the array has the same type
    //   If m_kind is Int32 or Double, then m_mayHaveMetatable must be false
    //
    using BFM_arrayKind = BitFieldMember<BitFieldCarrierType, Kind /*type*/, 1 /*start*/, 2 /*width*/>;
    Kind ArrayKind() { return BFM_arrayKind::Get(m_asValue); }
    void SetArrayKind(Kind v) { return BFM_arrayKind::Set(m_asValue, v); }

    // bit 3: bool m_hasSparseMap
    //   Whether there is a sparse map
    //
    using BFM_hasSparseMap = BitFieldMember<BitFieldCarrierType, bool /*type*/, 3 /*start*/, 1 /*width*/>;
    bool HasSparseMap() { return BFM_hasSparseMap::Get(m_asValue); }
    void SetHasSparseMap(bool v) { return BFM_hasSparseMap::Set(m_asValue, v); }

    // bit 4: bool m_sparseMapContainsVectorIndex
    //   Whether the sparse map contains index between [x_arrayBaseOrd, MaxInt32]
    //   When this is true, the vector part can no longer grow
    //
    using BFM_sparseMapContainsVectorIndex = BitFieldMember<BitFieldCarrierType, bool /*type*/, 4 /*start*/, 1 /*width*/>;
    bool SparseMapContainsVectorIndex() { return BFM_sparseMapContainsVectorIndex::Get(m_asValue); }
    void SetSparseMapContainsVectorIndex(bool v) { return BFM_sparseMapContainsVectorIndex::Set(m_asValue, v); }

    // bit 5: bool m_mayHaveMetatable
    //   Whether the object may have a non-null metatable
    //   When this is true, nil values have to be handled specially since it may invoke metatable methods
    //
    using BFM_mayHaveMetatable = BitFieldMember<BitFieldCarrierType, bool /*type*/, 5 /*start*/, 1 /*width*/>;
    bool MayHaveMetatable() { return BFM_mayHaveMetatable::Get(m_asValue); }
    void SetMayHaveMetatable(bool v) { return BFM_mayHaveMetatable::Set(m_asValue, v); }

    static ArrayType GetInitialArrayType()
    {
        return ArrayType(0);
    }

    // bit 6-7: always zero
    //
    BitFieldCarrierType m_asValue;
};
static_assert(sizeof(ArrayType) == 1);

class VM;

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

    static StructureTransitionTable* AllocateUninitialized(uint32_t hashTableSize)
    {
        uint32_t allocationSize = ComputeAllocationSize(hashTableSize);
        VM* vm = VM::GetActiveVMForCurrentThread();
        StructureTransitionTable* result = vm->GetHeapPtrTranslator().TranslateToRawPtr(vm->AllocFromSystemHeap(allocationSize).As<StructureTransitionTable>());
        ConstructInPlace(result);
        return result;
    }

    static StructureTransitionTable* AllocateInitialTable(int32_t key, SystemHeapPointer<Structure> value)
    {
        StructureTransitionTable* r = AllocateUninitialized(x_initialHashTableSize);
        r->m_hashTableMask = x_initialHashTableSize - 1;
        r->m_numElementsInHashTable = 1;
        memset(r->m_hashTable, 0, sizeof(HashTableEntry) * x_initialHashTableSize);
        bool found;
        HeapPtr<HashTableEntry> e = Find(SystemHeapPointer<StructureTransitionTable>(r).As(), key, found /*out*/);
        assert(!found);
        TCSet(e->m_key, key);
        TCSet(e->m_value, value);
        return r;
    }

    bool ShouldResizeForThisInsertion()
    {
        return m_numElementsInHashTable >= m_hashTableMask / 2 + 1;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructureTransitionTable>>>
    static ReinterpretCastPreservingAddressSpaceType<HashTableEntry*, T> WARN_UNUSED Find(T self, int32_t key, bool& found)
    {
        assert(key != x_key_invalid);
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

    StructureTransitionTable* WARN_UNUSED Expand()
    {
        assert(ShouldResizeForThisInsertion());
        uint32_t newHashMask = m_hashTableMask * 2 + 1;
        StructureTransitionTable* newTable = AllocateUninitialized(newHashMask + 1);
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
        HeapPtr<HeapString> s = stringKey.As<HeapString>();
        assert(s->m_type == Type::STRING);
        return s->m_hashLow;
    }

    static uint32_t GetHashValueForMaybeNonStringKey(UserHeapPointer<void> key)
    {
        HeapPtr<UserHeapGcObjectHeader> hdr = key.As<UserHeapGcObjectHeader>();
        if (hdr->m_type == Type::STRING)
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

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructureAnchorHashTable>>>
    static GeneralHeapPointer<void> GetPropertyNameAtSlot(T self, uint8_t ordinal)
    {
        assert(ordinal < self->m_numBlocks * x_hiddenClassBlockSize);
        uint8_t blockOrd = ordinal >> x_log2_hiddenClassBlockSize;
        uint8_t offset = ordinal & static_cast<uint8_t>(x_hiddenClassBlockSize - 1);
        SystemHeapPointer<GeneralHeapPointer<void>> p = TCGet(self->m_blockPointers[blockOrd]);
        return TCGet(p.As()[offset]);
    }

    static StructureAnchorHashTable* WARN_UNUSED Create(VM* vm, Structure* shc);

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructureAnchorHashTable>>>
    static ReinterpretCastPreservingAddressSpaceType<HashTableEntry*, T> GetHashTableBegin(T self)
    {
        uint32_t hashTableSize = GetHashTableSizeFromHashTableMask(self->m_hashTableMask);
        return GetHashTableEnd(self) - hashTableSize;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructureAnchorHashTable>>>
    static ReinterpretCastPreservingAddressSpaceType<HashTableEntry*, T> GetHashTableEnd(T self)
    {
        return ReinterpretCastPreservingAddressSpace<HashTableEntry*>(self);
    }

    void CloneHashTableTo(HashTableEntry* htStart, uint32_t htSize);

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructureAnchorHashTable>>>
    static bool WARN_UNUSED GetSlotOrdinalFromPropertyNameAndHash(T self, GeneralHeapPointer<void> key, uint32_t hashValue, uint32_t& result /*out*/)
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
    enum class Operation : uint8_t
    {
        AddProperty,
        SetMetatable,
        RemoveMetatable,
        UpdateArrayType
    };

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

    // TODO: refine butterfly growth strategy
    //
    static uint8_t WARN_UNUSED ComputeInitialButterflyCapacity(uint8_t inlineNamedStorageCapacity)
    {
        constexpr uint8_t x_initialMinimumButterflyCapacity = 4;
        constexpr uint8_t x_butterflyCapacityFromInlineCapacityFactor = 2;
        uint8_t capacity = inlineNamedStorageCapacity / x_butterflyCapacityFromInlineCapacityFactor;
        capacity = std::max(capacity, x_initialMinimumButterflyCapacity);
        // It needs to grow to up to x_maxNumSlot + 1: that's the real max capacity considering PolyMetatable
        //
        capacity = std::min(capacity, static_cast<uint8_t>(x_maxNumSlots + 1 - inlineNamedStorageCapacity));
        return capacity;
    }

    static uint8_t WARN_UNUSED ComputeNextButterflyCapacity(uint8_t inlineNamedStorageCapacity, uint8_t curButterflyCapacity)
    {
        constexpr uint8_t x_butterflyCapacityGrowthFactor = 2;
        uint32_t capacity = static_cast<uint32_t>(curButterflyCapacity) * x_butterflyCapacityGrowthFactor;
        // It needs to grow to up to x_maxNumSlot + 1: that's the real max capacity considering PolyMetatable
        //
        capacity = std::min(capacity, static_cast<uint32_t>(x_maxNumSlots + 1 - inlineNamedStorageCapacity));
        return static_cast<uint8_t>(capacity);
    }

    static constexpr int8_t x_inlineHashTableEmptyValue = 0x7f;

    struct InlineHashTableEntry
    {
        uint8_t m_checkHash;
        int8_t m_ordinal;
    };
    static_assert(sizeof(InlineHashTableEntry) == 2);

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, Structure>>>
    static ReinterpretCastPreservingAddressSpaceType<InlineHashTableEntry*, T> GetInlineHashTableBegin(T self)
    {
        uint32_t hashTableSize = ComputeHashTableSizeFromHashTableMask(self->m_inlineHashTableMask);
        return GetInlineHashTableEnd(self) - hashTableSize;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, Structure>>>
    static ReinterpretCastPreservingAddressSpaceType<InlineHashTableEntry*, T> GetInlineHashTableEnd(T self)
    {
        return ReinterpretCastPreservingAddressSpace<InlineHashTableEntry*>(self);
    }

    static uint32_t ComputeHashTableSizeFromHashTableMask(uint8_t mask)
    {
        uint32_t v = static_cast<uint32_t>(mask) + 1;
        assert(is_power_of_2(v));
        return v;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, Structure>>>
    static GeneralHeapPointer<void> GetLastAddedKey(T self)
    {
        assert(self->m_parentEdgeTransitionKind == TransitionKind::AddProperty ||
               self->m_parentEdgeTransitionKind == TransitionKind::AddPropertyAndGrowPropertyStorageCapacity);
        assert(self->m_numSlots > 0);
        uint8_t nonFullBlockLen = ComputeNonFullBlockLength(self->m_numSlots);
        assert(nonFullBlockLen > 0);
        return TCGet(self->m_values[nonFullBlockLen - 1]);
    }

    static constexpr uint8_t x_maxNumSlots = 253;

    struct AddNewPropertyResult
    {
        // If true, the Structure just transitioned into dictionary mode
        //
        bool m_transitionedToDictionaryMode;

        // If true, the caller should grow the object's butterfly property part
        //
        bool m_shouldGrowButterfly;

        // The slot ordinal to write into
        //
        uint32_t m_slotOrdinal;

        void* m_newStructureOrDictionary;
    };

    void AddNonExistentProperty(VM* vm, UserHeapPointer<void> key, AddNewPropertyResult& result /*out*/);

    struct AddOrRemoveMetatableResult
    {
        // If true, the Structure is in PolyMetatable mode, m_slotOrdinal is filled to contain the slot ordinal for the metatable,
        // and the user should fill the metatable (or 'nil' for remove metatable) into that slot
        //
        bool m_shouldInsertMetatable;

        // If true, the caller should grow the object's butterfly property part
        // Only relavent if m_transitionedToNewHiddenClass is true
        //
        bool m_shouldGrowButterfly;

        // If 'm_shouldInsertMetatable' is true, the slot ordinal to write into
        //
        uint32_t m_slotOrdinal;

        SystemHeapPointer<Structure> m_newStructure;
    };

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, Structure>>>
    static bool WARN_UNUSED IsAnchorTableContainsFinalBlock(T self)
    {
        SystemHeapPointer<StructureAnchorHashTable> p = TCGet(self->m_anchorHashTable);
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
        return CreateStructureForTransitionImpl(vm, SlotAdditionKind::AddSlotForPolyMetatable, vm->GetSpecialKeyForMetadataSlot());
    }

    Structure* WARN_UNUSED CloneStructure(VM* vm)
    {
        return CreateStructureForTransitionImpl(vm, SlotAdditionKind::NoSlotAdded, UserHeapPointer<void>());
    }

    static Structure* WARN_UNUSED CreateInitialStructure(VM* vm, uint8_t initialInlineCapacity, uint8_t initialButterflyCapacity);

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, Structure>>>
    static SystemHeapPointer<StructureAnchorHashTable> WARN_UNUSED BuildNewAnchorTableIfNecessary(T self);

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, Structure>>>
    static bool WARN_UNUSED GetSlotOrdinalFromStringProperty(T self, UserHeapPointer<HeapString> stringKey, uint32_t& result /*out*/)
    {
        return GetSlotOrdinalFromPropertyNameAndHash(self, stringKey, StructureKeyHashHelper::GetHashValueForStringKey(stringKey), result /*out*/);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, Structure>>>
    static bool WARN_UNUSED GetSlotOrdinalFromMaybeNonStringProperty(T self, UserHeapPointer<void> key, uint32_t& result /*out*/)
    {
        return GetSlotOrdinalFromPropertyNameAndHash(self, key, StructureKeyHashHelper::GetHashValueForMaybeNonStringKey(key), result /*out*/);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, Structure>>>
    static bool WARN_UNUSED GetSlotOrdinalFromPropertyNameAndHash(T self, UserHeapPointer<void> key, uint32_t hashvalue, uint32_t& result /*out*/)
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

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, Structure>>>
    static bool WARN_UNUSED QueryInlineHashTable(T self, GeneralHeapPointer<void> key, uint16_t hashvalue, uint32_t& result /*out*/)
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

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, Structure>>>
    static ReinterpretCastPreservingAddressSpaceType<SystemHeapPointer<GeneralHeapPointer<void>>*, T> GetFinalFullBlockPointerAddress(T self)
    {
        assert(HasFinalFullBlockPointer(self->m_numSlots));
        return ReinterpretCastPreservingAddressSpace<SystemHeapPointer<GeneralHeapPointer<void>>*>(self->m_values + self->m_nonFullBlockLen);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, Structure>>>
    static SystemHeapPointer<GeneralHeapPointer<void>> GetFinalFullBlockPointer(T self)
    {
        return TCGet(*GetFinalFullBlockPointerAddress(self));
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, Structure>>>
    static SystemHeapPointer<Structure> GetHiddenClassOfFullBlockPointer(T self)
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

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, Structure>>>
    static GeneralHeapPointer<void> GetPropertyNameFromInlineHashTableOrdinal(T self, int8_t ordinal)
    {
        if (ordinal >= 0)
        {
            // A non-negative offset denote the offset into the non-full block
            //
            assert(ordinal < self->m_nonFullBlockLen);
            return GeneralHeapPointer<void> { self->m_values[ordinal] };
        }
        else
        {
            // Ordinal [-x_hiddenClassBlockSize, 0) denote the offset into the final full block pointer
            // Note that the final full block pointer points at one past the end of the block, so we can simply index using the ordinal
            //
            assert(HasFinalFullBlockPointer(self->m_numSlots));
            assert(-static_cast<int8_t>(x_hiddenClassBlockSize) <= ordinal);
            SystemHeapPointer<GeneralHeapPointer<void>> u = GetFinalFullBlockPointer(self);
            return TCGet(u.As()[ordinal]);
        }
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, Structure>>>
    static uint32_t GetPropertySlotFromInlineHashTableOrdinal(T self, int8_t ordinal)
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
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, Structure>>>
    static int32_t WARN_UNUSED GetParentEdgeTransitionKey(T self)
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

    StructureTransitionTable* WARN_UNUSED InitializeOutlinedTransitionTable(int32_t keyForOnlyChild, SystemHeapPointer<Structure> onlyChild)
    {
        assert(m_transitionTable.IsType<Structure>());
        assert(m_transitionTable.As<Structure>() == onlyChild.As());
        assert(GetParentEdgeTransitionKey(m_transitionTable.As<Structure>()) == keyForOnlyChild);
        StructureTransitionTable* newTable = StructureTransitionTable::AllocateInitialTable(keyForOnlyChild, onlyChild);
        m_transitionTable.Store(SystemHeapPointer<StructureTransitionTable> { newTable });
        return newTable;
    }

    // Query if the transition key exists in the transition table
    // If not, call 'createNewStructureFunc' to create a new structure and insert it into transition table
    // In either case, return the structure after the transition
    //
    template<typename Func>
    Structure* WARN_UNUSED ALWAYS_INLINE QueryTransitionTableAndInsert(VM* vm, int32_t transitionKey, const Func& createNewStructureFunc)
    {
        if (m_transitionTable.IsNullPtr())
        {
            Structure* newStructure = createNewStructureFunc();
            m_transitionTable.Store(SystemHeapPointer<Structure>(newStructure));
            return newStructure;
        }
        else if (m_transitionTable.IsType<Structure>())
        {
            HeapPtr<Structure> onlyChild = m_transitionTable.As<Structure>();
            assert(TCGet(onlyChild->m_parent) == SystemHeapPointer<Structure>(this));
            int32_t keyForOnlyChild = GetParentEdgeTransitionKey(onlyChild);
            if (keyForOnlyChild == transitionKey)
            {
                return TranslateToRawPointer(vm, onlyChild);
            }
            else
            {
                // We need to create an outlined transition table
                //
                StructureTransitionTable* newTable = InitializeOutlinedTransitionTable(keyForOnlyChild, onlyChild);
                bool found;
                StructureTransitionTable::HashTableEntry* e = StructureTransitionTable::Find(newTable, transitionKey, found /*out*/);
                assert(!found);

                Structure* newStructure = createNewStructureFunc();

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
                Structure* newStructure = createNewStructureFunc();

                e->m_key = transitionKey;
                e->m_value = newStructure;
                table->m_numElementsInHashTable++;

                if (unlikely(table->ShouldResizeForThisInsertion()))
                {
                    StructureTransitionTable* newTable = table->Expand();
                    m_transitionTable.Store(SystemHeapPointer<StructureTransitionTable>(newTable));
                    assert(!newTable->ShouldResizeForThisInsertion());
                    // FIXME: delete old table!
                }

                return newStructure;
            }
            else
            {
                return TranslateToRawPointer(vm, e->m_value.As());
            }
        }
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

    uint16_t m_reserved;

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

class alignas(8) ArraySparseMap
{
public:
    struct HashTableEntry
    {
        double m_key;
        TValue m_value;
    };
    static_assert(sizeof(HashTableEntry) == 16);

    TValue GetByVal(double key)
    {
        auto it = m_map.find(key);
        if (it == m_map.end())
        {
            return TValue::Nil();
        }
        else
        {
            return it->second;
        }
    }

    // Currently we just use an outlined STL hash table for simplicity, we expect well-written code to not trigger SparseMap any way
    //
    std::unordered_map<double, TValue> m_map;
};

class alignas(8) ButterflyHeader
{
public:
    bool IsContinuous()
    {
        return m_arrayLengthIfContinuous >= ArrayGrowthPolicy::x_arrayBaseOrd;
    }

    bool CanUseFastPathGetForContinuousArray(int32_t idx)
    {
        assert(ArrayGrowthPolicy::x_arrayBaseOrd <= idx);
        return idx < m_arrayLengthIfContinuous;
    }

    bool IndexFitsInVectorCapacity(int32_t idx)
    {
        assert(ArrayGrowthPolicy::x_arrayBaseOrd <= idx);
        return idx < m_arrayStorageCapacity + ArrayGrowthPolicy::x_arrayBaseOrd;
    }

    bool HasSparseMap()
    {
        return m_arrayLengthIfContinuous < ArrayGrowthPolicy::x_arrayBaseOrd - 1;
    }

    HeapPtr<ArraySparseMap> GetSparseMap()
    {
        assert(HasSparseMap());
        assert(m_arrayLengthIfContinuous < 0);
        return GeneralHeapPointer<ArraySparseMap> { m_arrayLengthIfContinuous }.As();
    }

    // If == x_arrayBaseOrd - 1, it means the vector part is not continuous
    // If < x_arrayBaseOrd - 1, it means the vector part is not continuous and there is a sparse map,
    //     and the value can be interpreted as a GeneralHeapPointer<ArraySparseMap>
    // If >= x_arrayBaseOrd, it means the vector part is continuous and has no sparse map.
    //     That is, range [x_arrayBaseOrd, m_arrayLengthIfContinuous) are all non-nil values, and everything else are nils
    //     (note that in Lua arrays are 1-based)
    //
    int32_t m_arrayLengthIfContinuous;

    // The capacity of the array vector storage part
    //
    int32_t m_arrayStorageCapacity;
};
// This is very hacky: ButterflyHeader has a size of 1 slot, the Butterfly pointer points at the start of ButterflyHeader, and in Lua array is 1-based.
// This allows us to directly use ((TValue*)butterflyPtr)[index] to do array indexing
// If we want to port to a language with 0-based array, we can make Butterfly point to the end of ButterflyHeader instead, and the scheme will still work
//
static_assert(sizeof(ButterflyHeader) == 8 && sizeof(TValue) == 8, "see comment above");

class alignas(8) Butterfly
{
public:
    ButterflyHeader* GetHeader()
    {
        return reinterpret_cast<ButterflyHeader*>(this) - (1 - ArrayGrowthPolicy::x_arrayBaseOrd);
    }

    TValue* UnsafeGetInVectorIndexAddr(int32_t index)
    {
        assert(GetHeader()->IndexFitsInVectorCapacity(index));
        return reinterpret_cast<TValue*>(this) + index;
    }
};

class alignas(8) TableObject
{
public:
    // Check if the index qualifies for array indexing
    //
    static bool WARN_UNUSED ALWAYS_INLINE IsQualifiedForVectorIndex(double vidx, int32_t& idx /*out*/)
    {
        assert(!IsNaN(vidx));
        int64_t asInt64 = static_cast<int64_t>(vidx);
        if (!UnsafeFloatEqual(static_cast<double>(asInt64), vidx))
        {
            // The conversion is not lossless, return
            //
            return false;
        }
        if (asInt64 < ArrayGrowthPolicy::x_arrayBaseOrd || asInt64 > std::numeric_limits<int32_t>::max())
        {
            return false;
        }
        idx = SafeIntegerCast<int32_t>(asInt64);
        return true;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue GetByVal(T self, TValue tidx);

    // Handle the case that index is int32_t and >= x_arrayBaseOrd
    //
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue ALWAYS_INLINE GetByValVectorIndex(T self, TValue tidx, int32_t idx)
    {
        assert(idx >= ArrayGrowthPolicy::x_arrayBaseOrd);

        ArrayType arrType { self->m_arrayType };

        // Check fast path: continuous array
        //
        if (likely(arrType.IsContinuous()))
        {
            assert(arrType.ArrayKind() != ArrayType::NoButterflyArrayPart);
            if (likely(self->m_butterfly->GetHeader()->CanUseFastPathGetForContinuousArray(idx)))
            {
                // index is in range, we know the result must not be nil, no need to check metatable, return directly
                //
                TValue res = *(self->m_butterfly->UnsafeGetInVectorIndexAddr(idx));
                assert(!res.IsNil());
                AssertImp(arrType.ArrayKind() == ArrayType::Int32, res.IsInt32(TValue::x_int32Tag));
                AssertImp(arrType.ArrayKind() == ArrayType::Double, res.IsDouble(TValue::x_int32Tag));
                return res;
            }
            else
            {
                // index is out of range, raw result must be nil
                //
                return GetByValHandleMetatableKnowingNil(self, tidx);
            }
        }

        // Check case: No array at all
        //
        if (arrType.ArrayKind() == ArrayType::NoButterflyArrayPart)
        {
            return GetByValHandleMetatableKnowingNil(self, tidx);
        }

        // Check case: Array is not continuous, but index fits in the vector
        //
        if (self->m_butterfly->GetHeader()->IndexFitsInVectorCapacity(index))
        {
            TValue res = *(self->m_butterfly->UnsafeGetInVectorIndexAddr(idx));
            AssertImp(!res.IsNil() && arrType.ArrayKind() == ArrayType::Int32, res.IsInt32(TValue::x_int32Tag));
            AssertImp(!res.IsNil() && arrType.ArrayKind() == ArrayType::Double, res.IsDouble(TValue::x_int32Tag));
            return GetByValHandleMetatable(self, tidx, res);
        }

        // Check case: has sparse map
        // Even if there is a sparse map, if it doesn't contain vector-qualifying index,
        // since in this function 'idx' is vector-qualifying, it must be non-existent
        //
        if (unlikely(arrType.SparseMapContainsVectorIndex()))
        {
            assert(arrType.HasSparseMap());
            return GetByValArraySparseMap(self, tidx, static_cast<double>(idx));
        }
        else
        {
            return GetByValHandleMetatableKnowingNil(self, tidx);
        }
    }

    // Handle the case that index is known to be not in the vector part
    //
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue GetByValArraySparseMap(T self, TValue tidx, double index);

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue ALWAYS_INLINE GetByValHandleMetatableKnowingNil(T self, TValue tidx)
    {
        ArrayType arrType { self->m_arrayType };
        if (likely(!arrType.MayHaveMetatable()))
        {
            return TValue::Nil();
        }

        return GetByValCheckAndCallMetatableMethod(self, tidx);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue ALWAYS_INLINE GetByValHandleMetatable(T self, TValue tidx, TValue rawResult)
    {
        ArrayType arrType { self->m_arrayType };
        if (likely(!arrType.MayHaveMetatable()))
        {
            return TValue::Nil();
        }

        if (likely(!rawResult.IsNil()))
        {
            return rawResult;
        }

        return GetByValCheckAndCallMetatableMethod(self, tidx);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue GetByValCheckAndCallMetatableMethod(T self, TValue tidx);

    // Object header
    //
    SystemHeapPointer<void> m_structure;
    Type m_type;
    GcCellState m_cellState;

    ArrayType m_arrayType;
    uint8_t m_reserved;

    Butterfly* m_butterfly;
    TValue m_inlineStorage[0];
};
static_assert(sizeof(TableObject) == 16);

class StructureIterator
{
public:
    StructureIterator(Structure* hiddenClass);
    StructureIterator(SystemHeapPointer<Structure> hc)
    {
        HeapPtr<Structure> hiddenClass = hc.As();
        assert(hiddenClass->m_type == Type::Structure);
        SystemHeapPointer<StructureAnchorHashTable> aht = TCGet(hiddenClass->m_anchorHashTable);

        m_hiddenClass = hc;
        m_anchorTable = aht;
        m_ord = 0;
        m_maxOrd = hiddenClass->m_numSlots;

        if (aht.m_value != 0)
        {
            HeapPtr<StructureAnchorHashTable> anchorHashTable = aht.As();
            assert(anchorHashTable->m_numBlocks > 0);
            m_curPtr = TCGet(anchorHashTable->m_blockPointers[0]);
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
                HeapPtr<Structure> hiddenClass = m_hiddenClass.As<Structure>();
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
                    HeapPtr<StructureAnchorHashTable> anchorTable = m_anchorTable.As<StructureAnchorHashTable>();
                    m_curPtr = TCGet(anchorTable->m_blockPointers[m_ord >> x_log2_hiddenClassBlockSize]);
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

inline StructureIterator::StructureIterator(Structure* hiddenClass)
    : StructureIterator(VM::GetActiveVMForCurrentThread()->GetHeapPtrTranslator().TranslateToSystemHeapPtr(hiddenClass))
{ }

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
            HeapPtr<GeneralHeapPointer<void>> p = m_blockPointers[blockOrd].As();
            for (uint8_t offset = 0; offset < x_hiddenClassBlockSize; offset++)
            {
                GeneralHeapPointer<void> e = TCGet(p[offset]);
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

template<typename T, typename>
SystemHeapPointer<StructureAnchorHashTable> WARN_UNUSED Structure::BuildNewAnchorTableIfNecessary(T self)
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

    AssertIff(slotAdditionKind == SlotAdditionKind::AddSlotForPolyMetatable, key == vm->GetSpecialKeyForMetadataSlot());

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
                HeapPtr<Structure> anchorClass = GetHiddenClassOfFullBlockPointer(this).As<Structure>();
                assert(anchorClass->m_numSlots > 0 && anchorClass->m_numSlots % x_hiddenClassBlockSize == 0);
                assert(anchorClass->m_numSlots == m_numSlots - m_nonFullBlockLen);
                if (IsAnchorTableContainsFinalBlock(anchorClass))
                {
                    useUpdatedAnchor = true;
                    updatedAnchor = TCGet(anchorClass->m_anchorHashTable);
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
    assert(m_inlineNamedStorageCapacity < x_maxNumSlots);
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
                expandedButterflyCapacity = ComputeInitialButterflyCapacity(m_inlineNamedStorageCapacity);
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
                expandedButterflyCapacity = ComputeNextButterflyCapacity(m_inlineNamedStorageCapacity, m_butterflyNamedStorageCapacity);
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
    uint8_t numElementsInInlineHashTable = nonFullBlockCopyLengthForNewNode + static_cast<uint8_t>(shouldAddKey);
    if (hasFinalBlockPointer && inlineHashTableMustContainFinalBlock)
    {
        numElementsInInlineHashTable += static_cast<uint8_t>(x_hiddenClassBlockSize);
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
    InlineHashTableEntry* htBegin = vm->GetHeapPtrTranslator().TranslateToRawPtr(objectAddressStart.As<InlineHashTableEntry>());
    InlineHashTableEntry* htEnd = htBegin + htSize;
    Structure* r = reinterpret_cast<Structure*>(htEnd);

    ConstructInPlace(r);

    SystemHeapGcObjectHeader::Populate(r);
    r->m_numSlots = newNumSlots;
    r->m_nonFullBlockLen = nonFullBlockCopyLengthForNewNode + static_cast<uint8_t>(shouldAddKey);
    assert(r->m_nonFullBlockLen == ComputeNonFullBlockLength(r->m_numSlots));
    r->m_arrayType = m_arrayType;
    r->m_anchorHashTable = anchorTableForNewNode;
    r->m_inlineHashTableMask = htMaskToStore;
    r->m_inlineNamedStorageCapacity = m_inlineNamedStorageCapacity;
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

    r->m_parent = vm->GetHeapPtrTranslator().TranslateToSystemHeapPtr(this);
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
        if (inlineHashTableMustContainFinalBlock)
        {
            HeapPtr<GeneralHeapPointer<void>> p = GetFinalFullBlockPointer(r).As();
            for (int8_t i = -static_cast<int8_t>(x_hiddenClassBlockSize); i < 0; i++)
            {
                GeneralHeapPointer<void> e = TCGet(p[i]);
                insertNonExistentElementIntoInlineHashTable(e.As(), i /*ordinalOfElement*/);
            }
        }

        // Then insert the non-full block
        //
        size_t len = r->m_nonFullBlockLen;
        for (size_t i = 0; i < len; i++)
        {
            GeneralHeapPointer<void> e = r->m_values[i];
            insertNonExistentElementIntoInlineHashTable(e.As(), static_cast<int8_t>(i) /*ordinalOfElement*/);
        }
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

        uint32_t expectedElementsInInlineHt = r->m_nonFullBlockLen;
        if (HasFinalFullBlockPointer(r->m_numSlots) && !IsAnchorTableContainsFinalBlock(r))
        {
            expectedElementsInInlineHt += x_hiddenClassBlockSize;
        }
        ReleaseAssert(inlineHtElementCount == expectedElementsInInlineHt);
        ReleaseAssert(inlineHtElementSet.size() == inlineHtElementCount);
    }
#endif

    return r;
}

inline Structure* WARN_UNUSED Structure::CreateInitialStructure(VM* vm, uint8_t initialInlineCapacity, uint8_t initialButterflyCapacity)
{
    assert(static_cast<uint32_t>(initialButterflyCapacity) + initialButterflyCapacity <= x_maxNumSlots + 1);

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
    InlineHashTableEntry* htBegin = vm->GetHeapPtrTranslator().TranslateToRawPtr(objectAddressStart.As<InlineHashTableEntry>());
    InlineHashTableEntry* htEnd = htBegin + htSize;
    Structure* r = reinterpret_cast<Structure*>(htEnd);

    memset(htBegin, x_inlineHashTableEmptyValue, hashTableLengthBytes);

    ConstructInPlace(r);

    SystemHeapGcObjectHeader::Populate(r);
    r->m_numSlots = 0;
    r->m_nonFullBlockLen = 0;
    r->m_arrayType = ArrayType::GetInitialArrayType();
    r->m_anchorHashTable.m_value = 0;
    r->m_inlineHashTableMask = htMaskToStore;
    r->m_inlineNamedStorageCapacity = initialInlineCapacity;
    r->m_butterflyNamedStorageCapacity = initialButterflyCapacity;
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
        // TODO: transit to dictionary
        //
        ReleaseAssert(false);
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

    result.m_transitionedToDictionaryMode = false;
    result.m_shouldGrowButterfly = (tkind == TransitionKind::AddPropertyAndGrowPropertyStorageCapacity);
    assert(transitionStructure->m_numSlots == m_numSlots + 1);
    result.m_slotOrdinal = m_numSlots;
    result.m_newStructureOrDictionary = transitionStructure;

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

template<typename T, typename>
TValue TableObject::GetByVal(T self, TValue tidx)
{
    if (tidx.IsInt32(TValue::x_int32Tag))
    {
        int32_t idx = tidx.AsInt32();

        if (likely(ArrayGrowthPolicy::x_arrayBaseOrd <= idx))
        {
            return GetByValVectorIndex(self, tidx, idx);
        }
        else
        {
            ArrayType arrType { self->m_arrayType };

            // Not vector-qualifying index
            // If it exists, it must be in the sparse map
            //
            if (unlikely(arrType.HasSparseMap()))
            {
                assert(arrType.ArrayKind() != ArrayType::NoButterflyArrayPart);
                return GetByValArraySparseMap(self, tidx, static_cast<double>(idx));
            }
            else
            {
                return GetByValHandleMetatableKnowingNil(self, tidx);
            }
        }
    }

    if (tidx.IsDouble(TValue::x_int32Tag))
    {
        double dblIdx = tidx.AsDouble();
        if (IsNaN(dblIdx))
        {
            // TODO: throw error NaN as table index
            ReleaseAssert(false);
        }

        int32_t idx;
        if (likely(IsQualifiedForVectorIndex(dblIdx, idx /*out*/)))
        {
            return GetByValVectorIndex(self, tidx, idx);
        }
        else
        {
            ArrayType arrType { self->m_arrayType };

            // Not vector-qualifying index
            // If it exists, it must be in the sparse map
            //
            if (unlikely(arrType.HasSparseMap()))
            {
                assert(arrType.ArrayKind() != ArrayType::NoButterflyArrayPart);
                return GetByValArraySparseMap(self, tidx, dblIdx);
            }
            else
            {
                return GetByValHandleMetatableKnowingNil(self, tidx);
            }
        }
    }

    if (tidx.IsPointer(TValue::x_mivTag))
    {

    }

}

template<typename T, typename>
TValue TableObject::GetByValArraySparseMap(T self, TValue tidx, double index)
{
    HeapPtr<ArraySparseMap> sparseMap = self->m_butterfly->GetHeader()->GetSparseMap();
    TValue rawResult = TranslateToRawPointer(VM::GetActiveVMForCurrentThread(), sparseMap)->GetByVal(index);
#ifndef NDEBUG
    ArrayType arrType { self->m_arrayType };
    AssertImp(!rawResult.IsNil() && arrType.ArrayKind() == ArrayType::Int32, rawResult.IsInt32(TValue::x_int32Tag));
    AssertImp(!rawResult.IsNil() && arrType.ArrayKind() == ArrayType::Double, rawResult.IsDouble(TValue::x_int32Tag));
#endif
    return GetByValHandleMetatable(self, tidx, rawResult);
}

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

    // slot [m_variadicRetSlotBegin + ord] holds variadic return value 'ord'
    //
    uint32_t m_numVariadicRets;
    uint32_t m_variadicRetSlotBegin;

    // The stack object
    //
    uint64_t* m_stackObject;


};

using InterpreterFn = void(*)(CoroutineRuntimeContext* /*rc*/, RestrictPtr<void> /*stackframe*/, ConstRestrictPtr<uint8_t> /*instr*/, uint64_t /*unused*/);

// Base class for some executable, either an intrinsic, or a bytecode function with some fixed global object, or a user C function
//
class ExecutableCode : public SystemHeapGcObjectHeader
{
public:
    bool IsIntrinsic() const { return m_bytecode == nullptr; }
    bool IsUserCFunction() const { return reinterpret_cast<intptr_t>(m_bytecode) < 0; }
    bool IsBytecodeFunction() const { return reinterpret_cast<intptr_t>(m_bytecode) > 0; }

    using UserCFunctionPrototype = int(*)(void*);

    UserCFunctionPrototype GetCFunctionPtr() const
    {
        assert(IsUserCFunction());
        return reinterpret_cast<UserCFunctionPrototype>(~reinterpret_cast<uintptr_t>(m_bytecode));
    }

    uint8_t m_reserved;

    // The # of fixed arguments and whether it accepts variadic arguments
    // User C function always have m_numFixedArguments == 0 and m_hasVariadicArguments == true
    //
    bool m_hasVariadicArguments;
    uint32_t m_numFixedArguments;

    // This is nullptr iff it is an intrinsic, and negative iff it is a user-provided C function
    //
    uint8_t* m_bytecode;

    // For intrinsic, this is the entrypoint of the intrinsic function
    // For bytecode function, this is the most optimized implementation (interpreter or some JIT tier)
    // For user C function, this is a trampoline that calls the function
    // The 'codeBlock' parameter and 'curBytecode' parameter is not needed for intrinsic or JIT but we have them anyway for a unified interface
    //
    InterpreterFn m_bestEntryPoint;
};
static_assert(sizeof(ExecutableCode) == 24);

class BaselineCodeBlock;
class FLOCodeBlock;

class FunctionExecutable;

// This uniquely corresponds to each pair of <FunctionExecutable, GlobalObject>
// It owns the bytecode and the corresponding metadata (the bytecode is copied from the FunctionExecutable,
// we need our own copy because we do bytecode opcode specialization optimization)
//
class CodeBlock final : public ExecutableCode
{
public:
    GlobalObject* m_globalObject;

    uint32_t m_stackFrameNumSlots;
    uint32_t m_numUpValues;
    uint32_t m_bytecodeLength;
    uint32_t m_bytecodeMetadataLength;

    BaselineCodeBlock* m_baselineCodeBlock;
    FLOCodeBlock* m_floCodeBlock;

    FunctionExecutable* m_owner;

    uint64_t m_bytecodeMetadata[0];

    static constexpr size_t x_trailingArrayOffset = offsetof_member_v<&CodeBlock::m_bytecodeMetadata>;
};

// This uniquely corresponds to a piece of source code that defines a function
//
class FunctionExecutable
{
public:
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, FunctionExecutable>>>
    static CodeBlock* ALWAYS_INLINE GetCodeBlock(T self, GeneralHeapPointer<void> globalObject)
    {
        if (likely(globalObject == self->m_defaultGlobalObject))
        {
            return self->m_defaultCodeBlock;
        }
        assert(self->m_rareGOtoCBMap != nullptr);
        RareGlobalObjectToCodeBlockMap* rareMap = self->m_rareGOtoCBMap;
        auto iter = rareMap->find(globalObject.m_value);
        assert(iter != rareMap->end());
        return iter->second;
    }

    uint8_t* m_bytecode;
    uint32_t m_bytecodeLength;
    GeneralHeapPointer<void> m_defaultGlobalObject;
    CodeBlock* m_defaultCodeBlock;
    using RareGlobalObjectToCodeBlockMap = std::unordered_map<int32_t, CodeBlock*>;
    RareGlobalObjectToCodeBlockMap* m_rareGOtoCBMap;

    uint32_t m_numUpValues;
    uint32_t m_bytecodeMetadataLength;
    uint32_t m_stackFrameNumSlots;


};

class FunctionObject
{
public:
    // Object header
    //
    // Note that a CodeBlock defines both FunctionExecutable and GlobalObject,
    // so the upValue list does not contain the global object (if the ExecutableCode is not a CodeBlock, then the global object doesn't matter either)
    //
    SystemHeapPointer<ExecutableCode> m_executable;
    Type m_type;
    GcCellState m_cellState;

    uint16_t m_reserved;

    TValue m_upValues[0];
};
static_assert(sizeof(FunctionObject) == 8);

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
    BcConstant,
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

inline void EnterInterpreter(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
{
    Dispatch(rc, sfp, bcu);
}

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

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcCall* bc = reinterpret_cast<const BcCall*>(bcu);
        assert(bc->m_opcode == static_cast<uint8_t>(Opcode::BcCall));
        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);

        HeapPtr<ExecutableCode> callerEc = TCGet(hdr->m_func->m_executable).As();
        assert(TranslateToRawPointer(callerEc)->IsBytecodeFunction());
        HeapPtr<CodeBlock> callerCb = static_cast<HeapPtr<CodeBlock>>(callerEc);
        uint8_t* callerBytecodeStart = callerCb->m_bytecode;
        hdr->m_callerBytecodeOffset = SafeIntegerCast<uint32_t>(bcu - callerBytecodeStart);

        assert(bc->m_funcSlot.IsLocal());
        TValue* begin = StackFrameHeader::GetLocalAddr(sfp, bc->m_funcSlot);
        TValue func = *begin;
        begin++;

        if (func.IsPointer(TValue::x_mivTag))
        {
            if (func.AsPointer().As<UserHeapGcObjectHeader>()->m_type == Type::FUNCTION)
            {
                HeapPtr<FunctionObject> target = func.AsPointer().As<FunctionObject>();

                TValue* sfEnd = reinterpret_cast<TValue*>(sfp) + callerCb->m_stackFrameNumSlots;
                TValue* baseForNextFrame = sfEnd + x_sizeOfStackFrameHeaderInTermsOfTValue;

                uint32_t numFixedArgsToPass = bc->m_numFixedParams;
                uint32_t totalArgs = numFixedArgsToPass;
                if (bc->m_passVariadicRetAsParam)
                {
                    totalArgs += rc->m_numVariadicRets;
                }

                HeapPtr<ExecutableCode> calleeEc = TCGet(target->m_executable).As();

                uint32_t numCalleeExpectingArgs = calleeEc->m_numFixedArguments;
                bool calleeTakesVarArgs = calleeEc->m_hasVariadicArguments;

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
                sfh->m_caller = reinterpret_cast<StackFrameHeader*>(sfp);
                sfh->m_retAddr = reinterpret_cast<void*>(OnReturn);
                sfh->m_func = target;

                _Pragma("clang diagnostic push")
                _Pragma("clang diagnostic ignored \"-Wuninitialized\"")
                uint64_t unused;
                uint8_t* calleeBytecode = calleeEc->m_bytecode;
                InterpreterFn calleeFn = calleeEc->m_bestEntryPoint;
                [[clang::musttail]] return calleeFn(rc, baseForNextFrame, calleeBytecode, unused);
                _Pragma("clang diagnostic pop")
            }
            else
            {
                assert(false && "unimplemented");
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
        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackframe);
        HeapPtr<ExecutableCode> callerEc = TCGet(hdr->m_func->m_executable).As();
        assert(TranslateToRawPointer(callerEc)->IsBytecodeFunction());
        uint8_t* callerBytecodeStart = callerEc->m_bytecode;
        ConstRestrictPtr<uint8_t> bcu = callerBytecodeStart + hdr->m_callerBytecodeOffset;
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

class BcConstant
{
public:
    uint8_t m_opcode;
    BytecodeSlot m_dst;
    TValue m_value;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcConstant* bc = reinterpret_cast<const BcConstant*>(bcu);
        assert(bc->m_opcode == static_cast<uint8_t>(Opcode::BcConstant));
        *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = bc->m_value;
        Dispatch(rc, stackframe, bcu + sizeof(BcConstant));
    }
} __attribute__((__packed__));

}   // namespace ToyLang
