#pragma once

#include "common_utils.h"
#include "memory_ptr.h"
#include "vm.h"
#include "structure.h"
#include "butterfly.h"

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
        ArraySparseMap* r = TranslateToRawPointer(vm, vm->AllocFromUserHeap(sizeof(ArraySparseMap)).AsNoAssert<ArraySparseMap>());
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
        ArraySparseMap* r = TranslateToRawPointer(vm, vm->AllocFromUserHeap(sizeof(ArraySparseMap)).AsNoAssert<ArraySparseMap>());
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
        // No fast-path exists.
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
    // The below fields are only useful if IndexCheckKind == NoArrayPart: this is the only case where fast path changes array type & hidden class
    //
    SystemHeapPointer<void> m_hiddenClass;
    ArrayType m_newArrayType;
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

    static void ALWAYS_INLINE PrepareGetByIntegerIndex(TableObject* self, GetByIntegerIndexICInfo& icInfo /*out*/)
    {
        ArrayType arrType = self->m_arrayType;

        icInfo.m_mayHaveMetatable = arrType.MayHaveMetatable();
        icInfo.m_icKind = ComputeGetByIntegerIndexIcKindFromArrayType(arrType);
        icInfo.m_isContinuous = arrType.IsContinuous();
    }

    // Returns isSuccess = false if 'idx' does not fit in the vector storage
    //
    static std::pair<TValue, bool /*isSuccess*/> WARN_UNUSED ALWAYS_INLINE TryAccessIndexInVectorStorage(TableObject* self, int64_t idx)
    {
#ifndef NDEBUG
        ArrayType arrType = self->m_arrayType;
#endif
        assert(arrType.ArrayKind() != ArrayType::Kind::NoButterflyArrayPart);
        if (likely(self->m_butterfly->GetHeader()->IndexFitsInVectorCapacity(idx)))
        {
            TValue res = *(self->m_butterfly->UnsafeGetInVectorIndexAddr(idx));
            // If the array is continuous, result must be non-nil
            //
            AssertImp(arrType.IsContinuous() && self->m_butterfly->GetHeader()->CanUseFastPathGetForContinuousArray(idx), !res.Is<tNil>());
            AssertImp(!res.Is<tNil>() && arrType.ArrayKind() == ArrayType::Kind::Int32, res.Is<tInt32>());
            AssertImp(!res.Is<tNil>() && arrType.ArrayKind() == ArrayType::Kind::Double, res.Is<tDouble>());
            return std::make_pair(res, true /*isSuccess*/);
        }

        return std::make_pair(TValue(), false /*isSuccess*/);
    }

    static bool WARN_UNUSED ALWAYS_INLINE CheckIndexFitsInContinuousArray(TableObject* self, int64_t idx)
    {
#ifndef NDEBUG
        ArrayType arrType = self->m_arrayType;
#endif
        assert(arrType.IsContinuous());
        if (likely(self->m_butterfly->GetHeader()->CanUseFastPathGetForContinuousArray(idx)))
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

    static TValue GetByIntegerIndex(TableObject* self, int64_t idx, GetByIntegerIndexICInfo icInfo)
    {
#ifndef NDEBUG
        ArrayType arrType = self->m_arrayType;
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

    static TValue GetByInt32Val(TableObject* self, int32_t idx, GetByIntegerIndexICInfo icInfo)
    {
        return GetByIntegerIndex(self, idx, icInfo);
    }

    static TValue GetByDoubleVal(TableObject* self, double idx, GetByIntegerIndexICInfo icInfo)
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

    static TValue __attribute__((__preserve_most__)) NO_INLINE QueryArraySparseMap(TableObject* self, double idx)
    {
#ifndef NDEBUG
        ArrayType arrType = self->m_arrayType;
        assert(arrType.HasSparseMap());
#endif
        ArraySparseMap* sparseMap = self->m_butterfly->GetHeader()->GetSparseMap();
        TValue res = sparseMap->GetByVal(idx);
#ifndef NDEBUG
        AssertImp(!res.IsNil() && arrType.ArrayKind() == ArrayType::Kind::Int32, res.IsInt32());
        AssertImp(!res.IsNil() && arrType.ArrayKind() == ArrayType::Kind::Double, res.IsDouble());
#endif
        return res;
    }

    template<typename U>
    static void PrepareGetByIdImplForStructure(SystemHeapPointer<void> hiddenClass, UserHeapPointer<U> propertyName, GetByIdICInfo& icInfo /*out*/)
    {
        assert(hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure);

        Structure* structure = hiddenClass.As<Structure>();
        icInfo.m_mayHaveMetatable = (structure->m_metatable != 0);
        uint32_t inlineStorageCapacity = structure->m_inlineNamedStorageCapacity;

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
            icInfo.m_icKind = GetByIdICInfo::ICKind::MustBeNil;
        }
    }

    template<typename U>
    static void PrepareGetByIdImplForCacheableDictionary(SystemHeapPointer<void> hiddenClass, UserHeapPointer<U> propertyName, GetByIdICInfo& icInfo /*out*/)
    {
        assert(hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::CacheableDictionary);

        CacheableDictionary* dict = hiddenClass.As<CacheableDictionary>();
        icInfo.m_mayHaveMetatable = (dict->m_metatable.m_value != 0);
        uint32_t inlineStorageCapacity = dict->m_inlineNamedStorageCapacity;

        uint32_t slotOrd;
        bool found;
        if constexpr(std::is_same_v<U, HeapString>)
        {
            found = CacheableDictionary::GetSlotOrdinalFromStringProperty(dict, propertyName, slotOrd /*out*/);
        }
        else
        {
            found = CacheableDictionary::GetSlotOrdinalFromMaybeNonStringProperty(dict, propertyName, slotOrd /*out*/);
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
            // CacheableDictionary property miss is not cacheable
            //
            icInfo.m_icKind = GetByIdICInfo::ICKind::MustBeNilButUncacheable;
        }
    }

    template<typename U>
    static void PrepareGetByIdImpl(SystemHeapPointer<void> hiddenClass, UserHeapPointer<U> propertyName, GetByIdICInfo& icInfo /*out*/)
    {
        static_assert(std::is_same_v<U, void> || std::is_same_v<U, HeapString>);

        HeapEntityType ty = hiddenClass.As<SystemHeapGcObjectHeader>()->m_type;
        assert(ty == HeapEntityType::Structure || ty == HeapEntityType::CacheableDictionary || ty == HeapEntityType::UncacheableDictionary);

        if (likely(ty == HeapEntityType::Structure))
        {
            PrepareGetByIdImplForStructure(hiddenClass, propertyName, icInfo /*out*/);
        }
        else if (likely(ty == HeapEntityType::CacheableDictionary))
        {
            PrepareGetByIdImplForCacheableDictionary(hiddenClass, propertyName, icInfo /*out*/);
        }
        else
        {
            // TODO: support UncacheableDictionary
            //
            assert(false && "unimplemented");
            __builtin_unreachable();
        }
    }

    template<typename U>
    static void PrepareGetById(TableObject* self, UserHeapPointer<U> propertyName, GetByIdICInfo& icInfo /*out*/)
    {
        return PrepareGetByIdImpl(self->m_hiddenClass, propertyName, icInfo /*out*/);
    }


    // Specialized GetById for global object
    // Global object is guaranteed to be a CacheableDictionary so we can remove a branch
    //
    template<typename U>
    static void PrepareGetByIdForGlobalObject(TableObject* self, UserHeapPointer<U> propertyName, GetByIdICInfo& icInfo /*out*/)
    {
        return PrepareGetByIdImplForCacheableDictionary(self->m_hiddenClass, propertyName, icInfo /*out*/);
    }

    static TValue WARN_UNUSED ALWAYS_INLINE GetById(TableObject* self, UserHeapPointer<void> /*propertyName*/, GetByIdICInfo icInfo)
    {
        if (icInfo.m_icKind == GetByIdICInfo::ICKind::MustBeNil || icInfo.m_icKind == GetByIdICInfo::ICKind::MustBeNilButUncacheable)
        {
            return TValue::Nil();
        }

        if (icInfo.m_icKind == GetByIdICInfo::ICKind::InlinedStorage)
        {
            return self->m_inlineStorage[icInfo.m_slot];
        }

        if (icInfo.m_icKind == GetByIdICInfo::ICKind::OutlinedStorage)
        {
            return self->m_butterfly->GetNamedProperty(icInfo.m_slot);
        }

        // TODO: support UncacheableDictionary
        assert(false && "not implemented");
        __builtin_unreachable();
    }

    template<typename U>
    static void PreparePutByIdForCacheableDictionary(TableObject* self, CacheableDictionary* dict, UserHeapPointer<U> propertyName, PutByIdICInfo& icInfo /*out*/)
    {
        assert(self->m_hiddenClass.template As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::CacheableDictionary);
        assert(self->m_hiddenClass.template As<CacheableDictionary>() == dict);
        CacheableDictionary::PutByIdResult res;
        if constexpr(std::is_same_v<U, HeapString>)
        {
            CacheableDictionary::PreparePutById(dict, propertyName, res /*out*/);
        }
        else
        {
            CacheableDictionary::PreparePutByMaybeNonStringKey(dict, propertyName, res /*out*/);
        }

        // Since the dictionary is 1-on-1 with the object, this step *is* idempotent.
        //
        if (unlikely(res.m_shouldGrowButterfly))
        {
            TableObject* rawSelf = TranslateToRawPointer(self);
            assert(dict->m_butterflyNamedStorageCapacity < res.m_newButterflyCapacity);
            rawSelf->GrowButterflyKnowingNamedStorageCapacity<true /*isGrowNamedStorage*/>(dict->m_butterflyNamedStorageCapacity, res.m_newButterflyCapacity);
            dict->m_butterflyNamedStorageCapacity = res.m_newButterflyCapacity;
        }

        if (unlikely(res.m_shouldCheckForTransitionToUncacheableDictionary))
        {
           // TODO: check for transition to UncacheableDictionary
           //
        }

        // For Dictionary, since it is 1-on-1 with the object, we always insert the property if it doesn't exist (and this step is idempotent)
        // Since we always pre-fill every unused slot with 'nil', the new property always has value 'nil' if it were just inserted, as desired
        // (this works because unlike Javascript, Lua doesn't have the concept of 'undefined')
        //
        // Since we have inserted the property above, for the IC, the property should always appear to be existent
        //
        icInfo.m_isInlineCacheable = true;
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

    template<typename U>
    static void PreparePutByIdForStructure(Structure* structure, UserHeapPointer<U> propertyName, PutByIdICInfo& icInfo /*out*/)
    {
        icInfo.m_isInlineCacheable = true;
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

    template<typename U>
    static void PreparePutById(TableObject* self, UserHeapPointer<U> propertyName, PutByIdICInfo& icInfo /*out*/)
    {
        static_assert(std::is_same_v<U, void> || std::is_same_v<U, HeapString>);

        SystemHeapPointer<void> hiddenClass = TCGet(self->m_hiddenClass);
        HeapEntityType ty = hiddenClass.As<SystemHeapGcObjectHeader>()->m_type;
        assert(ty == HeapEntityType::Structure || ty == HeapEntityType::CacheableDictionary || ty == HeapEntityType::UncacheableDictionary);

        if (likely(ty == HeapEntityType::Structure))
        {
            Structure* structure = TranslateToRawPointer(hiddenClass.As<Structure>());
            PreparePutByIdForStructure(structure, propertyName, icInfo /*out*/);
        }
        else if (ty == HeapEntityType::CacheableDictionary)
        {
            CacheableDictionary* dict = TranslateToRawPointer(hiddenClass.As<CacheableDictionary>());
            PreparePutByIdForCacheableDictionary(self, dict, propertyName, icInfo /*out*/);
        }
        else
        {
            // icInfo.m_isInlineCacheable = false;
            // TODO: support UncacheableDictionary
            assert(false && "unimplemented");
            __builtin_unreachable();
        }
    }

    // Specialized PutById for global object
    // Global object is guaranteed to be a CacheableDictionary so we can remove a branch
    //
    template<typename U>
    static void PreparePutByIdForGlobalObject(TableObject* self, UserHeapPointer<U> propertyName, PutByIdICInfo& icInfo /*out*/)
    {
        static_assert(std::is_same_v<U, void> || std::is_same_v<U, HeapString>);

        SystemHeapPointer<void> hiddenClass = TCGet(self->m_hiddenClass);
        assert(hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::CacheableDictionary);

        CacheableDictionary* dict = TranslateToRawPointer(hiddenClass.As<CacheableDictionary>());
        PreparePutByIdForCacheableDictionary(self, dict, propertyName, icInfo /*out*/);
    }

    static bool WARN_UNUSED PutByIdNeedToCheckMetatable(TableObject* self, PutByIdICInfo icInfo)
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

    static TValue WARN_UNUSED GetValueForSlot(TableObject* self, uint32_t slotOrd, uint8_t inlineStorageCapacity)
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

    template<bool isGrowNamedStorage>
    void GrowButterflyFromNull(uint32_t newCapacity)
    {
        assert(m_butterfly == nullptr);
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
            butterfly->GetHeader()->m_arrayLengthIfContinuous = 0;

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
            butterfly->GetHeader()->m_arrayStorageCapacity = newCapacity;
            butterfly->GetHeader()->m_arrayLengthIfContinuous = 0;

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

    // If isGrowNamedStorage == true, grow named storage capacity to 'newCapacity', and keep current array storage
    // Otherwise, grow array storage capacity to 'newCapcity', and keep current named storage
    //
    // This function must be called before changing the structure
    //
    template<bool isGrowNamedStorage>
    void GrowButterflyKnowingNamedStorageCapacity(uint32_t oldNamedStorageCapacity, uint32_t newCapacity)
    {
        if (m_butterfly == nullptr)
        {
            GrowButterflyFromNull<isGrowNamedStorage>(newCapacity);
        }
        else
        {
            uint32_t oldArrayStorageCapacity = m_butterfly->GetHeader()->m_arrayStorageCapacity;
#ifndef NDEBUG
            HeapEntityType hiddenClassTy = m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type;
            if (hiddenClassTy == HeapEntityType::Structure)
            {
                assert(oldNamedStorageCapacity == m_hiddenClass.As<Structure>()->m_butterflyNamedStorageCapacity);
            }
            else if (hiddenClassTy == HeapEntityType::CacheableDictionary)
            {
                assert(oldNamedStorageCapacity == m_hiddenClass.As<CacheableDictionary>()->m_butterflyNamedStorageCapacity);
            }
            else
            {
                assert(hiddenClassTy == HeapEntityType::UncacheableDictionary);
                // TODO: support UncacheableDictionary
                assert(false && "unimplemented");
            }
#endif
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
                butterfly->GetHeader()->m_arrayStorageCapacity = newCapacity;
            }
            m_butterfly = butterfly;
        }
    }

    template<bool isGrowNamedStorage>
    void GrowButterfly(uint32_t newCapacity)
    {
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
            assert(false && "unimplemented");
            __builtin_unreachable();
        }
        GrowButterflyKnowingNamedStorageCapacity<isGrowNamedStorage>(oldNamedStorageCapacity, newCapacity);
    }

    template<bool isGrowNamedStorage>
    static void GrowButterflyKnowingNamedStorageCapacity(TableObject* tableObj, uint32_t oldNamedStorageCapacity, uint32_t newCapacity)
    {
        tableObj->GrowButterflyKnowingNamedStorageCapacity<isGrowNamedStorage>(oldNamedStorageCapacity, newCapacity);
    }

    static void __attribute__((__preserve_most__)) NO_INLINE GrowButterflyNamedStorage_RT(TableObject* tableObj, uint32_t oldNamedStorageCapacity, uint32_t newCapacity)
    {
        [[clang::always_inline]] tableObj->GrowButterflyKnowingNamedStorageCapacity<true /*isGrowNamedStorage*/>(oldNamedStorageCapacity, newCapacity);
    }

    template<bool isGrowNamedStorage>
    static void GrowButterfly(TableObject* tableObj, uint32_t newCapacity)
    {
        tableObj->GrowButterfly<isGrowNamedStorage>(newCapacity);
    }

    void PutByIdTransitionToDictionaryImpl(VM* vm, UserHeapPointer<void> prop, TValue newValue)
    {
        CacheableDictionary::CreateFromStructureResult res;
        assert(m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure);
        Structure* structure = TranslateToRawPointer(vm, m_hiddenClass.As<Structure>());
        CacheableDictionary::CreateFromStructure(vm, this, structure, prop, res /*out*/);
        CacheableDictionary* dictionary = res.m_dictionary;
        if (res.m_shouldGrowButterfly)
        {
            GrowButterflyKnowingNamedStorageCapacity<true /*isGrowNamedStorage*/>(structure->m_butterflyNamedStorageCapacity, dictionary->m_butterflyNamedStorageCapacity);
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

    static void PutByIdTransitionToDictionary(TableObject* self, UserHeapPointer<void> propertyName, TValue newValue)
    {
        VM* vm = VM::GetActiveVMForCurrentThread();
        TableObject* rawSelf = TranslateToRawPointer(vm, self);
        assert(rawSelf->m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure);
        rawSelf->PutByIdTransitionToDictionaryImpl(vm, propertyName, newValue);
    }

    static void ALWAYS_INLINE PutById(TableObject* self, UserHeapPointer<void> propertyName, TValue newValue, PutByIdICInfo icInfo)
    {
        if (icInfo.m_icKind == PutByIdICInfo::ICKind::TransitionedToDictionaryMode)
        {
            PutByIdTransitionToDictionary(self, propertyName, newValue);
            return;
        }

        if (!icInfo.m_propertyExists)
        {
            if (icInfo.m_shouldGrowButterfly)
            {
                TableObject::GrowButterfly<true /*isGrowNamedStorage*/>(self, icInfo.m_newStructure.As()->m_butterflyNamedStorageCapacity);
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

    static void PreparePutByIntegerIndex(TableObject* self, int64_t index, TValue value, PutByIntegerIndexICInfo& icInfo /*out*/)
    {
        ArrayType arrType = self->m_arrayType;
        icInfo.m_mayHaveMetatable = arrType.MayHaveMetatable();
        AssertImp(self->m_hiddenClass.template As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure,
                  arrType.m_asValue == self->m_hiddenClass.template As<Structure>()->m_arrayType.m_asValue);

        auto setForceSlowPath = [&icInfo]() ALWAYS_INLINE
        {
            icInfo.m_indexCheckKind = PutByIntegerIndexICInfo::IndexCheckKind::ForceSlowPath;
            icInfo.m_valueCheckKind = PutByIntegerIndexICInfo::ValueCheckKind::NoCheck;
        };

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

            auto setNewHiddenClassAndArrayType = [&](ArrayType::Kind newKind) ALWAYS_INLINE
            {
                ArrayType newArrType = arrType;
                newArrType.SetArrayKind(newKind);
                newArrType.SetIsContinuous(true);

                VM* vm = VM::GetActiveVMForCurrentThread();
                icInfo.m_hiddenClass = self->m_hiddenClass;
                HeapEntityType ty = icInfo.m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type;
                if (ty == HeapEntityType::Structure)
                {
                    Structure* structure = TranslateToRawPointer(icInfo.m_hiddenClass.As<Structure>());
                    Structure* newStructure = structure->UpdateArrayType(vm, newArrType);
                    icInfo.m_newHiddenClass = newStructure;
                }
                else if (ty == HeapEntityType::CacheableDictionary)
                {
                    icInfo.m_newHiddenClass = icInfo.m_hiddenClass;
                }
                else
                {
                    assert(false && "unimplemented");
                }
                icInfo.m_newArrayType = newArrType;
            };

            icInfo.m_indexCheckKind = PutByIntegerIndexICInfo::IndexCheckKind::NoArrayPart;
            if (value.IsInt32())
            {
                icInfo.m_valueCheckKind = PutByIntegerIndexICInfo::ValueCheckKind::Int32;
                setNewHiddenClassAndArrayType(ArrayType::Kind::Int32);
            }
            else if (value.IsDouble())
            {
                icInfo.m_valueCheckKind = PutByIntegerIndexICInfo::ValueCheckKind::Double;
                setNewHiddenClassAndArrayType(ArrayType::Kind::Double);
            }
            else if (!value.IsNil())
            {
                icInfo.m_valueCheckKind = PutByIntegerIndexICInfo::ValueCheckKind::NotNil;
                setNewHiddenClassAndArrayType(ArrayType::Kind::Any);
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

    static bool WARN_UNUSED ALWAYS_INLINE CheckValueMeetsPreconditionForPutByIntegerIndexFastPath(TValue valueToPut, PutByIntegerIndexICInfo::ValueCheckKind valueCheckKind)
    {
        switch (valueCheckKind)
        {
        case PutByIntegerIndexICInfo::ValueCheckKind::Int32:
        {
            if (!valueToPut.Is<tInt32>())
            {
                return false;
            }
            break;
        }
        case PutByIntegerIndexICInfo::ValueCheckKind::Int32OrNil:
        {
            if (!valueToPut.Is<tInt32>() && !valueToPut.Is<tNil>())
            {
                return false;
            }
            break;
        }
        case PutByIntegerIndexICInfo::ValueCheckKind::Double:
        {
            if (!valueToPut.Is<tDouble>())
            {
                return false;
            }
            break;
        }
        case PutByIntegerIndexICInfo::ValueCheckKind::DoubleOrNil:
        {
            if (!valueToPut.Is<tDouble>() && !valueToPut.Is<tNil>())
            {
                return false;
            }
            break;
        }
        case PutByIntegerIndexICInfo::ValueCheckKind::NotNil:
        {
            if (valueToPut.Is<tNil>())
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
        return true;
    }

    static bool WARN_UNUSED ALWAYS_INLINE TryPutByIntegerIndexFastPath_ContinuousArray(TableObject* self, int64_t index, TValue value)
    {
        // For the continuous case, a nil value should always fail the value check above
        //
        assert(!value.IsNil());
        Butterfly* butterfly = self->m_butterfly;
        if (likely(butterfly->GetHeader()->CanUseFastPathGetForContinuousArray(index)))
        {
            // The put is into a continuous array, we can just do it
            //
            *butterfly->UnsafeGetInVectorIndexAddr(index) = value;
            return true;
        }
        else if (likely(index == butterfly->GetHeader()->m_arrayLengthIfContinuous + ArrayGrowthPolicy::x_arrayBaseOrd))
        {
            // The put will extend the array length by 1. We can do it as long as we have enough capacity
            //
            assert(index <= static_cast<int64_t>(butterfly->GetHeader()->m_arrayStorageCapacity) + ArrayGrowthPolicy::x_arrayBaseOrd);
            if (unlikely(index == static_cast<int64_t>(butterfly->GetHeader()->m_arrayStorageCapacity) + ArrayGrowthPolicy::x_arrayBaseOrd))
            {
                return false;
            }
            *butterfly->UnsafeGetInVectorIndexAddr(index) = value;
            butterfly->GetHeader()->m_arrayLengthIfContinuous = static_cast<int32_t>(index + 1 - ArrayGrowthPolicy::x_arrayBaseOrd);
            return true;
        }
        else
        {
            return false;
        }
    }

    static bool WARN_UNUSED ALWAYS_INLINE TryPutByIntegerIndexFastPath_InBoundPut(TableObject* self, int64_t index, TValue value)
    {
        Butterfly* butterfly = self->m_butterfly;
        if (likely(butterfly->GetHeader()->IndexFitsInVectorCapacity(index)))
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

    // Return false if need to fallback to slow path
    //
    static bool WARN_UNUSED TryPutByIntegerIndexFast(TableObject* self, int64_t index, TValue value, PutByIntegerIndexICInfo icInfo)
    {
        if (!CheckValueMeetsPreconditionForPutByIntegerIndexFastPath(value, icInfo.m_valueCheckKind))
        {
            return false;
        }

        switch (icInfo.m_indexCheckKind)
        {
        case PutByIntegerIndexICInfo::IndexCheckKind::Continuous:
        {
            return TryPutByIntegerIndexFastPath_ContinuousArray(self, index, value);
        }
        case PutByIntegerIndexICInfo::IndexCheckKind::InBound:
        {
            return TryPutByIntegerIndexFastPath_InBoundPut(self, index, value);
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
                butterfly->GetHeader()->m_arrayLengthIfContinuous = 1;
                assert(self->m_hiddenClass.m_value == icInfo.m_hiddenClass.m_value);
                self->m_arrayType = icInfo.m_newArrayType;
                self->m_hiddenClass = icInfo.m_newHiddenClass;
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
                int64_t currentCapacity = static_cast<int64_t>(m_butterfly->GetHeader()->m_arrayStorageCapacity);
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
                assert(m_butterfly->GetHeader()->m_arrayLengthIfContinuous == 0);
                m_butterfly->GetHeader()->m_arrayLengthIfContinuous = 1;

                // Even if the array is continuous afterwards, we could still be hitting this path because the hidden class is different
                //
                DEBUG_ONLY(didSomethingNontrivial = true;)
            }
            else
            {
                newArrayType.SetIsContinuous(false);
                m_butterfly->GetHeader()->m_arrayLengthIfContinuous = -1;

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
            int64_t currentCapacity = static_cast<int64_t>(butterfly->GetHeader()->m_arrayStorageCapacity);

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
                    if (index != butterfly->GetHeader()->m_arrayLengthIfContinuous + ArrayGrowthPolicy::x_arrayBaseOrd)
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
                    assert(m_butterfly->GetHeader()->m_arrayLengthIfContinuous >= 0);
                    m_butterfly->GetHeader()->m_arrayLengthIfContinuous++;
                }
                else
                {
                    if (arrType.IsContinuous())
                    {
                        // We just turned from continuous to discontinuous
                        //
                        assert(m_butterfly->GetHeader()->m_arrayLengthIfContinuous >= 0);
                        m_butterfly->GetHeader()->m_arrayLengthIfContinuous = -1;
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
                    // We know that currently an element is non-nil iff its index is in [ArrayGrowthPolicy::x_arrayBaseOrd, continuousLength)
                    //
                    int32_t continuousLength = butterfly->GetHeader()->m_arrayLengthIfContinuous + ArrayGrowthPolicy::x_arrayBaseOrd;
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
                            butterfly->GetHeader()->m_arrayLengthIfContinuous = continuousLength - 1 - ArrayGrowthPolicy::x_arrayBaseOrd;
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
                            butterfly->GetHeader()->m_arrayLengthIfContinuous = continuousLength + 1 - ArrayGrowthPolicy::x_arrayBaseOrd;
                        }
                    }

                    if (!isContinuous)
                    {
                        // We just turned from continuous to discontinuous
                        //
                        assert(m_butterfly->GetHeader()->m_arrayLengthIfContinuous >= 0);
                        m_butterfly->GetHeader()->m_arrayLengthIfContinuous = -1;
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
                Structure* structure = TranslateToRawPointer(m_hiddenClass.As<Structure>());
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
            GrowButterflyFromNull<false /*isGrowNamedStorage*/>(0 /*newCapacity*/);
            return AllocateNewArraySparseMap(vm);
        }
        else
        {
            if (likely(m_butterfly->GetHeader()->HasSparseMap()))
            {
                return m_butterfly->GetHeader()->GetSparseMap();
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
                m_hiddenClass = TranslateToRawPointer(m_hiddenClass.As<Structure>())->UpdateArrayType(vm, newArrayType);
                m_arrayType = newArrayType;
            }
            else
            {
                m_arrayType = newArrayType;
            }
        }
    }

    static bool WARN_UNUSED TryPutByValIntegerIndexFastNoIC(TableObject* self, int64_t index, TValue value)
    {
        ArrayType arrType = TCGet(self->m_arrayType);
        AssertImp(TCGet(self->m_hiddenClass).template As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure,
                  arrType.m_asValue == TCGet(self->m_hiddenClass).template As<Structure>()->m_arrayType.m_asValue);

        if (arrType.IsContinuous())
        {
            switch (arrType.ArrayKind())
            {
            case ArrayType::Kind::Int32:
            {
                if (!CheckValueMeetsPreconditionForPutByIntegerIndexFastPath(value, PutByIntegerIndexICInfo::ValueCheckKind::Int32))
                {
                    return false;
                }
                break;
            }
            case ArrayType::Kind::Double:
            {
                if (!CheckValueMeetsPreconditionForPutByIntegerIndexFastPath(value, PutByIntegerIndexICInfo::ValueCheckKind::Double))
                {
                    return false;
                }
                break;
            }
            case ArrayType::Kind::Any:
            {
                if (!CheckValueMeetsPreconditionForPutByIntegerIndexFastPath(value, PutByIntegerIndexICInfo::ValueCheckKind::NotNil))
                {
                    return false;
                }
                break;
            }
            case ArrayType::Kind::NoButterflyArrayPart:
            {
                assert(false);
                __builtin_unreachable();
            }
            }

            return TryPutByIntegerIndexFastPath_ContinuousArray(self, index, value);
        }

        if (unlikely(arrType.ArrayKind() == ArrayType::Kind::NoButterflyArrayPart))
        {
            return false;
        }
        else
        {
            // In bound put
            //
            switch (arrType.ArrayKind())
            {
            case ArrayType::Kind::Int32:
            {
                if (!CheckValueMeetsPreconditionForPutByIntegerIndexFastPath(value, PutByIntegerIndexICInfo::ValueCheckKind::Int32OrNil))
                {
                    return false;
                }
                break;
            }
            case ArrayType::Kind::Double:
            {
                if (!CheckValueMeetsPreconditionForPutByIntegerIndexFastPath(value, PutByIntegerIndexICInfo::ValueCheckKind::DoubleOrNil))
                {
                    return false;
                }
                break;
            }
            case ArrayType::Kind::Any:
            {
                // For non-continuous array, writing 'nil' is fine as long as it's in bound, no check needed
                //
                break;
            }
            case ArrayType::Kind::NoButterflyArrayPart:
            {
                assert(false);
                __builtin_unreachable();
            }
            }

            return TryPutByIntegerIndexFastPath_InBoundPut(self, index, value);
        }
    }

    static void RawPutByValIntegerIndex(TableObject* self, int64_t index, TValue value)
    {
        if (unlikely(!TableObject::TryPutByValIntegerIndexFastNoIC(self, index, value)))
        {
            VM* vm = VM::GetActiveVMForCurrentThread();
            TableObject* obj = TranslateToRawPointer(vm, self);
            obj->PutByIntegerIndexSlow(vm, index, value);
        }
    }

    static void RawPutByValDoubleIndex(TableObject* self, double index, TValue value)
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
            self->PutIndexIntoSparseMap(vm, false /*isVectorQualifyingIndex*/, index, value);
        }
    }

    static uint32_t ComputeObjectAllocationSize(uint8_t inlineCapacity)
    {
        constexpr size_t x_baseSize = offsetof_member_v<&TableObject::m_inlineStorage>;
        size_t allocationSize = x_baseSize + inlineCapacity * sizeof(TValue);
        return static_cast<uint32_t>(allocationSize);
    }

    // Does NOT set m_hiddenClass and m_butterfly, and does NOT fill nils to the inline storage!
    //
    static TableObject* WARN_UNUSED AllocateObjectImpl(VM* vm, uint8_t inlineCapacity)
    {
        uint32_t allocationSize = ComputeObjectAllocationSize(inlineCapacity);
        TableObject* r = TranslateToRawPointer(vm->AllocFromUserHeap(allocationSize).AsNoAssert<TableObject>());
        UserHeapGcObjectHeader::Populate(r);
        r->m_arrayType = ArrayType::GetInitialArrayType();
        return r;
    }

    static TableObject* WARN_UNUSED CreateEmptyTableObjectImpl(VM* vm, Structure* emptyStructure, uint8_t inlineCapacity, uint32_t initialButterflyArrayPartCapacity)
    {
        assert(emptyStructure->m_numSlots == 0);
        assert(emptyStructure->m_metatable == 0);
        assert(emptyStructure->m_arrayType.m_asValue == 0);
        assert(emptyStructure->m_butterflyNamedStorageCapacity == 0);

        assert(inlineCapacity == emptyStructure->m_inlineNamedStorageCapacity);
        TableObject* r = AllocateObjectImpl(vm, inlineCapacity);
        r->m_hiddenClass = SystemHeapPointer<void> { emptyStructure };
        // Initialize the butterfly storage
        //
        r->m_butterfly = nullptr;
        if (initialButterflyArrayPartCapacity > 0)
        {
            r->GrowButterflyFromNull<false /*isGrowNamedStorage*/>(initialButterflyArrayPartCapacity);
        }
        // Initialize the inline storage
        //
        TValue nilVal = TValue::Nil();
        for (size_t i = 0; i < inlineCapacity; i++)
        {
            r->m_inlineStorage[i] = nilVal;
        }
        return r;
    }

    static TableObject* WARN_UNUSED CreateEmptyTableObject(VM* vm, Structure* emptyStructure, uint32_t initialButterflyArrayPartCapacity)
    {
        return CreateEmptyTableObjectImpl(vm, emptyStructure, emptyStructure->m_inlineNamedStorageCapacity, initialButterflyArrayPartCapacity);
    }

    static TableObject* WARN_UNUSED CreateEmptyTableObject(VM* vm, uint32_t inlineCapcity, uint32_t initialButterflyArrayPartCapacity)
    {
        SystemHeapPointer<Structure> initialStructure = Structure::GetInitialStructureForInlineCapacity(vm, inlineCapcity);
        UserHeapPointer<TableObject> o = TableObject::CreateEmptyTableObject(vm, TranslateToRawPointer(vm, initialStructure.As()), initialButterflyArrayPartCapacity);
        return TranslateToRawPointer(o.As());
    }

    static TableObject* WARN_UNUSED CreateEmptyGlobalObject(VM* vm)
    {
        uint8_t inlineCapacity = Structure::x_maxNumSlots;
        CacheableDictionary* hc = CacheableDictionary::CreateEmptyDictionary(vm, 128 /*anticipatedNumSlots*/, inlineCapacity, true /*shouldNeverTransitToUncacheableDictionary*/);
        TableObject* r = AllocateObjectImpl(vm, inlineCapacity);
        r->m_hiddenClass = SystemHeapPointer<void> { hc };
        r->m_butterfly = nullptr;
        TValue nilVal = TValue::Nil();
        for (size_t i = 0; i < inlineCapacity; i++)
        {
            r->m_inlineStorage[i] = nilVal;
        }
        return r;
    }

    Butterfly* WARN_UNUSED CloneButterfly(uint32_t butterflyNamedStorageCapacity)
    {
        assert(m_butterfly != nullptr);
        uint32_t arrayStorageCapacity = m_butterfly->GetHeader()->m_arrayStorageCapacity;
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
            ArraySparseMap* oldSparseMap = hdr->GetSparseMap();
            ArraySparseMap* newSparseMap = oldSparseMap->Clone(vm);
            hdr->m_arrayLengthIfContinuous = GeneralHeapPointer<ArraySparseMap>(newSparseMap).m_value;
            assert(hdr->HasSparseMap() && hdr->GetSparseMap() == newSparseMap);
        }

        return butterflyPtr;
    }

    TableObject* WARN_UNUSED ShallowCloneTableObject(VM* vm)
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
        if (likely(m_butterfly == nullptr))
        {
            r->m_butterfly = nullptr;
        }
        else
        {
            r->m_butterfly = CloneButterfly(butterflyNamedStorageCapacity);
        }
        return r;
    }

    // Specialized CloneButterfly for TableDup, which leverages the statically known information for better code.
    //
    // Specifically, this function assumes that the butterfly has no named storage part and no SparseArray part
    //
    Butterfly* WARN_UNUSED CloneButterflyForTableDup()
    {
        assert(m_butterfly != nullptr);
        uint32_t arrayStorageCapacity = m_butterfly->GetHeader()->m_arrayStorageCapacity;
        assert(m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure);
        assert(m_hiddenClass.As<Structure>()->m_butterflyNamedStorageCapacity == 0);
        uint32_t butterflySlots = arrayStorageCapacity + 1;
        uint32_t butterflyStartOffset = static_cast<uint32_t>(1 - ArrayGrowthPolicy::x_arrayBaseOrd);
        uint64_t* butterflyStart = reinterpret_cast<uint64_t*>(m_butterfly) - butterflyStartOffset;
        uint64_t* butterflyCopy = new uint64_t[butterflySlots];
        memcpy(butterflyCopy, butterflyStart, sizeof(uint64_t) * butterflySlots);
        Butterfly* butterflyPtr = reinterpret_cast<Butterfly*>(butterflyCopy + butterflyStartOffset);
        assert(!butterflyPtr->GetHeader()->HasSparseMap());
        return butterflyPtr;
    }

    // These are just some artificial configuration limits for specialized TableDup opcodes
    // Maybe the config shouldn't be put here, but let's think about that later..
    //
    static constexpr uint8_t TableDupMaxInlineCapacitySteppingForNoButterflyCase() { return 3; }
    static constexpr uint8_t TableDupMaxInlineCapacitySteppingForHasButterflyCase() { return 2; }

    // Specialized ShallowCloneTableObject for TableDup, which leverages the statically known information for better code.
    // This function is ALWAYS_INLINE because by design the arguments will be constants after inlining.
    //
    // This function makes the following assumption:
    // (1) The table hidden class is a structure
    // (2) The table has no array sparse map and the butterfly named storage has zero capacity
    // (3) 'inlineCapacityStepping' and 'hasButterfly' accurately reflects the info in the table
    //
    TableObject* WARN_UNUSED ALWAYS_INLINE ShallowCloneTableObjectForTableDup(VM* vm, uint8_t inlineCapacityStepping, bool hasButterfly)
    {
        assert(m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure);
        [[maybe_unused]] Structure* structure = TranslateToRawPointer(m_hiddenClass.As<Structure>());
        uint8_t inlineCapacity = internal::x_inlineStorageSizeForSteppingArray[inlineCapacityStepping];
        assert(inlineCapacity == structure->m_inlineNamedStorageCapacity);
        assert(structure->m_butterflyNamedStorageCapacity == 0);

        uint32_t allocationSize = ComputeObjectAllocationSize(inlineCapacity);
        TableObject* r = TranslateToRawPointer(vm, vm->AllocFromUserHeap(allocationSize).AsNoAssert<TableObject>());
        // We can simply copy everything, except that we need to fix up the GC state
        // and the butterfly pointer (which needs to be cloned) manually afterwards
        //
        memcpy(r, this, allocationSize);
        r->m_cellState = GcCellState::White;
        if (hasButterfly)
        {
            r->m_butterfly = CloneButterflyForTableDup();
        }
        else
        {
            assert(r->m_butterfly == nullptr);
        }
        return r;
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

    static GetMetatableResult GetMetatable(TableObject* self)
    {
        SystemHeapPointer<void> hc = self->m_hiddenClass;
        HeapEntityType ty = hc.As<SystemHeapGcObjectHeader>()->m_type;
        assert(ty == HeapEntityType::Structure || ty == HeapEntityType::CacheableDictionary || ty == HeapEntityType::UncacheableDictionary);

        if (likely(ty == HeapEntityType::Structure))
        {
            Structure* structure = hc.As<Structure>();
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
            CacheableDictionary* cd = hc.As<CacheableDictionary>();
            return GetMetatableResult {
                .m_result = cd->m_metatable,
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
                    GrowButterflyKnowingNamedStorageCapacity<true /*isGrowNamedStorage*/>(structure->m_butterflyNamedStorageCapacity, result.m_newStructure.As()->m_butterflyNamedStorageCapacity);
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
    static bool WARN_UNUSED TryQuicklyRuleOutMetamethod(TableObject* self, LuaMetamethodKind mtKind)
    {
        SystemHeapPointer<void> hc = self->m_hiddenClass;
        if (likely(hc.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure))
        {
            return hc.As<Structure>()->m_knownNonexistentMetamethods & (static_cast<LuaMetamethodBitVectorT>(static_cast<LuaMetamethodBitVectorT>(1) << static_cast<size_t>(mtKind)));
        }
        else
        {
            return false;
        }
    }

    static uint32_t WARN_UNUSED NO_INLINE GetTableLengthWithLuaSemanticsSlowPathSlowPath(ArraySparseMap* sparseMap, uint32_t existentLb)
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

    static uint32_t WARN_UNUSED NO_INLINE GetTableLengthWithLuaSemanticsSlowPath(TableObject* self)
    {
        ArrayType arrType = self->m_arrayType;
        Butterfly* butterfly = self->m_butterfly;
        TValue* tv = reinterpret_cast<TValue*>(butterfly);
        uint32_t arrayStorageCap = butterfly->GetHeader()->m_arrayStorageCapacity;
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
                uint32_t ub = arrayStorageCap;
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
                ArraySparseMap* sparseMap = butterfly->GetHeader()->GetSparseMap();
                return GetTableLengthWithLuaSemanticsSlowPathSlowPath(sparseMap, arrayStorageCap);
            }
            else
            {
                // The sparse map doesn't exist, so 'arrayStorageCap + 1' is nil
                //
                return arrayStorageCap;
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
            ArraySparseMap* sparseMap = butterfly->GetHeader()->GetSparseMap();
            TValue val = sparseMap->GetByVal(1);
            if (val.IsNil())
            {
                // slot '1' doesn't exist, so length is 0
                //
                return 0;
            }
            // Slot '1' exists, so we have an existent lower bound, we can call GetTableLengthWithLuaSemanticsSlowPath now
            //
            return GetTableLengthWithLuaSemanticsSlowPathSlowPath(sparseMap, 1 /*existentLb*/);
        }
    }

    // The 'length' operator under Lua semantics
    // The definition is any non-negative integer index n such that value for index 'n' is non-nil but value for index 'n+1' is nil,
    // or if index '1' is nil, the length is 0
    //
    static std::pair<bool /*success*/, uint32_t /*length*/> WARN_UNUSED ALWAYS_INLINE TryGetTableLengthWithLuaSemanticsFastPath(TableObject* self)
    {
        static_assert(ArrayGrowthPolicy::x_arrayBaseOrd == 1, "this function currently only works under lua semantics");
        ArrayType arrType = TCGet(self->m_arrayType);
        if (likely(arrType.IsContinuous()))
        {
            // Fast path: the array is continuous
            //
            int32_t val = self->m_butterfly->GetHeader()->m_arrayLengthIfContinuous;
            assert(val >= 0);
            return std::make_pair(true /*success*/, static_cast<uint32_t>(val - 1 + ArrayGrowthPolicy::x_arrayBaseOrd));
        }
        else if (arrType.ArrayKind() == ArrayType::Kind::NoButterflyArrayPart)
        {
            // Edge case: no butterfly array part
            //
            return std::make_pair(true /*success*/, 0);
        }
        else
        {
            return std::make_pair(false /*success*/, uint32_t());
        }
    }

    static uint32_t WARN_UNUSED GetTableLengthWithLuaSemantics(TableObject* self)
    {
        auto [success, len] = TryGetTableLengthWithLuaSemanticsFastPath(self);
        if (likely(success))
        {
            return len;
        }
        else
        {
            return GetTableLengthWithLuaSemanticsSlowPath(self);
        }
    }

    // Object header
    //
    SystemHeapPointer<void> m_hiddenClass;
    HeapEntityType m_type;
    GcCellState m_cellState;

    uint8_t m_reserved;
    ArrayType m_arrayType;

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
            TableObject* tableObj = TranslateToRawPointer(value.AsPointer<TableObject>().As());
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

inline FunctionObject* GetCallMetamethodFromMetatableImpl(UserHeapPointer<void> metatableMaybeNull)
{
    if (metatableMaybeNull.m_value == 0)
    {
        return nullptr;
    }
    assert(metatableMaybeNull.As<UserHeapGcObjectHeader>()->m_type == HeapEntityType::Table);
    TableObject* metatable = TranslateToRawPointer(metatableMaybeNull.As<TableObject>());
    GetByIdICInfo icInfo;
    TableObject::PrepareGetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Call), icInfo /*out*/);
    TValue target = TableObject::GetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Call).As<void>(), icInfo);
    if (likely(target.Is<tFunction>()))
    {
        return TranslateToRawPointer(target.As<tFunction>());
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
inline FunctionObject* WARN_UNUSED NO_INLINE GetCallTargetViaMetatable(TValue value)
{
    assert(!value.Is<tFunction>());

    if (likely(value.Is<tHeapEntity>()))
    {
        HeapEntityType ty = value.GetHeapEntityType();
        assert(ty != HeapEntityType::Function);

        if (likely(ty == HeapEntityType::Table))
        {
            TableObject* tableObj = TranslateToRawPointer(value.As<tTable>());
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

    KeyValuePair WARN_UNUSED Advance(TableObject* obj)
    {
        HeapEntityType hcType;
        Structure* structure;
        CacheableDictionary* cacheableDict;
        ArraySparseMap* sparseMap;

        if (unlikely(m_state == IteratorState::Uninitialized))
        {
            hcType = obj->m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type;
            assert(hcType == HeapEntityType::Structure || hcType == HeapEntityType::CacheableDictionary || hcType == HeapEntityType::UncacheableDictionary);
            if (hcType == HeapEntityType::Structure)
            {
                structure = obj->m_hiddenClass.As<Structure>();
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
                cacheableDict = obj->m_hiddenClass.As<CacheableDictionary>();
                m_state = IteratorState::NamedProperty;
                m_namedPropertyOrd = 0;
                goto try_find_and_get_cd_prop;
            }
            else
            {
                // TODO: support UncacheableDictionary
                assert(false && "unimplemented");
                __builtin_unreachable();
            }
        }

        if (m_state == IteratorState::NamedProperty)
        {
            hcType = obj->m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type;
            assert(hcType == HeapEntityType::Structure || hcType == HeapEntityType::CacheableDictionary || hcType == HeapEntityType::UncacheableDictionary);

            if (likely(hcType == HeapEntityType::Structure))
            {
                structure = obj->m_hiddenClass.As<Structure>();

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
                                .m_key = TValue::CreatePointer(UserHeapPointer<void>(TranslateToRawPointer(entry.m_key.As()))),
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
                assert(false && "unimplemented");
                __builtin_unreachable();
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
            uint32_t vectorStorageCapacity = butterfly->GetHeader()->m_arrayStorageCapacity;
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
        sparseMap = obj->m_butterfly->GetHeader()->GetSparseMap();
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
    static bool WARN_UNUSED GetNextFromKey(TableObject* obj, TValue key, KeyValuePair& out /*out*/)
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
                Structure* structure = obj->m_hiddenClass.As<Structure>();
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
                CacheableDictionary* cacheableDict = TCGet(obj->m_hiddenClass).As<CacheableDictionary>();
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

            if (!obj->m_butterfly->GetHeader()->IndexFitsInVectorCapacity(idx64))
            {
                goto get_next_from_sparse_map;
            }

            TableObjectIterator iter;
            iter.m_state = IteratorState::VectorStorage;
            iter.m_vectorStorageOrd = SafeIntegerCast<uint32_t>(idx64);
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

            ArraySparseMap* sparseMap = obj->m_butterfly->GetHeader()->GetSparseMap();
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
        uint32_t m_vectorStorageOrd;
        uint32_t m_sparseMapOrd;
    };
    // Ugly: m_state must come after the union, so it occupies the higher bytes (due to little endianness).
    // And since its highest bit is 0, this will make the TableObjectIterator look like a double due to
    // our TValue boxing scheme, which is safe (it would be unsafe if it looks like a pointer, as DFG JIT
    // logic may try to dereference it).
    //
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
