#pragma once

// Prevent this header file from being accidentally included
//
#ifndef TEST_LLVM_CONSTANT_PARSER
#error "You shouldn't include this header file except in unit test!"
#endif

#include "common.h"

struct TestConstantParserStruct
{
    int8_t a;
    uint32_t b;
    int64_t c;
    bool d;
    uint64_t e;
    int16_t f;
};

__attribute__((__used__)) inline constexpr int testconstant1[9] { 1, 2, 3, 4 };

__attribute__((__used__)) inline constexpr int testconstant2[20][12] {
    { 1, 2 },
    { 3, 4, 5 },
    { 6, 7, 8 },
    { 9, 10, 11 },
    { 12 },
    { 14 },
    { },
    { },
    { 16, 17, 18, 19 }
};

__attribute__((__used__)) inline constexpr int testconstant3[20][12] {
    { 1, 2 },
    { 3, 4 },
    { 5, 6 },
    { 7, 8 },
    { 9, 10 },
    { 11, 12 },
    { 13, 14 },
    { 15, 16 }
};

__attribute__((__used__)) inline constexpr int testconstant4[100] { };

__attribute__((__used__)) inline constexpr int testconstant5[10][10][10] {
    { { -1 }, { -2 } },
    { { -3 }, { -4 } },
    { { -5 }, { -6 } },
    { { 7 }, { 8 } },
    { { 9 }, { 10 } },
};

__attribute__((__used__)) inline constexpr TestConstantParserStruct testconstant6 {
    .a = 1,
    .b = 3000000000U,
    .c = 10000000000000000LL,
    .d = true,
    .e = 9000000000000000000ULL,
    .f = 2333
};

__attribute__((__used__)) inline constexpr TestConstantParserStruct testconstant7 {
    .a = -1,
    .b = 1000000000U,
    .c = -10000000000000000LL,
    .d = false,
    .e = 18000000000000000000ULL,
    .f = -1234
};

__attribute__((__used__)) inline constexpr TestConstantParserStruct testconstant8 { };
