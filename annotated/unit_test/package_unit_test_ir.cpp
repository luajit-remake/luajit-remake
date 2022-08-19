#include "common.h"
#include "check_file_md5.h"
#include "sha1_utils.h"
#include "transactional_output_file.h"
#include "read_file.h"

constexpr const char* x_file_prefix = "deegen_unittest.";
constexpr const char* x_file_suffix = ".ir.cpp";

std::string GetSourceFileName(const std::string& inputFile)
{
    ReleaseAssert(inputFile.length() > 0);
    size_t i = inputFile.length() - 1;
    while (i > 0 && inputFile[i] != '/') i--;
    ReleaseAssert(inputFile[i] == '/');
    std::string filename = inputFile.substr(i + 1);
    ReleaseAssert(filename.length() > 2);
    ReleaseAssert(filename.substr(filename.length() - 2) == ".o");
    filename = filename.substr(0, filename.length() - 2);
    return filename;
}

std::string GetOutputFilePath(const std::string& inputFile, const std::string& outputDirectory)
{
    std::string filename = GetSourceFileName(inputFile);
    std::string res = outputDirectory + "/" + x_file_prefix + filename + x_file_suffix;
    return res;
}

void DoWork(bool selfBinaryChanged, const std::vector<std::string>& files, const std::string& outputDirectory)
{
    std::string declCode = "";
    std::string mapInitCode = "";
    std::set<std::string> allFileComponentNames;

    for (const std::string& file : files)
    {
        std::string sourceFileName = GetSourceFileName(file);
        ReleaseAssert(!allFileComponentNames.count(sourceFileName));
        allFileComponentNames.insert(sourceFileName);

        ReleaseAssert(sourceFileName.length() > 4 && sourceFileName.substr(sourceFileName.length() - 4) == ".cpp");
        std::string mapIndexName = sourceFileName.substr(0, sourceFileName.length() - 4);

        std::string id = GetSHA1HashHex(file);
        declCode += std::string("extern const std::pair<const char*, size_t> g_generated_deegen_unittest_sym_") + id + ";\n";
        mapInitCode += std::string("{ \"") + mapIndexName + "\", &g_generated_deegen_unittest_sym_" + id + " },\n";

        bool md5Match = CheckMd5Match(file);
        bool shouldRework = selfBinaryChanged || !md5Match;
        if (!shouldRework)
        {
            continue;
        }

        std::string s = ReadFileContentAsString(file);

        std::string outputFileName = GetOutputFilePath(file, outputDirectory);
        TransactionalOutputFile o(outputFileName);

        fprintf(o.fp(), "#include <cstring>\n");
        fprintf(o.fp(), "#include <utility>\n");
        fprintf(o.fp(), "static constexpr size_t s_len = %llu;\n", static_cast<unsigned long long>(s.length()));
        fprintf(o.fp(), "static const char* s_data = \n");

        fprintf(o.fp(), "\"");
        for (size_t i = 0; i < s.length(); i++)
        {
            fprintf(o.fp(), "\\x%02x", static_cast<unsigned int>(static_cast<uint8_t>(s[i])));
            if (i % 32 == 31 && i < s.length() - 1)
            {
                fprintf(o.fp(), "\"\n\"");
            }
        }
        fprintf(o.fp(), "\";\n");

        fprintf(o.fp(), "extern const std::pair<const char*, size_t> g_generated_deegen_unittest_sym_%s;\n", id.c_str());
        fprintf(o.fp(), "const std::pair<const char*, size_t> g_generated_deegen_unittest_sym_%s = std::make_pair(s_data, s_len);\n", id.c_str());

        o.Commit();

        UpdateMd5Checksum(file);
    }

    {
        std::string outputFileName = outputDirectory + "/deegen_unit_test_ir_accessor.cpp";
        TransactionalOutputFile o(outputFileName);

        fprintf(o.fp(), "#include \"annotated/unit_test/unit_test_ir_accessor.h\"\n");
        fprintf(o.fp(), "%s", declCode.c_str());
        fprintf(o.fp(), "std::unordered_map<std::string, const std::pair<const char*, size_t>*> g_deegenUnitTestLLVMIRMap {\n");
        fprintf(o.fp(), "%s", mapInitCode.c_str());
        fprintf(o.fp(), "};\n");

        o.Commit();
    }
}

int main(int argc, char** argv)
{
    ReleaseAssert(argc >= 2);
    std::string outputDirectory = argv[argc - 1];
    std::vector<std::string> files;
    for (int i = 1; i < argc - 1; i++)
    {
        files.push_back(std::string(argv[i]));
    }

    std::string self_binary = argv[0];
    bool selfBinaryChanged = !CheckMd5Match(self_binary);

    DoWork(selfBinaryChanged, files, outputDirectory);

    if (selfBinaryChanged)
    {
        UpdateMd5Checksum(self_binary);
    }
    return 0;
}
