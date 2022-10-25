#pragma once

#include "common_utils.h"

// The current implementation of std::tuple cannot be used for non-type template parameter
// So we hand-write this simple tuple class to workaround this limit.
// This class should only be used for NTTP purpose, since std::tuple is superior in all other aspects.
//
template<typename... T>
struct NTTPTuple
{
    static_assert(sizeof...(T) == 0);   // otherwise should have been specialized
    static constexpr size_t size() { return 0; }
    constexpr NTTPTuple() { }
};

template<typename F, typename... T>
struct NTTPTuple<F, T...>
{
    static constexpr size_t size() { return 1 + sizeof...(T); }

    constexpr NTTPTuple(F&& fv, T&&... ts)
        : f(std::forward<F>(fv))
        , rest(std::forward<T>(ts)...)
    { }

    template<size_t ord>
    constexpr const auto& get() const
    {
        if constexpr(ord == 0)
        {
            return f;
        }
        else
        {
            return rest.template get<ord - 1>();
        }
    }

    F f;
    NTTPTuple<T...> rest;
};

template<typename... T>
constexpr NTTPTuple<std::unwrap_ref_decay_t<T>...> MakeNTTPTuple(T&&... args)
{
    return NTTPTuple<std::unwrap_ref_decay_t<T>...>(std::forward<T>(args)...);
}

// Describes a piece of initialization record:
//     [offset, offset + dataLen) shall be initialized by initData
//
template<size_t dataLen>
struct DeegenMetadataInitRecord
{
    size_t offset;
    std::array<uint8_t, dataLen> initData;
};

namespace detail {

template<typename T>
struct IsValidDeegenMetadataInitDescriptor : std::false_type {};

template<size_t... dataLens>
struct IsValidDeegenMetadataInitDescriptor<NTTPTuple<DeegenMetadataInitRecord<dataLens>...>> : std::true_type {};

}   // namespace detail

template<typename T>
constexpr bool is_valid_deegen_metadata_init_descriptor_v = detail::IsValidDeegenMetadataInitDescriptor<T>::value;

namespace detail {

template<size_t objSize, auto desc>
struct InitializeDeegenMetadataOnePiece
{
    static_assert(desc.offset + desc.initData.size() <= objSize);

    static void ALWAYS_INLINE Run(uint8_t* mdPtr)
    {
        memcpy(mdPtr + desc.offset, desc.initData.data(), desc.initData.size());
    }
};

template<size_t objSize, auto desc>
struct InitializeDeegenMetadataHelper
{
    using T = decltype(desc);
    static_assert(is_valid_deegen_metadata_init_descriptor_v<T>);
    static constexpr size_t x_tuple_size = T::size();

    template<size_t ord>
    static void ALWAYS_INLINE RunImpl(uint8_t* mdPtr)
    {
        if constexpr(ord >= x_tuple_size)
        {
            return;
        }
        else
        {
            InitializeDeegenMetadataOnePiece<objSize, desc.template get<ord>()>::Run(mdPtr);
            RunImpl<ord + 1>(mdPtr);
        }
    }

    static void ALWAYS_INLINE Run(uint8_t* mdPtr)
    {
        RunImpl<0>(mdPtr);
    }
};

}   // namespace detail

template<size_t alignment, size_t size, auto initDesc>
struct alignas(alignment) DeegenMetadata
{
    static_assert(is_valid_deegen_metadata_init_descriptor_v<decltype(initDesc)>);
    static_assert(is_power_of_2(alignment));
    static_assert(size > 0 && size % alignment == 0);

    static constexpr size_t GetAlignment() { return alignment; }
    static constexpr size_t GetSize() { return size; }

    static DeegenMetadata* WARN_UNUSED CastFromAddr(void* addr)
    {
        assert(reinterpret_cast<uintptr_t>(addr) % alignment == 0);
        return reinterpret_cast<DeegenMetadata*>(addr);
    }

    void Init()
    {
        static_assert(sizeof(DeegenMetadata) == size);
        detail::InitializeDeegenMetadataHelper<size /*objSize*/, initDesc>::Run(reinterpret_cast<uint8_t*>(this));
    }

    uint8_t m_opaqueData[size];
};

namespace detail {

template<typename Tuple>
struct ComputeConstexprTraitArrayForTupleImpl
{
    template<typename TraitType, size_t ord, typename Lambda>
    static consteval void RunImpl(std::array<TraitType, std::tuple_size_v<Tuple>>& res, const Lambda& lambda)
    {
        if constexpr(ord < std::tuple_size_v<Tuple>)
        {
            res[ord] = lambda.template operator()<std::tuple_element_t<ord, Tuple>>();
            RunImpl<TraitType, ord + 1>(res, lambda);
        }
    }

    template<typename TraitType, typename Lambda>
    static consteval std::array<TraitType, std::tuple_size_v<Tuple>> Run(const Lambda& lambda)
    {
        std::array<TraitType, std::tuple_size_v<Tuple>> res;
        RunImpl<TraitType, 0>(res, lambda);
        return res;
    }
};

}   // namespace detail

template<typename TraitType, typename TupleType, typename Lambda>
consteval auto ComputeConstexprTraitArrayForTuple(const Lambda& lambda)
{
    return detail::ComputeConstexprTraitArrayForTupleImpl<TupleType>::template Run<TraitType>(lambda);
}
