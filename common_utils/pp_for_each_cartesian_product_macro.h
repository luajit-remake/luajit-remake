#pragma once

#include "pp_common.h"

// Macro utility 'PP_FOR_EACH' and 'PP_FOR_EACH_CARTESIAN_PRODUCT'
// Apply macro to all elements in a list, or all elements in the Cartesian product of multiple lists
// Requires C++20.
//
// ----------------
// PP_FOR_EACH(m, ...)
//     Expands to m(l) for all l in list
//     m(l) will expand further if 'm' is a macro
//
// Example: PP_FOR_EACH(m, 1, 2, 3) expands to m(1) m(2) m(3)
//
// ----------------
// PP_FOR_EACH_CARTESIAN_PRODUCT(m, lists...)
//
//     Expands to m(l) for all l in List1 * ... * ListN where * denotes Cartesian product.
//     m(l) will expand further if 'm' is a macro
//     The terms are enumerated in lexical order of the lists
//
// Example:
//     PP_FOR_EACH_CARTESIAN_PRODUCT(m, (1,2), (A,B), (x,y))
// expands to
//     m(1,A,x) m(1,A,y) m(1,B,x) m(1,B,y) m(2,A,x) m(2,A,y) m(2,B,x) m(2,B,y)
//
// The implementation is inspired by the following articles:
//     https://www.scs.stanford.edu/~dm/blog/va-opt.html
//

#if (defined(__cplusplus) && (__cplusplus >= 202002L))

#define DETAIL_ADD_COMMA_IF_NONEMPTY(...) __VA_OPT__(__VA_ARGS__ ,)
#define DETAIL_EXPAND_LIST_TRAIL_COMMA(...) DETAIL_ADD_COMMA_IF_NONEMPTY( PP_EXPAND_LIST_IMPL __VA_ARGS__ )

// Takes "a list where the first element is also a list" and a value, perform a rotation like below:
// (L1, v2, v3, v4), v5 => (v2, v3, v4, v5), expanded(L1)
//
#define DETAIL_FECP_LIST_ROTATION(a, b) DETAIL_FECP_LIST_ROTATION_IMPL1(PP_EXPAND_LIST(a), b)
#define DETAIL_FECP_LIST_ROTATION_IMPL1(...) DETAIL_FECP_LIST_ROTATION_IMPL2(__VA_ARGS__)
#define DETAIL_FECP_LIST_ROTATION_IMPL2(a, ...) (__VA_ARGS__), PP_EXPAND_LIST(a)

#define DETAIL_CARTESIAN_IMPL_ENTRY(dimLeft, macro, vpack, ...)		\
    __VA_OPT__(DETAIL_CARTESIAN_IMPL_AGAIN_2 PP_PARENS (dimLeft, macro, vpack, __VA_ARGS__))

#define DETAIL_CARTESIAN_EMIT_ONE(macro, ...) macro(__VA_ARGS__)
#define DETAIL_CARTESIAN_EMIT_ONE_PARAMS(vpack, vfirst) DETAIL_EXPAND_LIST_TRAIL_COMMA(vpack) vfirst

#define DETAIL_CARTESIAN_IMPL(dimLeft, macro, vpack, vfirst, ...)                                                           \
    PP_IF_EQUAL_ZERO(dimLeft)((                                                                                             \
        DETAIL_CARTESIAN_EMIT_ONE(macro, DETAIL_CARTESIAN_EMIT_ONE_PARAMS(vpack, vfirst))                                   \
    ), (                                                                                                                    \
        DETAIL_CARTESIAN_IMPL_ENTRY_AGAIN_2 PP_PARENS (PP_DEC(dimLeft), macro, DETAIL_FECP_LIST_ROTATION(vpack, vfirst))	\
    ))                                                                                                                      \
    __VA_OPT__(DETAIL_CARTESIAN_IMPL_AGAIN PP_PARENS (dimLeft, macro, vpack, __VA_ARGS__))

#define DETAIL_CARTESIAN_IMPL_AGAIN_2() DETAIL_CARTESIAN_IMPL_AGAIN PP_PARENS
#define DETAIL_CARTESIAN_IMPL_AGAIN() DETAIL_CARTESIAN_IMPL
#define DETAIL_CARTESIAN_IMPL_ENTRY_AGAIN_2() DETAIL_CARTESIAN_IMPL_ENTRY_AGAIN PP_PARENS
#define DETAIL_CARTESIAN_IMPL_ENTRY_AGAIN() DETAIL_CARTESIAN_IMPL_ENTRY

