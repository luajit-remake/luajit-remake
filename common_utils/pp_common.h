#pragma once

#if (defined(__cplusplus) && (__cplusplus >= 202002L))

// Some of the code is from the following articles:
//     https://github.com/pfultz2/Cloak/wiki/C-Preprocessor-tricks,-tips,-and-idioms
//     https://stackoverflow.com/questions/2308243/macro-returning-the-number-of-arguments-it-is-given-in-c
//

#define PP_CAT(a, ...) PP_PRIMITIVE_CAT(a, __VA_ARGS__)
#define PP_PRIMITIVE_CAT(a, ...) a ## __VA_ARGS__

// PP_IS_EXACTLY_TWO_ARGS(...)
// Expands to 1 if exactly two parameters is passed in, otherwise expands to 0
//
#define PP_IS_EXACTLY_TWO_ARGS(...) DETAIL_GET_FIRST_ARG(__VA_OPT__(DETAIL_IS_TWO_ARGS_IMPL1(__VA_ARGS__) , ) 0)
#define DETAIL_GET_FIRST_ARG(a, ...) a
#define DETAIL_IS_TWO_ARGS_IMPL1(p1, ...) DETAIL_GET_FIRST_ARG(__VA_OPT__(DETAIL_IS_TWO_ARGS_IMPL2(__VA_ARGS__) , ) 0)
#define DETAIL_IS_TWO_ARGS_IMPL2(p1, ...) DETAIL_GET_FIRST_ARG(__VA_OPT__(0, ) 1)

// PP_IS_ZERO(x): Expands to 1 if x is 0, otherwise expands to 0
//
#define PP_IS_ZERO(x) PP_IS_EXACTLY_TWO_ARGS(PP_PRIMITIVE_CAT(DETAIL_IS_ZERO_IMPL_TAG_, x))
#define DETAIL_IS_ZERO_IMPL_TAG_0 0, 0

// PP_EXPAND_LIST((list)): Expands to list (i.e. the parenthesis is removed)
//
#define PP_EXPAND_LIST_IMPL(...) __VA_ARGS__
#define PP_EXPAND_LIST(...) PP_EXPAND_LIST_IMPL __VA_ARGS__

// PP_IF_EQUAL_ZERO(cond)((true_br), (false_br))
// Expands to true_br if cond is 0, otherwise expands to false_br
//
#define PP_IF_EQUAL_ZERO(cond) PP_CAT(DETAIL_IF_EQUAL_ZERO_IMPL_TAG_, PP_IS_ZERO(cond))
#define DETAIL_IF_EQUAL_ZERO_IMPL_TAG_1(truebr, falsebr) PP_EXPAND_LIST(truebr)
#define DETAIL_IF_EQUAL_ZERO_IMPL_TAG_0(truebr, falsebr) PP_EXPAND_LIST(falsebr)

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
// WARNING: breaks down if more than 64 arguments are passed
// https://stackoverflow.com/questions/2308243/macro-returning-the-number-of-arguments-it-is-given-in-c
//
#define PP_COUNT_ARGS(...)                   \
    DETAIL_COUNT_ARGS_IMPL(__VA_ARGS__ __VA_OPT__(,) DETAIL_COUNT_ARGS_IMPL_SEQ())
#define DETAIL_COUNT_ARGS_IMPL(...)              \
    DETAIL_COUNT_ARGS_IMPL_GET_64TH_ARG(__VA_ARGS__)
#define DETAIL_COUNT_ARGS_IMPL_GET_64TH_ARG( \
     a1, a2, a3, a4, a5, a6, a7, a8, a9,a10, \
    a11,a12,a13,a14,a15,a16,a17,a18,a19,a20, \
    a21,a22,a23,a24,a25,a26,a27,a28,a29,a30, \
    a31,a32,a33,a34,a35,a36,a37,a38,a39,a40, \
    a41,a42,a43,a44,a45,a46,a47,a48,a49,a50, \
    a51,a52,a53,a54,a55,a56,a57,a58,a59,a60, \
    a61,a62,a63,  N, ...) N
#define DETAIL_COUNT_ARGS_IMPL_SEQ()   \
    63,62,61,60,                       \
    59,58,57,56,55,54,53,52,51,50,     \
    49,48,47,46,45,44,43,42,41,40,     \
    39,38,37,36,35,34,33,32,31,30,     \
    29,28,27,26,25,24,23,22,21,20,     \
    19,18,17,16,15,14,13,12,11,10,     \
     9, 8, 7, 6, 5, 4, 3, 2, 1, 0

#define PP_PARENS ()

#define PP_EXPAND(...)      DETAIL_EXPAND4(DETAIL_EXPAND4(DETAIL_EXPAND4(DETAIL_EXPAND4(__VA_ARGS__))))
#define DETAIL_EXPAND4(...) DETAIL_EXPAND3(DETAIL_EXPAND3(DETAIL_EXPAND3(DETAIL_EXPAND3(__VA_ARGS__))))
#define DETAIL_EXPAND3(...) DETAIL_EXPAND2(DETAIL_EXPAND2(DETAIL_EXPAND2(DETAIL_EXPAND2(__VA_ARGS__))))
#define DETAIL_EXPAND2(...) DETAIL_EXPAND1(DETAIL_EXPAND1(DETAIL_EXPAND1(DETAIL_EXPAND1(__VA_ARGS__))))
#define DETAIL_EXPAND1(...) __VA_ARGS__

