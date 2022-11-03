#pragma once

#include "common_utils.h"
#include "memory_ptr.h"
#include "vm.h"
#include "structure.h"

// This doesn't really need to inherit the GC header, but for now let's make thing simple..
//
class alignas(8) ArraySparseMap final : public UserHeapGcObjectHeader
{
public:
    static constexpr uint32_t x_hiddenClassForArraySparseMap = 0x20;

    using StorageType = std::unordered_map<double, TValue>;

    struct HashTableEntry
    {
        // An empty slot is represented by m_key == NaN
        // This is fine because Lua doesn't allow NaN to be used as an array index
        //
        double m_key;
        TValue m_value;
    };

    ~ArraySparseMap()
    {
        delete [] m_hashTable;
    }

    static ArraySparseMap* WARN_UNUSED AllocateEmptyArraySparseMap(VM* vm)
    {
        HeapPtr<ArraySparseMap> hp = vm->AllocFromUserHeap(sizeof(ArraySparseMap)).AsNoAssert<ArraySparseMap>();
        ArraySparseMap* r = TranslateToRawPointer(vm, hp);
        ConstructInPlace(r);
        UserHeapGcObjectHeader::Populate(r);
        r->m_hiddenClass = ArraySparseMap::x_hiddenClassForArraySparseMap;
        r->m_hashMask = 1;
        r->m_elementCount = 0;
        r->m_hashTable = new HashTableEntry[2];
        r->m_hashTable[0].m_key = std::numeric_limits<double>::quiet_NaN();
        r->m_hashTable[1].m_key = std::numeric_limits<double>::quiet_NaN();
        return r;
    }

    ArraySparseMap* WARN_UNUSED Clone(VM* vm)
    {
        HeapPtr<ArraySparseMap> hp = vm->AllocFromUserHeap(sizeof(ArraySparseMap)).AsNoAssert<ArraySparseMap>();
        ArraySparseMap* r = TranslateToRawPointer(vm, hp);
        ConstructInPlace(r);
        UserHeapGcObjectHeader::Populate(r);
        r->m_hiddenClass = ArraySparseMap::x_hiddenClassForArraySparseMap;
        r->m_hashMask = m_hashMask;
        r->m_elementCount = m_elementCount;
        r->m_hashTable = new HashTableEntry[m_hashMask + 1];
        memcpy(r->m_hashTable, m_hashTable, sizeof(HashTableEntry) * (m_hashMask + 1));
        return r;
    }

    void ResizeIfNeeded()
    {
        if (likely(m_elementCount * 2 <= m_hashMask + 1))
        {
            return;
        }
        ResizeImpl();
    }

    void NO_INLINE ResizeImpl()
    {
        assert(is_power_of_2(m_hashMask + 1));
        uint32_t oldMask = m_hashMask;
        uint32_t newMask = oldMask * 2 + 1;
        ReleaseAssert(newMask < std::numeric_limits<uint32_t>::max());
        m_hashMask = newMask;

        HashTableEntry* oldHt = m_hashTable;
        m_hashTable = new HashTableEntry[newMask + 1];
        for (size_t i = 0; i <= newMask; i++)
        {
             m_hashTable[i].m_key = std::numeric_limits<double>::quiet_NaN();
        }

        DEBUG_ONLY(uint32_t cnt = 0;)
        uint32_t nonNilElement = 0;
        HashTableEntry* oldHtEnd = oldHt + oldMask + 1;
        HashTableEntry* curEntry = oldHt;
        while (curEntry < oldHtEnd)
        {
            double key = curEntry->m_key;
            if (!IsNaN(key))
            {
                TValue value = curEntry->m_value;
                if (!value.IsNil())
                {
                    size_t slot = HashPrimitiveTypes(key) & newMask;
                    while (!IsNaN(m_hashTable[slot].m_key))
                    {
                        assert(!UnsafeFloatEqual(m_hashTable[slot].m_key, key));
                        slot = (slot + 1) & newMask;
                    }
                    m_hashTable[slot].m_key = key;
                    m_hashTable[slot].m_value = value;
                    nonNilElement++;
                }
                DEBUG_ONLY(cnt++;)
            }
            curEntry++;
        }
        assert(cnt == m_elementCount);
        m_elementCount = nonNilElement;

        delete [] oldHt;
    }

    TValue GetByVal(double key)
    {
        size_t hashMask = m_hashMask;
        size_t slot = HashPrimitiveTypes(key) & hashMask;
        while (true)
        {
            HashTableEntry& entry = m_hashTable[slot];
            if (UnsafeFloatEqual(entry.m_key, key))
            {
                return entry.m_value;
            }
            if (IsNaN(entry.m_key))
            {
                return TValue::Nil();
            }
            slot = (slot + 1) & hashMask;
        }
    }

    // Return -1 if the key isn't found in the hashtable
    // This is used by Lua 'next', so a 'nil' value is intentionally treated as 'found'
    //
    uint32_t GetHashSlotOrdinal(double key)
    {
        size_t hashMask = m_hashMask;
        size_t slot = HashPrimitiveTypes(key) & hashMask;
        while (true)
        {
            HashTableEntry& entry = m_hashTable[slot];
            if (UnsafeFloatEqual(entry.m_key, key))
            {
                return static_cast<uint32_t>(slot);
            }
            if (IsNaN(entry.m_key))
            {
                return static_cast<uint32_t>(-1);
            }
            slot = (slot + 1) & hashMask;
        }
    }

    void Insert(double key, TValue value)
    {
        assert(!IsNaN(key));
        size_t hashMask = m_hashMask;
        size_t slot = HashPrimitiveTypes(key) & hashMask;
        while (true)
        {
            HashTableEntry& entry = m_hashTable[slot];
            if (UnsafeFloatEqual(entry.m_key, key))
            {
                entry.m_value = value;
                return;
            }
            if (IsNaN(entry.m_key))
            {
                entry.m_key = key;
                entry.m_value = value;
                m_elementCount++;
                ResizeIfNeeded();
                return;
            }
            slot = (slot + 1) & hashMask;
        }
    }

    uint32_t m_hashMask;
    uint32_t m_elementCount;
    HashTableEntry* m_hashTable;
};

class alignas(8) ButterflyHeader
{
public:
    bool IsContinuous()
    {
        return m_arrayLengthIfContinuous >= ArrayGrowthPolicy::x_arrayBaseOrd;
    }

    bool CanUseFastPathGetForContinuousArray(int64_t idx)
    {
        assert(ArrayGrowthPolicy::x_arrayBaseOrd <= idx);
        return idx < m_arrayLengthIfContinuous;
    }

    bool IndexFitsInVectorCapacity(int64_t idx)
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

    TValue* UnsafeGetInVectorIndexAddr(int64_t index)
    {
        assert(GetHeader()->IndexFitsInVectorCapacity(index));
        return reinterpret_cast<TValue*>(this) + index;
    }

    static int32_t WARN_UNUSED GetOutlineStorageIndex(uint32_t slot, uint32_t inlineCapacity)
    {
        assert(slot >= inlineCapacity);
        return static_cast<int32_t>(inlineCapacity) - static_cast<int32_t>(slot) - 1 - (1 - ArrayGrowthPolicy::x_arrayBaseOrd);
    }

    TValue* GetNamedPropertyAddr(int32_t ord)
    {
        assert(ord < ArrayGrowthPolicy::x_arrayBaseOrd - 1);
        return &(reinterpret_cast<TValue*>(this)[ord]);
    }

    TValue GetNamedProperty(int32_t ord)
    {
        return *GetNamedPropertyAddr(ord);
    }
};

struct GetByIdICInfo
{
    // Condition on the fixed hidden class (structure or dictionary), whether the GetById can use inline cache
    //
    enum class ICKind : uint8_t
    {
        // The hidden class is a UncachableDictionary
        //
        UncachableDictionary,
        // The GetById must return nil because the property doesn't exist
        //
        MustBeNil,
        // The GetById must return nil because the property doesn't exist,
        // however this is not cacheable because the hidden class is a CacheableDictionary
        //
        MustBeNilButUncacheable,
        // The property is in the inlined storage in m_slot
        //
        InlinedStorage,
        // The property is in the outlined storage in m_slot
        //
        OutlinedStorage
    };

    ICKind m_icKind;
    // If 'm_mayHaveMetatable = true', the table may have a metatable containing a '__index' handler
    // so if the result turns out to be nil, we must inspect the metatable
    //
    bool m_mayHaveMetatable;
    int32_t m_slot;
};

struct GetByIntegerIndexICInfo
{
    enum class ICKind: uint8_t
    {
        // It must be in the vector storage if it exists
        // So if the index is not a vector index or not within range, the result must be nil
        //
        VectorStorage,
        // It must return nil because we have no butterfly array part
        //
        NoArrayPart,
        // The array has a sparse map but the sparse map doesn't contain vector-qualifying index
        // So for vector-qualifying index it must be in vector range if exists, but otherwise it must be in sparse map if exists
        //
        VectorStorageXorSparseMap,
        // The array has a sparse map and the sparse map contains vector-qualifying index
        // So for vector-qualifying index we must check both
        //
        VectorStorageOrSparseMap
    };

    ICKind m_icKind;
    // If 'm_mayHaveMetatable = true', the table may have a metatable containing a '__index' handler
    // so if the result turns out to be nil, we must inspect the metatable
    //
    bool m_mayHaveMetatable;
    // If 'm_isContinuous' is true, it means the array is continuous. This also implies that ICKind must be VectorStorage
    //
    bool m_isContinuous;
};

struct PutByIdICInfo
{
    // The destination slot of this PutById
    // Note that for 'TransitionedToDictionaryMode', m_slot isn't filled
    //
    enum class ICKind : uint8_t
    {
        // The PutById transitioned the table from Structure mode to CacheableDictionary mode, not inline cachable
        //
        TransitionedToDictionaryMode,
        // The property should be written to the inlined storage in m_slot
        //
        InlinedStorage,
        // The property should be written to the outlined storage in m_slot
        //
        OutlinedStorage
    };

    ICKind m_icKind;
    // Whether or not the property exists
    // Note that iff m_propertyExists == false, the PutById will transit the current structure to a new structure,
    // and the new structure is stored in m_newStructure
    //
    bool m_propertyExists;
    // Whether or not we will need to grow the butterfly
    // Only possible if m_propertyExists == false
    //
    bool m_shouldGrowButterfly;
    // If 'm_mayHaveMetatable = true', the table may have a metatable containing a '__newindex' handler
    // so we must check if the property contains value nil to decide if we need to inspect the metatable
    //
    bool m_mayHaveMetatable;
    // Whether this PutById is cacheable
    //
    bool m_isInlineCacheable;
    SystemHeapPointer<void> m_hiddenClass;
    int32_t m_slot;
    SystemHeapPointer<Structure> m_newStructure;
};

struct PutByIntegerIndexICInfo
{
    // Unlike PutById, the array-part-put always have the possibility of going to a slow path, since the
    // capacity of the vector storage is stored in the object, and the index is also not known in advance.
    //
    // So for a given cached ArrayType, we establish a fast-path guarded by two checks: IndexCheckKind and ValueCheckKind.
    // If both checks pass, we can run the fast-path for that ArrayType. Otherwise, we need to fallback to the general slow-path.
    //
    // IndexCheckKind: if the index to write fails the check, we need to go to slow-path
    //
    enum class IndexCheckKind : uint8_t
    {
        // No fast-path exists. The main case for this is if the object is in dictionary mode.
        // This is because the hidden class is the IC key (which is necessary because the object might not even be a table, so we cannot use ArrayType as IC key),
        // and if we want to make CacheableDictionary cachable for array indexing, we will have to migrate it to a new address whenever
        // we change array type, which is annoying, and it's unclear if doing so is beneficial (since it seems bizzare to have a dictionary with array part).
        //
        ForceSlowPath,
        // Fast-path check for ArrayType's NoButterflyArrayPart array kind
        // Check that the butterfly exists, the vector capacity is > 0, and we are writing to x_arrayBaseOrd
        // (so after the write the array is a continuous array of length 1)
        //
        NoArrayPart,
        // Fast-path check for ArrayType's non-continuous case
        // Check that the index is in bound. If yes, we can just write there
        //
        InBound,
        // Fast-path check for ArrayType's continuous case
        // Check that the index is in bound and within continuousLength + 1, so after the write the array is still continuous
        //
        Continuous
    };

    // ValueCheckKind: if the value to write fails the check, we need to go to slow-path
    //
    enum class ValueCheckKind : uint8_t
    {
        Int32,
        Int32OrNil,
        Double,
        DoubleOrNil,
        NotNil,
        NoCheck
    };

    IndexCheckKind m_indexCheckKind;
    ValueCheckKind m_valueCheckKind;
    bool m_mayHaveMetatable;
    // Only useful if IndexCheckKind == NoArrayPart: this is the only case where fast path changes array type & hidden class
    //
    ArrayType m_newArrayType;
    SystemHeapPointer<void> m_hiddenClass;
    // Only useful if IndexCheckKind == NoArrayPart: this is the only case where fast path changes array type & hidden class
    //
    SystemHeapPointer<void> m_newHiddenClass;
};

class alignas(8) TableObject
{
public:
    static bool WARN_UNUSED ALWAYS_INLINE IsInt64Index(double vidx, int64_t& idx /*out*/)
    {
        // FIXME: out-of-range float-to-int conversion is UB in C/C++, and it can actually cause incorrect behavior either if
        // clang deduces 'vidx' is out-of-range (and cause undesired "optimization" of the expression into a poison value),
        // or on non-x86-64 architectures (e.g. ARM64) where the float-to-int instruction has saturation or other semantics.
        // However, if we manually check for the range, while it worksaround the UB and works correctly on every
        // architecture, clang cannot optimize the check away on x86-64 and generates inferior code on x86-64,
        // so we leave the code with the UB as for now.
        //
        idx = static_cast<int64_t>(vidx);
        if (!UnsafeFloatEqual(static_cast<double>(idx), vidx))
        {
            // The conversion is not lossless, return
            //
            return false;
        }
        return true;
    }

