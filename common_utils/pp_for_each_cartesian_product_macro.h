#pragma once

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
//     https://github.com/pfultz2/Cloak/wiki/C-Preprocessor-tricks,-tips,-and-idioms
//     https://stackoverflow.com/questions/2308243/macro-returning-the-number-of-arguments-it-is-given-in-c
//

#if (defined(__cplusplus) && (__cplusplus >= 202002L))

#define PP_CAT(a, ...) PP_PRIMITIVE_CAT(a, __VA_ARGS__)
#define PP_PRIMITIVE_CAT(a, ...) a ## __VA_ARGS__

// PP_IS_EXACTLY_TWO_ARGS(...)
// Expands to 1 if exactly two parameters is passed in, otherwise expands to 0
//
#define PP_IS_EXACTLY_TWO_ARGS(...) PP_GET_FIRST_ARG(__VA_OPT__(PP_IS_TWO_ARGS_IMPL1(__VA_ARGS__) , ) 0)
#define PP_GET_FIRST_ARG(a, ...) a
#define PP_IS_TWO_ARGS_IMPL1(p1, ...) PP_GET_FIRST_ARG(__VA_OPT__(PP_IS_TWO_ARGS_IMPL2(__VA_ARGS__) , ) 0)
#define PP_IS_TWO_ARGS_IMPL2(p1, ...) PP_GET_FIRST_ARG(__VA_OPT__(0, ) 1)

// PP_IS_ZERO(x): Expands to 1 if x is 0, otherwise expands to 0
//
#define PP_IS_ZERO(x) PP_IS_EXACTLY_TWO_ARGS(PP_PRIMITIVE_CAT(PP_IS_ZERO_IMPL_, x))
#define PP_IS_ZERO_IMPL_0 0, 0

// PP_EXPAND_LIST((list)): Expands to list (i.e. the parenthesis is removed)
//
#define PP_EXPAND_LIST_IMPL(...) __VA_ARGS__
#define PP_EXPAND_LIST(...) PP_EXPAND_LIST_IMPL __VA_ARGS__
#define PP_ADD_COMMA_IF_NONEMPTY(...) __VA_OPT__(__VA_ARGS__ ,)
#define PP_EXPAND_LIST_TRAIL_COMMA(...) PP_ADD_COMMA_IF_NONEMPTY( PP_EXPAND_LIST_IMPL __VA_ARGS__ )

// PP_IF_EQUAL_ZERO(cond)((true_br), (false_br))
// Expands to true_br if cond is 0, otherwise expands to false_br
//
#define PP_IF_EQUAL_ZERO(cond) PP_CAT(PP_IF_EQUAL_ZERO_IMPL_, PP_IS_ZERO(cond))
#define PP_IF_EQUAL_ZERO_IMPL_1(truebr, falsebr) PP_EXPAND_LIST(truebr)
#define PP_IF_EQUAL_ZERO_IMPL_0(truebr, falsebr) PP_EXPAND_LIST(falsebr)

// PP_INC(x) increments x
//
#define PP_INC(x) PP_PRIMITIVE_CAT(PP_INC_, x)
#define PP_INC_0 1
#define PP_INC_1 2
#define PP_INC_2 3
#define PP_INC_3 4
#define PP_INC_4 5
#define PP_INC_5 6
#define PP_INC_6 7
#define PP_INC_7 8
#define PP_INC_8 9
#define PP_INC_9 10
#define PP_INC_10 11
#define PP_INC_11 12
#define PP_INC_12 13
#define PP_INC_13 14
#define PP_INC_14 15
#define PP_INC_15 16
#define PP_INC_16 17
#define PP_INC_17 18
#define PP_INC_18 19
#define PP_INC_19 19

// PP_DEC(x) decrements x
//
#define PP_DEC(x) PP_PRIMITIVE_CAT(PP_DEC_, x)
#define PP_DEC_0 0
#define PP_DEC_1 0
#define PP_DEC_2 1
#define PP_DEC_3 2
#define PP_DEC_4 3
#define PP_DEC_5 4
#define PP_DEC_6 5
#define PP_DEC_7 6
#define PP_DEC_8 7
#define PP_DEC_9 8
#define PP_DEC_10 9
#define PP_DEC_11 10
#define PP_DEC_12 11
#define PP_DEC_13 12
#define PP_DEC_14 13
#define PP_DEC_15 14
#define PP_DEC_16 15
#define PP_DEC_17 16
#define PP_DEC_18 17
#define PP_DEC_19 18

// PP_COUNT_ARGS(...): returns the total number of arguments
// https://stackoverflow.com/questions/2308243/macro-returning-the-number-of-arguments-it-is-given-in-c
//
#define PP_COUNT_ARGS(...)                   \
    PP_COUNT_ARGS_IMPL(__VA_ARGS__ __VA_OPT__(,) PP_COUNT_ARGS_IMPL_SEQ())
#define PP_COUNT_ARGS_IMPL(...)              \
    PP_COUNT_ARGS_IMPL_GET_64TH_ARG(__VA_ARGS__)
