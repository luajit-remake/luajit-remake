#pragma once

#include "common_utils.h"

// Fast sorting of short fixed-length sequence using sorting network
//
struct SortingNetwork
{
    template<size_t N, typename T>
    static void SortAscend(T* a /*inout*/)
    {
        static_assert((std::is_integral_v<T> && !std::is_same_v<T, bool>) || std::is_floating_point_v<T>);
        SortImpl<true /*forAscend*/, N>(a);
    }

    template<size_t N, typename T>
    static void SortDescend(T* a /*inout*/)
    {
        static_assert((std::is_integral_v<T> && !std::is_same_v<T, bool>) || std::is_floating_point_v<T>);
        SortImpl<false /*forAscend*/, N>(a);
    }

private:
    template<bool forAscend, typename T>
    static void ALWAYS_INLINE DoCmp(T& a, T& b)
    {
        bool c = (forAscend ? (a <= b) : (a >= b));
        T tmp = a;
        a = (c) ? a : b;
        b = (c) ? b : tmp;
    }

    template<bool forAscend, typename T, auto v, size_t idx>
    static void ALWAYS_INLINE RunImpl(T* a)
    {
        if constexpr(idx + 1 < v.size())
        {
            static_assert(v[idx] != v[idx + 1]);
            DoCmp<forAscend, T>(a[v[idx]], a[v[idx + 1]]);
            RunImpl<forAscend, T, v, idx + 2>(a);
        }
        else
        {
            static_assert(idx == v.size());
        }
    }

    template<bool forAscend, size_t N, typename T>
    static void ALWAYS_INLINE RunWrapper([[maybe_unused]] T* a)
    {
        if constexpr(N >= 2)
        {
            static_assert(N < std::tuple_size_v<decltype(x_data)> + 2);
            RunImpl<forAscend, T, std::get<N - 2>(x_data), 0>(a);
        }
    }

    template<bool forAscend, size_t N, typename T>
    static void ALWAYS_INLINE SortImpl(T* a /*inout*/)
    {
        T temp[N];
        memcpy(temp, a, sizeof(T) * N);
        RunWrapper<forAscend, N>(temp);
        memcpy(a, temp, sizeof(T) * N);

#ifndef NDEBUG
        if (forAscend)
        {
            for (size_t i = 0; i + 1 < N; i++) { Assert(a[i] <= a[i + 1]); }
        }
        else
        {
            for (size_t i = 0; i + 1 < N; i++) { Assert(a[i] >= a[i + 1]); }
        }
#endif
    }

