#pragma once

#include "common_utils.h"
#include "memory_ptr.h"
#include "vm.h"
#include "vm_string.h"
#include "structure.h"

namespace ToyLang {

using namespace CommonUtils;

// This doesn't really need to inherit the GC header, but for now let's make thing simple..
//
class alignas(8) ArraySparseMap final : public UserHeapGcObjectHeader
{
public:
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

    TValue* GetNamedPropertyAddr(int32_t ord)
    {
        assert(ord < 0);
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
    SystemHeapPointer<void> m_hiddenClass;
};

struct GetByIntegerIndexICInfo
{
    enum class ICKind: uint8_t
    {
        // It must return nil because we have no butterfly array part
        //
        NoArrayPart,
        // It must be in the vector storage if it exists
        // So if the index is not a vector index or not within range, the result must be nil
        //
        VectorStorage,
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
    SystemHeapPointer<void> m_hiddenClass;
};

struct PutByIdICInfo
{
    // Condition on the fixed structure, whether the GetById can use inline cache
    // Note that if the table is in dictionary mode, or will transit to dictionary mode by this PutById,
    // then this operation is never inline cachable
    //
    enum class ICKind : uint8_t
    {
        // The PutById transitioned the table from Structure mode to CacheableDictionary mode
        //
        TransitionedToDictionaryMode,
        // The table is in UncacheableDictionary mode
        //
        UncacheableDictionary,
        // The table is in CacheableDictionary mode, and the property should be written to outlined storage in m_slot
        // (note that in dictionary mode we never make use of the inline storage for simplicity)
        //
        CacheableDictionary,
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
    SystemHeapPointer<Structure> m_structure;
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
        // FIXME: converting NaN to integer is UB and clang can actually generate faulty code
        // if it can deduce that for some call the 'vidx' is NaN! However, if we manually check isnan,
        // while it worksaround the UB, clang cannot optimize the check away and generates inferior code,
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

