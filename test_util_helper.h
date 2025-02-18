#pragma once

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/DebugInfo.h"

#include "common_utils.h"
#include "gtest/gtest.h"

// Whether we are in update-expected-output mode
//
extern bool g_is_update_expected_mode;

// a bunch of utility helpers for tests

// return if string 'a' starts with 'b'
//
inline bool WARN_UNUSED StartsWith(const char* a, const char* b)
{
    size_t la = strlen(a);
    size_t lb = strlen(b);
    CHECK(la >= lb);
    return strncmp(a, b, lb) == 0;
}

namespace {

const char* const x_expected_output_dir = "test_expected_output";

inline std::string GetExpectedOutputFileNameForTestCase(const std::string& suiteName, const std::string& caseName, const std::string& suffix)
{
    return std::string(x_expected_output_dir) + std::string("/") +
        suiteName + std::string(".") + caseName +
        (suffix == "" ? "" : std::string(".") + suffix) + ".expected";
}

inline std::string GetExpectedOutputFileName(const std::string& suffix)
{
    const char* namePart1 = ::testing::UnitTest::GetInstance()->current_test_info()->test_case_name();
    const char* namePart2 = ::testing::UnitTest::GetInstance()->current_test_info()->name();
    return GetExpectedOutputFileNameForTestCase(namePart1, namePart2, suffix);
}

inline void CreateExpectedOutputFolderIfNeeded()
{
    struct stat st;
    if (stat(x_expected_output_dir, &st) == -1)
    {
        int ret = mkdir(x_expected_output_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        if (ret != 0)
        {
            printf("Creating expected output file directory failed with errno=%d (%s)\n", errno, strerror(errno));
            ReleaseAssert(false);
        }
    }
    ReleaseAssert(stat(x_expected_output_dir, &st) == 0);
}

inline size_t GetFileSize(const char *filename) {
    struct stat st;
    if (stat(filename, &st) == 0)
    {
        return static_cast<size_t>(st.st_size);
    }
    else
    {
        printf("Failed to access expected output file %s, errno=%d (%s)\n", filename, errno, strerror(errno));
        ReleaseAssert(false);
    }
}

inline void TrimTrailingWhitespaces(std::string& s, const char* t = " \t\n\r\f\v")
{
    size_t pos = s.find_last_not_of(t);
    if (pos == std::string::npos)
    {
        s = "";
    }
    else
    {
        s.erase(pos + 1);
    }
}

inline void NO_RETURN DumpExpectedAndActualAndFail(const std::string& actual, const std::string& expected)
{
    printf("!!! TEST FAILED !!!\n");
    printf("=============== Expected Output ===============\n");
    for (size_t i = 0; i < expected.length(); i++)
    {
        printf("%c", expected[i]);
    }
    printf("\n");
    printf("================ Actual Output ================\n");
    for (size_t i = 0; i < actual.length(); i++)
    {
        printf("%c", actual[i]);
    }
    printf("\n");
    printf("===============================================\n");
    ReleaseAssert(false);
}

}   // anonymous namespace

inline void AssertOutputAgreesWithExpectedOutputFile(std::string out, std::string filename)
{
    FILE* pFile = fopen(filename.c_str(), "r");
    if (pFile == nullptr)
    {
        printf("!!! TEST CONFIGURATION ISSUE !!!\n");
        printf("=============== Expected Output ===============\n");
        printf("[ERROR] Failed to open expected output file %s, errno=%d (%s)\n", filename.c_str(), errno, strerror(errno));
        printf("================ Actual Output ================\n");
        for (size_t i = 0; i < out.length(); i++)
        {
            printf("%c", out[i]);
        }
        printf("\n");
        printf("===============================================\n");
        ReleaseAssert(false);
    }
    Auto(fclose(pFile));
    size_t fileSize = GetFileSize(filename.c_str());
    std::string expected(fileSize, ' ');
    ReleaseAssert(fread(expected.data(), sizeof(char), fileSize, pFile) == fileSize);

    TrimTrailingWhitespaces(expected);
    TrimTrailingWhitespaces(out);

    if (expected != out)
    {
        DumpExpectedAndActualAndFail(out, expected);
    }
}

// Check the output against the expected output file of this test
//
inline void AssertIsExpectedOutput(const std::string& out, std::string suffix = "")
{
    if (g_is_update_expected_mode)
    {
        CreateExpectedOutputFolderIfNeeded();
        std::string filename = GetExpectedOutputFileName(suffix);
        FILE* pFile = fopen(filename.c_str(), "w");
        if (pFile == nullptr)
        {
            printf("Failed to open file %s for write, errno=%d (%s)\n", filename.c_str(), errno, strerror(errno));
            ReleaseAssert(false);
        }
        ReleaseAssert(fwrite(out.data(), sizeof(char), out.length(), pFile) == out.length());
        fclose(pFile);
        printf("[UPDATE_EXPECTED_OUTPUT] Succesfully updated expected output file.\n");
    }
    else
    {
        std::string filename = GetExpectedOutputFileName(suffix);
        AssertOutputAgreesWithExpectedOutputFile(out, filename);
    }
}

