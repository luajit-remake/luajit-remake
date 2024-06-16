#include "runtime_utils.h"
#include "gtest/gtest.h"

TEST(NaNBoxing, Correctness)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());

    // test int
    //
    for (int testcase = 0; testcase < 100000; testcase++)
    {
        int x1 = rand() % 65536;
        int x2 = rand() % 65536;
        uint32_t v = (static_cast<uint32_t>(x1) << 16) + static_cast<uint32_t>(x2);
        int32_t value = static_cast<int32_t>(v);

        TValue r = TValue::CreateInt32(value);
        ReleaseAssert(r.IsInt32());
        ReleaseAssert(!r.IsMIV());
        ReleaseAssert(!r.IsDouble());
        ReleaseAssert(!r.IsPointer());
        ReleaseAssert(r.AsInt32() == value);
    }

    // test MIV
    //
    {
        auto testValue = [](MiscImmediateValue value)
        {
            TValue r = TValue::CreateMIV(value);
            ReleaseAssert(!r.IsInt32());
            ReleaseAssert(r.IsMIV());
            ReleaseAssert(!r.IsDouble());
            ReleaseAssert(!r.IsPointer());
            ReleaseAssert(r.AsMIV().m_value == value.m_value);
        };

        testValue(MiscImmediateValue { MiscImmediateValue::x_nil });
        testValue(MiscImmediateValue { MiscImmediateValue::x_false });
        testValue(MiscImmediateValue { MiscImmediateValue::x_true });

    }

    // test double
    //
    {
        auto testValue = [](double value)
        {
            TValue r = TValue::CreateDouble(value);
            ReleaseAssert(!r.IsInt32());
            ReleaseAssert(!r.IsMIV());
            ReleaseAssert(r.IsDouble());
            ReleaseAssert(!r.IsPointer());
            if (std::isnan(value))
            {
                ReleaseAssert(std::isnan(r.AsDouble()));
            }
            else
            {
                SUPRESS_FLOAT_EQUAL_WARNING(ReleaseAssert(r.AsDouble() == value);)
            }
        };
        auto testWithRange = [&testValue](double lb, double ub)
        {
            std::uniform_real_distribution<double> unif(lb, ub);
            std::default_random_engine re;
            for (int testcase = 0; testcase < 50000; testcase++)
            {
                double value = unif(re);
                testValue(value);
            }
        };
        testWithRange(-1e300, 1e300);
        testWithRange(-10000, 10000);
        testWithRange(-1, 1);
        testWithRange(-1e-30, 1e-30);
        testWithRange(-1e-305, 1e-305);
        testWithRange(-1e-315, 1e-315);
        testWithRange(-1e-321, 1e-321);
        testValue(std::numeric_limits<double>::infinity());
        testValue(-std::numeric_limits<double>::infinity());
        testValue(std::numeric_limits<double>::quiet_NaN());
        testValue(0);
        testValue(-0);
    }

    // test pointer
    //
    {
        for (int testcase = 0; testcase < 100000; testcase++)
        {
            int x1 = rand() % 65536;
            int x2 = rand() % 65536;
            uint32_t v = (static_cast<uint32_t>(x1) << 16) + static_cast<uint32_t>(x2);
            v &= ~7U;

            int64_t ptr = static_cast<int64_t>(0xFFFFFFFE00000000ULL + static_cast<uint32_t>(v));
            UserHeapPointer<void> value { TranslateToRawPointer(reinterpret_cast<HeapPtr<void>>(ptr)) };

            TValue r = TValue::CreatePointer(value);
            ReleaseAssert(!r.IsInt32());
            ReleaseAssert(!r.IsMIV());
            ReleaseAssert(!r.IsDouble());
            ReleaseAssert(r.IsPointer());
            ReleaseAssert(r.AsPointer() == value);
        }
    }
}
