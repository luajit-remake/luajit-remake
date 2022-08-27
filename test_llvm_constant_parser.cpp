#include "gtest/gtest.h"

#include "annotated/unit_test/unit_test_ir_accessor.h"

#include "misc_llvm_helper.h"

#define TEST_LLVM_CONSTANT_PARSER
#include "annotated/unit_test/llvm_constant_parser_test.h"

using namespace llvm;
using namespace dast;

TEST(AnnotationParser, LLVMConstantParser)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::unique_ptr<Module> module = GetDeegenUnitTestLLVMIR(ctx, "llvm_constant_parser");

    {
        Constant* v = GetConstexprGlobalValue(module.get(), "testconstant1");
        LLVMConstantArrayReader reader(module.get(), v);
        ReleaseAssert(std::rank_v<decltype(testconstant1)> == 1);
        constexpr size_t dim = std::extent_v<decltype(testconstant1), 0 /*dim*/>;
        for (size_t i = 0; i < dim; i++)
        {
            ReleaseAssert(reader.GetValue<int>(i) == testconstant1[i]);
        }
    }

    {
        Constant* v = GetConstexprGlobalValue(module.get(), "testconstant2");
        LLVMConstantArrayReader reader(module.get(), v);
        ReleaseAssert(std::rank_v<decltype(testconstant2)> == 2);
        constexpr size_t dim1 = std::extent_v<decltype(testconstant2), 0 /*dim*/>;
        constexpr size_t dim2 = std::extent_v<decltype(testconstant2), 1 /*dim*/>;
        for (size_t i = 0; i < dim1; i++)
        {
            Constant* sa = reader.Get<decltype(testconstant2[0])>(i);
            LLVMConstantArrayReader saReader(module.get(), sa);
            for (size_t j = 0; j < dim2; j++)
            {
                ReleaseAssert(saReader.GetValue<int>(j) == testconstant2[i][j]);
            }
        }
    }

    {
        Constant* v = GetConstexprGlobalValue(module.get(), "testconstant3");
        LLVMConstantArrayReader reader(module.get(), v);
        ReleaseAssert(std::rank_v<decltype(testconstant3)> == 2);
        constexpr size_t dim1 = std::extent_v<decltype(testconstant3), 0 /*dim*/>;
        constexpr size_t dim2 = std::extent_v<decltype(testconstant3), 1 /*dim*/>;
        for (size_t i = 0; i < dim1; i++)
        {
            Constant* sa = reader.Get<decltype(testconstant3[0])>(i);
            LLVMConstantArrayReader saReader(module.get(), sa);
            for (size_t j = 0; j < dim2; j++)
            {
                ReleaseAssert(saReader.GetValue<int>(j) == testconstant3[i][j]);
            }
        }
    }

    {
        Constant* v = GetConstexprGlobalValue(module.get(), "testconstant4");
        LLVMConstantArrayReader reader(module.get(), v);
        ReleaseAssert(std::rank_v<decltype(testconstant4)> == 1);
        constexpr size_t dim = std::extent_v<decltype(testconstant4), 0 /*dim*/>;
        for (size_t i = 0; i < dim; i++)
        {
            ReleaseAssert(reader.GetValue<int>(i) == testconstant4[i]);
        }
    }

    {
        Constant* v = GetConstexprGlobalValue(module.get(), "testconstant5");
        LLVMConstantArrayReader reader(module.get(), v);
        ReleaseAssert(std::rank_v<decltype(testconstant5)> == 3);
        constexpr size_t dim1 = std::extent_v<decltype(testconstant5), 0 /*dim*/>;
        constexpr size_t dim2 = std::extent_v<decltype(testconstant5), 1 /*dim*/>;
        constexpr size_t dim3 = std::extent_v<decltype(testconstant5), 2 /*dim*/>;
        for (size_t i = 0; i < dim1; i++)
        {
            Constant* sa = reader.Get<decltype(testconstant5[0])>(i);
            LLVMConstantArrayReader saReader(module.get(), sa);
            for (size_t j = 0; j < dim2; j++)
            {
                Constant* sa2 = saReader.Get<decltype(testconstant5[0][0])>(j);
                LLVMConstantArrayReader sa2Reader(module.get(), sa2);
                for (size_t k = 0; k < dim3; k++)
                {
                    ReleaseAssert(sa2Reader.GetValue<int>(k) == testconstant5[i][j][k]);
                }
            }
        }
    }

    auto testStruct = [&](TestConstantParserStruct expectedVal, std::string name)
    {
        Constant* v = GetConstexprGlobalValue(module.get(), name);
        LLVMConstantStructReader reader(module.get(), v);
        ReleaseAssert(reader.GetValue<&TestConstantParserStruct::a>() == expectedVal.a);
        ReleaseAssert(reader.GetValue<&TestConstantParserStruct::b>() == expectedVal.b);
        ReleaseAssert(reader.GetValue<&TestConstantParserStruct::c>() == expectedVal.c);
        ReleaseAssert(reader.GetValue<&TestConstantParserStruct::d>() == expectedVal.d);
        ReleaseAssert(reader.GetValue<&TestConstantParserStruct::e>() == expectedVal.e);
        ReleaseAssert(reader.GetValue<&TestConstantParserStruct::f>() == expectedVal.f);
    };

    testStruct(testconstant6, "testconstant6");
    testStruct(testconstant7, "testconstant7");
    testStruct(testconstant8, "testconstant8");
}
