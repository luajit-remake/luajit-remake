#include "gtest/gtest.h"

#include "tvalue.h"

TEST(Deegen, DumpTypeSpeculationDefs)
{
    std::string s = DumpHumanReadableTypeSpeculationDefinitions();

    std::string expected =
            "== Type Speculation Mask Defintions ==\n"
            "\n"
            "Defintion for each bit:\n"
            "Bit 0: tNil (0x1)\n"
            "Bit 1: tBool (0x2)\n"
            "Bit 2: tDoubleNotNaN (0x4)\n"
            "Bit 3: tDoubleNaN (0x8)\n"
            "Bit 4: tInt32 (0x10)\n"
            "Bit 5: tString (0x20)\n"
            "Bit 6: tFunction (0x40)\n"
            "Bit 7: tUserdata (0x80)\n"
            "Bit 8: tThread (0x100)\n"
            "Bit 9: tTable (0x200)\n"
            "\n"
            "Compound Mask Definitions:\n"
            "tDouble (0xc): tDoubleNotNaN | tDoubleNaN\n"
            "tHeapEntity (0x3e0): tString | tFunction | tUserdata | tThread | tTable\n"
            "tTop (0x3ff): tNil | tBool | tDoubleNotNaN | tDoubleNaN | tInt32 | tString | tFunction | tUserdata | tThread | tTable\n";

    ReleaseAssert(s == expected);
}

TEST(Deegen, DumpTypeSpeculation)
{
    std::string s;

    s = DumpHumanReadableTypeSpeculation(x_typeSpeculationMaskFor<tBottom>);
    ReleaseAssert(s == "tBottom (0x0)");

    s = DumpHumanReadableTypeSpeculation(x_typeSpeculationMaskFor<tTop>);
    ReleaseAssert(s == "tTop (0x3ff)");

    s = DumpHumanReadableTypeSpeculation(x_typeSpeculationMaskFor<tTable>);
    ReleaseAssert(s == "tTable (0x200)");

    s = DumpHumanReadableTypeSpeculation(x_typeSpeculationMaskFor<tNil> | x_typeSpeculationMaskFor<tBool>);
    ReleaseAssert(s == "tBool | tNil (0x3)");

    s = DumpHumanReadableTypeSpeculation(x_typeSpeculationMaskFor<tDouble>);
    ReleaseAssert(s == "tDouble (0xc)");

    s = DumpHumanReadableTypeSpeculation(x_typeSpeculationMaskFor<tNil> | x_typeSpeculationMaskFor<tDouble> | x_typeSpeculationMaskFor<tTable>);
    ReleaseAssert(s == "tTable | tDouble | tNil (0x20d)");
}
