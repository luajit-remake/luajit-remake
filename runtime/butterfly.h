#pragma once

#include "common_utils.h"
#include "tvalue.h"
#include "array_type.h"

class alignas(8) ButterflyHeader
{
public:
    bool IsContinuous()
    {
        return m_arrayLengthIfContinuous >= 0;
    }

    bool CanUseFastPathGetForContinuousArray(int64_t idx)
    {
        assert(IsContinuous());
        // We want to compute 'ArrayGrowthPolicy::x_arrayBaseOrd <= idx && idx < m_arrayLengthIfContinuous + x_arrayBaseOrd'
        // but we do not want to emit two branches.
        //
        // The trick is to take advantage of the fact that m_arrayLengthIfContinuous >= 0.
        // So we can cast 'idx' to unsigned, and compare
        //     idx - ArrayGrowthPolicy::x_arrayBaseOrd < m_arrayLengthIfContinuous
        //
        // If idx < ArrayGrowthPolicy::x_arrayBaseOrd, we will underflow and get a huge number.
        // So in that case LHS is guaranteed to be larger than RHS, as desired.
        //
        uint64_t lhs = static_cast<uint64_t>(idx) - ArrayGrowthPolicy::x_arrayBaseOrd;
        uint64_t rhs = static_cast<uint64_t>(static_cast<int64_t>(m_arrayLengthIfContinuous));
        bool result = lhs < rhs;
        // Just sanity check we didn't screw anything up..
        //
        AssertIff(result, ArrayGrowthPolicy::x_arrayBaseOrd <= idx && idx < m_arrayLengthIfContinuous + ArrayGrowthPolicy::x_arrayBaseOrd);
        return result;
    }

    bool IndexFitsInVectorCapacity(int64_t idx)
    {
        // Similar to the above function, we want to compute
        //     ArrayGrowthPolicy::x_arrayBaseOrd <= idx && idx < m_arrayStorageCapacity + ArrayGrowthPolicy::x_arrayBaseOrd
        // and we use the same trick to only produce one branch.
        //
        uint64_t lhs = static_cast<uint64_t>(idx) - ArrayGrowthPolicy::x_arrayBaseOrd;
        uint64_t rhs = static_cast<uint64_t>(m_arrayStorageCapacity);
        bool result = lhs < rhs;
        // Just sanity check we didn't screw anything up..
        //
        AssertIff(result, ArrayGrowthPolicy::x_arrayBaseOrd <= idx && idx < static_cast<int64_t>(m_arrayStorageCapacity) + ArrayGrowthPolicy::x_arrayBaseOrd);
        return result;
    }

    bool HasSparseMap()
    {
        return m_arrayLengthIfContinuous < - 1;
    }

    HeapPtr<ArraySparseMap> GetSparseMap()
    {
        assert(HasSparseMap());
        return GeneralHeapPointer<ArraySparseMap> { m_arrayLengthIfContinuous }.As();
    }

    // If == - 1, it means the vector part is not continuous
    // If < - 1, it means the vector part is not continuous and there is a sparse map,
    //     and the value can be interpreted as a GeneralHeapPointer<ArraySparseMap>
    // If >= 0, it means the vector part is continuous and has no sparse map.
    //     That is, range [x_arrayBaseOrd, m_arrayLengthIfContinuous + x_arrayBaseOrd) are all non-nil values, and everything else are nils
    //     (note that in Lua arrays are 1-based)
    //
    int32_t m_arrayLengthIfContinuous;

    // The capacity of the array vector storage part
    //
    uint32_t m_arrayStorageCapacity;
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
