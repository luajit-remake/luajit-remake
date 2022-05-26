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

// PP_IS_ONE(x): Expands to 1 if x is 1, otherwise expands to 0
//
#define PP_IS_ONE(x) PP_IS_EXACTLY_TWO_ARGS(PP_PRIMITIVE_CAT(DETAIL_IS_ONE_IMPL_TAG_, x))
#define DETAIL_IS_ONE_IMPL_TAG_1 0, 0

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

// PP_IF_EQUAL_ONE(cond)((true_br), (false_br))
// Expands to true_br if cond is 1, otherwise expands to false_br
//
#define PP_IF_EQUAL_ONE(cond) PP_CAT(DETAIL_IF_EQUAL_ONE_IMPL_TAG_, PP_IS_ONE(cond))
#define DETAIL_IF_EQUAL_ONE_IMPL_TAG_1(truebr, falsebr) PP_EXPAND_LIST(truebr)
#define DETAIL_IF_EQUAL_ONE_IMPL_TAG_0(truebr, falsebr) PP_EXPAND_LIST(falsebr)

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

#define PP_NATURAL_NUMBERS_LIST                                                                                             \
    0 , 1 , 2 , 3 , 4 , 5 , 6 , 7 , 8 , 9 , 10 , 11 , 12 , 13 , 14 , 15 , 16 , 17 , 18 , 19 , 20 ,                          \
    21 , 22 , 23 , 24 , 25 , 26 , 27 , 28 , 29 , 30 , 31 , 32 , 33 , 34 , 35 , 36 , 37 , 38 , 39 , 40 ,                     \
    41 , 42 , 43 , 44 , 45 , 46 , 47 , 48 , 49 , 50 , 51 , 52 , 53 , 54 , 55 , 56 , 57 , 58 , 59 , 60 ,                     \
    61 , 62 , 63 , 64 , 65 , 66 , 67 , 68 , 69 , 70 , 71 , 72 , 73 , 74 , 75 , 76 , 77 , 78 , 79 , 80 ,                     \
    81 , 82 , 83 , 84 , 85 , 86 , 87 , 88 , 89 , 90 , 91 , 92 , 93 , 94 , 95 , 96 , 97 , 98 , 99 , 100 ,                    \
    101 , 102 , 103 , 104 , 105 , 106 , 107 , 108 , 109 , 110 , 111 , 112 , 113 , 114 , 115 , 116 , 117 , 118 , 119 , 120 , \
    121 , 122 , 123 , 124 , 125 , 126 , 127 , 128 , 129 , 130 , 131 , 132 , 133 , 134 , 135 , 136 , 137 , 138 , 139 , 140 , \
    141 , 142 , 143 , 144 , 145 , 146 , 147 , 148 , 149 , 150 , 151 , 152 , 153 , 154 , 155 , 156 , 157 , 158 , 159 , 160 , \
    161 , 162 , 163 , 164 , 165 , 166 , 167 , 168 , 169 , 170 , 171 , 172 , 173 , 174 , 175 , 176 , 177 , 178 , 179 , 180 , \
    181 , 182 , 183 , 184 , 185 , 186 , 187 , 188 , 189 , 190 , 191 , 192 , 193 , 194 , 195 , 196 , 197 , 198 , 199 , 200

// Expands to 1 if the tuple is non-empty, 0 otherwise
// E.g. PP_IS_TUPLE_NONEMPTY(()) expands to 0, PP_IS_TUPLE_NONEMPTY((1, 2)) expands to 1
//
#define PP_IS_TUPLE_NONEMPTY(tup)           \
    PP_IF_EQUAL_ZERO(PP_COUNT_ARGS tup)((   \
        0                                   \
    ), (                                    \
        1                                   \
    ))

// Expands to 1 if the tuple contains exactly one element, 0 otherwise
// E.g. PP_IS_TUPLE_LENGTH_ONE(()) expands to 0,
//      PP_IS_TUPLE_LENGTH_ONE((a)) expands to 1,
//      PP_IS_TUPLE_LENGTH_ONE((1, 2)) expands to 0
//
#define PP_IS_TUPLE_LENGTH_ONE(tup)         \
    PP_IF_EQUAL_ONE(PP_COUNT_ARGS tup)((    \
        1                                   \
    ), (                                    \
        0                                   \
    ))

// Given two lists (a1, ..., an) and (b1, ... bm)
// Expands to (a1, b1), (a2, b2), .. , (ak, bk) where k = min(n, m)
// E.g. PP_ZIP_TWO_LISTS((a, b), (1, 2, 3)) expands to (a, 1), (b, 2)
//
#define PP_ZIP_TWO_LISTS(lista, listb) PP_EXPAND(DETAIL_ZIP_TWO_LISTS_IMPL(lista, listb))

#define DETAIL_ZIP_POP(a1, ...) __VA_OPT__( (__VA_ARGS__) , )
#define DETAIL_ZIP_TWO_LISTS_IMPL(tuplea, tupleb)                   \
    PP_IF_EQUAL_ONE(PP_IS_TUPLE_NONEMPTY(tuplea))((                 \
        PP_IF_EQUAL_ONE(PP_IS_TUPLE_NONEMPTY(tupleb))((             \
            (PP_TUPLE_GET_1(tuplea), PP_TUPLE_GET_1(tupleb))        \
            PP_IF_EQUAL_ZERO(PP_IS_TUPLE_LENGTH_ONE(tuplea))((      \
                PP_IF_EQUAL_ZERO(PP_IS_TUPLE_LENGTH_ONE(tupleb))((  \
                    ,                                               \
                ), (                                                \
                ))                                                  \
            ), (                                                    \
            ))                                                      \
        ), (                                                        \
        ))                                                          \
    ), (                                                            \
    ))                                                              \
    DETAIL_ZIP_TWO_LISTS_HELPER_AGAIN PP_PARENS                     \
        (DETAIL_ZIP_POP tuplea DETAIL_ZIP_POP tupleb 1)

#define DETAIL_ZIP_TWO_LISTS_HELPER_AGAIN() DETAIL_ZIP_TWO_LISTS_HELPER
#define DETAIL_ZIP_TWO_LISTS_HELPER(lista, ...)	__VA_OPT__(DETAIL_ZIP_TWO_LISTS_HELPER_2(lista, __VA_ARGS__))
#define DETAIL_ZIP_TWO_LISTS_HELPER_2(lista, listb, ...) __VA_OPT__(DETAIL_ZIP_TWO_LISTS_IMPL(lista, listb))

#else	// __cplusplus >= 202002L
static_assert(false, "C++20 __VA_OPT__ is required for this header file to work");
#endif  // __cplusplus >= 202002L

