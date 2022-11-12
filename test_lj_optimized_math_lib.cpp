#include "gtest/gtest.h"
#include "runtime_utils.h"

static std::vector<double> GetInterestingDoubleValues(size_t scaleFactor)
{
    ReleaseAssert(scaleFactor > 0);

    std::vector<double> testValueList;
    // Make sure we cover all the special values
    //
    testValueList.push_back(0);
    testValueList.push_back(-0.0);
    testValueList.push_back(std::numeric_limits<double>::infinity());
    testValueList.push_back(-std::numeric_limits<double>::infinity());
    testValueList.push_back(std::numeric_limits<double>::quiet_NaN());
    testValueList.push_back(std::numeric_limits<double>::signaling_NaN());
    testValueList.push_back(std::numeric_limits<double>::denorm_min());
    testValueList.push_back(-std::numeric_limits<double>::denorm_min());
    testValueList.push_back(std::numeric_limits<double>::epsilon());
    testValueList.push_back(-std::numeric_limits<double>::epsilon());
    testValueList.push_back(std::numeric_limits<double>::min());
    testValueList.push_back(-std::numeric_limits<double>::min());
    testValueList.push_back(std::numeric_limits<double>::max());
    testValueList.push_back(std::numeric_limits<double>::lowest());
    testValueList.push_back(std::numeric_limits<double>::round_error());
    testValueList.push_back(-std::numeric_limits<double>::round_error());

    // Add some edge case values that could be interesting if the double is cast to integer
    //
    auto addValAround = [&](double v)
    {
        testValueList.push_back(v);
        testValueList.push_back(v+1);
        testValueList.push_back(v-1);
    };
    addValAround(static_cast<double>(std::numeric_limits<int8_t>::max()));
    addValAround(static_cast<double>(std::numeric_limits<int8_t>::min()));
    addValAround(static_cast<double>(std::numeric_limits<uint8_t>::max()));
    addValAround(-static_cast<double>(std::numeric_limits<uint8_t>::max()));
    addValAround(static_cast<double>(std::numeric_limits<int16_t>::max()));
    addValAround(static_cast<double>(std::numeric_limits<int16_t>::min()));
    addValAround(static_cast<double>(std::numeric_limits<uint16_t>::max()));
    addValAround(-static_cast<double>(std::numeric_limits<uint16_t>::max()));
    addValAround(static_cast<double>(std::numeric_limits<int32_t>::max()));
    addValAround(static_cast<double>(std::numeric_limits<int32_t>::min()));
    addValAround(static_cast<double>(std::numeric_limits<uint32_t>::max()));
    addValAround(-static_cast<double>(std::numeric_limits<uint32_t>::max()));
    addValAround(static_cast<double>(std::numeric_limits<int64_t>::max()));
    addValAround(static_cast<double>(std::numeric_limits<int64_t>::min()));
    addValAround(static_cast<double>(std::numeric_limits<uint64_t>::max()));
    addValAround(-static_cast<double>(std::numeric_limits<uint64_t>::max()));

    // Push some integer values
    //
    for (int i = -static_cast<int>(scaleFactor); i < static_cast<int>(scaleFactor); i++)
    {
        testValueList.push_back(i);
    }

    auto createRandomValuesInRange = [&](size_t num, double l, double h)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(l, h);
        for (size_t i = 0; i < num; i++)
        {
            testValueList.push_back(dis(gen));
        }
    };

    createRandomValuesInRange(scaleFactor, -1, 1);
    createRandomValuesInRange(scaleFactor, -std::numeric_limits<double>::min(), std::numeric_limits<double>::min());
    createRandomValuesInRange(scaleFactor, -std::numeric_limits<double>::epsilon(), std::numeric_limits<double>::epsilon());
    createRandomValuesInRange(scaleFactor, 0, std::numeric_limits<double>::max());
    createRandomValuesInRange(scaleFactor, std::numeric_limits<double>::lowest(), 0);
    createRandomValuesInRange(scaleFactor, -50, 50);
    createRandomValuesInRange(scaleFactor, -1000, 1000);
    createRandomValuesInRange(scaleFactor, -1e6, 1e6);
    createRandomValuesInRange(scaleFactor, -1e14, 1e14);
    createRandomValuesInRange(scaleFactor, -1e18, 1e18);
    createRandomValuesInRange(scaleFactor, -1e28, 1e28);
    createRandomValuesInRange(scaleFactor, -1e100, 1e100);

    auto createRandomValuesExpDist = [&](size_t num)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0, 709.7);
        for (size_t i = 0; i < num; i++)
        {
            testValueList.push_back(exp(dis(gen)));
        }
        for (size_t i = 0; i < num; i++)
        {
            testValueList.push_back(-exp(dis(gen)));
        }
    };

    createRandomValuesExpDist(scaleFactor);

    return testValueList;
}

