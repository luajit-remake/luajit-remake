#include "gtest/gtest.h"

#include "test_util_helper.h"
#include "tvalue.h"

TEST(Deegen, DumpTypeSpeculationDefs)
{
    std::string s = DumpHumanReadableTypeSpeculationDefinitions();
    AssertIsExpectedOutput(s);
}

TEST(Deegen, DumpTypeSpeculation)
{
    std::string s;

    s = DumpHumanReadableTypeSpeculation(x_typeMaskFor<tBottom>);
    ReleaseAssert(s == "tBottom");

    s = DumpHumanReadableTypeSpeculation(x_typeMaskFor<tBoxedValueTop>);
    ReleaseAssert(s == "tBoxedValueTop");

    s = DumpHumanReadableTypeSpeculation(x_typeMaskFor<tTable>);
    ReleaseAssert(s == "tTable");

    s = DumpHumanReadableTypeSpeculation(x_typeMaskFor<tNil> | x_typeMaskFor<tDouble>);
    ReleaseAssert(s == "tDouble | tNil");

    s = DumpHumanReadableTypeSpeculation(x_typeMaskFor<tDouble>);
    ReleaseAssert(s == "tDouble");

    s = DumpHumanReadableTypeSpeculation(x_typeMaskFor<tNil> | x_typeMaskFor<tDouble> | x_typeMaskFor<tTable>);
    ReleaseAssert(s == "tTable | tDouble | tNil");
}