#define PP_COUNT_ARGS_IMPL_GET_64TH_ARG(     \
     a1, a2, a3, a4, a5, a6, a7, a8, a9,a10, \
    a11,a12,a13,a14,a15,a16,a17,a18,a19,a20, \
    a21,a22,a23,a24,a25,a26,a27,a28,a29,a30, \
    a31,a32,a33,a34,a35,a36,a37,a38,a39,a40, \
    a41,a42,a43,a44,a45,a46,a47,a48,a49,a50, \
    a51,a52,a53,a54,a55,a56,a57,a58,a59,a60, \
    a61,a62,a63,  N, ...) N
#define PP_COUNT_ARGS_IMPL_SEQ()   \
    63,62,61,60,                   \
    59,58,57,56,55,54,53,52,51,50, \
    49,48,47,46,45,44,43,42,41,40, \
    39,38,37,36,35,34,33,32,31,30, \
    29,28,27,26,25,24,23,22,21,20, \
    19,18,17,16,15,14,13,12,11,10, \
     9, 8, 7, 6, 5, 4, 3, 2, 1, 0

// Takes "a list where the first element is also a list" and a value, perform a rotation like below:
// (L1, v2, v3, v4), v5 => (v2, v3, v4, v5), expanded(L1)
//
#define PP_LIST_ROTATION(a, b) PP_LIST_ROTATION_IMPL1(PP_EXPAND_LIST(a), b)
#define PP_LIST_ROTATION_IMPL1(...) PP_LIST_ROTATION_IMPL2(__VA_ARGS__)
#define PP_LIST_ROTATION_IMPL2(a, ...) (__VA_ARGS__), PP_EXPAND_LIST(a)

#define PP_PARENS ()

#define PP_CARTESIAN_IMPL_ENTRY(dimLeft, macro, vpack, ...)		\
    __VA_OPT__(PP_CARTESIAN_IMPL_AGAIN_2 PP_PARENS (dimLeft, macro, vpack, __VA_ARGS__))

#define PP_CARTESIAN_EMIT_ONE(macro, ...) macro(__VA_ARGS__)
#define PP_CARTESIAN_EMIT_ONE_PARAMS(vpack, vfirst) PP_EXPAND_LIST_TRAIL_COMMA(vpack) vfirst

#define PP_CARTESIAN_IMPL(dimLeft, macro, vpack, vfirst, ...)                                               \
    PP_IF_EQUAL_ZERO(dimLeft)((                                                                             \
        PP_CARTESIAN_EMIT_ONE(macro, PP_CARTESIAN_EMIT_ONE_PARAMS(vpack, vfirst))                           \
    ), (                                                                                                    \
        PP_CARTESIAN_IMPL_ENTRY_AGAIN_2 PP_PARENS (PP_DEC(dimLeft), macro, PP_LIST_ROTATION(vpack, vfirst))	\
    ))                                                                                                      \
    __VA_OPT__(PP_CARTESIAN_IMPL_AGAIN PP_PARENS (dimLeft, macro, vpack, __VA_ARGS__))

#define PP_CARTESIAN_IMPL_AGAIN_2() PP_CARTESIAN_IMPL_AGAIN PP_PARENS
#define PP_CARTESIAN_IMPL_AGAIN() PP_CARTESIAN_IMPL
#define PP_CARTESIAN_IMPL_ENTRY_AGAIN_2() PP_CARTESIAN_IMPL_ENTRY_AGAIN PP_PARENS
#define PP_CARTESIAN_IMPL_ENTRY_AGAIN() PP_CARTESIAN_IMPL_ENTRY

#define PP_EXPAND(...)  PP_EXPAND4(PP_EXPAND4(PP_EXPAND4(PP_EXPAND4(__VA_ARGS__))))
#define PP_EXPAND4(...) PP_EXPAND3(PP_EXPAND3(PP_EXPAND3(PP_EXPAND3(__VA_ARGS__))))
#define PP_EXPAND3(...) PP_EXPAND2(PP_EXPAND2(PP_EXPAND2(PP_EXPAND2(__VA_ARGS__))))
#define PP_EXPAND2(...) PP_EXPAND1(PP_EXPAND1(PP_EXPAND1(PP_EXPAND1(__VA_ARGS__))))
#define PP_EXPAND1(...) __VA_ARGS__

// FOR_EACH implementation from https://www.scs.stanford.edu/~dm/blog/va-opt.html
// See comment at beginning of this file
//
#define PP_FOR_EACH(macro, ...) __VA_OPT__(PP_EXPAND(PP_FOR_EACH_HELPER(macro, __VA_ARGS__)))
#define PP_FOR_EACH_HELPER(macro, a1, ...) macro(a1) __VA_OPT__(PP_FOR_EACH_AGAIN PP_PARENS (macro, __VA_ARGS__))
#define PP_FOR_EACH_AGAIN() PP_FOR_EACH_HELPER

// FOR_EACH_CARTESIAN_PRODUCT(macro, lists...)
// See comment at beginning of this file
//
#define PP_FOR_EACH_CARTESIAN_PRODUCT(macro, list1, ...)	\
    PP_EXPAND(PP_CARTESIAN_IMPL_ENTRY(PP_COUNT_ARGS(__VA_ARGS__), macro, (__VA_ARGS__), PP_EXPAND_LIST(list1)))

#define PP_STRINGIFY(x) PP_PRIMITIVE_STRINGIFY(x)
#define PP_PRIMITIVE_STRINGIFY(x) #x

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