#define PP_STRINGIFY(x) PP_PRIMITIVE_STRINGIFY(x)
#define PP_PRIMITIVE_STRINGIFY(x) #x

// PP_GET_ARG_N(...): retrives the N-th argument
// Example: PP_GET_ARG_2(a,b,c) expands to b
//
#define PP_GET_ARG_1(a1, ...) a1
#define PP_GET_ARG_2(a1, a2, ...) a2
#define PP_GET_ARG_3(a1, a2, a3, ...) a3
#define PP_GET_ARG_4(a1, a2, a3, a4, ...) a4
#define PP_GET_ARG_5(a1, a2, a3, a4, a5, ...) a5
#define PP_GET_ARG_6(a1, a2, a3, a4, a5, a6, ...) a6
#define PP_GET_ARG_7(a1, a2, a3, a4, a5, a6, a7, ...) a7
#define PP_GET_ARG_8(a1, a2, a3, a4, a5, a6, a7, a8, ...) a8
#define PP_GET_ARG_9(a1, a2, a3, a4, a5, a6, a7, a8, a9, ...) a9
#define PP_GET_ARG_10(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, ...) a10

// PP_TUPLE_GET_N(tuple): retrieves the N-th element in the tuple
// Example: PP_TUPLE_GET_2((a,b,c)) expands to b
//
#define PP_TUPLE_GET_1(e) DETAIL_TUPLE_GET_1_IMPL(PP_EXPAND_LIST(e))
#define PP_TUPLE_GET_2(e) DETAIL_TUPLE_GET_2_IMPL(PP_EXPAND_LIST(e))
#define PP_TUPLE_GET_3(e) DETAIL_TUPLE_GET_3_IMPL(PP_EXPAND_LIST(e))
#define PP_TUPLE_GET_4(e) DETAIL_TUPLE_GET_4_IMPL(PP_EXPAND_LIST(e))
#define PP_TUPLE_GET_5(e) DETAIL_TUPLE_GET_5_IMPL(PP_EXPAND_LIST(e))
#define PP_TUPLE_GET_6(e) DETAIL_TUPLE_GET_6_IMPL(PP_EXPAND_LIST(e))
#define PP_TUPLE_GET_7(e) DETAIL_TUPLE_GET_7_IMPL(PP_EXPAND_LIST(e))
#define PP_TUPLE_GET_8(e) DETAIL_TUPLE_GET_8_IMPL(PP_EXPAND_LIST(e))
#define PP_TUPLE_GET_9(e) DETAIL_TUPLE_GET_9_IMPL(PP_EXPAND_LIST(e))
#define PP_TUPLE_GET_10(e) DETAIL_TUPLE_GET_10_IMPL(PP_EXPAND_LIST(e))

#define DETAIL_TUPLE_GET_1_IMPL(...) PP_GET_ARG_1(__VA_ARGS__)
#define DETAIL_TUPLE_GET_2_IMPL(...) PP_GET_ARG_2(__VA_ARGS__)
#define DETAIL_TUPLE_GET_3_IMPL(...) PP_GET_ARG_3(__VA_ARGS__)
#define DETAIL_TUPLE_GET_4_IMPL(...) PP_GET_ARG_4(__VA_ARGS__)
#define DETAIL_TUPLE_GET_5_IMPL(...) PP_GET_ARG_5(__VA_ARGS__)
#define DETAIL_TUPLE_GET_6_IMPL(...) PP_GET_ARG_6(__VA_ARGS__)
#define DETAIL_TUPLE_GET_7_IMPL(...) PP_GET_ARG_7(__VA_ARGS__)
#define DETAIL_TUPLE_GET_8_IMPL(...) PP_GET_ARG_8(__VA_ARGS__)
#define DETAIL_TUPLE_GET_9_IMPL(...) PP_GET_ARG_9(__VA_ARGS__)
#define DETAIL_TUPLE_GET_10_IMPL(...) PP_GET_ARG_10(__VA_ARGS__)

// PP_OPTIONAL_DEFAULT_PARAM(defaultParam, ...): If '...' is empty, expands to 'defaultParam', otherwise expands to '...'
// Example: PP_OPTIONAL_DEFAULT_PARAM(a) expands to a, PP_OPTIONAL_DEFAULT_PARAM(a, b, c) expands to b, c
//
#define PP_OPTIONAL_DEFAULT_PARAM(defaultParam, ...)    \
    PP_IF_EQUAL_ZERO(PP_COUNT_ARGS(__VA_ARGS__))((      \
        defaultParam                                    \
    ), (                                                \
        __VA_ARGS__                                     \
    ))

#else	// __cplusplus >= 202002L
static_assert(false, "C++20 __VA_OPT__ is required for this header file to work");
#endif  // __cplusplus >= 202002L

