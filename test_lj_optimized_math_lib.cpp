#include "gtest/gtest.h"
#include "runtime_utils.h"

// Test that we didn't screw up anything when porting LuaJIT's optimized arithmetic
// modulus operator: it should exhibit identical behavior as Lua's original version
//
TEST(Misc, LJOptimizedModulus)
{
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

    // Push some integer values
    //
    for (int i = -25; i < 25; i++)
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

    createRandomValuesInRange(25, -1, 1);
    createRandomValuesInRange(25, -std::numeric_limits<double>::min(), std::numeric_limits<double>::min());
    createRandomValuesInRange(25, -std::numeric_limits<double>::epsilon(), std::numeric_limits<double>::epsilon());
    createRandomValuesInRange(25, 0, std::numeric_limits<double>::max());
    createRandomValuesInRange(25, std::numeric_limits<double>::lowest(), 0);
    createRandomValuesInRange(25, -1e14, 1e14);
    createRandomValuesInRange(25, -1e18, 1e18);
    createRandomValuesInRange(25, -1e28, 1e28);
    createRandomValuesInRange(25, -1e100, 1e100);

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

    createRandomValuesExpDist(25);

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

    ReleaseAssert(testValueList.size() == 1705);

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
