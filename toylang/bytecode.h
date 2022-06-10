#pragma once

#include "common_utils.h"
#include "memory_ptr.h"
#include "vm.h"
#include "vm_string.h"
#include "structure.h"

namespace ToyLang {

using namespace CommonUtils;

class alignas(8) ArraySparseMap
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

struct GetByNumericICInfo
{
    enum class ICKind: uint8_t
    {
        // It must return nil because we have no butterfly array part
        //
        MustBeNil,
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

struct PutByNumericICInfo
{

};

class alignas(8) TableObject
{
public:
    // Check if the index qualifies for array indexing
    //
    static bool WARN_UNUSED ALWAYS_INLINE IsQualifiedForVectorIndex(double vidx, int32_t& idx /*out*/)
    {
        // FIXME: converting NaN to integer is UB and clang can actually generate faulty code
        // if it can deduce that for some call the 'vidx' is NaN! However, if we manually check isnan,
        // while it worksaround the UB, clang cannot optimize the check away and generates inferior code,
        // so we leave the code with the UB as for now.
        //
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
    static TValue GetByInt32Val(T self, TValue tidx, GetByNumericICInfo icInfo)
    {
        assert(tidx.IsInt32(TValue::x_int32Tag));

        int32_t idx = tidx.AsInt32();
        TValue res;
        if (likely(ArrayGrowthPolicy::x_arrayBaseOrd <= idx))
        {
            res = GetByValVectorIndex(self, idx, icInfo);
            if (likely(!icInfo.m_mayHaveMetatable))
            {
                return res;
            }
        }
        else
        {
#ifndef NDEBUG
            ArrayType arrType { self->m_arrayType };
#endif

            // Not vector-qualifying index
            // If it exists, it must be in the sparse map
            //
            if (icInfo.m_icKind == GetByNumericICInfo::ICKind::VectorStorageOrSparseMap ||
                icInfo.m_icKind == GetByNumericICInfo::ICKind::VectorStorageXorSparseMap)
            {
                assert(arrType.ArrayKind() != ArrayType::NoButterflyArrayPart);
                res = QuerySparseMap(self, tidx, static_cast<double>(idx));
            }
            else
            {
                res = TValue::Nil();
            }

            if (likely(!icInfo.m_mayHaveMetatable))
            {
                return res;
            }
        }

        if (likely(!res.IsNil()))
        {
            return res;
        }

        // Check metatable
        //
        ReleaseAssert(false && "unimplemented");
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue GetByDoubleVal(T self, TValue tidx, GetByNumericICInfo icInfo)
    {
        assert(tidx.IsDouble(TValue::x_int32Tag));
        double dblIdx = tidx.AsDouble();
        int32_t idx;
        TValue res;
        if (likely(IsQualifiedForVectorIndex(dblIdx, idx /*out*/)))
        {
            res = GetByValVectorIndex(self, idx, icInfo);
            if (likely(!icInfo.m_mayHaveMetatable))
            {
                return res;
            }
        }
        else
        {
            if (IsNaN(dblIdx))
            {
                // TODO: throw error NaN as table index
                ReleaseAssert(false);
            }

#ifndef NDEBUG
            ArrayType arrType { self->m_arrayType };
#endif
            // Not vector-qualifying index
            // If it exists, it must be in the sparse map
            //
            if (icInfo.m_icKind == GetByNumericICInfo::ICKind::VectorStorageOrSparseMap ||
                icInfo.m_icKind == GetByNumericICInfo::ICKind::VectorStorageXorSparseMap)
            {
                assert(arrType.ArrayKind() != ArrayType::NoButterflyArrayPart);
                res = QuerySparseMap(self, dblIdx);
            }
            else
            {
                res = TValue::Nil();
            }

            if (likely(!icInfo.m_mayHaveMetatable))
            {
                return res;
            }
        }

        if (likely(!res.IsNil()))
        {
            return res;
        }

        // Check metatable
        //
        ReleaseAssert(false && "unimplemented");
    }

    // TODO: we should make this function a static mapping from arrType to GetByNumericICInfo for better perf
    //
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static void ALWAYS_INLINE PrepareGetByNumeric(T self, GetByNumericICInfo& icInfo /*out*/)
    {
        ArrayType arrType = TCGet(self->m_arrayType);

        icInfo.m_hiddenClass = TCGet(self->m_hiddenClass);
        icInfo.m_mayHaveMetatable = arrType.MayHaveMetatable();

        // Check case: continuous array
        //
        if (likely(arrType.IsContinuous()))
        {
            icInfo.m_icKind = GetByNumericICInfo::ICKind::VectorStorage;
            return;
        }

        // Check case: No array at all
        //
        if (arrType.ArrayKind() == ArrayType::NoButterflyArrayPart)
        {
            assert(!arrType.HasSparseMap());
            icInfo.m_icKind = GetByNumericICInfo::ICKind::MustBeNil;
            return;
        }

        // Now, we know the array type contains a vector part and potentially a sparse map
        //
        if (unlikely(arrType.SparseMapContainsVectorIndex()))
        {
            icInfo.m_icKind = GetByNumericICInfo::ICKind::VectorStorageOrSparseMap;
        }
        else if (unlikely(arrType.HasSparseMap()))
        {
            icInfo.m_icKind = GetByNumericICInfo::ICKind::VectorStorageXorSparseMap;
        }
        else
        {
            icInfo.m_icKind = GetByNumericICInfo::ICKind::VectorStorage;
        }
    }

    // Handle the case that index is int32_t and >= x_arrayBaseOrd
    //
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue ALWAYS_INLINE GetByValVectorIndex(T self, int32_t idx, GetByNumericICInfo icInfo)
    {
        assert(idx >= ArrayGrowthPolicy::x_arrayBaseOrd);
#ifndef NDEBUG
        ArrayType arrType = TCGet(self->m_arrayType);
#endif

        if (icInfo.m_icKind == GetByNumericICInfo::ICKind::VectorStorage)
        {
            // TODO: for continuous vector, we should have a way to tell our caller that metatable check
            // may be skipped without checking if the result is nil. For simplicity we don't do it now.
            //
            if (likely(self->m_butterfly->GetHeader()->IndexFitsInVectorCapacity(index)))
            {
                TValue res = *(self->m_butterfly->UnsafeGetInVectorIndexAddr(idx));
                // If the array is actually continuous, result must be non-nil
                //
                AssertImp(self->m_butterfly->GetHeader()->CanUseFastPathGetForContinuousArray(idx), !res.IsNil());
                AssertImp(!res.IsNil() && arrType.ArrayKind() == ArrayType::Int32, res.IsInt32(TValue::x_int32Tag));
                AssertImp(!res.IsNil() && arrType.ArrayKind() == ArrayType::Double, res.IsDouble(TValue::x_int32Tag));
                return res;
            }
            else
            {
                // index is out of range, raw result must be nil
                //
                return TValue::Nil();
            }
        }

        if (icInfo.m_icKind == GetByNumericICInfo::ICKind::MustBeNil)
        {
            assert(!arrType.HasSparseMap());
            return TValue::Nil();
        }

        // Check case: has sparse map
        // Even if there is a sparse map, if it doesn't contain vector-qualifying index,
        // since in this function 'idx' is vector-qualifying, it must be non-existent
        //
        if (icInfo.m_icKind == GetByNumericICInfo::ICKind::VectorStorageOrSparseMap)
        {
            assert(arrType.HasSparseMap());
            TValue res = QuerySparseMap(self, idx);
            return res;
        }
        else
        {
            return TValue::Nil();
        }
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, TableObject>>>
    static TValue QuerySparseMap(T self, double idx)
    {
#ifndef NDEBUG
        ArrayType arrType = TCGet(self->m_arrayType);
        assert(arrType.HasSparseMap());
#endif
        HeapPtr<ArraySparseMap> sparseMap = self->m_butterfly->GetHeader()->GetSparseMap();
        TValue res = TranslateToRawPointer(sparseMap)->GetByVal(idx);
#ifndef NDEBUG
        AssertImp(!res.IsNil() && arrType.ArrayKind() == ArrayType::Int32, res.IsInt32(TValue::x_int32Tag));
        AssertImp(!res.IsNil() && arrType.ArrayKind() == ArrayType::Double, res.IsDouble(TValue::x_int32Tag));
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