    // https://bertdobbelaere.github.io/sorting_networks.html
    //
    static constexpr auto x_data = std::make_tuple(
        // Len = 2
        std::to_array<size_t>({
            0,1
        }),

        // Len = 3
        std::to_array<size_t>({
            0,2,
            0,1,
            1,2
        }),

        // Len = 4
        std::to_array<size_t>({
            0,2,1,3,
            0,1,2,3,
            1,2
        }),

        // Len = 5
        std::to_array<size_t>({
            0,3,1,4,
            0,2,1,3,
            0,1,2,4,
            1,2,3,4,
            2,3
        }),

        // Len = 6
        std::to_array<size_t>({
            0,5,1,3,2,4,
            1,2,3,4,
            0,3,2,5,
            0,1,2,3,4,5,
            1,2,3,4
        }),

        // Len = 7
        std::to_array<size_t>({
            0,6,2,3,4,5,
            0,2,1,4,3,6,
            0,1,2,5,3,4,
            1,2,4,6,
            2,3,4,5,
            1,2,3,4,5,6
        }),

        // Len = 8
        std::to_array<size_t>({
            0,2,1,3,4,6,5,7,
            0,4,1,5,2,6,3,7,
            0,1,2,3,4,5,6,7,
            2,4,3,5,
            1,4,3,6,
            1,2,3,4,5,6
        }),

        // Len = 9
        std::to_array<size_t>({
            0,3,1,7,2,5,4,8,
            0,7,2,4,3,8,5,6,
            0,2,1,3,4,5,7,8,
            1,4,3,6,5,7,
            0,1,2,4,3,5,6,8,
            2,3,4,5,6,7,
            1,2,3,4,5,6
        }),

        // Len = 10
        std::to_array<size_t>({
            0,8,1,9,2,7,3,5,4,6,
            0,2,1,4,5,8,7,9,
            0,3,2,4,5,7,6,9,
            0,1,3,6,8,9,
            1,5,2,3,4,8,6,7,
            1,2,3,5,4,6,7,8,
            2,3,4,5,6,7,
            3,4,5,6
        }),

        // Len = 11
        std::to_array<size_t>({
            0,9,1,6,2,4,3,7,5,8,
            0,1,3,5,4,10,6,9,7,8,
            1,3,2,5,4,7,8,10,
            0,4,1,2,3,7,5,9,6,8,
            0,1,2,6,4,5,7,8,9,10,
            2,4,3,6,5,7,8,9,
            1,2,3,4,5,6,7,8,
            2,3,4,5,6,7
        }),

        // Len = 12
        std::to_array<size_t>({
            0,8,1,7,2,6,3,11,4,10,5,9,
            0,1,2,5,3,4,6,9,7,8,10,11,
            0,2,1,6,5,10,9,11,
            0,3,1,2,4,6,5,7,8,11,9,10,
            1,4,3,5,6,8,7,10,
            1,3,2,5,6,9,8,10,
            2,3,4,5,6,7,8,9,
            4,6,5,7,
            3,4,5,6,7,8
        }),

        // Len = 13
        std::to_array<size_t>({
            0,12,1,10,2,9,3,7,5,11,6,8,
            1,6,2,3,4,11,7,9,8,10,
            0,4,1,2,3,6,7,8,9,10,11,12,
            4,6,5,9,8,11,10,12,
            0,5,3,8,4,7,6,11,9,10,
            0,1,2,5,6,9,7,8,10,11,
            1,3,2,4,5,6,9,10,
            1,2,3,4,5,7,6,8,
            2,3,4,5,6,7,8,9,
            3,4,5,6
        }),

        // Len = 14
        std::to_array<size_t>({
            0,1,2,3,4,5,6,7,8,9,10,11,12,13,
            0,2,1,3,4,8,5,9,10,12,11,13,
            0,4,1,2,3,7,5,8,6,10,9,13,11,12,
            0,6,1,5,3,9,4,10,7,13,8,12,
            2,10,3,11,4,6,7,9,
            1,3,2,8,5,11,6,7,10,12,
            1,4,2,6,3,5,7,11,8,10,9,12,
            2,4,3,6,5,8,7,10,9,11,
            3,4,5,6,7,8,9,10,
            6,7
        }),

        // Len = 15
        std::to_array<size_t>({
            1,2,3,10,4,14,5,8,6,13,7,12,9,11,
            0,14,1,5,2,8,3,7,6,9,10,12,11,13,
            0,7,1,6,2,9,4,10,5,11,8,13,12,14,
            0,6,2,4,3,5,7,11,8,10,9,12,13,14,
            0,3,1,2,4,7,5,9,6,8,10,11,12,13,
            0,1,2,3,4,6,7,9,10,12,11,13,
            1,2,3,5,8,10,11,12,
            3,4,5,6,7,8,9,10,
            2,3,4,5,6,7,8,9,10,11,
            5,6,7,8
        }),

        // Len = 16
        std::to_array<size_t>({
            0,13,1,12,2,15,3,14,4,8,5,6,7,11,9,10,
            0,5,1,7,2,9,3,4,6,13,8,14,10,15,11,12,
            0,1,2,3,4,5,6,8,7,9,10,11,12,13,14,15,
            0,2,1,3,4,10,5,11,6,7,8,9,12,14,13,15,
            1,2,3,12,4,6,5,7,8,10,9,11,13,14,
            1,4,2,6,5,8,7,10,9,13,11,14,
            2,4,3,6,9,12,11,13,
            3,5,6,8,7,9,10,12,
            3,4,5,6,7,8,9,10,11,12,
            6,7,8,9
        })
    );
};
