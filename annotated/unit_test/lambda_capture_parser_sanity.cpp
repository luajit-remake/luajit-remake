#include "common.h"
#include "api_preserve_lambda_helper.h"

struct TestStruct1
{
    int a;
};

struct TestStruct2
{
    int a;
    uint64_t b;
};

struct TestStruct3
{
    double a;
    double b;
    double c;
    int d;
};

template<typename Lambda>
void TestClosure(const Lambda& lambda)
{
    std::ignore = DeegenGetLambdaClosureAddr(lambda);
    std::ignore = DeegenGetLambdaFunctorPP(lambda);
}

extern "C" void testfn01(int x)
{
    TestStruct1 ts1; ts1.a = 1;
    TestStruct2 ts2; ts2.a = 2; ts2.b = 3;
    TestStruct3 ts3; ts3.a = 4; ts3.b = 5; ts3.c = 6; ts3.d = 7;
    double z = 1.23;
    TestClosure([ts1, &ts2, ts3, &x, z]() {
        TestStruct1 tt1; tt1.a = 8;
        TestStruct2 tt2; tt2.a = 9; tt2.b = 10;
        TestStruct3 tt3; tt3.a = 11; tt3.b = 12; tt3.c = 13; tt3.d = 14;
        TestClosure([&ts1, &tt1, ts2, &x, ts3, tt2]() {
            return ts1.a + ts2.a + ts3.a + x + tt1.a + tt2.a;
        });
        return ts1.a + ts2.a + ts3.a + x + z;
    });
}

extern "C" void testfn02(int x)
{
    TestClosure([&x]() {
        int z = x;
        TestClosure([z]() {
            return z;
        });
    });
}

extern "C" void testfn03(int x)
{
    TestClosure([&x]() {
        int z = x;
        TestClosure([&z]() {
            return z;
        });
    });
}

extern "C" void testfn04(int x)
{
    TestClosure([x]() {
        int z = x;
        TestClosure([z]() {
            return z;
        });
    });
}

extern "C" void testfn05(int x)
{
    TestClosure([x]() {
        int z = x;
        TestClosure([&z]() {
            return z;
        });
    });
}

extern "C" void testfn06(int x)
{
    TestClosure([x]() {
        TestClosure([&x]() {
            return x;
        });
    });
}

extern "C" void testfn07(int x)
{
    TestClosure([x]() {
        TestClosure([x]() {
            return x;
        });
    });
}

extern "C" void testfn08(int x)
{
    TestClosure([&x]() {
        TestClosure([x]() {
            return x;
        });
    });
}

extern "C" void testfn09(int x)
{
    TestClosure([&x]() {
        TestClosure([&x]() {
            return x;
        });
    });
}

extern "C" void testfn10()
{
    TestStruct3 ts3; ts3.a = 4; ts3.b = 5; ts3.c = 6; ts3.d = 7;
    TestClosure([&ts3]() {
        TestStruct3 tt3 = ts3;
        TestClosure([tt3]() {
            return tt3;
        });
    });
}

extern "C" void testfn11()
{
    TestStruct3 ts3; ts3.a = 4; ts3.b = 5; ts3.c = 6; ts3.d = 7;
    TestClosure([&ts3]() {
        TestStruct3 tt3 = ts3;
        TestClosure([&tt3]() {
            return tt3;
        });
    });
}

extern "C" void testfn12()
{
    TestStruct3 ts3; ts3.a = 4; ts3.b = 5; ts3.c = 6; ts3.d = 7;
    TestClosure([ts3]() {
        TestStruct3 tt3 = ts3;
        TestClosure([&tt3]() {
            return tt3;
        });
    });
}

extern "C" void testfn13()
{
    TestStruct3 ts3; ts3.a = 4; ts3.b = 5; ts3.c = 6; ts3.d = 7;
    TestClosure([ts3]() {
        TestStruct3 tt3 = ts3;
        TestClosure([tt3]() {
            return tt3;
        });
    });
}

extern "C" void testfn14()
{
    TestStruct3 ts3; ts3.a = 4; ts3.b = 5; ts3.c = 6; ts3.d = 7;
    TestClosure([&ts3]() {
        TestClosure([ts3]() {
            return ts3;
        });
    });
}

extern "C" void testfn15()
{
    TestStruct3 ts3; ts3.a = 4; ts3.b = 5; ts3.c = 6; ts3.d = 7;
    TestClosure([ts3]() {
        TestClosure([ts3]() {
            return ts3;
        });
    });
}

extern "C" void testfn16()
{
    TestStruct3 ts3; ts3.a = 4; ts3.b = 5; ts3.c = 6; ts3.d = 7;
    TestClosure([ts3]() {
        TestClosure([&ts3]() {
            return ts3;
        });
    });
}

extern "C" void testfn17()
{
    TestStruct3 ts3; ts3.a = 4; ts3.b = 5; ts3.c = 6; ts3.d = 7;
    TestClosure([&ts3]() {
        TestClosure([&ts3]() {
            return ts3;
        });
    });
}
