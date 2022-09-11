#include "common.h"
#include "check_file_md5.h"
#include "sha1_utils.h"
#include "transactional_output_file.h"
#include "read_file.h"
#include "llvm/IR/TypeFinder.h"

#include "define_deegen_common_snippet.h"

// This is really bad.. but it works for now..
///
#include "llvm_extract_function.cpp"

constexpr const char* x_file_prefix = "deegen_common_snippet.";
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

std::pair<std::string /*name*/, std::string /*IR*/> WARN_UNUSED GetProcessedFileContent(const std::string& fileName, const std::string& fileData)
{
    using namespace llvm;
    SMDiagnostic llvmErr;
    MemoryBufferRef mb(StringRef(fileData.data(), fileData.length()), StringRef(fileName.data(), fileName.length()));

    std::unique_ptr<LLVMContext> ctxHolder = std::make_unique<LLVMContext>();
    LLVMContext& ctx = *ctxHolder.get();
    std::unique_ptr<Module> module = parseIR(mb, llvmErr, ctx);
    if (module == nullptr)
    {
        fprintf(stderr, "[INTERNAL ERROR] Bitcode for %s cannot be parsed.\n", fileName.c_str());
        abort();
    }

    dast::RunLLVMOptimizePass(module.get());

    constexpr const char* snippetNameVarName = PP_STRINGIFY(DEEGEN_COMMON_SNIPPET_NAME_VARNAME);
    constexpr const char* snippetTargetVarName = PP_STRINGIFY(DEEGEN_COMMON_SNIPPET_TARGET_VARNAME);

    GlobalVariable* gvName = module->getGlobalVariable(snippetNameVarName);
    ReleaseAssert(gvName != nullptr);

    GlobalVariable* gvTarget = module->getGlobalVariable(snippetTargetVarName);
    ReleaseAssert(gvTarget != nullptr);

    std::string snippetName = dast::GetValueFromLLVMConstantCString(dast::GetConstexprGlobalValue(module.get(), snippetNameVarName));

    ReleaseAssert(gvTarget->isConstant());
    Function* func = dyn_cast<Function>(gvTarget->getInitializer());
    ReleaseAssert(func != nullptr);

    std::string targetFuncName = func->getName().str();
    std::unique_ptr<Module> extractedModule = dast::ExtractFunction(module.get(), targetFuncName);

    TypeFinder typeFinder;
    typeFinder.run(*extractedModule.get(), true /*onlyNamed*/);
    for (StructType* stype : typeFinder)
    {
        ReleaseAssert(stype->hasName());
        stype->setName("");
    }

    Function* targetFunc = extractedModule->getFunction(targetFuncName);
    ReleaseAssert(targetFunc != nullptr);

    std::string renameToName = std::string(x_deegen_common_snippet_function_name_prefix) + snippetName;
    ReleaseAssert(extractedModule->getNamedValue(renameToName) == nullptr);
    targetFunc->setName(renameToName);

    AnonymousFile file;
    {
        raw_fd_ostream fdStream(file.GetUnixFd(), true /*shouldClose*/);
        WriteBitcodeToFile(*extractedModule.get(), fdStream);
        if (fdStream.has_error())
        {
            std::error_code ec = fdStream.error();
            fprintf(stderr, "Attempt to serialize of LLVM IR failed with errno = %d (%s)\n", ec.value(), ec.message().c_str());
            abort();
        }
        /* fd closed when fdStream is destructed here */
    }

    std::string fileContents = file.GetFileContents();
    return std::make_pair(snippetName, fileContents);
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
        declCode += std::string("extern const std::pair<const char*, size_t> g_generated_deegen_common_snippet_") + id + ";\n";
        declCode += std::string("extern const char* const g_generated_deegen_common_snippet_name_") + id + ";\n";

        mapInitCode += std::string("{ g_generated_deegen_common_snippet_name_") + id + ", &g_generated_deegen_common_snippet_" + id + " },\n";

        bool md5Match = CheckMd5Match(file);
        bool shouldRework = selfBinaryChanged || !md5Match;
        if (!shouldRework)
        {
            continue;
        }

        std::string originData = ReadFileContentAsString(file);
        std::pair<std::string /*name*/, std::string /*IR*/> resultData = GetProcessedFileContent(file /*fileName*/, originData);

        std::string outputFileName = GetOutputFilePath(file, outputDirectory);
        TransactionalOutputFile o(outputFileName);

        fprintf(o.fp(), "#include <cstring>\n");
        fprintf(o.fp(), "#include <utility>\n");
        fprintf(o.fp(), "static constexpr size_t s_len = %llu;\n", static_cast<unsigned long long>(resultData.second.length()));
        fprintf(o.fp(), "static const char* s_data = \n");

        fprintf(o.fp(), "\"");
        for (size_t i = 0; i < resultData.second.length(); i++)
        {
            fprintf(o.fp(), "\\x%02x", static_cast<unsigned int>(static_cast<uint8_t>(resultData.second[i])));
            if (i % 32 == 31 && i < resultData.second.length() - 1)
            {
                fprintf(o.fp(), "\"\n\"");
            }
        }
        fprintf(o.fp(), "\";\n");

        fprintf(o.fp(), "extern const std::pair<const char*, size_t> g_generated_deegen_common_snippet_%s;\n", id.c_str());
        fprintf(o.fp(), "extern const char* const g_generated_deegen_common_snippet_name_%s;\n", id.c_str());
        fprintf(o.fp(), "const std::pair<const char*, size_t> g_generated_deegen_common_snippet_%s = std::make_pair(s_data, s_len);\n", id.c_str());
        fprintf(o.fp(), "const char* const g_generated_deegen_common_snippet_name_%s = \"%s\";\n", id.c_str(), resultData.first.c_str());

        o.Commit();

        UpdateMd5Checksum(file);
    }

    {
        std::string outputFileName = outputDirectory + "/deegen_common_snippet_ir_accessor.cpp";
        TransactionalOutputFile o(outputFileName);

        fprintf(o.fp(), "#include \"annotated/deegen_common_snippets/deegen_common_snippet_ir_accessor.h\"\n");
        fprintf(o.fp(), "%s", declCode.c_str());
        fprintf(o.fp(), "std::unordered_map<std::string, const std::pair<const char*, size_t>*> g_deegenCommonSnippetLLVMIRMap {\n");
        fprintf(o.fp(), "%s", mapInitCode.c_str());
        fprintf(o.fp(), "};\n");

        o.Commit();
    }
}

std::vector<std::string> GetFileList(std::string semicolonSeparatedFiles)
{
    std::vector<std::string> out;
    size_t curPos = 0;
    while (true)
    {
        size_t nextPos = semicolonSeparatedFiles.find(";", curPos);
        if (nextPos == std::string::npos)
        {
            ReleaseAssert(curPos < semicolonSeparatedFiles.length());
            out.push_back(semicolonSeparatedFiles.substr(curPos));
            break;
        }
        ReleaseAssert(curPos < nextPos);
        out.push_back(semicolonSeparatedFiles.substr(curPos, nextPos - curPos));
        curPos = nextPos + 1;
    }
    return out;
}

int main(int argc, char** argv)
{
    ReleaseAssert(argc == 3);
    std::string outputDirectory = argv[2];
    std::string semicolonSeparatedFiles = argv[1];
    std::vector<std::string> files = GetFileList(semicolonSeparatedFiles);

    std::string self_binary = argv[0];
    bool selfBinaryChanged = !CheckMd5Match(self_binary);

    DoWork(selfBinaryChanged, files, outputDirectory);

    if (selfBinaryChanged)
    {
        UpdateMd5Checksum(self_binary);
    }
    return 0;
}
