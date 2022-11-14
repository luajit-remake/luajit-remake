#pragma once

#include "common_utils.h"

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

    // The initial capacity for vector part
    //
    constexpr static uint32_t x_initialVectorPartCapacity = 4;

    // The growth factor for vector part
    //
    constexpr static uint32_t x_vectorGrowthFactor = 2;

    static_assert(x_unconditionallySparseMapCutoff < std::numeric_limits<uint32_t>::max() / 2 / x_vectorGrowthFactor);
};

struct ArrayType
{
    constexpr ArrayType() : m_asValue(0) { }
    explicit constexpr ArrayType(uint8_t value) : m_asValue(value) { }

    enum class Kind : uint8_t  // must fit in 2 bits
    {
        // NoButterflyArrayPart means one of the following:
        // (1) The butterfly doesn't exist at all
        // (2) The butterfly exists but the array part contains nothing (specifically, the sparse map must not exist, but the vector storage capacity might be > 0)
        // In either case, it means any access to the array index must yeild nil
        //
        NoButterflyArrayPart,
        Int32,
        Double,
        Any
    };

    using BitFieldCarrierType = uint8_t;

    // The storage value is interpreted as the following:
    //
    // bit 0-1: Kind m_kind
    //   Whether all non-hole values in the array has the same type (including sparse map part)
    //   If m_kind is Int32 or Double, then m_mayHaveMetatable must be false
    //
    using BFM_arrayKind = BitFieldMember<BitFieldCarrierType, Kind /*type*/, 0 /*start*/, 2 /*width*/>;
    constexpr Kind ArrayKind() { return BFM_arrayKind::Get(m_asValue); }
    constexpr void SetArrayKind(Kind v) { return BFM_arrayKind::Set(m_asValue, v); }

    // bit 2: bool m_hasSparseMap
    //   Whether there is a sparse map
    //
    using BFM_hasSparseMap = BitFieldMember<BitFieldCarrierType, bool /*type*/, 2 /*start*/, 1 /*width*/>;
    constexpr bool HasSparseMap() { return BFM_hasSparseMap::Get(m_asValue); }
    constexpr void SetHasSparseMap(bool v) { return BFM_hasSparseMap::Set(m_asValue, v); }

    // bit 3: bool m_sparseMapContainsVectorIndex
    //   Whether the sparse map contains index between [x_arrayBaseOrd, MaxInt32]
    //   When this is true, the vector part can no longer grow
    //
    using BFM_sparseMapContainsVectorIndex = BitFieldMember<BitFieldCarrierType, bool /*type*/, 3 /*start*/, 1 /*width*/>;
    constexpr bool SparseMapContainsVectorIndex() { return BFM_sparseMapContainsVectorIndex::Get(m_asValue); }
    constexpr void SetSparseMapContainsVectorIndex(bool v) { return BFM_sparseMapContainsVectorIndex::Set(m_asValue, v); }

    // bit 4: bool m_isContinuous
    //   Whether the array is continuous, i.e., entry is non-nil iff index in [x_arrayBaseOrd, len). Notes:
    //   (1) If Kind is NoButterflyArrayPart, m_isContinuous is false
    //   (2) m_isContinuous = true also implies m_hasSparseMap = false
    //
    using BFM_isContinuous = BitFieldMember<BitFieldCarrierType, bool /*type*/, 4 /*start*/, 1 /*width*/>;
    constexpr bool IsContinuous() { return BFM_isContinuous::Get(m_asValue); }
    constexpr void SetIsContinuous(bool v) { return BFM_isContinuous::Set(m_asValue, v); }

    // bit 5: bool m_mayHaveMetatable
    //   Whether the object may have a non-null metatable
    //   When this is true, nil values have to be handled specially since it may invoke metatable methods
    //
    using BFM_mayHaveMetatable = BitFieldMember<BitFieldCarrierType, bool /*type*/, 5 /*start*/, 1 /*width*/>;
    constexpr bool MayHaveMetatable() { return BFM_mayHaveMetatable::Get(m_asValue); }
    constexpr void SetMayHaveMetatable(bool v) { return BFM_mayHaveMetatable::Set(m_asValue, v); }

    static constexpr ArrayType GetInitialArrayType()
    {
        return ArrayType(0);
    }

    constexpr static BitFieldCarrierType x_usefulBitsMask = static_cast<BitFieldCarrierType>((1 << 6) - 1);

    // bit 6: bool isCoroutine
    // The ArrayType field in the object header is also used by coroutine objects to accomplish 'isCoroutine + status' check in one branch
    // bit 6 is used to uniquely distinguish that the object is a coroutine.
    // So for any valid array type, bit 6 must be 0.
    //
    constexpr static uint8_t x_coroutineTypeTag = 64;

    // bit 7: bool isInvalid
    // The ArrayType field of any non-table and non-coroutine object must have bit 7 == 1 and all other bits == 0
    // So for any valid array type, bit 7 must be 0.
    //
    BitFieldCarrierType m_asValue;

    // The ArrayType field of all objects that is not a table or coroutine must have the following invalid array type
    //
    constexpr static uint8_t x_invalidArrayType = 128;

    // This must be different from all valid array types, coroutine status types and x_invalidArrayType
    // This is a bit pattern where no heap object's ArrayType field can have
    //
    // All table objects: bit 6 == 0, bit 7 == 0
    // All coroutine objects: bit 6 == 1, bit 7 == 0
    // All other objects: bit 6 == 0, bit 7 == 1
    // So bit 6 == 1, bit 7 == 1 is never possible
    //
    constexpr static uint8_t x_impossibleArrayType = 128 + 64;
};
static_assert(sizeof(ArrayType) == 1);