    template<bool ignoreAssert = false>
    static constexpr GetByIntegerIndexICInfo::ICKind NaiveComputeGetByIntegerIndexIcKindFromArrayType(ArrayType arrType)
    {
        // Check case: continuous array
        //
        if (likely(arrType.IsContinuous()))
        {
            return GetByIntegerIndexICInfo::ICKind::VectorStorage;
        }
        // Check case: No array at all
        //
        if (arrType.ArrayKind() == ArrayType::Kind::NoButterflyArrayPart)
        {
            assert(ignoreAssert || !arrType.HasSparseMap());
            return GetByIntegerIndexICInfo::ICKind::NoArrayPart;
        }

        // Now, we know the array type contains a vector part and potentially a sparse map
        //
        if (unlikely(arrType.SparseMapContainsVectorIndex()))
        {
            return GetByIntegerIndexICInfo::ICKind::VectorStorageOrSparseMap;
        }
        else if (unlikely(arrType.HasSparseMap()))
        {
            return GetByIntegerIndexICInfo::ICKind::VectorStorageXorSparseMap;
        }
        else
        {
            return GetByIntegerIndexICInfo::ICKind::VectorStorage;
        }
    }

    static consteval uint64_t ComputeGetByIntegerIndexIcBitVectorForNonContinuousArray()
    {
        // This is a trick that precomputes a bitvector to speed up some steps in PrepareGetByIntegerIndex
        // We rely on the fact that ArrayType has 6 useful bits, and the mayHaveMetatable bit is bit 5
        //
        // So we can enumerate through all the ArrayType by enumerating [0, 32)
        // There are 32 such combinations, and GetByIntegerIndexICInfo::ICKind also happens to take 2 bits
        // So we can precompute all the results and store them into a uint64_t bitvector.
        //
        static_assert(ArrayType::x_usefulBitsMask == 63);
        static_assert(ArrayType::BFM_mayHaveMetatable::BitOffset() == 5 && ArrayType::BFM_mayHaveMetatable::BitWidth() == 1);

        uint64_t result = 0;
        for (uint8_t mask = 0; mask < 32; mask++)
        {
            ArrayType arrType;
            arrType.m_asValue = mask;
            GetByIntegerIndexICInfo::ICKind kind = NaiveComputeGetByIntegerIndexIcKindFromArrayType<true /*ignoreAssert*/>(arrType);
            uint64_t k64 = static_cast<uint64_t>(kind);
            assert(k64 < 4);
            result |= k64 << (mask * 2);
        }
        return result;
    }