    // TODO: we should make this function a static mapping from arrType to GetByNumericICInfo for better perf
    //
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static void ALWAYS_INLINE PrepareGetByIntegerIndex(T self, GetByIntegerIndexICInfo& icInfo /*out*/)
    {
        ArrayType arrType = TCGet(self->m_arrayType);

        icInfo.m_hiddenClass = TCGet(self->m_hiddenClass);
        icInfo.m_mayHaveMetatable = arrType.MayHaveMetatable();

        // Check case: continuous array
        //
        if (likely(arrType.IsContinuous()))
        {
            icInfo.m_icKind = GetByIntegerIndexICInfo::ICKind::VectorStorage;
            return;
        }

        // Check case: No array at all
        //
        if (arrType.ArrayKind() == ArrayType::Kind::NoButterflyArrayPart)
        {
            assert(!arrType.HasSparseMap());
            icInfo.m_icKind = GetByIntegerIndexICInfo::ICKind::NoArrayPart;
            return;
        }

        // Now, we know the array type contains a vector part and potentially a sparse map
        //
        if (unlikely(arrType.SparseMapContainsVectorIndex()))
        {
            icInfo.m_icKind = GetByIntegerIndexICInfo::ICKind::VectorStorageOrSparseMap;
        }
        else if (unlikely(arrType.HasSparseMap()))
        {
            icInfo.m_icKind = GetByIntegerIndexICInfo::ICKind::VectorStorageXorSparseMap;
        }
        else
        {
            icInfo.m_icKind = GetByIntegerIndexICInfo::ICKind::VectorStorage;
        }
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue ALWAYS_INLINE GetByIntegerIndex(T self, int64_t idx, GetByIntegerIndexICInfo icInfo)
    {
        // TODO: handle metatable

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
        // TODO: when we support metatable, we should add optimization for continuous vector as the metatable check can potentially be skipped
        //
        if (likely(ArrayGrowthPolicy::x_arrayBaseOrd <= idx && self->m_butterfly->GetHeader()->IndexFitsInVectorCapacity(idx)))
        {
            TValue res = *(self->m_butterfly->UnsafeGetInVectorIndexAddr(idx));
            // If the array is actually continuous, result must be non-nil
            //
            AssertImp(self->m_butterfly->GetHeader()->CanUseFastPathGetForContinuousArray(idx), !res.IsNil());
            AssertImp(!res.IsNil() && arrType.ArrayKind() == ArrayType::Kind::Int32, res.IsInt32(TValue::x_int32Tag));
            AssertImp(!res.IsNil() && arrType.ArrayKind() == ArrayType::Kind::Double, res.IsDouble(TValue::x_int32Tag));
            return res;
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
        // TODO: handle metatable

        return GetByIntegerIndex(self, idx, icInfo);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue GetByDoubleVal(T self, double idx, GetByIntegerIndexICInfo icInfo)
    {
        // TODO: handle metatable

        if (IsNaN(idx))
        {
            // TODO: throw error NaN as table index
            ReleaseAssert(false);
        }

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
    static TValue QueryArraySparseMap(T self, double idx)
    {
#ifndef NDEBUG
        ArrayType arrType = TCGet(self->m_arrayType);
        assert(arrType.HasSparseMap());
#endif
        HeapPtr<ArraySparseMap> sparseMap = self->m_butterfly->GetHeader()->GetSparseMap();
        TValue res = TranslateToRawPointer(sparseMap)->GetByVal(idx);
#ifndef NDEBUG
        AssertImp(!res.IsNil() && arrType.ArrayKind() == ArrayType::Kind::Int32, res.IsInt32(TValue::x_int32Tag));
        AssertImp(!res.IsNil() && arrType.ArrayKind() == ArrayType::Kind::Double, res.IsDouble(TValue::x_int32Tag));
#endif
        return res;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static void PrepareGetById(T self, UserHeapPointer<HeapString> propertyName, GetByIdICInfo& icInfo /*out*/)
    {
        SystemHeapPointer<void> hiddenClass = TCGet(self->m_hiddenClass);
        Type ty = hiddenClass.As<SystemHeapGcObjectHeader>()->m_type;
        assert(ty == Type::Structure || ty == Type::CacheableDictionary);
        icInfo.m_hiddenClass = hiddenClass;

        if (likely(ty == Type::Structure))
        {
            HeapPtr<Structure> structure = hiddenClass.As<Structure>();
            icInfo.m_mayHaveMetatable = (structure->m_metatable != 0);

            uint32_t slotOrd;
            bool found = Structure::GetSlotOrdinalFromStringProperty(structure, propertyName, slotOrd /*out*/);
            if (found)
            {
                uint32_t inlineStorageCapacity = structure->m_inlineNamedStorageCapacity;
                if (slotOrd < inlineStorageCapacity)
                {
                    icInfo.m_icKind = GetByIdICInfo::ICKind::InlinedStorage;
                    icInfo.m_slot = static_cast<int32_t>(slotOrd);
                }
                else
                {
                    icInfo.m_icKind = GetByIdICInfo::ICKind::OutlinedStorage;
                    icInfo.m_slot = static_cast<int32_t>(inlineStorageCapacity - slotOrd - 1);
                }
            }
            else
            {
                icInfo.m_icKind = GetByIdICInfo::ICKind::MustBeNil;
            }
        }
        else
        {
            ReleaseAssert(false && "unimplemented");
        }
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue WARN_UNUSED ALWAYS_INLINE GetById(T self, UserHeapPointer<HeapString> /*propertyName*/, GetByIdICInfo icInfo)
    {
        // TODO: handle metatable

        if (icInfo.m_icKind == GetByIdICInfo::ICKind::MustBeNil)
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

        ReleaseAssert(false && "not implemented");
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static void PreparePutById(T self, UserHeapPointer<HeapString> propertyName, PutByIdICInfo& icInfo /*out*/)
    {
        SystemHeapPointer<void> hiddenClass = TCGet(self->m_hiddenClass);
        Type ty = hiddenClass.As<SystemHeapGcObjectHeader>()->m_type;
        assert(ty == Type::Structure || ty == Type::CacheableDictionary);

        if (likely(ty == Type::Structure))
        {
            HeapPtr<Structure> structure = hiddenClass.As<Structure>();
            icInfo.m_structure = structure;
            icInfo.m_mayHaveMetatable = (structure->m_metatable != 0);

            uint32_t slotOrd;
            bool found = Structure::GetSlotOrdinalFromStringProperty(structure, propertyName, slotOrd /*out*/);
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
                    icInfo.m_slot = static_cast<int32_t>(inlineStorageCapacity - slotOrd - 1);
                }
            }
            else
            {
                VM* vm = VM::GetActiveVMForCurrentThread();
                Structure::AddNewPropertyResult addNewPropResult;
                TranslateToRawPointer(vm, structure)->AddNonExistentProperty(vm, propertyName.As(), addNewPropResult /*out*/);
                if (unlikely(addNewPropResult.m_transitionedToDictionaryMode))
                {
                    icInfo.m_icKind = PutByIdICInfo::ICKind::TransitionedToDictionaryMode;
                    ReleaseAssert(false && "unimplemented");
                }
                else
                {
                    slotOrd = addNewPropResult.m_slotOrdinal;
                    uint32_t inlineStorageCapacity = structure->m_inlineNamedStorageCapacity;
                    icInfo.m_shouldGrowButterfly = addNewPropResult.m_shouldGrowButterfly;
                    icInfo.m_newStructure = addNewPropResult.m_newStructureOrDictionary;
                    if (slotOrd < inlineStorageCapacity)
                    {
                        icInfo.m_icKind = PutByIdICInfo::ICKind::InlinedStorage;
                        icInfo.m_slot = static_cast<int32_t>(slotOrd);
                    }
                    else
                    {
                        assert(slotOrd < icInfo.m_newStructure.As()->m_numSlots);
                        icInfo.m_icKind = PutByIdICInfo::ICKind::OutlinedStorage;
                        icInfo.m_slot = static_cast<int32_t>(inlineStorageCapacity - slotOrd - 1);
                    }
                }
            }
        }
        else
        {
            ReleaseAssert(false && "unimplemented");
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
            uint32_t oldNamedStorageCapacity = m_hiddenClass.As<Structure>()->m_butterflyNamedStorageCapacity;
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

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static void ALWAYS_INLINE PutById(T self, UserHeapPointer<HeapString> /*propertyName*/, TValue newValue, PutByIdICInfo icInfo)
    {
        if (icInfo.m_icKind == PutByIdICInfo::ICKind::UncacheableDictionary)
        {
            ReleaseAssert(false && "unimplemented");
        }

        if (icInfo.m_icKind == PutByIdICInfo::ICKind::CacheableDictionary)
        {
            ReleaseAssert(false && "unimplemented");
        }

        if (icInfo.m_icKind == PutByIdICInfo::ICKind::TransitionedToDictionaryMode)
        {
            ReleaseAssert(false && "unimplemented");
        }

        // TODO: check for metatable

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
        assert(arrType.m_asValue == icInfo.m_hiddenClass.As<Structure>()->m_arrayType.m_asValue);

        auto setForceSlowPath = [&icInfo]() ALWAYS_INLINE
        {
            icInfo.m_indexCheckKind = PutByIntegerIndexICInfo::IndexCheckKind::ForceSlowPath;
            icInfo.m_valueCheckKind = PutByIntegerIndexICInfo::ValueCheckKind::NoCheck;
        };

        // We cannot IC any fast path if the object is in dictionary mode: see comment on IndexCheckKind::ForceSlowPath
        //
        if (icInfo.m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type != Type::Structure)
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
            if (value.IsInt32(TValue::x_int32Tag))
            {
                icInfo.m_valueCheckKind = PutByIntegerIndexICInfo::ValueCheckKind::Int32;
                setNewStructureAndArrayType(ArrayType::Kind::Int32);
            }
            else if (value.IsDouble(TValue::x_int32Tag))
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
        // TODO: handle metatable

        switch (icInfo.m_valueCheckKind)
        {
        case PutByIntegerIndexICInfo::ValueCheckKind::Int32:
        {
            if (!value.IsInt32(TValue::x_int32Tag))
            {
                return false;
            }
            break;
        }
        case PutByIntegerIndexICInfo::ValueCheckKind::Int32OrNil:
        {
            if (!value.IsInt32(TValue::x_int32Tag) && !value.IsNil())
            {
                return false;
            }
            break;
        }
        case PutByIntegerIndexICInfo::ValueCheckKind::Double:
        {
            if (!value.IsDouble(TValue::x_int32Tag))
            {
                return false;
            }
            break;
        }
        case PutByIntegerIndexICInfo::ValueCheckKind::DoubleOrNil:
        {
            if (!value.IsDouble(TValue::x_int32Tag) && !value.IsNil())
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
        // TODO: handle metatable

        if (index64 < ArrayGrowthPolicy::x_arrayBaseOrd || index64 > ArrayGrowthPolicy::x_unconditionallySparseMapCutoff)
        {
            PutIndexIntoSparseMap(vm, false /*isVectorQualifyingIndex*/, static_cast<double>(index64), value);
            return;
        }

        if (m_type != Type::TABLE)
        {
            ReleaseAssert(false && "unimplemented");
        }

        // This debug variable validates that we did not enter this slow-path for no reason (i.e. the fast path should have handled the case it ought to handle)
        //
        DEBUG_ONLY(bool didSomethingNontrivial = false;)

        // The fast path can only handle the case where the hidden class is structure
        //
        DEBUG_ONLY(didSomethingNontrivial |= (m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type != Type::Structure));

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
                int32_t currentCapcity = m_butterfly->GetHeader()->m_arrayStorageCapacity;
                if (index >= currentCapcity + ArrayGrowthPolicy::x_arrayBaseOrd)
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
            if (value.IsInt32(TValue::x_int32Tag))
            {
                newArrayType.SetArrayKind(ArrayType::Kind::Int32);
            }
            else if (value.IsDouble(TValue::x_int32Tag))
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
            int32_t currentCapcity = butterfly->GetHeader()->m_arrayStorageCapacity;

            if (index >= currentCapcity + ArrayGrowthPolicy::x_arrayBaseOrd)
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
                uint32_t newCapacity = static_cast<uint32_t>(currentCapcity * ArrayGrowthPolicy::x_vectorGrowthFactor);
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
                        assert(index == currentCapcity + ArrayGrowthPolicy::x_arrayBaseOrd);
                        DEBUG_ONLY(shouldIncrementContinuousLength = true;)
                    }
                }
                AssertIff(shouldIncrementContinuousLength, isContinuous);

                // Base on the array policy, check if we should put it to sparse map
                // Note: it is intentional that we do not check x_unconditionallySparseMapCutoff,
                // because it has been checked at the start of the function
                //
                bool shouldPutToSparseMap = false;
                if (newCapacity > ArrayGrowthPolicy::x_sparseMapUnlessContinuousCutoff)
                {
                    if (!isContinuous)
                    {
                        shouldPutToSparseMap = true;
                    }
                }
                else if (newCapacity > ArrayGrowthPolicy::x_alwaysVectorCutoff)
                {
                    uint64_t nonNilCount = 1;
                    for (int32_t i = ArrayGrowthPolicy::x_arrayBaseOrd; i < currentCapcity + ArrayGrowthPolicy::x_arrayBaseOrd; i++)
                    {
                        if (!butterfly->UnsafeGetInVectorIndexAddr(i)->IsNil())
                        {
                            nonNilCount++;
                        }
                    }
                    if (nonNilCount * ArrayGrowthPolicy::x_densityCutoff < newCapacity)
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
                if (!value.IsInt32(TValue::x_int32Tag) && !value.IsNil())
                {
                    newArrayType.SetArrayKind(ArrayType::Kind::Any);
                }
            }
            else if(arrType.ArrayKind() == ArrayType::Kind::Double)
            {
                if (!value.IsDouble(TValue::x_int32Tag) && !value.IsNil())
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
            Type hiddenClassType = m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type;
            assert(hiddenClassType == Type::Structure || hiddenClassType == Type::CacheableDictionary);
            if (hiddenClassType == Type::Structure)
            {
                Structure* structure = TranslateToRawPointer(vm, m_hiddenClass.As<Structure>());
                Structure* newStructure = structure->UpdateArrayType(vm, newArrayType);
                m_hiddenClass = newStructure;
                m_arrayType = newArrayType;
            }
            else
            {
                ReleaseAssert(false && "unimplemented");
            }

            DEBUG_ONLY(didSomethingNontrivial = true;)
        }

        assert(didSomethingNontrivial);
    }

    ArraySparseMap* WARN_UNUSED AllocateNewArraySparseMap(VM* vm)
    {
        assert(m_butterfly != nullptr);
        assert(!m_butterfly->GetHeader()->HasSparseMap());
        HeapPtr<ArraySparseMap> hp = vm->AllocFromUserHeap(sizeof(ArraySparseMap)).AsNoAssert<ArraySparseMap>();
        ArraySparseMap* sparseMap = TranslateToRawPointer(vm, hp);
        ConstructInPlace(sparseMap);
        UserHeapGcObjectHeader::Populate(sparseMap);
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

    void PutIndexIntoSparseMap(VM* vm, bool isVectorQualifyingIndex, double index, TValue value)
    {
#ifndef NDEBUG
        // Assert that the 'isVectorQualifyingIndex' parameter is accurate
        //
        {
            int64_t idx64;
            if (IsInt64Index(index, idx64 /*out*/))
            {
                AssertIff(isVectorQualifyingIndex, idx64 >= ArrayGrowthPolicy::x_arrayBaseOrd && idx64 <= ArrayGrowthPolicy::x_unconditionallySparseMapCutoff);
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

        // TODO: handle metatable

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
        sparseMap->m_map[index] = value;

        if (arrType.m_asValue != newArrayType.m_asValue)
        {
            Type ty = m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type;
            if (ty == Type::Structure)
            {
                m_hiddenClass = TranslateToRawPointer(vm, m_hiddenClass.As<Structure>())->UpdateArrayType(vm, newArrayType);
                m_arrayType = newArrayType;
            }
            else
            {
                ReleaseAssert(false && "unimplemented");
            }
        }
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static void PutByValIntegerIndex(T self, int64_t index, TValue value)
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
    static void PutByValDoubleIndex(T self, double index, TValue value)
    {
        int64_t idx64;
        if (likely(IsInt64Index(index, idx64 /*out*/)))
        {
            PutByValIntegerIndex(self, idx64, value);
        }
        else
        {
            if (IsNaN(index))
            {
                // should throw out an error here
                ReleaseAssert(false && "unimplemented");
            }

            VM* vm = VM::GetActiveVMForCurrentThread();
            TableObject* obj = TranslateToRawPointer(vm, self);
            obj->PutIndexIntoSparseMap(vm, false /*isVectorQualifyingIndex*/, index, value);
        }
    }

    static HeapPtr<TableObject> CreateEmptyTableObject(VM* vm, Structure* emptyStructure, uint32_t initialButterflyArrayPartCapacity)
    {
        assert(emptyStructure->m_numSlots == 0);
        assert(emptyStructure->m_metatable == 0);
        assert(emptyStructure->m_arrayType.m_asValue == 0);
        assert(emptyStructure->m_butterflyNamedStorageCapacity == 0);
        constexpr size_t x_baseSize = offsetof_member_v<&TableObject::m_inlineStorage>;
        size_t inlineCapacity = emptyStructure->m_inlineNamedStorageCapacity;
        size_t allocationSize = x_baseSize + inlineCapacity * sizeof(TValue);
        HeapPtr<TableObject> r = vm->AllocFromUserHeap(static_cast<uint32_t>(allocationSize)).AsNoAssert<TableObject>();
        UserHeapGcObjectHeader::Populate(r);
        TCSet(r->m_hiddenClass, SystemHeapPointer<void> { emptyStructure });
        TCSet(r->m_arrayType, ArrayType::GetInitialArrayType());
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

    // Object header
    //
    SystemHeapPointer<void> m_hiddenClass;
    Type m_type;
    GcCellState m_cellState;

    ArrayType m_arrayType;
    uint8_t m_reserved;

    Butterfly* m_butterfly;
    TValue m_inlineStorage[0];
};
static_assert(sizeof(TableObject) == 16);


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
