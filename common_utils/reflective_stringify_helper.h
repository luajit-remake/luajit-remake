#pragma once

#include <string_view>

// Intentionally defined in root namespace
//

// Returns the type of T
// WARNING: this breaks down when called outside a function.
//
template<typename T>
constexpr std::string_view __stringify_type__()
{
    constexpr const char* p = __PRETTY_FUNCTION__;
    constexpr std::string_view s(p);
    constexpr size_t prefix_len = 43;
    constexpr size_t suffix_len = 1;
    static_assert(s.substr(0, prefix_len) == "std::string_view __stringify_type__() [T = ");
    static_assert(s.substr(s.length() - suffix_len, suffix_len) == "]");
    constexpr std::string_view res(s.substr(prefix_len, s.length() - prefix_len - suffix_len));
    return res;
}

// When v is a function pointer or member function pointer, returns the value of v
// WARNING: this breaks down when called outside a function.
//
template<auto v>
constexpr std::string_view __stringify_value__()
{
    constexpr const char* p = __PRETTY_FUNCTION__;
    constexpr std::string_view s(p);
    constexpr size_t prefix_len = 45;
    constexpr size_t suffix_len = 1;
    static_assert(s.substr(0, prefix_len) == "std::string_view __stringify_value__() [v = &");
    static_assert(s.substr(s.length() - suffix_len, suffix_len) == "]");
    constexpr std::string_view res(s.substr(prefix_len, s.length() - prefix_len - suffix_len));
    return res;
}
