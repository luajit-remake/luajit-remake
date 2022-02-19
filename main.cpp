#include <cstdio>
#include <cstdlib> 
#include "gtest/gtest.h"

#include "test_util_helper.h"

bool g_is_update_expected_mode = false;

namespace {

void PrintInformation()
{
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
    printf("--------------- General Information ---------------\n");
    printf("Host:         ");
    fflush(stdout);
    std::ignore = system("whoami | tr -d '\\n' && printf '@' && cat /etc/hostname");
    printf("Build flavor: %s\n", TOSTRING(BUILD_FLAVOR));
    printf("---------------------------------------------------\n");
#undef TOSTRING
#undef STRINGIFY
}

}	// annoymous namespace

int main(int argc, char **argv)
{
    PrintInformation();

    llvm::InitLLVM X(argc, argv);
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    ::testing::InitGoogleTest(&argc, argv);

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--update-expected") == 0)
        {
            g_is_update_expected_mode = true;
        }
        else
        {
            printf("Unknown command-line argument: %s\n", argv[i]);
            ReleaseAssert(false);
        }
    }

    return RUN_ALL_TESTS();
}

