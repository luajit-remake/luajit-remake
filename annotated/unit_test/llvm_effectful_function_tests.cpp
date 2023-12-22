#include "common.h"

extern "C" void NO_INLINE f1(uint64_t* a)
{
    *a = 123;
}

extern "C" uint64_t NO_INLINE f2(uint64_t a, uint64_t b)
{
    return a + b;
}

extern "C" uint64_t NO_INLINE f3(uint64_t a)
{
    uint64_t b;
    f1(&b);
    return a + b;
}

extern "C" uint64_t NO_INLINE f4(uint64_t* a, uint64_t* b)
{
    return *a + *b;
}

extern "C" void NO_INLINE f5(void(*a)(uint64_t), uint64_t b)
{
    a(b);
}

extern "C" uint64_t NO_INLINE f6(uint64_t a)
{
    uint64_t b[10];
    f1(b);
    f1(b + 1);
    f1(b + 2);
    f1(b + 3);
    f1(b + 4);
    return a + b[0] + b[1] + b[2] + b[3] + b[4];
}

extern "C" void NO_INLINE f7(uint64_t* a, uint64_t* b)
{
    a[1] = a[2] + b[3];
}

extern "C" void e1(uint64_t* a);

extern "C" uint64_t NO_INLINE f8(uint64_t a, uint64_t b)
{
    uint64_t c;
    e1(&c);
    return a + b + c;
}

extern "C" uint64_t NO_INLINE f9(uint64_t* a, uint64_t* b)
{
    e1(a);
    return *a + *b;
}

