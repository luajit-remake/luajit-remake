#include "gtest/gtest.h"

#include "common_utils.h"
#include "drt/sorting_network.h"

namespace {

struct SortingNetworkFnArrayGetter
{
    template<size_t len>
    constexpr void Init()
    {
        m_ascendSorter[len] = SortingNetwork::SortAscend<len>;
        m_descendSorter[len] = SortingNetwork::SortDescend<len>;
        if constexpr(len < SortingNetwork::x_maxLength)
        {
            Init<len + 1>();
        }
    }

    constexpr SortingNetworkFnArrayGetter()
    {
        m_ascendSorter[0] = nullptr;
        m_descendSorter[0] = nullptr;
        Init<1>();
    }

    using FnTy = void(*)(uint32_t*);
    std::array<FnTy, SortingNetwork::x_maxLength + 1> m_ascendSorter;
    std::array<FnTy, SortingNetwork::x_maxLength + 1> m_descendSorter;
};

TEST(SortingNetwork, Sanity)
{
    std::random_device rd;
    std::mt19937_64 rdgen(rd());

    constexpr SortingNetworkFnArrayGetter fn;

    for (size_t len = 1; len <= SortingNetwork::x_maxLength; len++)
    {
        std::vector<uint32_t> data;
        data.resize(len);
        for (size_t iter = 0; iter < 500; iter++)
        {
            for (size_t i = 0; i < len; i++)
            {
                data[i] = static_cast<uint32_t>(rdgen());
            }
            fn.m_ascendSorter[len](data.data());
            for (size_t i = 1; i < len; i++)
            {
                ReleaseAssert(data[i] >= data[i - 1]);
            }

            for (size_t i = 0; i < len; i++)
            {
                data[i] = static_cast<uint32_t>(rdgen());
            }
            fn.m_descendSorter[len](data.data());
            for (size_t i = 1; i < len; i++)
            {
                ReleaseAssert(data[i] <= data[i - 1]);
            }
        }
    }
}

}   // anonymous namespace