// FOR_EACH implementation from https://www.scs.stanford.edu/~dm/blog/va-opt.html
// See comment at beginning of this file
//
#define PP_FOR_EACH(macro, ...) __VA_OPT__(PP_EXPAND(DETAIL_FOR_EACH_HELPER(macro, __VA_ARGS__)))
#define DETAIL_FOR_EACH_HELPER(macro, a1, ...) macro(a1) __VA_OPT__(DETAIL_FOR_EACH_AGAIN PP_PARENS (macro, __VA_ARGS__))
#define DETAIL_FOR_EACH_AGAIN() DETAIL_FOR_EACH_HELPER

// Same as 'FOR_EACH', except that it expects that each element in the list is a tuple, and it unpacks the tuple before feeding to 'macro'
//
#define PP_FOR_EACH_UNPACK_TUPLE(macro, ...) __VA_OPT__(PP_EXPAND(DETAIL_FOR_EACH_UNPACK_TUPLE_HELPER(macro, __VA_ARGS__)))
#define DETAIL_FOR_EACH_UNPACK_TUPLE_HELPER(macro, a1, ...) macro a1 __VA_OPT__(DETAIL_FOR_EACH_UNPACK_TUPLE_AGAIN PP_PARENS (macro, __VA_ARGS__))
#define DETAIL_FOR_EACH_UNPACK_TUPLE_AGAIN() DETAIL_FOR_EACH_UNPACK_TUPLE_HELPER

// FOR_EACH_CARTESIAN_PRODUCT(macro, lists...)
// See comment at beginning of this file
//
#define PP_FOR_EACH_CARTESIAN_PRODUCT(macro, list1, ...)	\
    PP_EXPAND(DETAIL_CARTESIAN_IMPL_ENTRY(PP_COUNT_ARGS(__VA_ARGS__), macro, (__VA_ARGS__), PP_EXPAND_LIST(list1)))

//#define ENABLE_PP_CARTISIAN_PRODUCT_TEST
#ifdef ENABLE_PP_CARTISIAN_PRODUCT_TEST
#include <string_view>

// Sanity tests
//
#define TEST_CARTESIAN_PRODUCT_MACRO(a, b, c) mac(a,b,c)
static_assert(std::string_view(PP_STRINGIFY(PP_FOR_EACH_CARTESIAN_PRODUCT(TEST_CARTESIAN_PRODUCT_MACRO, (1,2,3),(4,5,6),(7,8,9)))) ==
    "mac(1,4,7) mac(1,4,8) mac(1,4,9) mac(1,5,7) mac(1,5,8) mac(1,5,9) mac(1,6,7) mac(1,6,8) mac(1,6,9) "
    "mac(2,4,7) mac(2,4,8) mac(2,4,9) mac(2,5,7) mac(2,5,8) mac(2,5,9) mac(2,6,7) mac(2,6,8) mac(2,6,9) "
    "mac(3,4,7) mac(3,4,8) mac(3,4,9) mac(3,5,7) mac(3,5,8) mac(3,5,9) mac(3,6,7) mac(3,6,8) mac(3,6,9)");
#undef TEST_CARTESIAN_PRODUCT_MACRO
#define TEST_CARTESIAN_PRODUCT_MACRO(a, b, c, d) mac(a,b,c,d)
static_assert(std::string_view(PP_STRINGIFY(PP_FOR_EACH_CARTESIAN_PRODUCT(TEST_CARTESIAN_PRODUCT_MACRO, (0),(1,2),(3,4),(5)))) ==
    "mac(0,1,3,5) mac(0,1,4,5) mac(0,2,3,5) mac(0,2,4,5)");
static_assert(std::string_view(PP_STRINGIFY(PP_FOR_EACH_CARTESIAN_PRODUCT(TEST_CARTESIAN_PRODUCT_MACRO, (1,2,3),(4,5,6),()))) == "");
#undef TEST_CARTESIAN_PRODUCT_MACRO
#define TEST_CARTESIAN_PRODUCT_MACRO(a) mac(a)
static_assert(std::string_view(PP_STRINGIFY(PP_FOR_EACH_CARTESIAN_PRODUCT(TEST_CARTESIAN_PRODUCT_MACRO, (1,2,3,4,5)))) ==
    "mac(1) mac(2) mac(3) mac(4) mac(5)");
static_assert(std::string_view(PP_STRINGIFY(PP_FOR_EACH(TEST_CARTESIAN_PRODUCT_MACRO, 1,2,3,4,5))) ==
    "mac(1) mac(2) mac(3) mac(4) mac(5)");
static_assert(std::string_view(PP_STRINGIFY(PP_FOR_EACH(TEST_CARTESIAN_PRODUCT_MACRO))) == "");
static_assert(std::string_view(PP_STRINGIFY(PP_FOR_EACH(TEST_CARTESIAN_PRODUCT_MACRO, 1))) == "mac(1)");
#undef TEST_CARTESIAN_PRODUCT_MACRO
#endif

#else	// __cplusplus >= 202002L
static_assert(false, "C++20 __VA_OPT__ is required for this header file to work");
#endif  // __cplusplus >= 202002L