    static constexpr GetByIntegerIndexICInfo::ICKind ComputeGetByIntegerIndexIcKindFromArrayType(ArrayType arrType)
    {
        constexpr uint64_t x_result_bitvector = ComputeGetByIntegerIndexIcBitVectorForNonContinuousArray();
        GetByIntegerIndexICInfo::ICKind result = static_cast<GetByIntegerIndexICInfo::ICKind>((x_result_bitvector >> ((arrType.m_asValue * 2) & 63)) & 3);
        assert(result == NaiveComputeGetByIntegerIndexIcKindFromArrayType(arrType));
        return result;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static void ALWAYS_INLINE PrepareGetByIntegerIndex(T self, GetByIntegerIndexICInfo& icInfo /*out*/)
    {
        ArrayType arrType = TCGet(self->m_arrayType);

        icInfo.m_mayHaveMetatable = arrType.MayHaveMetatable();
        icInfo.m_icKind = ComputeGetByIntegerIndexIcKindFromArrayType(arrType);
        icInfo.m_isContinuous = arrType.IsContinuous();
    }

    // Returns isSuccess = false if 'idx' does not fit in the vector storage
    //
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static std::pair<TValue, bool /*isSuccess*/> WARN_UNUSED ALWAYS_INLINE TryAccessIndexInVectorStorage(T self, int64_t idx)
    {
#ifndef NDEBUG
        ArrayType arrType = TCGet(self->m_arrayType);
#endif
        assert(arrType.ArrayKind() != ArrayType::Kind::NoButterflyArrayPart);
        if (likely(ArrayGrowthPolicy::x_arrayBaseOrd <= idx && self->m_butterfly->GetHeader()->IndexFitsInVectorCapacity(idx)))
        {
            TValue res = *(self->m_butterfly->UnsafeGetInVectorIndexAddr(idx));
            // If the array is continuous, result must be non-nil
            //
            AssertImp(self->m_butterfly->GetHeader()->CanUseFastPathGetForContinuousArray(idx), !res.Is<tNil>());
            AssertImp(!res.Is<tNil>() && arrType.ArrayKind() == ArrayType::Kind::Int32, res.Is<tInt32>());
            AssertImp(!res.Is<tNil>() && arrType.ArrayKind() == ArrayType::Kind::Double, res.Is<tDouble>());
            return std::make_pair(res, true /*isSuccess*/);
        }

        return std::make_pair(TValue(), false /*isSuccess*/);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static bool WARN_UNUSED ALWAYS_INLINE CheckIndexFitsInContinuousArray(T self, int64_t idx)
    {
#ifndef NDEBUG
        ArrayType arrType = TCGet(self->m_arrayType);
#endif
        assert(arrType.IsContinuous());
        if (likely(ArrayGrowthPolicy::x_arrayBaseOrd <= idx && self->m_butterfly->GetHeader()->CanUseFastPathGetForContinuousArray(idx)))
        {
#ifndef NDEBUG
            TValue res = *(self->m_butterfly->UnsafeGetInVectorIndexAddr(idx));
            assert(!res.Is<tNil>());
            AssertImp(arrType.ArrayKind() == ArrayType::Kind::Int32, res.Is<tInt32>());
            AssertImp(arrType.ArrayKind() == ArrayType::Kind::Double, res.Is<tDouble>());
#endif
            return true;
        }

#ifndef NDEBUG
        {
            auto [res, success] = TryAccessIndexInVectorStorage(self, idx);
            AssertImp(success, res.IsNil());
        }
#endif
        return false;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue GetByIntegerIndex(T self, int64_t idx, GetByIntegerIndexICInfo icInfo)
    {
#ifndef NDEBUG
        ArrayType arrType = TCGet(self->m_arrayType);
#endif

        if (icInfo.m_icKind == GetByIntegerIndexICInfo::ICKind::NoArrayPart)
        {
            assert(!arrType.HasSparseMap());
            return TValue::Nil();
        }

        // Check if the index is in vector storage range
        //
        {
            auto [res, success] = TryAccessIndexInVectorStorage(self, idx);
            if (likely(success))
            {
                return res;
            }
        }

        // Now we know the index is outside vector storage range
        //
        switch (icInfo.m_icKind)
        {
        case GetByIntegerIndexICInfo::ICKind::VectorStorage:
        {
            // The array part doesn't have a sparse map, so result must be nil
            //
            return TValue::Nil();
        }
        case GetByIntegerIndexICInfo::ICKind::NoArrayPart:
        {
            __builtin_unreachable();
        }
        case GetByIntegerIndexICInfo::ICKind::VectorStorageXorSparseMap:
        {
            if (idx < ArrayGrowthPolicy::x_arrayBaseOrd || idx > ArrayGrowthPolicy::x_unconditionallySparseMapCutoff)
            {
                assert(arrType.HasSparseMap());
                return QueryArraySparseMap(self, static_cast<double>(idx));
            }
            else
            {
                // The sparse map is known to not contain vector-qualifying index, so the result must be nil
                //
                return TValue::Nil();
            }
        }
        case GetByIntegerIndexICInfo::ICKind::VectorStorageOrSparseMap:
        {
            assert(arrType.HasSparseMap());
            return QueryArraySparseMap(self, static_cast<double>(idx));
        }
        }
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue GetByInt32Val(T self, int32_t idx, GetByIntegerIndexICInfo icInfo)
    {
        return GetByIntegerIndex(self, idx, icInfo);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue GetByDoubleVal(T self, double idx, GetByIntegerIndexICInfo icInfo)
    {
        // This function expects that 'idx' is not NaN
        //
        assert(!IsNaN(idx));

        {
            int64_t idx64;
            if (likely(IsInt64Index(idx, idx64 /*out*/)))
            {
                return GetByIntegerIndex(self, idx64, icInfo);
            }
        }

        // Not vector-qualifying index
        // If it exists, it must be in the sparse map
        //
        if (icInfo.m_icKind == GetByIntegerIndexICInfo::ICKind::VectorStorageOrSparseMap ||
            icInfo.m_icKind == GetByIntegerIndexICInfo::ICKind::VectorStorageXorSparseMap)
        {
            assert(TCGet(self->m_arrayType).ArrayKind() != ArrayType::Kind::NoButterflyArrayPart);
            return QueryArraySparseMap(self, idx);
        }
        else
        {
            return TValue::Nil();
        }
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue __attribute__((__preserve_most__)) NO_INLINE QueryArraySparseMap(T self, double idx)
    {
#ifndef NDEBUG
        ArrayType arrType = TCGet(self->m_arrayType);
        assert(arrType.HasSparseMap());
#endif
        HeapPtr<ArraySparseMap> sparseMap = self->m_butterfly->GetHeader()->GetSparseMap();
        TValue res = TranslateToRawPointer(sparseMap)->GetByVal(idx);
#ifndef NDEBUG
        AssertImp(!res.IsNil() && arrType.ArrayKind() == ArrayType::Kind::Int32, res.IsInt32());
        AssertImp(!res.IsNil() && arrType.ArrayKind() == ArrayType::Kind::Double, res.IsDouble());
#endif
        return res;
    }

    template<typename U>
    static void PrepareGetById(SystemHeapPointer<void> hiddenClass, UserHeapPointer<U> propertyName, GetByIdICInfo& icInfo /*out*/)
    {
        static_assert(std::is_same_v<U, void> || std::is_same_v<U, HeapString>);

        HeapEntityType ty = hiddenClass.As<SystemHeapGcObjectHeader>()->m_type;
        assert(ty == HeapEntityType::Structure || ty == HeapEntityType::CacheableDictionary || ty == HeapEntityType::UncacheableDictionary);

        uint32_t slotOrd;
        uint32_t inlineStorageCapacity;
        bool found;

        if (likely(ty == HeapEntityType::Structure))
        {
            HeapPtr<Structure> structure = hiddenClass.As<Structure>();
            icInfo.m_mayHaveMetatable = (structure->m_metatable != 0);
            inlineStorageCapacity = structure->m_inlineNamedStorageCapacity;

            if constexpr(std::is_same_v<U, HeapString>)
            {
                found = Structure::GetSlotOrdinalFromStringProperty(structure, propertyName, slotOrd /*out*/);
            }
            else
            {
                found = Structure::GetSlotOrdinalFromMaybeNonStringProperty(structure, propertyName, slotOrd /*out*/);
            }
        }
        else if (likely(ty == HeapEntityType::CacheableDictionary))
        {
            HeapPtr<CacheableDictionary> dict = hiddenClass.As<CacheableDictionary>();
            icInfo.m_mayHaveMetatable = (dict->m_metatable.m_value != 0);
            inlineStorageCapacity = dict->m_inlineNamedStorageCapacity;

            if constexpr(std::is_same_v<U, HeapString>)
            {
                found = CacheableDictionary::GetSlotOrdinalFromStringProperty(dict, propertyName, slotOrd /*out*/);
            }
            else
            {
                found = CacheableDictionary::GetSlotOrdinalFromMaybeNonStringProperty(dict, propertyName, slotOrd /*out*/);
            }
        }
        else
        {
            // TODO: support UncacheableDictionary
            ReleaseAssert(false && "unimplemented");
        }

        if (found)
        {
            if (slotOrd < inlineStorageCapacity)
            {
                icInfo.m_icKind = GetByIdICInfo::ICKind::InlinedStorage;
                icInfo.m_slot = static_cast<int32_t>(slotOrd);
            }
            else
            {
                icInfo.m_icKind = GetByIdICInfo::ICKind::OutlinedStorage;
                icInfo.m_slot = Butterfly::GetOutlineStorageIndex(slotOrd, inlineStorageCapacity);
            }
        }
        else
        {
            if (ty == HeapEntityType::Structure)
            {
                icInfo.m_icKind = GetByIdICInfo::ICKind::MustBeNil;
            }
            else
            {
                assert(ty == HeapEntityType::CacheableDictionary);
                icInfo.m_icKind = GetByIdICInfo::ICKind::MustBeNilButUncacheable;
            }
        }
    }

    template<typename U>
    static void PrepareGetById(TableObject* self, UserHeapPointer<U> propertyName, GetByIdICInfo& icInfo /*out*/)
    {
        return PrepareGetById(self->m_hiddenClass, propertyName, icInfo /*out*/);
    }

    template<typename U>
    static void PrepareGetById(HeapPtr<TableObject> self, UserHeapPointer<U> propertyName, GetByIdICInfo& icInfo /*out*/)
    {
        return PrepareGetById(TCGet(self->m_hiddenClass), propertyName, icInfo /*out*/);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue WARN_UNUSED ALWAYS_INLINE GetById(T self, UserHeapPointer<void> /*propertyName*/, GetByIdICInfo icInfo)
    {
        if (icInfo.m_icKind == GetByIdICInfo::ICKind::MustBeNil || icInfo.m_icKind == GetByIdICInfo::ICKind::MustBeNilButUncacheable)
        {
            return TValue::Nil();
        }

        if (icInfo.m_icKind == GetByIdICInfo::ICKind::InlinedStorage)
        {
            return TCGet(self->m_inlineStorage[icInfo.m_slot]);
        }

        if (icInfo.m_icKind == GetByIdICInfo::ICKind::OutlinedStorage)
        {
            return self->m_butterfly->GetNamedProperty(icInfo.m_slot);
        }

        // TODO: support UncacheableDictionary
        ReleaseAssert(false && "not implemented");
    }

    template<typename T, typename U, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static void PreparePutById(T self, UserHeapPointer<U> propertyName, PutByIdICInfo& icInfo /*out*/)
    {
        static_assert(std::is_same_v<U, void> || std::is_same_v<U, HeapString>);

        SystemHeapPointer<void> hiddenClass = TCGet(self->m_hiddenClass);
        HeapEntityType ty = hiddenClass.As<SystemHeapGcObjectHeader>()->m_type;
        assert(ty == HeapEntityType::Structure || ty == HeapEntityType::CacheableDictionary || ty == HeapEntityType::UncacheableDictionary);

        icInfo.m_hiddenClass = hiddenClass;
        icInfo.m_isInlineCacheable = true;

        if (likely(ty == HeapEntityType::Structure))
        {
            HeapPtr<Structure> structure = hiddenClass.As<Structure>();
            icInfo.m_mayHaveMetatable = (structure->m_metatable != 0);

            uint32_t slotOrd;
            bool found;
            if constexpr(std::is_same_v<U, HeapString>)
            {
                found = Structure::GetSlotOrdinalFromStringProperty(structure, propertyName, slotOrd /*out*/);
            }
            else
            {
                found = Structure::GetSlotOrdinalFromMaybeNonStringProperty(structure, propertyName, slotOrd /*out*/);
            }
            icInfo.m_propertyExists = found;
            if (found)
            {
                icInfo.m_shouldGrowButterfly = false;
                uint32_t inlineStorageCapacity = structure->m_inlineNamedStorageCapacity;
                if (slotOrd < inlineStorageCapacity)
                {
                    icInfo.m_icKind = PutByIdICInfo::ICKind::InlinedStorage;
                    icInfo.m_slot = static_cast<int32_t>(slotOrd);

                }
                else
                {
                    icInfo.m_icKind = PutByIdICInfo::ICKind::OutlinedStorage;
                    icInfo.m_slot = Butterfly::GetOutlineStorageIndex(slotOrd, inlineStorageCapacity);
                }
            }
            else
            {
                VM* vm = VM::GetActiveVMForCurrentThread();
                Structure::AddNewPropertyResult addNewPropResult;
                TranslateToRawPointer(vm, structure)->AddNonExistentProperty(vm, propertyName.template As<void>(), addNewPropResult /*out*/);
                if (unlikely(addNewPropResult.m_shouldTransitionToDictionaryMode))
                {
                    icInfo.m_icKind = PutByIdICInfo::ICKind::TransitionedToDictionaryMode;
                    icInfo.m_isInlineCacheable = false;
                }
                else
                {
                    slotOrd = addNewPropResult.m_slotOrdinal;
                    uint32_t inlineStorageCapacity = structure->m_inlineNamedStorageCapacity;
                    icInfo.m_shouldGrowButterfly = addNewPropResult.m_shouldGrowButterfly;
                    icInfo.m_newStructure = addNewPropResult.m_newStructure;
                    if (slotOrd < inlineStorageCapacity)
                    {
                        icInfo.m_icKind = PutByIdICInfo::ICKind::InlinedStorage;
                        icInfo.m_slot = static_cast<int32_t>(slotOrd);
                    }
                    else
                    {
                        assert(slotOrd < icInfo.m_newStructure.As()->m_numSlots);
                        icInfo.m_icKind = PutByIdICInfo::ICKind::OutlinedStorage;
                        icInfo.m_slot = Butterfly::GetOutlineStorageIndex(slotOrd, inlineStorageCapacity);
                    }
                }
            }
        }
        else if (ty == HeapEntityType::CacheableDictionary)
        {
            HeapPtr<CacheableDictionary> dict = hiddenClass.As<CacheableDictionary>();

            CacheableDictionary::PutByIdResult res;
            if constexpr(std::is_same_v<U, HeapString>)
            {
                CacheableDictionary::PreparePutById(dict, propertyName, res /*out*/);
            }
            else
            {
                CacheableDictionary::PreparePutByMaybeNonStringKey(dict, propertyName, res /*out*/);
            }

            if (unlikely(res.m_shouldGrowButterfly))
            {
                TableObject* rawSelf = TranslateToRawPointer(self);
                assert(dict->m_butterflyNamedStorageCapacity < res.m_newButterflyCapacity);
                rawSelf->GrowButterfly<true /*isGrowNamedStorage*/>(res.m_newButterflyCapacity);
                dict->m_butterflyNamedStorageCapacity = res.m_newButterflyCapacity;
            }

            if (unlikely(res.m_shouldCheckForTransitionToUncacheableDictionary))
            {
                // TODO: check for transition to UncacheableDictionary
                //
            }

            // For Dictionary, since it is 1-on-1 with the object, we always insert the property if it doesn't exist
            // Since we always pre-fill every unused slot with 'nil', the new property always has value 'nil' if it were just inserted, as desired
            // (this works because unlike Javascript, Lua doesn't have the concept of 'undefined')
            // So for the IC, the property should always appear to be existent
            //
            icInfo.m_propertyExists = true;
            icInfo.m_shouldGrowButterfly = false;
            icInfo.m_mayHaveMetatable = (dict->m_metatable.m_value != 0);

            uint32_t slotOrd = res.m_slot;
            uint32_t inlineStorageCapacity = dict->m_inlineNamedStorageCapacity;
            if (slotOrd < inlineStorageCapacity)
            {
                icInfo.m_icKind = PutByIdICInfo::ICKind::InlinedStorage;
                icInfo.m_slot = static_cast<int32_t>(slotOrd);
            }
            else
            {
                assert(slotOrd < dict->m_slotCount);
                icInfo.m_icKind = PutByIdICInfo::ICKind::OutlinedStorage;
                icInfo.m_slot = Butterfly::GetOutlineStorageIndex(slotOrd, inlineStorageCapacity);
            }
        }
        else
        {
            icInfo.m_isInlineCacheable = false;
            // TODO: support UncacheableDictionary
            ReleaseAssert(false && "unimplemented");
        }
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static bool WARN_UNUSED PutByIdNeedToCheckMetatable(T self, PutByIdICInfo icInfo)
    {
        if (likely(!icInfo.m_mayHaveMetatable))
        {
            return false;
        }

        if (!icInfo.m_propertyExists)
        {
            return true;
        }

        // Now we need to determine if the property is nil
        //
        assert(icInfo.m_icKind != PutByIdICInfo::ICKind::TransitionedToDictionaryMode);
        if (icInfo.m_icKind == PutByIdICInfo::ICKind::InlinedStorage)
        {
            TValue val = TCGet(self->m_inlineStorage[icInfo.m_slot]);
            return val.IsNil();
        }
        else
        {
            assert(icInfo.m_icKind == PutByIdICInfo::ICKind::OutlinedStorage);
            TValue val = TCGet(*self->m_butterfly->GetNamedPropertyAddr(icInfo.m_slot));
            return val.IsNil();
        }
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue WARN_UNUSED GetValueForSlot(T self, uint32_t slotOrd, uint8_t inlineStorageCapacity)
    {
        if (slotOrd < inlineStorageCapacity)
        {
            return TCGet(self->m_inlineStorage[slotOrd]);
        }
        else
        {
            int32_t butterflySlot = Butterfly::GetOutlineStorageIndex(slotOrd, inlineStorageCapacity);
            return self->m_butterfly->GetNamedProperty(butterflySlot);
        }
    }

    // If isGrowNamedStorage == true, grow named storage capacity to 'newCapacity', and keep current array storage
    // Otherwise, grow array storage capacity to 'newCapcity', and keep current named storage
    //
    // This function must be called before changing the structure
    //
    template<bool isGrowNamedStorage>
    void ALWAYS_INLINE GrowButterfly(uint32_t newCapacity)
    {
        if (m_butterfly == nullptr)
        {
#ifndef NDEBUG
            if (m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure)
            {
                assert(m_hiddenClass.As<Structure>()->m_butterflyNamedStorageCapacity == 0);
            }
            else if (m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::CacheableDictionary)
            {
                assert(m_hiddenClass.As<CacheableDictionary>()->m_butterflyNamedStorageCapacity == 0);
            }
            else
            {
                // TODO: assert for UncacheableDictionary
            }
#endif
            uint64_t* butterflyStart = new uint64_t[newCapacity + 1];
            uint32_t offset;
            if constexpr(isGrowNamedStorage)
            {
                offset = newCapacity + static_cast<uint32_t>(1 - ArrayGrowthPolicy::x_arrayBaseOrd);
            }
            else
            {
                offset = static_cast<uint32_t>(1 - ArrayGrowthPolicy::x_arrayBaseOrd);
            }
            Butterfly* butterfly = reinterpret_cast<Butterfly*>(butterflyStart + offset);
            if constexpr(isGrowNamedStorage)
            {
                butterfly->GetHeader()->m_arrayStorageCapacity = 0;
                butterfly->GetHeader()->m_arrayLengthIfContinuous = ArrayGrowthPolicy::x_arrayBaseOrd;

                uint64_t nilVal = TValue::Nil().m_value;
                uint64_t* nilFillBegin = butterflyStart;
                uint64_t* nilFillEnd = butterflyStart + newCapacity;
                while (nilFillBegin < nilFillEnd)
                {
                    *nilFillBegin = nilVal;
                    nilFillBegin++;
                }
            }
            else
            {
                butterfly->GetHeader()->m_arrayStorageCapacity = static_cast<int32_t>(newCapacity);
                butterfly->GetHeader()->m_arrayLengthIfContinuous = ArrayGrowthPolicy::x_arrayBaseOrd;

                uint64_t nilVal = TValue::Nil().m_value;
                uint64_t* nilFillBegin = butterflyStart + 1;
                uint64_t* nilFillEnd = butterflyStart + 1 + newCapacity;
                while (nilFillBegin < nilFillEnd)
                {
                    *nilFillBegin = nilVal;
                    nilFillBegin++;
                }
            }
            m_butterfly = butterfly;
        }
        else
        {
            uint32_t oldArrayStorageCapacity = static_cast<uint32_t>(m_butterfly->GetHeader()->m_arrayStorageCapacity);
            uint32_t oldNamedStorageCapacity;
            HeapEntityType hiddenClassTy = m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type;
            if (hiddenClassTy == HeapEntityType::Structure)
            {
                oldNamedStorageCapacity = m_hiddenClass.As<Structure>()->m_butterflyNamedStorageCapacity;
            }
            else if (hiddenClassTy == HeapEntityType::CacheableDictionary)
            {
                oldNamedStorageCapacity = m_hiddenClass.As<CacheableDictionary>()->m_butterflyNamedStorageCapacity;
            }
            else
            {
                assert(hiddenClassTy == HeapEntityType::UncacheableDictionary);
                // TODO: support UncacheableDictionary
                ReleaseAssert(false && "unimplemented");
            }
            uint32_t oldButterflySlots = oldArrayStorageCapacity + oldNamedStorageCapacity + 1;

            uint32_t oldButterflyStartOffset = oldNamedStorageCapacity + static_cast<uint32_t>(1 - ArrayGrowthPolicy::x_arrayBaseOrd);
            uint64_t* oldButterflyStart = reinterpret_cast<uint64_t*>(m_butterfly) - oldButterflyStartOffset;

            uint32_t newButterflySlots;
            uint32_t newButterflyNamedStorageCapacity;
            if constexpr(isGrowNamedStorage)
            {
                assert(newCapacity > oldNamedStorageCapacity);
                newButterflySlots = newCapacity + oldArrayStorageCapacity + 1;
                newButterflyNamedStorageCapacity = newCapacity;
            }
            else
            {
                assert(newCapacity > oldArrayStorageCapacity);
                newButterflySlots = oldNamedStorageCapacity + newCapacity + 1;
                newButterflyNamedStorageCapacity = oldNamedStorageCapacity;
            }

            assert(newButterflySlots > oldButterflySlots);

            uint64_t* newButterflyStart = new uint64_t[newButterflySlots];
            if constexpr(isGrowNamedStorage)
            {
                uint64_t nilVal = TValue::Nil().m_value;
                uint64_t* nilFillBegin = newButterflyStart;
                uint64_t* nilFillEnd = newButterflyStart + newButterflySlots - oldButterflySlots;
                while (nilFillBegin < nilFillEnd)
                {
                    *nilFillBegin = nilVal;
                    nilFillBegin++;
                }
                memcpy(nilFillEnd, oldButterflyStart, oldButterflySlots * sizeof(uint64_t));
            }
            else
            {
                memcpy(newButterflyStart, oldButterflyStart, oldButterflySlots * sizeof(uint64_t));
                uint64_t nilVal = TValue::Nil().m_value;
                uint64_t* nilFillBegin = newButterflyStart + oldButterflySlots;
                uint64_t* nilFillEnd = newButterflyStart + newButterflySlots;
                while (nilFillBegin < nilFillEnd)
                {
                    *nilFillBegin = nilVal;
                    nilFillBegin++;
                }
            }

            delete [] oldButterflyStart;
            uint32_t offset = newButterflyNamedStorageCapacity + static_cast<uint32_t>(1 - ArrayGrowthPolicy::x_arrayBaseOrd);
            Butterfly* butterfly = reinterpret_cast<Butterfly*>(newButterflyStart + offset);
            if constexpr(!isGrowNamedStorage)
            {
                butterfly->GetHeader()->m_arrayStorageCapacity = static_cast<int32_t>(newCapacity);
            }
            m_butterfly = butterfly;
        }
    }

    void PutByIdTransitionToDictionary(VM* vm, UserHeapPointer<void> prop, Structure* structure, TValue newValue)
    {
        CacheableDictionary::CreateFromStructureResult res;
        CacheableDictionary::CreateFromStructure(vm, this, structure, prop, res /*out*/);
        CacheableDictionary* dictionary = res.m_dictionary;
        if (res.m_shouldGrowButterfly)
        {
            GrowButterfly<true /*isGrowNamedStorage*/>(dictionary->m_butterflyNamedStorageCapacity);
        }
        else
        {
            assert(structure->m_butterflyNamedStorageCapacity == dictionary->m_butterflyNamedStorageCapacity);
        }
        assert(res.m_slot < dictionary->m_inlineNamedStorageCapacity + dictionary->m_butterflyNamedStorageCapacity);
        if (res.m_slot < dictionary->m_inlineNamedStorageCapacity)
        {
            m_inlineStorage[res.m_slot] = newValue;
        }
        else
        {
            int32_t index = Butterfly::GetOutlineStorageIndex(res.m_slot, dictionary->m_inlineNamedStorageCapacity);
            *m_butterfly->GetNamedPropertyAddr(index) = newValue;
        }
        m_hiddenClass = dictionary;

        // If the dictionary has a metatable, it should always be treated as PolyMetatable mode,
        // since changing the metatable will not change the dictionary pointer
        //
        {
            bool hasMetatable = (dictionary->m_metatable.m_value != 0);
            m_arrayType.SetMayHaveMetatable(hasMetatable);
        }
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static void ALWAYS_INLINE PutById(T self, UserHeapPointer<void> propertyName, TValue newValue, PutByIdICInfo icInfo)
    {
        if (icInfo.m_icKind == PutByIdICInfo::ICKind::TransitionedToDictionaryMode)
        {
            VM* vm = VM::GetActiveVMForCurrentThread();
            TableObject* rawSelf = TranslateToRawPointer(self);
            rawSelf->PutByIdTransitionToDictionary(vm, propertyName, TranslateToRawPointer(vm, icInfo.m_hiddenClass.As<Structure>()), newValue);
            return;
        }

        if (!icInfo.m_propertyExists)
        {
            if (icInfo.m_shouldGrowButterfly)
            {
                TableObject* rawSelf = TranslateToRawPointer(self);
                rawSelf->GrowButterfly<true /*isGrowNamedStorage*/>(icInfo.m_newStructure.As()->m_butterflyNamedStorageCapacity);
            }

            TCSet(self->m_hiddenClass, SystemHeapPointer<void> { icInfo.m_newStructure.As() });
        }

        if (icInfo.m_icKind == PutByIdICInfo::ICKind::InlinedStorage)
        {
            TCSet(self->m_inlineStorage[icInfo.m_slot], newValue);
        }

        if (icInfo.m_icKind == PutByIdICInfo::ICKind::OutlinedStorage)
        {
            TCSet(*(self->m_butterfly->GetNamedPropertyAddr(icInfo.m_slot)), newValue);
        }
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static void PreparePutByIntegerIndex(T self, int64_t index, TValue value, PutByIntegerIndexICInfo& icInfo /*out*/)
    {
        ArrayType arrType = TCGet(self->m_arrayType);
        icInfo.m_mayHaveMetatable = arrType.MayHaveMetatable();
        icInfo.m_hiddenClass = TCGet(self->m_hiddenClass);
        AssertImp(icInfo.m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure,
                  arrType.m_asValue == icInfo.m_hiddenClass.As<Structure>()->m_arrayType.m_asValue);

        auto setForceSlowPath = [&icInfo]() ALWAYS_INLINE
        {
            icInfo.m_indexCheckKind = PutByIntegerIndexICInfo::IndexCheckKind::ForceSlowPath;
            icInfo.m_valueCheckKind = PutByIntegerIndexICInfo::ValueCheckKind::NoCheck;
        };

        // We cannot IC any fast path if the object is in dictionary mode: see comment on IndexCheckKind::ForceSlowPath
        //
        if (icInfo.m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type != HeapEntityType::Structure)
        {
            setForceSlowPath();
            return;
        }

        if (index < ArrayGrowthPolicy::x_arrayBaseOrd)
        {
            setForceSlowPath();
            return;
        }

        if (unlikely(arrType.ArrayKind() == ArrayType::Kind::NoButterflyArrayPart))
        {
            if (index != ArrayGrowthPolicy::x_arrayBaseOrd)
            {
                setForceSlowPath();
                return;
            }

            auto setNewStructureAndArrayType = [&](ArrayType::Kind newKind) ALWAYS_INLINE
            {
                VM* vm = VM::GetActiveVMForCurrentThread();
                Structure* structure = TranslateToRawPointer(vm, icInfo.m_hiddenClass.As<Structure>());
                ArrayType newArrType = arrType;
                newArrType.SetArrayKind(newKind);
                newArrType.SetIsContinuous(true);
                Structure* newStructure = structure->UpdateArrayType(vm, newArrType);
                icInfo.m_newHiddenClass = newStructure;
                icInfo.m_newArrayType = newArrType;
            };

            icInfo.m_indexCheckKind = PutByIntegerIndexICInfo::IndexCheckKind::NoArrayPart;
            if (value.IsInt32())
            {
                icInfo.m_valueCheckKind = PutByIntegerIndexICInfo::ValueCheckKind::Int32;
                setNewStructureAndArrayType(ArrayType::Kind::Int32);
            }
            else if (value.IsDouble())
            {
                icInfo.m_valueCheckKind = PutByIntegerIndexICInfo::ValueCheckKind::Double;
                setNewStructureAndArrayType(ArrayType::Kind::Double);
            }
            else if (!value.IsNil())
            {
                icInfo.m_valueCheckKind = PutByIntegerIndexICInfo::ValueCheckKind::NotNil;
                setNewStructureAndArrayType(ArrayType::Kind::Any);
            }
            else
            {
                setForceSlowPath();
                return;
            }
        }
        else if (arrType.IsContinuous())
        {
            icInfo.m_indexCheckKind = PutByIntegerIndexICInfo::IndexCheckKind::Continuous;
            switch (arrType.ArrayKind())
            {
            case ArrayType::Kind::Int32:
            {
                icInfo.m_valueCheckKind = PutByIntegerIndexICInfo::ValueCheckKind::Int32;
                break;
            }
            case ArrayType::Kind::Double:
            {
                icInfo.m_valueCheckKind = PutByIntegerIndexICInfo::ValueCheckKind::Double;
                break;
            }
            case ArrayType::Kind::Any:
            {
                // For continuous array, the fast path must not write nil, since it can make the array non-continuous
                //
                icInfo.m_valueCheckKind = PutByIntegerIndexICInfo::ValueCheckKind::NotNil;
                break;
            }
            case ArrayType::Kind::NoButterflyArrayPart:
            {
                assert(false);
                __builtin_unreachable();
            }
            }
        }
        else
        {
            icInfo.m_indexCheckKind = PutByIntegerIndexICInfo::IndexCheckKind::InBound;
            switch (arrType.ArrayKind())
            {
            case ArrayType::Kind::Int32:
            {
                icInfo.m_valueCheckKind = PutByIntegerIndexICInfo::ValueCheckKind::Int32OrNil;
                break;
            }
            case ArrayType::Kind::Double:
            {
                icInfo.m_valueCheckKind = PutByIntegerIndexICInfo::ValueCheckKind::DoubleOrNil;
                break;
            }
            case ArrayType::Kind::Any:
            {
                // For non-continuous array, writing 'nil' is fine as long as it's in bound
                //
                icInfo.m_valueCheckKind = PutByIntegerIndexICInfo::ValueCheckKind::NoCheck;
                break;
            }
            case ArrayType::Kind::NoButterflyArrayPart:
            {
                assert(false);
                __builtin_unreachable();
            }
            }
        }
    }

    // Return false if need to fallback to slow path
    //
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static bool WARN_UNUSED TryPutByIntegerIndexFast(T self, int64_t index, TValue value, PutByIntegerIndexICInfo icInfo)
    {
        switch (icInfo.m_valueCheckKind)
        {
        case PutByIntegerIndexICInfo::ValueCheckKind::Int32:
        {
            if (!value.IsInt32())
            {
                return false;
            }
            break;
        }
        case PutByIntegerIndexICInfo::ValueCheckKind::Int32OrNil:
        {
            if (!value.IsInt32() && !value.IsNil())
            {
                return false;
            }
            break;
        }
        case PutByIntegerIndexICInfo::ValueCheckKind::Double:
        {
            if (!value.IsDouble())
            {
                return false;
            }
            break;
        }
        case PutByIntegerIndexICInfo::ValueCheckKind::DoubleOrNil:
        {
            if (!value.IsDouble() && !value.IsNil())
            {
                return false;
            }
            break;
        }
        case PutByIntegerIndexICInfo::ValueCheckKind::NotNil:
        {
            if (value.IsNil())
            {
                return false;
            }
            break;
        }
        case PutByIntegerIndexICInfo::ValueCheckKind::NoCheck:
        {
            break;  // no-op
        }
        }

        switch (icInfo.m_indexCheckKind)
        {
        case PutByIntegerIndexICInfo::IndexCheckKind::Continuous:
        {
            // For the continuous case, a nil value should always fail the value check above
            //
            assert(!value.IsNil());
            Butterfly* butterfly = self->m_butterfly;
            if (likely(ArrayGrowthPolicy::x_arrayBaseOrd <= index && butterfly->GetHeader()->CanUseFastPathGetForContinuousArray(index)))
            {
                // The put is into a continuous array, we can just do it
                //
                *butterfly->UnsafeGetInVectorIndexAddr(index) = value;
                return true;
            }
            else if (likely(index == butterfly->GetHeader()->m_arrayLengthIfContinuous))
            {
                // The put will extend the array length by 1. We can do it as long as we have enough capacity
                //
                assert(index <= butterfly->GetHeader()->m_arrayStorageCapacity + ArrayGrowthPolicy::x_arrayBaseOrd);
                if (unlikely(index == butterfly->GetHeader()->m_arrayStorageCapacity + ArrayGrowthPolicy::x_arrayBaseOrd))
                {
                    return false;
                }
                *butterfly->UnsafeGetInVectorIndexAddr(index) = value;
                butterfly->GetHeader()->m_arrayLengthIfContinuous = static_cast<int32_t>(index + 1);
                return true;
            }
            else
            {
                return false;
            }
        }
        case PutByIntegerIndexICInfo::IndexCheckKind::InBound:
        {
            Butterfly* butterfly = self->m_butterfly;
            if (likely(ArrayGrowthPolicy::x_arrayBaseOrd <= index && butterfly->GetHeader()->IndexFitsInVectorCapacity(index)))
            {
                // The put is within bound, we can just do it
                //
                *butterfly->UnsafeGetInVectorIndexAddr(index) = value;
                return true;
            }
            else
            {
                return false;
            }
        }
        case PutByIntegerIndexICInfo::IndexCheckKind::NoArrayPart:
        {
            // The fast path is that the operation transits from empty array to continuous array (i.e. index == x_arrayBaseOrd)
            // AND the butterfly vector part capacity is > 0. In this case we can just do the write,
            // and transit the structure and arrayType base on our pre-computation
            //
            if (unlikely(self->m_butterfly == nullptr))
            {
                return false;
            }
            if (unlikely(index != ArrayGrowthPolicy::x_arrayBaseOrd))
            {
                return false;
            }
            Butterfly* butterfly = self->m_butterfly;
            if (likely(butterfly->GetHeader()->m_arrayStorageCapacity > 0))
            {
                *butterfly->UnsafeGetInVectorIndexAddr(ArrayGrowthPolicy::x_arrayBaseOrd) = value;
                butterfly->GetHeader()->m_arrayLengthIfContinuous = 1 + ArrayGrowthPolicy::x_arrayBaseOrd;
                TCSet(self->m_arrayType, icInfo.m_newArrayType);
                TCSet(self->m_hiddenClass, icInfo.m_newHiddenClass);
                return true;
            }
            else
            {
                return false;
            }
        }
        case PutByIntegerIndexICInfo::IndexCheckKind::ForceSlowPath:
        {
            return false;
        }
        }
    }

    void PutByIntegerIndexSlow(VM* vm, int64_t index64, TValue value)
    {
        if (index64 < ArrayGrowthPolicy::x_arrayBaseOrd || index64 > ArrayGrowthPolicy::x_unconditionallySparseMapCutoff)
        {
            PutIndexIntoSparseMap(vm, false /*isVectorQualifyingIndex*/, static_cast<double>(index64), value);
            return;
        }

        // This debug variable validates that we did not enter this slow-path for no reason (i.e. the fast path should have handled the case it ought to handle)
        //
        DEBUG_ONLY(bool didSomethingNontrivial = false;)

        // The fast path can only handle the case where the hidden class is structure
        //
        DEBUG_ONLY(didSomethingNontrivial |= (m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type != HeapEntityType::Structure));

        int32_t index = static_cast<int32_t>(index64);
        ArrayType arrType = m_arrayType;
        ArrayType newArrayType = arrType;

        // This huge if statement below is responsible for the following:
        // 1. Make the decision of go to sparse map
        // 2. Grow the butterfly if necessary
        // 3. Update the IsContinuous() field for newArrayType
        // 4. Update m_arrayLengthIfContinuous if necessary
        // 5. Update the ArrayKind() field of newArrayType
        //
        if (arrType.ArrayKind() == ArrayType::Kind::NoButterflyArrayPart)
        {
            // The array part doesn't exist and we are putting a nil, so no-op
            //
            if (value.IsNil())
            {
                return;
            }

            // The array part contains 0 elements now, so the density check is known to fail.
            // So if index is > x_alwaysVectorCutoff, it will always go to sparse map
            //
            if (index > ArrayGrowthPolicy::x_alwaysVectorCutoff)
            {
                PutIndexIntoSparseMap(vm, true /*isVectorQualifyingIndex*/, static_cast<double>(index), value);
                return;
            }

            bool needToGrowOrCreateButterfly = false;
            if (m_butterfly == nullptr)
            {
                needToGrowOrCreateButterfly = true;
            }
            else
            {
                int32_t currentCapacity = m_butterfly->GetHeader()->m_arrayStorageCapacity;
                if (index >= currentCapacity + ArrayGrowthPolicy::x_arrayBaseOrd)
                {
                    needToGrowOrCreateButterfly = true;
                }
            }

            if (needToGrowOrCreateButterfly)
            {
                uint32_t newCapacity = ArrayGrowthPolicy::x_initialVectorPartCapacity;
                newCapacity = std::max(newCapacity, static_cast<uint32_t>(index + 1 - ArrayGrowthPolicy::x_arrayBaseOrd));
                GrowButterfly<false /*isGrowNamedStorage*/>(newCapacity);

                // We did something nontrivial because the fast path cannot create or grow butterfly
                //
                DEBUG_ONLY(didSomethingNontrivial = true;)
            }

            // The array after the put is continuous iff we are putting to x_arrayBaseOrd
            //
            if (index == ArrayGrowthPolicy::x_arrayBaseOrd)
            {
                newArrayType.SetIsContinuous(true);
                assert(m_butterfly->GetHeader()->m_arrayLengthIfContinuous == ArrayGrowthPolicy::x_arrayBaseOrd);
                m_butterfly->GetHeader()->m_arrayLengthIfContinuous = ArrayGrowthPolicy::x_arrayBaseOrd + 1;
            }
            else
            {
                newArrayType.SetIsContinuous(false);
                m_butterfly->GetHeader()->m_arrayLengthIfContinuous = ArrayGrowthPolicy::x_arrayBaseOrd - 1;

                // We did something nontrivial because the fast path only handles the case that the array after the operation is continuous
                //
                DEBUG_ONLY(didSomethingNontrivial = true;)
            }

            assert(didSomethingNontrivial);

            // Update the ArrayType Kind
            //
            if (value.IsInt32())
            {
                newArrayType.SetArrayKind(ArrayType::Kind::Int32);
            }
            else if (value.IsDouble())
            {
                newArrayType.SetArrayKind(ArrayType::Kind::Double);
            }
            else
            {
                assert(!value.IsNil());
                newArrayType.SetArrayKind(ArrayType::Kind::Any);
            }
        }
        else
        {
            Butterfly* butterfly = m_butterfly;
            assert(butterfly != nullptr);
            int32_t currentCapacity = butterfly->GetHeader()->m_arrayStorageCapacity;

            if (index >= currentCapacity + ArrayGrowthPolicy::x_arrayBaseOrd)
            {
                // The put is out of bound, we need to either grow butterfly to accommodate or go to sparse map
                //
                if (arrType.SparseMapContainsVectorIndex())
                {
                    // The butterfly cannot grow, so go to sparse map
                    //
                    PutIndexIntoSparseMap(vm, true /*isVectorQualifyingIndex*/, static_cast<double>(index), value);
                    return;
                }

                if (value.IsNil())
                {
                    // We are putting to a vector-qualifying index, the sparse map doesn't contain any vector-qualifying index, and we are out-of-bound
                    // This implies that the original value in 'index' is nil, and the value to put is also nil, so no-op
                    //
                    return;
                }

                // Figure out the new butterfly capacity
                // By default we grow the capacity by x_vectorGrowthFactor (but do not exceed x_unconditionallySparseMapCutoff),
                // but if that's still not enough, we grow to just enough capacity to hold the index
                //
                uint32_t newCapacity = static_cast<uint32_t>(currentCapacity * ArrayGrowthPolicy::x_vectorGrowthFactor);
                newCapacity = std::min(newCapacity, static_cast<uint32_t>(ArrayGrowthPolicy::x_unconditionallySparseMapCutoff));
                newCapacity = std::max(newCapacity, static_cast<uint32_t>(index + 1 - ArrayGrowthPolicy::x_arrayBaseOrd));

                // In the case of a out-of-bound put, the new array is continuous only if the old array
                // is continuous and the put is one past the end of the continuous vector storage
                //
                bool isContinuous = arrType.IsContinuous();
                DEBUG_ONLY(bool shouldIncrementContinuousLength = false;)
                if (isContinuous)
                {
                    if (index != butterfly->GetHeader()->m_arrayLengthIfContinuous)
                    {
                        isContinuous = false;
                    }
                    else
                    {
                        assert(index == currentCapacity + ArrayGrowthPolicy::x_arrayBaseOrd);
                        DEBUG_ONLY(shouldIncrementContinuousLength = true;)
                    }
                }
                AssertIff(shouldIncrementContinuousLength, isContinuous);

                // Base on the array policy, check if we should put it to sparse map
                // Note: it is intentional that we do not check x_unconditionallySparseMapCutoff,
                // because it has been checked at the start of the function
                //
                bool shouldPutToSparseMap = false;
                if (index > ArrayGrowthPolicy::x_sparseMapUnlessContinuousCutoff)
                {
                    if (!isContinuous)
                    {
                        shouldPutToSparseMap = true;
                    }
                }
                else if (index > ArrayGrowthPolicy::x_alwaysVectorCutoff)
                {
                    uint64_t nonNilCount = 1;
                    for (int32_t i = ArrayGrowthPolicy::x_arrayBaseOrd; i < currentCapacity + ArrayGrowthPolicy::x_arrayBaseOrd; i++)
                    {
                        if (!butterfly->UnsafeGetInVectorIndexAddr(i)->IsNil())
                        {
                            nonNilCount++;
                        }
                    }

                    // This check is a bit problematic: we are growing our array based on the initial butterfly size,
                    // and if we change our initial butterfly size, it can change whether an array which density is close to threshold
                    // should go to sparse map or not, causing unexpected performance change.
                    // We should probably make our growth strategy not depend on the initial butterfly size to fix this problem
                    // (and it's a good idea anyway once we have a real memory allocator). But for now let's be simple.
                    //
                    uint64_t maxCapacityMaintainingMinimalDensity = nonNilCount * ArrayGrowthPolicy::x_densityCutoff;
                    if (newCapacity > maxCapacityMaintainingMinimalDensity)
                    {
                        shouldPutToSparseMap = true;
                    }
                }

                if (shouldPutToSparseMap)
                {
                    PutIndexIntoSparseMap(vm, true /*isVectorQualifyingIndex*/, static_cast<double>(index), value);
                    return;
                }

                GrowButterfly<false /*isGrowNamedStorage*/>(newCapacity);

                newArrayType.SetIsContinuous(isContinuous);
                if (isContinuous)
                {
                    assert(m_butterfly->GetHeader()->m_arrayLengthIfContinuous >= ArrayGrowthPolicy::x_arrayBaseOrd);
                    m_butterfly->GetHeader()->m_arrayLengthIfContinuous++;
                }
                else
                {
                    if (arrType.IsContinuous())
                    {
                        // We just turned from continuous to discontinuous
                        //
                        assert(m_butterfly->GetHeader()->m_arrayLengthIfContinuous >= ArrayGrowthPolicy::x_arrayBaseOrd);
                        m_butterfly->GetHeader()->m_arrayLengthIfContinuous = ArrayGrowthPolicy::x_arrayBaseOrd - 1;
                    }
                }

                // We did something nontrivial, because we growed the butterfly, which the fast path cannot do
                //
                DEBUG_ONLY(didSomethingNontrivial = true;)
            }
            else
            {
                // In bound, no need to grow capacity and will never go to sparse map
                //
                // Update IsContinuous() and m_arrayLengthIfContinuous
                //
                bool isContinuous = arrType.IsContinuous();
                if (isContinuous)
                {
                    int32_t continuousLength = butterfly->GetHeader()->m_arrayLengthIfContinuous;
                    if (value.IsNil())
                    {
                        // The fast path cannot handle the case of writing a nil to a continuous array
                        //
                        DEBUG_ONLY(didSomethingNontrivial = true;)

                        if (index >= continuousLength)
                        {
                            // We are writing a nil value into a slot known to be nil, no-op
                            //
                            return;
                        }

                        if (index == continuousLength - 1)
                        {
                            // We just deleted the last element in the continuous sequence
                            // The sequence is still continuous, but we need to update length
                            //
                            butterfly->GetHeader()->m_arrayLengthIfContinuous = continuousLength - 1;
                        }
                        else
                        {
                            isContinuous = false;
                        }
                    }
                    else
                    {
                        // We are writing a non-nil value
                        // The array becomes not continuous if the index is > continuousLength
                        //
                        if (index > continuousLength)
                        {
                            isContinuous = false;
                        }
                        else if (index == continuousLength)
                        {
                            butterfly->GetHeader()->m_arrayLengthIfContinuous = continuousLength + 1;
                        }
                    }

                    if (!isContinuous)
                    {
                        // We just turned from continuous to discontinuous
                        //
                        assert(m_butterfly->GetHeader()->m_arrayLengthIfContinuous >= ArrayGrowthPolicy::x_arrayBaseOrd);
                        m_butterfly->GetHeader()->m_arrayLengthIfContinuous = ArrayGrowthPolicy::x_arrayBaseOrd - 1;
                    }
                }

                newArrayType.SetIsContinuous(isContinuous);
            }

            // Update ArrayKind of the new array type
            //
            if (arrType.ArrayKind() == ArrayType::Kind::Int32)
            {
                if (!value.IsInt32() && !value.IsNil())
                {
                    newArrayType.SetArrayKind(ArrayType::Kind::Any);
                }
            }
            else if (arrType.ArrayKind() == ArrayType::Kind::Double)
            {
                if (!value.IsDouble() && !value.IsNil())
                {
                    newArrayType.SetArrayKind(ArrayType::Kind::Any);
                }
            }
        }

        // Perform the write
        //
        *m_butterfly->UnsafeGetInVectorIndexAddr(index) = value;

        // If the new array type is different from the old array type, we need to update m_arrayType and m_hiddenClass
        //
        if (arrType.m_asValue != newArrayType.m_asValue)
        {
            HeapEntityType hiddenClassType = m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type;
            assert(hiddenClassType == HeapEntityType::Structure || hiddenClassType == HeapEntityType::CacheableDictionary || hiddenClassType == HeapEntityType::UncacheableDictionary);
            if (hiddenClassType == HeapEntityType::Structure)
            {
                Structure* structure = TranslateToRawPointer(vm, m_hiddenClass.As<Structure>());
                Structure* newStructure = structure->UpdateArrayType(vm, newArrayType);
                m_hiddenClass = newStructure;
                m_arrayType = newArrayType;
            }
            else
            {
                // For dictionary, just update m_arrayType
                //
                m_arrayType = newArrayType;
            }

            DEBUG_ONLY(didSomethingNontrivial = true;)
        }

        assert(didSomethingNontrivial);
    }

    ArraySparseMap* WARN_UNUSED AllocateNewArraySparseMap(VM* vm)
    {
        assert(m_butterfly != nullptr);
        assert(!m_butterfly->GetHeader()->HasSparseMap());
        ArraySparseMap* sparseMap = ArraySparseMap::AllocateEmptyArraySparseMap(vm);
        m_butterfly->GetHeader()->m_arrayLengthIfContinuous = GeneralHeapPointer<ArraySparseMap>(sparseMap).m_value;
        return sparseMap;
    }

    ArraySparseMap* WARN_UNUSED GetOrAllocateSparseMap(VM* vm)
    {
        if (unlikely(m_butterfly == nullptr))
        {
            GrowButterfly<false /*isGrowNamedStorage*/>(0 /*newCapacity*/);
            return AllocateNewArraySparseMap(vm);
        }
        else
        {
            if (likely(m_butterfly->GetHeader()->HasSparseMap()))
            {
                return TranslateToRawPointer(vm, m_butterfly->GetHeader()->GetSparseMap());
            }
            else
            {
                return AllocateNewArraySparseMap(vm);
            }
        }
    }

    static bool IsVectorQualifyingIndex(double index)
    {
        int64_t idx64;
        if (IsInt64Index(index, idx64 /*out*/))
        {
            return idx64 >= ArrayGrowthPolicy::x_arrayBaseOrd && idx64 <= ArrayGrowthPolicy::x_unconditionallySparseMapCutoff;
        }
        else
        {
            return false;
        }
    }

    void PutIndexIntoSparseMap(VM* vm, bool isVectorQualifyingIndex, double index, TValue value)
    {
#ifndef NDEBUG
        // Assert that the 'isVectorQualifyingIndex' parameter is accurate
        //
        {
            AssertIff(isVectorQualifyingIndex, IsVectorQualifyingIndex(index));
            int64_t idx64;
            if (IsInt64Index(index, idx64 /*out*/))
            {
                // If the index is within vector storage range it should never reach this function
                //
                if (isVectorQualifyingIndex && m_butterfly)
                {
                    assert(!m_butterfly->GetHeader()->IndexFitsInVectorCapacity(idx64));
                }
            }
            else
            {
                assert(!isVectorQualifyingIndex);
            }
        }
#endif

        assert(!IsNaN(index));

        ArrayType arrType = m_arrayType;
        ArrayType newArrayType = arrType;

        newArrayType.SetHasSparseMap(true);
        if (isVectorQualifyingIndex)
        {
            newArrayType.SetSparseMapContainsVectorIndex(true);
        }
        newArrayType.SetIsContinuous(false);
        // For now, don't bother with a more accurate ArrayKind for simplicity
        //
        newArrayType.SetArrayKind(ArrayType::Kind::Any);

        ArraySparseMap* sparseMap = GetOrAllocateSparseMap(vm);
        sparseMap->Insert(index, value);

        if (arrType.m_asValue != newArrayType.m_asValue)
        {
            HeapEntityType ty = m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type;
            assert(ty == HeapEntityType::Structure || ty == HeapEntityType::CacheableDictionary || ty == HeapEntityType::UncacheableDictionary);
            if (ty == HeapEntityType::Structure)
            {
                m_hiddenClass = TranslateToRawPointer(vm, m_hiddenClass.As<Structure>())->UpdateArrayType(vm, newArrayType);
                m_arrayType = newArrayType;
            }
            else
            {
                m_arrayType = newArrayType;
            }
        }
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static void RawPutByValIntegerIndex(T self, int64_t index, TValue value)
    {
        PutByIntegerIndexICInfo icInfo;
        PreparePutByIntegerIndex(self, index, value, icInfo /*out*/);
        if (!TryPutByIntegerIndexFast(self, index, value, icInfo))
        {
            VM* vm = VM::GetActiveVMForCurrentThread();
            TableObject* obj = TranslateToRawPointer(vm, self);
            obj->PutByIntegerIndexSlow(vm, index, value);
        }
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static void RawPutByValDoubleIndex(T self, double index, TValue value)
    {
        assert(!IsNaN(index));
        int64_t idx64;
        if (likely(IsInt64Index(index, idx64 /*out*/)))
        {
            RawPutByValIntegerIndex(self, idx64, value);
        }
        else
        {
            VM* vm = VM::GetActiveVMForCurrentThread();
            TableObject* obj = TranslateToRawPointer(vm, self);
            obj->PutIndexIntoSparseMap(vm, false /*isVectorQualifyingIndex*/, index, value);
        }
    }

    // Does NOT set m_hiddenClass and m_butterfly, and does NOT fill nils to the inline storage!
    //
    static HeapPtr<TableObject> WARN_UNUSED AllocateObjectImpl(VM* vm, uint8_t inlineCapacity)
    {
        constexpr size_t x_baseSize = offsetof_member_v<&TableObject::m_inlineStorage>;
        size_t allocationSize = x_baseSize + inlineCapacity * sizeof(TValue);
        HeapPtr<TableObject> r = vm->AllocFromUserHeap(static_cast<uint32_t>(allocationSize)).AsNoAssert<TableObject>();
        UserHeapGcObjectHeader::Populate(r);
        TCSet(r->m_arrayType, ArrayType::GetInitialArrayType());
        return r;
    }

    static HeapPtr<TableObject> WARN_UNUSED CreateEmptyTableObject(VM* vm, Structure* emptyStructure, uint32_t initialButterflyArrayPartCapacity)
    {
        assert(emptyStructure->m_numSlots == 0);
        assert(emptyStructure->m_metatable == 0);
        assert(emptyStructure->m_arrayType.m_asValue == 0);
        assert(emptyStructure->m_butterflyNamedStorageCapacity == 0);

        uint8_t inlineCapacity = emptyStructure->m_inlineNamedStorageCapacity;
        HeapPtr<TableObject> r = AllocateObjectImpl(vm, inlineCapacity);
        TCSet(r->m_hiddenClass, SystemHeapPointer<void> { emptyStructure });
        // Initialize the butterfly storage
        //
        r->m_butterfly = nullptr;
        if (initialButterflyArrayPartCapacity > 0)
        {
            TranslateToRawPointer(vm, r)->GrowButterfly<false /*isGrowNamedStorage*/>(initialButterflyArrayPartCapacity);
        }
        // Initialize the inline storage
        //
        TValue nilVal = TValue::Nil();
        for (size_t i = 0; i < inlineCapacity; i++)
        {
            TCSet(r->m_inlineStorage[i], nilVal);
        }
        return r;
    }

    static HeapPtr<TableObject> WARN_UNUSED CreateEmptyGlobalObject(VM* vm)
    {
        uint8_t inlineCapacity = Structure::x_maxNumSlots;
        CacheableDictionary* hc = CacheableDictionary::CreateEmptyDictionary(vm, 128 /*anticipatedNumSlots*/, inlineCapacity, true /*shouldNeverTransitToUncacheableDictionary*/);
        HeapPtr<TableObject> r = AllocateObjectImpl(vm, inlineCapacity);
        TCSet(r->m_hiddenClass, SystemHeapPointer<void> { hc });
        r->m_butterfly = nullptr;
        TValue nilVal = TValue::Nil();
        for (size_t i = 0; i < inlineCapacity; i++)
        {
            TCSet(r->m_inlineStorage[i], nilVal);
        }
        return r;
    }

    Butterfly* WARN_UNUSED CloneButterfly(uint32_t butterflyNamedStorageCapacity)
    {
        if (m_butterfly == nullptr)
        {
            return nullptr;
        }
        else
        {
            uint32_t arrayStorageCapacity = static_cast<uint32_t>(m_butterfly->GetHeader()->m_arrayStorageCapacity);
#ifndef NDEBUG
            HeapEntityType hiddenClassTy = m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type;
            if (hiddenClassTy == HeapEntityType::Structure)
            {
                assert(butterflyNamedStorageCapacity == m_hiddenClass.As<Structure>()->m_butterflyNamedStorageCapacity);
            }
            else if (hiddenClassTy == HeapEntityType::CacheableDictionary)
            {
                assert(butterflyNamedStorageCapacity == m_hiddenClass.As<CacheableDictionary>()->m_butterflyNamedStorageCapacity);
            }
            else
            {
                assert(hiddenClassTy == HeapEntityType::UncacheableDictionary);
                // TODO: support UncacheableDictionary
                ReleaseAssert(false && "unimplemented");
            }
#endif
            uint32_t butterflySlots = arrayStorageCapacity + butterflyNamedStorageCapacity + 1;

            uint32_t butterflyStartOffset = butterflyNamedStorageCapacity + static_cast<uint32_t>(1 - ArrayGrowthPolicy::x_arrayBaseOrd);
            uint64_t* butterflyStart = reinterpret_cast<uint64_t*>(m_butterfly) - butterflyStartOffset;

            uint64_t* butterflyCopy = new uint64_t[butterflySlots];
            memcpy(butterflyCopy, butterflyStart, sizeof(uint64_t) * butterflySlots);
            Butterfly* butterflyPtr = reinterpret_cast<Butterfly*>(butterflyCopy + butterflyStartOffset);

            // Clone array sparse map if exists
            //
            ButterflyHeader* hdr = butterflyPtr->GetHeader();
            if (hdr->HasSparseMap())
            {
                VM* vm = VM::GetActiveVMForCurrentThread();
                ArraySparseMap* oldSparseMap = TranslateToRawPointer(vm, hdr->GetSparseMap());
                ArraySparseMap* newSparseMap = oldSparseMap->Clone(vm);
                hdr->m_arrayLengthIfContinuous = GeneralHeapPointer<ArraySparseMap>(newSparseMap).m_value;
                assert(hdr->HasSparseMap() && TranslateToRawPointer(hdr->GetSparseMap()) == newSparseMap);
            }

            return butterflyPtr;
        }
    }

    HeapPtr<TableObject> WARN_UNUSED ShallowCloneTableObject(VM* vm)
    {
        HeapEntityType ty = m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type;
        assert(ty == HeapEntityType::Structure || ty == HeapEntityType::CacheableDictionary || ty == HeapEntityType::UncacheableDictionary);

        SystemHeapPointer<void> newHiddenClass;
        uint8_t inlineCapacity;
        uint32_t butterflyNamedStorageCapacity;
        if (likely(ty == HeapEntityType::Structure))
        {
            Structure* structure = TranslateToRawPointer(m_hiddenClass.As<Structure>());
            inlineCapacity = structure->m_inlineNamedStorageCapacity;
            butterflyNamedStorageCapacity = structure->m_butterflyNamedStorageCapacity;
            newHiddenClass = m_hiddenClass;
        }
        else if (ty == HeapEntityType::CacheableDictionary)
        {
            CacheableDictionary* cd = TranslateToRawPointer(m_hiddenClass.As<CacheableDictionary>());
            CacheableDictionary* cloneCd = cd->Clone(vm);
            inlineCapacity = cd->m_inlineNamedStorageCapacity;
            butterflyNamedStorageCapacity = cd->m_butterflyNamedStorageCapacity;
            newHiddenClass = cloneCd;
        }
        else
        {
            // TODO: support UncacheableDictionary
            ReleaseAssert(false && "unimplemented");
        }

        TableObject* r = TranslateToRawPointer(vm, AllocateObjectImpl(vm, inlineCapacity));
        r->m_arrayType = m_arrayType;
        r->m_hiddenClass = newHiddenClass;
        memcpy(r->m_inlineStorage, m_inlineStorage, sizeof(TValue) * inlineCapacity);
        r->m_butterfly = CloneButterfly(butterflyNamedStorageCapacity);
        return TranslateToHeapPtr(r);
    }

    struct GetMetatableResult
    {
        // The resulted metatable
        //
        UserHeapPointer<void> m_result;
        // Whether one can IC this result using the hidden class
        //
        bool m_isCacheable;
    };

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static GetMetatableResult GetMetatable(T self)
    {
        SystemHeapPointer<void> hc = TCGet(self->m_hiddenClass);
        HeapEntityType ty = hc.As<SystemHeapGcObjectHeader>()->m_type;
        assert(ty == HeapEntityType::Structure || ty == HeapEntityType::CacheableDictionary || ty == HeapEntityType::UncacheableDictionary);

        if (likely(ty == HeapEntityType::Structure))
        {
            HeapPtr<Structure> structure = hc.As<Structure>();
            if (Structure::HasNoMetatable(structure))
            {
                // Any object with this structure is guaranteed to have no metatable
                //
                return GetMetatableResult {
                    .m_result = {},
                    .m_isCacheable = true
                };
            }
            else if (Structure::HasMonomorphicMetatable(structure))
            {
                // Any object with this structure is guaranteed to have the same metatable
                //
                return GetMetatableResult {
                    .m_result = Structure::GetMonomorphicMetatable(structure),
                    .m_isCacheable = true
                };
            }
            else
            {
                assert(Structure::IsPolyMetatable(structure));
                UserHeapPointer<void> res = GetPolyMetatableFromObjectWithStructureHiddenClass(TranslateToRawPointer(self), Structure::GetPolyMetatableSlot(structure), structure->m_inlineNamedStorageCapacity);
                return GetMetatableResult {
                    .m_result = res,
                    .m_isCacheable = false
                };
            }
        }
        else if (ty == HeapEntityType::CacheableDictionary)
        {
            HeapPtr<CacheableDictionary> cd = hc.As<CacheableDictionary>();
            return GetMetatableResult {
                .m_result = TCGet(cd->m_metatable),
                .m_isCacheable = true
            };
        }
        else
        {
            // TODO: support UncacheableDictionary
            ReleaseAssert(false && "unimplemented");
        }
    }

    void SetMetatable(VM* vm, UserHeapPointer<void> newMetatable)
    {
        assert(newMetatable.m_value != 0);
        SystemHeapPointer<void> hc = m_hiddenClass;
        HeapEntityType ty = hc.As<SystemHeapGcObjectHeader>()->m_type;
        assert(ty == HeapEntityType::Structure || ty == HeapEntityType::CacheableDictionary || ty == HeapEntityType::UncacheableDictionary);

        if (likely(ty == HeapEntityType::Structure))
        {
            Structure* structure = TranslateToRawPointer(vm, hc.As<Structure>());
            Structure::AddMetatableResult result;
            structure->SetMetatable(vm, newMetatable, result /*out*/);

            if (unlikely(result.m_shouldInsertMetatable))
            {
                if (unlikely(result.m_shouldGrowButterfly))
                {
                    GrowButterfly<true /*isGrowNamedStorage*/>(result.m_newStructure.As()->m_butterflyNamedStorageCapacity);
                }

                uint32_t inlineStorageCapacity = structure->m_inlineNamedStorageCapacity;
                if (result.m_slotOrdinal < inlineStorageCapacity)
                {
                    m_inlineStorage[result.m_slotOrdinal] = TValue::CreatePointer(newMetatable);

                }
                else
                {
                    assert(m_butterfly != nullptr);
                    *m_butterfly->GetNamedPropertyAddr(Butterfly::GetOutlineStorageIndex(result.m_slotOrdinal, inlineStorageCapacity)) = TValue::CreatePointer(newMetatable);
                }
            }
            m_hiddenClass = result.m_newStructure.As();
            m_arrayType = TCGet(result.m_newStructure.As()->m_arrayType);
            assert(m_arrayType.MayHaveMetatable());
        }
        else if (ty == HeapEntityType::CacheableDictionary)
        {
            CacheableDictionary* cd = TranslateToRawPointer(vm, hc.As<CacheableDictionary>());
            if (cd->m_metatable == newMetatable)
            {
                // The metatable is unchanged, no-op
                //
                return;
            }

            CacheableDictionary* newCd = cd->RelocateForAddingOrRemovingMetatable(vm);
            newCd->m_metatable = newMetatable.As();
            m_hiddenClass = newCd;
            m_arrayType.SetMayHaveMetatable(true);
        }
        else
        {
            // TODO: support UncacheableDictionary
            ReleaseAssert(false && "unimplemented");
        }
    }

    void RemoveMetatable(VM* vm)
    {
        SystemHeapPointer<void> hc = m_hiddenClass;
        HeapEntityType ty = hc.As<SystemHeapGcObjectHeader>()->m_type;
        assert(ty == HeapEntityType::Structure || ty == HeapEntityType::CacheableDictionary || ty == HeapEntityType::UncacheableDictionary);

        if (likely(ty == HeapEntityType::Structure))
        {
            Structure* structure = TranslateToRawPointer(vm, hc.As<Structure>());
            Structure::RemoveMetatableResult result;
            structure->RemoveMetatable(vm, result /*out*/);

            if (unlikely(result.m_shouldInsertMetatable))
            {
                uint32_t inlineStorageCapacity = structure->m_inlineNamedStorageCapacity;
                if (result.m_slotOrdinal < inlineStorageCapacity)
                {
                    m_inlineStorage[result.m_slotOrdinal] = TValue::Nil();

                }
                else
                {
                    assert(m_butterfly != nullptr);
                    *m_butterfly->GetNamedPropertyAddr(Butterfly::GetOutlineStorageIndex(result.m_slotOrdinal, inlineStorageCapacity)) = TValue::Nil();
                }
            }
            m_hiddenClass = result.m_newStructure.As();
            m_arrayType = TCGet(result.m_newStructure.As()->m_arrayType);
            AssertIff(m_arrayType.MayHaveMetatable(), Structure::IsPolyMetatable(result.m_newStructure.As()));
        }
        else if (ty == HeapEntityType::CacheableDictionary)
        {
            CacheableDictionary* cd = TranslateToRawPointer(vm, hc.As<CacheableDictionary>());
            if (cd->m_metatable.m_value == 0)
            {
                // The metatable is unchanged, no-op
                //
                return;
            }

            CacheableDictionary* newCd = cd->RelocateForAddingOrRemovingMetatable(vm);
            newCd->m_metatable.m_value = 0;
            m_hiddenClass = newCd;
            m_arrayType.SetMayHaveMetatable(false);
        }
        else
        {
            // TODO: support UncacheableDictionary
            ReleaseAssert(false && "unimplemented");
        }
    }

    // If returns true, it is guaranteed that field 'mtKind' doesn't exist if 'self'
    // If returns false, however, there is no guarantee on anything.
    //
    static bool WARN_UNUSED TryQuicklyRuleOutMetamethod(HeapPtr<TableObject> self, LuaMetamethodKind mtKind)
    {
        SystemHeapPointer<void> hc = TCGet(self->m_hiddenClass);
        if (likely(hc.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure))
        {
            return hc.As<Structure>()->m_knownNonexistentMetamethods & (static_cast<LuaMetamethodBitVectorT>(static_cast<LuaMetamethodBitVectorT>(1) << static_cast<size_t>(mtKind)));
        }
        else
        {
            return false;
        }
    }

    static uint32_t WARN_UNUSED GetTableLengthWithLuaSemanticsSlowPath(ArraySparseMap* sparseMap, uint32_t existentLb)
    {
        assert(existentLb > 0);
        uint32_t ub = existentLb;
        while (true)
        {
            if (ub >= std::numeric_limits<uint32_t>::max() / 2)
            {
                // Adversarially-constructed array, don't bother with it, just fall back to O(n) approach
                //
                uint32_t cur = existentLb;
                while (cur < std::numeric_limits<uint32_t>::max() && !sparseMap->GetByVal(cur + 1).IsNil())
                {
                    cur++;
                }
                // If cur == uint32_max we will just return it. However, this should never happen,
                // because 'existentLb' is always < ArrayGrowthPolicy::x_unconditionallySparseMapCutoff
                // which means the array has more than 4*10^9 elements.. This should have reached other system limits far before reaching here.
                //
                return cur;
            }
            ub = (ub + 1) / 2 * 3;
            if (sparseMap->GetByVal(ub).IsNil())
            {
                break;
            }
        }

        // Now we can binary search
        // The invariant is that at any moment, our range [l,r] satisfies slot 'l' is not nil and slot 'r' is nil
        //
        uint32_t lb = existentLb;
        assert(lb < ub && sparseMap->GetByVal(ub).IsNil());
        while (lb + 1 < ub)
        {
            uint32_t mid = static_cast<uint32_t>((static_cast<uint64_t>(lb) + ub) / 2);

            // There is a tricky thing here: existentLb doesn't necessarily exist in sparse map! (because it can be in vector part)
            // but fortunately due to how we write this binary search, mid should always be > existentLb. But for sanity, still assert this.
            //
            assert(existentLb < mid);
            if (sparseMap->GetByVal(mid).IsNil())
            {
                ub = mid;
            }
            else
            {
                lb = mid;
            }
        }
        assert(lb + 1 == ub);
        return lb;
    }

    // The 'length' operator under Lua semantics
    // The definition is any non-negative integer index n such that value for index 'n' is non-nil but value for index 'n+1' is nil,
    // or if index '1' is nil, the length is 0
    //
    static uint32_t WARN_UNUSED GetTableLengthWithLuaSemantics(HeapPtr<TableObject> self)
    {
        static_assert(ArrayGrowthPolicy::x_arrayBaseOrd == 1, "this function currently only works under lua semantics");
        ArrayType arrType = TCGet(self->m_arrayType);
        if (likely(arrType.IsContinuous()))
        {
            // Fast path: the array is continuous
            //
            int32_t val = self->m_butterfly->GetHeader()->m_arrayLengthIfContinuous;
            assert(val >= ArrayGrowthPolicy::x_arrayBaseOrd);
            return static_cast<uint32_t>(val - 1);
        }
        else if (arrType.ArrayKind() == ArrayType::Kind::NoButterflyArrayPart)
        {
            // Edge case: no butterfly array part
            //
            return 0;
        }
        else
        {
            Butterfly* butterfly = self->m_butterfly;
            TValue* tv = reinterpret_cast<TValue*>(butterfly);
            int32_t arrayStorageCap = butterfly->GetHeader()->m_arrayStorageCapacity;
            if (arrayStorageCap > 0)
            {
                // The array has a vector storage of at least length 1
                //
                // Case 1: if slot 1 is nil, we found a valid length of '0'
                //
                if (tv[1].IsNil())
                {
                    return 0;
                }
                // Case 2: now we know slot 1 isn't nil. If the last slot 'cap' is nil,
                // we can always use binary search to narrow down to a slot 'k' satisfying 'k' is not nil but 'k+1' is nil
                // (the invariant is that at any moment, our range [l,r] satisfies slot 'l' is not nil and slot 'r' is nil)
                //
                if (tv[arrayStorageCap].IsNil())
                {
                    uint32_t lb = 1;
                    uint32_t ub = static_cast<uint32_t>(arrayStorageCap);
                    while (lb + 1 < ub)
                    {
                        uint32_t mid = (lb + ub) / 2;
                        if (tv[mid].IsNil())
                        {
                            ub = mid;
                        }
                        else
                        {
                            lb = mid;
                        }
                    }
                    assert(lb + 1 == ub);
                    assert(!tv[lb].IsNil() && tv[lb + 1].IsNil());
                    return lb;
                }
                // Case 3: the last slot 'cap' is not nil, we are not guaranteed to find an empty slot in vector range.
                // Try to find in sparse map
                //
                if (unlikely(arrType.HasSparseMap()))
                {
                    ArraySparseMap* sparseMap = TranslateToRawPointer(butterfly->GetHeader()->GetSparseMap());
                    return GetTableLengthWithLuaSemanticsSlowPath(sparseMap, static_cast<uint32_t>(arrayStorageCap));
                }
                else
                {
                    // The sparse map doesn't exist, so 'arrayStorageCap + 1' is nil
                    //
                    return static_cast<uint32_t>(arrayStorageCap);
                }
            }
            else
            {
                // The array doesn't have vector part, now check the sparse map part
                //
                if (likely(!arrType.HasSparseMap()))
                {
                    // It doesn't have sparse map part either, so length is 0
                    //
                    return 0;
                }
                ArraySparseMap* sparseMap = TranslateToRawPointer(butterfly->GetHeader()->GetSparseMap());
                TValue val = sparseMap->GetByVal(1);
                if (val.IsNil())
                {
                    // slot '1' doesn't exist, so length is 0
                    //
                    return 0;
                }
                // Slot '1' exists, so we have an existent lower bound, we can call GetTableLengthWithLuaSemanticsSlowPath now
                //
                return GetTableLengthWithLuaSemanticsSlowPath(sparseMap, 1 /*existentLb*/);
            }
        }
    }

    // Object header
    //
    SystemHeapPointer<void> m_hiddenClass;
    HeapEntityType m_type;
    GcCellState m_cellState;

    ArrayType m_arrayType;
    uint8_t m_reserved;

    Butterfly* m_butterfly;
    TValue m_inlineStorage[0];
};
static_assert(sizeof(TableObject) == 16);

inline UserHeapPointer<void> GetMetatableForValue(TValue value)
{
    if (likely(value.IsPointer()))
    {
        HeapEntityType ty = value.AsPointer<UserHeapGcObjectHeader>().As()->m_type;

        if (likely(ty == HeapEntityType::Table))
        {
            HeapPtr<TableObject> tableObj = value.AsPointer<TableObject>().As();
            TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
            return result.m_result;
        }

        if (ty == HeapEntityType::String)
        {
            return VM::GetActiveVMForCurrentThread()->m_metatableForString;
        }

        if (ty == HeapEntityType::Function)
        {
            return VM::GetActiveVMForCurrentThread()->m_metatableForFunction;
        }

        if (ty == HeapEntityType::Thread)
        {
            return VM::GetActiveVMForCurrentThread()->m_metatableForCoroutine;
        }

        // TODO: support USERDATA type
        ReleaseAssert(false && "unimplemented");
    }

    if (value.IsMIV())
    {
        if (value.IsNil())
        {
            return VM::GetActiveVMForCurrentThread()->m_metatableForNil;
        }
        else
        {
            assert(value.AsMIV().IsBoolean());
            return VM::GetActiveVMForCurrentThread()->m_metatableForBoolean;
        }
    }

    assert(value.IsDouble() || value.IsInt32());
    return VM::GetActiveVMForCurrentThread()->m_metatableForNumber;
}

inline HeapPtr<FunctionObject> GetCallMetamethodFromMetatableImpl(UserHeapPointer<void> metatableMaybeNull)
{
    if (metatableMaybeNull.m_value == 0)
    {
        return nullptr;
    }
    assert(metatableMaybeNull.As<UserHeapGcObjectHeader>()->m_type == HeapEntityType::Table);
    HeapPtr<TableObject> metatable = metatableMaybeNull.As<TableObject>();
    GetByIdICInfo icInfo;
    TableObject::PrepareGetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Call), icInfo /*out*/);
    TValue target = TableObject::GetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Call).As<void>(), icInfo);
    if (likely(target.Is<tFunction>()))
    {
        return target.As<tFunction>();
    }
    else
    {
        return nullptr;
    }
}

// If a non-function value is called, Lua will execute the call by searching for the call metamethod of the value.
//
// This function returns the metamethod function to call, or nullptr if failure.
//
// In Lua 5.1-5.3, the '__call' metamethod is non-recursive: say mt_a.__call = b, and 'b' is not
// a function, the VM will throw an error, instead of recursively considering the metatable for 'b'
// We support Lua 5.1 behavior for now.
//
// In Lua 5.4 this behavior has changed and __call can be recursive, and this function will need to be extended correspondingly.
//
inline HeapPtr<FunctionObject> WARN_UNUSED NO_INLINE GetCallTargetViaMetatable(TValue value)
{
    assert(!value.Is<tFunction>());

    if (likely(value.Is<tHeapEntity>()))
    {
        HeapEntityType ty = value.GetHeapEntityType();
        assert(ty != HeapEntityType::Function);

        if (likely(ty == HeapEntityType::Table))
        {
            HeapPtr<TableObject> tableObj = value.As<tTable>();
            TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
            return GetCallMetamethodFromMetatableImpl(result.m_result);
        }

        if (ty == HeapEntityType::String)
        {
            return GetCallMetamethodFromMetatableImpl(VM::GetActiveVMForCurrentThread()->m_metatableForString);
        }

        if (ty == HeapEntityType::Thread)
        {
            return GetCallMetamethodFromMetatableImpl(VM::GetActiveVMForCurrentThread()->m_metatableForCoroutine);
        }

        // TODO: support USERDATA type
        ReleaseAssert(false && "unimplemented");
    }

    if (value.Is<tNil>())
    {
        return GetCallMetamethodFromMetatableImpl(VM::GetActiveVMForCurrentThread()->m_metatableForNil);
    }
    else if (value.Is<tMIV>())
    {
        assert(value.Is<tBool>());
        return GetCallMetamethodFromMetatableImpl(VM::GetActiveVMForCurrentThread()->m_metatableForBoolean);
    }

    assert(value.Is<tDouble>() || value.Is<tInt32>());
    return GetCallMetamethodFromMetatableImpl(VM::GetActiveVMForCurrentThread()->m_metatableForNumber);
}

// TableObjectIterator: the class used by Lua to iterate all key-value pairs in a table
//
// DEVNOTE: currently this iterator lives on the stack, so the GC is unaware of the layout of this iterator.
// Therefore, this iterator can not store the hidden class or any pointer, as these pointers cannot be recognized by GC
// so the pointed object can be GC'ed (or even worse, ABA'ed) in between two iterator calls. (And due to the possibility
// of ABA, even validating the pointer equals the pointer stored in the table won't work.) This unfortunately adds a bunch
// of branches. We can improve this by making it a buffered iterator living on the heap, see TODO.
//
// Lua explicitly states that if new keys are added, the behavior for iterator is undefined. So we don't need to worry
// about correctness when there's a change in hidden class, as long as we don't crash or cause data corruptions in such cases.
//
// The story could be more difficult once we support UncacheableDictionary, as Lua explicitly allows deletion of keys during a traversal.
// Here's the proposal to deal with this issue: transition from CacheableDictionary to UncacheableDictionary, or resizing (including
// shrinking) of UncacheableDictionary's hash table should be made to only happen upon key insertion, never at other times. Now, if
// the table transited to UncacheableDictionary or the UncacheableDictionary's hash table gets rehashed during a traversal, it means
// the user must have already violated the Lua standard by inserted a new key, so we are free to exhibit undefined behavior, so we are good.
//
// TODO: we probably should make this a buffered iterator living on the heap for better performance, but this will complicate the design a lot..
//
struct TableObjectIterator
{
    enum class IteratorState
    {
        Uninitialized,
        NamedProperty,
        VectorStorage,
        SparseMap,
        Terminated
    };

    struct KeyValuePair
    {
        TValue m_key;
        TValue m_value;
    };

    TableObjectIterator()
        : m_namedPropertyOrd(0), m_state(IteratorState::Uninitialized)
    { }

    KeyValuePair WARN_UNUSED Advance(HeapPtr<TableObject> obj)
    {
        HeapEntityType hcType;
        HeapPtr<Structure> structure;
        HeapPtr<CacheableDictionary> cacheableDict;
        ArraySparseMap* sparseMap;

        if (unlikely(m_state == IteratorState::Uninitialized))
        {
            hcType = TCGet(obj->m_hiddenClass).As<SystemHeapGcObjectHeader>()->m_type;
            assert(hcType == HeapEntityType::Structure || hcType == HeapEntityType::CacheableDictionary || hcType == HeapEntityType::UncacheableDictionary);
            if (hcType == HeapEntityType::Structure)
            {
                structure = TCGet(obj->m_hiddenClass).As<Structure>();
                if (unlikely(structure->m_numSlots == 0))
                {
                    goto try_start_iterating_vector_storage;
                }
                m_state = IteratorState::NamedProperty;
                m_namedPropertyOrd = static_cast<uint32_t>(-1);
                goto try_get_next_structure_prop;
            }
            else if (hcType == HeapEntityType::CacheableDictionary)
            {
                cacheableDict = TCGet(obj->m_hiddenClass).As<CacheableDictionary>();
                m_state = IteratorState::NamedProperty;
                m_namedPropertyOrd = 0;
                goto try_find_and_get_cd_prop;
            }
            else
            {
                // TODO: support UncacheableDictionary
                ReleaseAssert(false && "unimplemented");
            }
        }

        if (m_state == IteratorState::NamedProperty)
        {
            hcType = TCGet(obj->m_hiddenClass).As<SystemHeapGcObjectHeader>()->m_type;
            assert(hcType == HeapEntityType::Structure || hcType == HeapEntityType::CacheableDictionary || hcType == HeapEntityType::UncacheableDictionary);

            if (likely(hcType == HeapEntityType::Structure))
            {
                structure = TCGet(obj->m_hiddenClass).As<Structure>();

try_get_next_structure_prop:
                TValue value;
                while (true)
                {
                    m_namedPropertyOrd++;
                    if (unlikely(m_namedPropertyOrd >= structure->m_numSlots))
                    {
                        goto try_start_iterating_vector_storage;
                    }
                    if (unlikely(Structure::IsSlotUsedByPolyMetatable(structure, m_namedPropertyOrd)))
                    {
                        continue;
                    }
                    value = TableObject::GetValueForSlot(obj, m_namedPropertyOrd, structure->m_inlineNamedStorageCapacity);
                    if (value.IsNil())
                    {
                        continue;
                    }
                    break;
                }

                UserHeapPointer<void> key = Structure::GetKeyForSlotOrdinal(structure, static_cast<uint8_t>(m_namedPropertyOrd));
                if (unlikely(key == VM_GetSpecialKeyForBoolean(false).As<void>()))
                {
                    return KeyValuePair {
                        .m_key = TValue::CreateFalse(),
                        .m_value = value
                    };
                }
                if (unlikely(key == VM_GetSpecialKeyForBoolean(true).As<void>()))
                {
                    return KeyValuePair {
                        .m_key = TValue::CreateTrue(),
                        .m_value = value
                    };
                }
                return KeyValuePair {
                    .m_key = TValue::CreatePointer(key),
                    .m_value = value
                };
            }
            else if (hcType == HeapEntityType::CacheableDictionary)
            {
                cacheableDict = TCGet(obj->m_hiddenClass).As<CacheableDictionary>();
                m_namedPropertyOrd++;

try_find_and_get_cd_prop:
                CacheableDictionary::HashTableEntry* ht = cacheableDict->m_hashTable;
                uint32_t htMask = cacheableDict->m_hashTableMask;
                while (m_namedPropertyOrd <= htMask)
                {
                    CacheableDictionary::HashTableEntry& entry = ht[m_namedPropertyOrd];
                    if (entry.m_key.m_value != 0)
                    {
                        TValue value = TableObject::GetValueForSlot(obj, entry.m_slot, cacheableDict->m_inlineNamedStorageCapacity);
                        if (!value.IsNil())
                        {
                            return KeyValuePair {
                                .m_key = TValue::CreatePointer(UserHeapPointer<void>(entry.m_key.As())),
                                .m_value = value
                            };
                        }
                    }
                    m_namedPropertyOrd++;
                }
                goto try_start_iterating_vector_storage;
            }
            else
            {
                // TODO: support UncacheableDictionary
                ReleaseAssert(false && "unimplemented");
            }

try_start_iterating_vector_storage:
            if (unlikely(obj->m_butterfly == nullptr))
            {
                goto finished_iteration;
            }
            m_state = IteratorState::VectorStorage;
            m_vectorStorageOrd = 0;
            goto try_find_next_vector_entry;
        }

        if (m_state == IteratorState::VectorStorage)
        {
try_find_next_vector_entry:
            Butterfly* butterfly = obj->m_butterfly;
            int32_t vectorStorageCapacity = butterfly->GetHeader()->m_arrayStorageCapacity;
            // Note that Lua array is 1-based, so the valid range is [1, vectorStorageCapacity]
            //
            while (m_vectorStorageOrd < vectorStorageCapacity)
            {
                m_vectorStorageOrd++;
                TValue value = reinterpret_cast<TValue*>(butterfly)[m_vectorStorageOrd];
                if (!value.IsNil())
                {
                    return KeyValuePair {
                        // TODO: we may want to change this when we have true support for integer type
                        //
                        .m_key = TValue::CreateDouble(m_vectorStorageOrd),
                        .m_value = value
                    };
                }
            }

            if (butterfly->GetHeader()->HasSparseMap())
            {
                sparseMap = TranslateToRawPointer(butterfly->GetHeader()->GetSparseMap());
                m_state = IteratorState::SparseMap;
                m_sparseMapOrd = 0;
                goto try_find_next_sparse_map_entry;
            }

            goto finished_iteration;
        }

        assert(m_state == IteratorState::SparseMap);
        sparseMap = TranslateToRawPointer(obj->m_butterfly->GetHeader()->GetSparseMap());
        m_sparseMapOrd++;

try_find_next_sparse_map_entry:
        while (m_sparseMapOrd <= sparseMap->m_hashMask)
        {
            ArraySparseMap::HashTableEntry& entry = sparseMap->m_hashTable[m_sparseMapOrd];
            double key = entry.m_key;
            if (!IsNaN(key))
            {
                TValue value = entry.m_value;
                if (!value.IsNil())
                {
                    return KeyValuePair {
                        .m_key = TValue::CreateDouble(key),
                        .m_value = value
                    };
                }
            }
            m_sparseMapOrd++;
        }
        goto finished_iteration;

finished_iteration:
        m_state = IteratorState::Terminated;
        return KeyValuePair {
            .m_key = TValue::Nil(),
            .m_value = TValue::Nil()
        };
    }

    // Get the next KV pair from the current key
    // This is used to implement the Lua 'next', so it has really weird semantics.
    // Specifically, if the key is obtained from another 'next' call, this function should work properly (i.e. allows user
    // to eventually iterate through every key-value pair) as long as no new key has been inserted, even if the passed-in key
    // have been deleted! But in other cases where the key doesn't exist, this function can exhibit undefined behavior (we
    // will try to throw out an error, but this is not guaranteed).
    //
    static bool WARN_UNUSED GetNextFromKey(HeapPtr<TableObject> obj, TValue key, KeyValuePair& out /*out*/)
    {
        if (key.IsNil())
        {
            // Input key is nil, in this case we should return first KV pair in table
            //
            TableObjectIterator iter;
            out = iter.Advance(obj);
            return true;
        }

        if (key.IsMIV())
        {
            MiscImmediateValue miv = key.AsMIV();
            if (miv.IsNil())
            {
                return false;
            }
            assert(miv.IsBoolean());
            key = TValue::CreatePointer(VM_GetSpecialKeyForBoolean(miv.GetBooleanValue()));
        }

        if (key.IsPointer())
        {
            // Input key is a pointer, locate its slot ordinal and iterate from there
            //
            HeapEntityType hcType = TCGet(obj->m_hiddenClass).As<SystemHeapGcObjectHeader>()->m_type;
            assert(hcType == HeapEntityType::Structure || hcType == HeapEntityType::CacheableDictionary || hcType == HeapEntityType::UncacheableDictionary);
            UserHeapPointer<void> prop = key.AsPointer();
            if (hcType == HeapEntityType::Structure)
            {
                HeapPtr<Structure> structure = TCGet(obj->m_hiddenClass).As<Structure>();
                uint32_t slotOrd;
                bool found = Structure::GetSlotOrdinalFromMaybeNonStringProperty(structure, prop, slotOrd /*out*/);
                if (!found)
                {
                    return false;
                }
                assert(slotOrd < structure->m_numSlots);
                TableObjectIterator iter;
                iter.m_state = IteratorState::NamedProperty;
                iter.m_namedPropertyOrd = slotOrd;
                out = iter.Advance(obj);
                return true;
            }
            else if (hcType == HeapEntityType::CacheableDictionary)
            {
                HeapPtr<CacheableDictionary> cacheableDict = TCGet(obj->m_hiddenClass).As<CacheableDictionary>();
                uint32_t hashTableSlot = CacheableDictionary::GetHashTableSlotNumberForProperty(cacheableDict, prop);
                if (hashTableSlot == static_cast<uint32_t>(-1))
                {
                    return false;
                }
                TableObjectIterator iter;
                iter.m_state = IteratorState::NamedProperty;
                iter.m_namedPropertyOrd = hashTableSlot;
                out = iter.Advance(obj);
                return true;
            }
            else
            {
                // TODO: support UncacheableDictionary
                ReleaseAssert(false && "unimplemented");
            }
        }

        if (key.IsInt32())
        {
            // TODO: int32 type
            ReleaseAssert(false && "unimplemented");
        }

        assert(key.IsDouble());
        double idx = key.AsDouble();
        if (IsNaN(idx))
        {
            return false;
        }

        if (obj->m_butterfly == nullptr)
        {
            // It's impossible that a butterfly doesn't exist yet a previous 'next' call returned an array part index
            //
            return false;
        }

        {
            int64_t idx64;
            if (!TableObject::IsInt64Index(idx, idx64 /*out*/))
            {
                goto get_next_from_sparse_map;
            }

            if (idx64 < ArrayGrowthPolicy::x_arrayBaseOrd || !obj->m_butterfly->GetHeader()->IndexFitsInVectorCapacity(idx64))
            {
                goto get_next_from_sparse_map;
            }

            TableObjectIterator iter;
            iter.m_state = IteratorState::VectorStorage;
            iter.m_vectorStorageOrd = static_cast<int32_t>(idx64);
            out = iter.Advance(obj);
            return true;
        }

get_next_from_sparse_map:
        {
            if (!obj->m_butterfly->GetHeader()->HasSparseMap())
            {
                // Currently we never shrink butterfly array part, so if control reaches here, it must be the user passing
                // in some value that is not returned from 'next'. This is undefined behavior, but we choose to not throw
                // an error, but instead terminates the iteration, so this doesn't break if someday we added functionality
                // to shrink butterfly array part
                //
                out = KeyValuePair { TValue::Nil(), TValue::Nil() };
                return true;
            }

            ArraySparseMap* sparseMap = TranslateToRawPointer(obj->m_butterfly->GetHeader()->GetSparseMap());
            uint32_t slot = sparseMap->GetHashSlotOrdinal(idx);
            if (slot == static_cast<uint32_t>(-1))
            {
                return false;
            }

            TableObjectIterator iter;
            iter.m_state = IteratorState::SparseMap;
            iter.m_sparseMapOrd = slot;
            out = iter.Advance(obj);
            return true;
        }
    }

    union {
        uint32_t m_namedPropertyOrd;
        int32_t m_vectorStorageOrd;
        uint32_t m_sparseMapOrd;
    };
    IteratorState m_state;
};
// This struct must fit in 8 bytes as that's all we have on the stack to store it.
//
static_assert(sizeof(TableObjectIterator) == 8);

inline UserHeapPointer<void> WARN_UNUSED GetPolyMetatableFromObjectWithStructureHiddenClass(TableObject* obj, uint32_t slot, uint32_t inlineCapacity)
{
    assert(obj->m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure);
    assert(Structure::IsPolyMetatable(obj->m_hiddenClass.As<Structure>()));
    assert(Structure::GetPolyMetatableSlot(obj->m_hiddenClass.As<Structure>()) == slot);
    TValue res;
    if (slot < inlineCapacity)
    {
        res = obj->m_inlineStorage[slot];
    }
    else
    {
        int32_t index = Butterfly::GetOutlineStorageIndex(slot, inlineCapacity);
        res = obj->m_butterfly->GetNamedProperty(index);
    }
    if (res.IsNil())
    {
        return UserHeapPointer<void>();
    }
    else
    {
        assert(res.IsPointer());
        return res.AsPointer<void>();
    }
}
