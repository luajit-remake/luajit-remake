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

    s = DumpHumanReadableTypeSpeculation(x_typeSpeculationMaskFor<tBottom>);
    ReleaseAssert(s == "tBottom");

    s = DumpHumanReadableTypeSpeculation(x_typeSpeculationMaskFor<tTop>);
    ReleaseAssert(s == "tTop");

    s = DumpHumanReadableTypeSpeculation(x_typeSpeculationMaskFor<tTable>);
    ReleaseAssert(s == "tTable");

    s = DumpHumanReadableTypeSpeculation(x_typeSpeculationMaskFor<tNil> | x_typeSpeculationMaskFor<tDouble>);
    ReleaseAssert(s == "tDouble | tNil");

    s = DumpHumanReadableTypeSpeculation(x_typeSpeculationMaskFor<tDouble>);
    ReleaseAssert(s == "tDouble");

    s = DumpHumanReadableTypeSpeculation(x_typeSpeculationMaskFor<tNil> | x_typeSpeculationMaskFor<tDouble> | x_typeSpeculationMaskFor<tTable>);
    ReleaseAssert(s == "tTable | tDouble | tNil");
}
