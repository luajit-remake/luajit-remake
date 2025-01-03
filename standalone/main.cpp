#include "runtime_utils.h"
#include "lj_parser_wrapper.h"

#define LJR_VERSION_MAJOR_NUMBER 0
#define LJR_VERSION_MINOR_NUMBER 0
#define LJR_VERSION_PATCH_NUMBER 1

extern const char* x_git_commit_hash;
constexpr const char* x_build_flavor_version_output = x_isTestBuild ? (x_isDebugBuild ? "**DEBUG** build" : "**TESTREL** build") : "release build";

static void PrintLJRVersion()
{
    fprintf(stderr, "LuaJIT Remake v" PP_STRINGIFY(LJR_VERSION_MAJOR_NUMBER) "."
            PP_STRINGIFY(LJR_VERSION_MINOR_NUMBER) "." PP_STRINGIFY(LJR_VERSION_PATCH_NUMBER) " (%s, %s)\n",
            x_git_commit_hash, x_build_flavor_version_output);
    fprintf(stderr, "Copyright (C) 2022 Haoran Xu. https://luajit-remake.github.io\n");
}

static void PrintLJRUsage()
{
    PrintLJRVersion();
    fprintf(stderr, "\nusage: luajitr <script> [args]...\n");
}

static void LaunchScript(int argc, char** argv)
{
    Assert(argc >= 2);
    VM* vm = VM::Create();

    // According to Lua Standard:
    //     Before starting to run the script, lua collects all arguments in the command line in a global table called arg.
    //     The script name is stored at index 0, the first argument after the script name goes to index 1, and so on.
    //     Any arguments before the script name (that is, the interpreter name plus the options) go to negative indices.
    //
    HeapPtr<TableObject> arg = TableObject::CreateEmptyTableObject(vm, 0U /*inlineCapacity*/, static_cast<uint32_t>(argc) /*arrayCapacity*/);
    // TODO: this needs to be changed when we support options
    //
    for (int i = 0; i < argc; i++)
    {
        TValue opt = TValue::Create<tString>(vm->CreateStringObjectFromRawCString(argv[i]));
        TableObject::RawPutByValIntegerIndex(arg, i - 1 /*index*/, opt);
    }

    {
        UserHeapPointer<void> strArg = vm->CreateStringObjectFromRawCString("arg");
        HeapPtr<TableObject> globalObj = vm->GetRootGlobalObject();
        PutByIdICInfo info;
        TableObject::PreparePutByIdForGlobalObject(globalObj, strArg, info);
        TableObject::PutById(globalObj, strArg, TValue::Create<tTable>(arg), info);
    }

    const char* scriptFilename = argv[1];
    ParseResult pr = ParseLuaScriptFromFile(vm->GetRootCoroutine(), scriptFilename);
    if (pr.m_scriptModule.get() == nullptr)
    {
        fprintf(stderr, "Failed to parse file '%s'. Error message:\n", scriptFilename);
        PrintTValue(stderr, pr.errMsg);
        fprintf(stderr, "\n");
        exit(1);
    }

    vm->LaunchScript(pr.m_scriptModule.get());
}

int main(int argc, char** argv)
{
    if (argc <= 1)
    {
        PrintLJRUsage();
        return 0;
    }
    if (strcmp(argv[1], "-v") == 0)
    {
        PrintLJRVersion();
        return 0;
    }
    LaunchScript(argc, argv);
    return 0;
}
