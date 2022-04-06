#pragma once

#include "common.h"
#include "thirdparty_xxhash_impl.h"

// XXHash library marks various function as "unused" to workaround some GCC compiler warning
// As a result, now they are triggering new false warnings on clang...
//
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wused-but-marked-unused"

namespace CommonUtils
{

template<typename T>
uint64_t WARN_UNUSED HashPrimitiveTypes(T value)
{
    static_assert(std::is_arithmetic_v<T>, "only works for integral or floating point types");
    return XXH3_64bits(&value, sizeof(T));
}

inline uint64_t WARN_UNUSED HashString(const void* s, size_t len)
{
    return XXH3_64bits(s, len);
}

struct StringLengthAndHash
{
    size_t m_length;
    uint64_t m_hashValue;
};

// Hash a string represented by multiple pieces
//
// The iterator should provide two methods:
// (1) bool HasMore() returns true if it has not yet reached the end
// (2) std::pair<const char*, size_t> GetAndAdvance() returns the current string piece and advance the iterator
//
template<typename Iterator>
StringLengthAndHash WARN_UNUSED HashMultiPieceString(Iterator iterator)
{
    // DEVNOTE: XXH64_reset and XXH64_update has a return value for error,
    // but the implementation always return success.
    //
    // After all, it's absurd to assume that a hash function can fail (and it's
    // absurd to ask our caller to deal with it), so we don't check it.
    //
    XXH3_state_t state;
    [[maybe_unused]] XXH_errorcode err;

    err = XXH3_64bits_reset(&state);
    assert(err == XXH_OK);

    size_t totalLength = 0;
    while (iterator.HasMore())
    {
        const void* str;
        size_t len;
        std::tie(str, len) = iterator.GetAndAdvance();
        totalLength += len;
        err = XXH3_64bits_update(&state, str, len);
        assert(err == XXH_OK);
    }

    uint64_t hash = XXH3_64bits_digest(&state);
    return StringLengthAndHash {
        .m_length = totalLength,
        .m_hashValue = hash
    };
}

}   // namespace CommonUtils

#pragma clang diagnostic pop