// Test that we didn't screw up anything when porting LuaJIT's optimized arithmetic
// modulus operator: it should exhibit identical behavior as Lua's original version
//
TEST(Misc, LJOptimizedModulus)
{
    std::vector<double> testValueList = GetInterestingDoubleValues(20 /*scaleFactor*/);

    // For each value, put some multiples of it into the list as well
    //
    {
        std::vector<double> extraList;
        for (double v: testValueList)
        {
            extraList.push_back(v / (rand() % 8 + 2));
            extraList.push_back(v / (rand() % 8 + 20));
            extraList.push_back(v * (rand() % 8 + 2));
            extraList.push_back(v * (rand() % 8 + 20));
        }
        for (double v : extraList)
        {
            testValueList.push_back(v);
        }
    }

    // Now run the test for each value pair in the list
    //
    for (double lhs : testValueList)
    {
        for (double rhs : testValueList)
        {
            double res = ModulusWithLuaSemantics(lhs, rhs);
            double gold = NaiveModulusWithLuaSemantics_PUCLuaReference_5_1(lhs, rhs);

            bool resIsNaN = IsNaN(res);
            bool goldIsNaN = IsNaN(gold);

            uint64_t resBits = cxx2a_bit_cast<uint64_t>(res);
            uint64_t goldBits = cxx2a_bit_cast<uint64_t>(gold);

            bool ok = false;
            if (!goldIsNaN)
            {
                // If the expected result is not NaN, the actual result should be bit-equal
                //
                ok = (resBits == goldBits);
            }
            else
            {
                // Otherwise, the actual result should also be a NaN (we will check for impure NaN separately)
                //
                ok = resIsNaN;
            }

            if (!ok)
            {
                fprintf(stderr, "Error detected! lhs = %.16e (bits = 0x%llx), rhs = %.16e (bits = 0x%llx), expected %.16e (bits = 0x%llx), got %.16e (bits = 0x%llx)\n",
                        lhs, static_cast<unsigned long long>(cxx2a_bit_cast<uint64_t>(lhs)),
                        rhs, static_cast<unsigned long long>(cxx2a_bit_cast<uint64_t>(rhs)),
                        gold, static_cast<unsigned long long>(goldBits),
                        res, static_cast<unsigned long long>(resBits));
                abort();
            }

            // Just additionally sanity check that no impure NaN is produced
            //
            if (resBits >= TValue::x_int32Tag)
            {
                fprintf(stderr, "An impure NaN result %.16e (bits = 0x%llx) is produced for lhs = %.16e (bits = 0x%llx), rhs = %.16e (bits = 0x%llx)!\n",
                        res, static_cast<unsigned long long>(resBits),
                        lhs, static_cast<unsigned long long>(cxx2a_bit_cast<uint64_t>(lhs)),
                        rhs, static_cast<unsigned long long>(cxx2a_bit_cast<uint64_t>(rhs)));
                abort();
            }
        }
    }
}

TEST(Misc, LJOptimizedPow)
{
    std::vector<double> baseList = GetInterestingDoubleValues(25 /*scaleFactor*/);
    std::vector<double> exponentList = GetInterestingDoubleValues(2 /*scaleFactor*/);
    for (int i = -256; i <= 256; i++)
    {
        exponentList.push_back(i);
        exponentList.push_back(i + 0.5);
    }
    exponentList.push_back(std::numeric_limits<int32_t>::max());

    double maxRelDiff = 0;
    for (double base : baseList)
    {
        for (double exponent : exponentList)
        {
            uint64_t baseBits = cxx2a_bit_cast<uint64_t>(base);
            uint64_t exponentBits = cxx2a_bit_cast<uint64_t>(exponent);

            if (baseBits == 0x7ff4000000000000 /*signaling NaN*/ && UnsafeFloatEqual(exponent, 0.0))
            {
                continue;
            }

            double gold = pow(base, exponent);
            double res = math_fast_pow(base, exponent);

            uint64_t resBits = cxx2a_bit_cast<uint64_t>(res);
            uint64_t goldBits = cxx2a_bit_cast<uint64_t>(gold);

            bool ok = false;
            if (IsNaN(gold))
            {
                ok = IsNaN(res);
            }
            else if (std::isinf(gold))
            {
                ok = UnsafeFloatEqual(res, gold);
            }
            else
            {
                if (std::isinf(res) || IsNaN(res))
                {
                    ok = false;
                }
                else
                {
                    double diff = abs(gold - res);
                    if (diff < std::numeric_limits<double>::min())
                    {
                        ok = true;
                    }
                    else
                    {
                        double magnitude = std::max(abs(gold), abs(res));
                        ok = (diff <= magnitude * 2e-14);
                        maxRelDiff = std::max(maxRelDiff, diff / magnitude);
                    }
                }
            }
            if (!ok)
            {
                fprintf(stderr, "Error detected! base = %.16e (bits = 0x%llx), exponent = %.16e (bits = 0x%llx), expected %.16e (bits = 0x%llx), got %.16e (bits = 0x%llx)\n",
                        base, static_cast<unsigned long long>(baseBits),
                        exponent, static_cast<unsigned long long>(exponentBits),
                        gold, static_cast<unsigned long long>(goldBits),
                        res, static_cast<unsigned long long>(resBits));
                abort();
            }
        }
    }
    std::ignore = maxRelDiff;
    // printf("max relative diff = %.16e\n", maxRelDiff);
}
