#include "invoke_clang_helper.h"
#include "read_file.h"

namespace dast {

static std::string WARN_UNUSED GetRandomFileName()
{
    std::string res = "";
    for (size_t i = 0; i < 10; i++)
    {
        char ch = 'a' + rand() % 26;
        res += ch;
    }
    return res;
}

// Return nullptr if the filename already exists!
//
static FILE* WARN_UNUSED TryCreateTmpFile(const std::string& filename)
{
    // Use O_CREAT | O_EXCL to make sure 'open' will fail if the file already exists
    //
    int fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (fd == -1)
    {
        ReleaseAssert(errno == EEXIST);
        return nullptr;
    }
    FILE* file = fdopen(fd, "w");   // ownership of 'fd' is transferred to 'file'
    ReleaseAssert(file != nullptr);
    return file;
}

static bool WARN_UNUSED IsFileExists(const char* filename)
{
    struct stat buf;
    return (stat(filename, &buf) == 0);
}

std::string WARN_UNUSED CompileAssemblyFileToObjectFile(const std::string& asmFileContents, const std::string& extraCmdlineArgs)
{
    FILE* fp = nullptr;
    std::string fileBodyName;
    std::string asmFilePath, objFilePath;
    while (true)
    {
        fileBodyName = GetRandomFileName();
        asmFilePath = std::string("/tmp/") + fileBodyName + ".s";
        objFilePath = std::string("/tmp/") + fileBodyName + ".o";
        fp = TryCreateTmpFile(asmFilePath);
        if (fp != nullptr) { break; }
    }
    ReleaseAssert(fp != nullptr);

    ReleaseAssert(fwrite(asmFileContents.data(), 1, asmFileContents.length(), fp) == asmFileContents.length());
    ReleaseAssert(fclose(fp) == 0);
    fp = nullptr;

    ReleaseAssert(!IsFileExists(objFilePath.c_str()));

    std::string cmd =
        std::string("clang -O3 -Weverything -Werror ")
        + extraCmdlineArgs
        + " -o " + objFilePath
        + " -c "
        + asmFilePath;

    int retVal = system(cmd.c_str());
    if (retVal != 0)
    {
        fprintf(stderr, "Input ASM file: %s\n", asmFileContents.c_str());
        fprintf(stderr, "[ERROR] Compilation command '%s' failed with return value %d\n", cmd.c_str(), retVal);
        abort();
    }

    ReleaseAssert(IsFileExists(objFilePath.c_str()));

    std::string output = ReadFileContentAsString(objFilePath);

    // Important to remove 'objFile' first to avoid race condition
    //
    ReleaseAssert(unlink(objFilePath.c_str()) == 0);
    ReleaseAssert(unlink(asmFilePath.c_str()) == 0);

    return output;
}

static std::string WARN_UNUSED CompileCppFileToObjectFileOrLLVMBitcodeImpl(const std::string& cppFileContents, bool compileToLLVMIR, const std::string& storePath)
{
    FILE* fp = nullptr;
    std::string cppFilePath, resFilePath;
    bool shouldRemoveFiles;
    if (storePath == "")
    {
        shouldRemoveFiles = true;
        std::string fileBodyName;
        while (true)
        {
            fileBodyName = GetRandomFileName();
            cppFilePath = std::string("/tmp/") + fileBodyName + ".cpp";
            resFilePath = std::string("/tmp/") + fileBodyName + (compileToLLVMIR ? ".bc" : ".o");
            fp = TryCreateTmpFile(cppFilePath);
            if (fp != nullptr) { break; }
        }
    }
    else
    {
        shouldRemoveFiles = false;
        cppFilePath = storePath;
        for (size_t i = 0; i < cppFilePath.length(); i++) { ReleaseAssert(cppFilePath[i] != ' '); }
        ReleaseAssert(cppFilePath.ends_with(".cpp"));
        resFilePath = cppFilePath.substr(0, cppFilePath.length() - 4) + (compileToLLVMIR ? ".bc" : ".o");
        fp = fopen(cppFilePath.c_str(), "w");
        if (fp == nullptr)
        {
            fprintf(stderr, "Fail to open file '%s' for write.\n", cppFilePath.c_str());
            abort();
        }
    }

    ReleaseAssert(fp != nullptr);
    ReleaseAssert(cppFilePath != "" && resFilePath != "");

    ReleaseAssert(fwrite(cppFileContents.data(), 1, cppFileContents.length(), fp) == cppFileContents.length());
    ReleaseAssert(fclose(fp) == 0);
    fp = nullptr;

    if (shouldRemoveFiles)
    {
        ReleaseAssert(!IsFileExists(resFilePath.c_str()));
    }

    std::string cmd =
        std::string("ccache clang++ -O3 -fno-pic -fno-pie -mfsgsbase -mbmi -msse4 "
        " -Weverything -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-c++20-compat -Wno-unused-macros -Wno-padded "
        " -Wno-missing-prototypes -Wno-zero-length-array -Wno-reserved-identifier -Wno-disabled-macro-expansion "
        " -Wno-gnu-zero-variadic-macro-arguments -Wno-packed -Wno-overlength-strings -Wno-switch-enum -Werror "
        " -c -std=c++20 ")
        + (compileToLLVMIR ? " -emit-llvm " : "")
        + " -o " + resFilePath + " "
        + cppFilePath;

    int retVal = system(cmd.c_str());
    if (retVal != 0)
    {
        fprintf(stderr, "%s\n", cppFileContents.c_str());
        fprintf(stderr, "[ERROR] Command '%s' failed with return value %d\n", cmd.c_str(), retVal);
        abort();
    }

    ReleaseAssert(IsFileExists(resFilePath.c_str()));

    std::string output = ReadFileContentAsString(resFilePath);

    // Important to remove 'resFilePath' first to avoid race condition
    //
    if (shouldRemoveFiles)
    {
        ReleaseAssert(unlink(resFilePath.c_str()) == 0);
        ReleaseAssert(unlink(cppFilePath.c_str()) == 0);
    }

    return output;
}

std::string WARN_UNUSED CompileCppFileToObjectFile(const std::string& cppFileContents, const std::string& storePath)
{
    return CompileCppFileToObjectFileOrLLVMBitcodeImpl(cppFileContents, false /*compileToLLVMIR*/, storePath);
}

std::string WARN_UNUSED CompileCppFileToLLVMBitcode(const std::string& cppFileContents, const std::string& storePath)
{
    return CompileCppFileToObjectFileOrLLVMBitcodeImpl(cppFileContents, true /*compileToLLVMIR*/, storePath);
}

}   // namespace dast
