#pragma once

#include "common_utils.h"

// Solves the following use case: a constexpr array is collectively built by a bunch of functions,
// where each function populates its own (non-overlapping) area in the array,
// and each element in the array is guaranteed to be populated in the end.
//
// For Deegen internal use (generated C++ code) only, so all names are hardcoded.
//
template<typename T, size_t N, template<typename> class... funcs>
struct constexpr_multipart_array_builder_helper
{
    struct impl
    {
        consteval impl()
        {
            m_populated.fill(false);
        }

        consteval void set(size_t ord, T value)
        {
            ReleaseAssert(ord < N);
            ReleaseAssert(!m_populated[ord]);
            m_populated[ord] = true;
            m_data[ord] = value;
        }

        consteval std::array<T, N> get_result()
        {
            for (size_t i = 0; i < N; i++)
            {
                ReleaseAssert(m_populated[i]);
            }
            return m_data;
        }

        std::array<bool, N> m_populated;
        std::array<T, N> m_data;
    };

    template<template<typename> class FirstFn, template<typename> class... RemainingFns>
    struct run_funcs
    {
        static consteval void run(impl* p)
        {
            FirstFn<impl>::run(p);
            if constexpr(sizeof...(RemainingFns) > 0)
            {
                run_funcs<RemainingFns...>::run(p);
            }
        }
    };

    static consteval std::array<T, N> get()
    {
        impl p;
        if constexpr(sizeof...(funcs) > 0)
        {
            run_funcs<funcs...>::run(&p);
        }
        return p.get_result();
    }
};
