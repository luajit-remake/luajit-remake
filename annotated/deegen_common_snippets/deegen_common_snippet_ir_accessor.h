#pragma once

#include "common.h"

#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

// Provides the functionality for deegen builder to access the common snippets defined in this directory
//

extern std::unordered_map<std::string, std::pair < const std::pair<const char* /*buffer*/, size_t /*length*/>* , int /*kind*/ > > g_deegenCommonSnippetLLVMIRMap;

inline std::unique_ptr<llvm::Module> WARN_UNUSED GetDeegenCommonSnippetLLVMIR(llvm::LLVMContext& ctx, const std::string& name, int expectedKind)
{
    ReleaseAssert(g_deegenCommonSnippetLLVMIRMap.count(name));
    auto it = g_deegenCommonSnippetLLVMIRMap.find(name);

    const std::string& persistentName = it->first;
    std::pair<const char*, size_t> buf = *(it->second.first);
    int kind = it->second.second;
    ReleaseAssert(expectedKind == kind);

    using namespace llvm;
    SMDiagnostic llvmErr;
    MemoryBufferRef mb(StringRef(buf.first, buf.second), StringRef(persistentName));

    std::unique_ptr<Module> module = parseIR(mb, llvmErr, ctx);
    if (module == nullptr)
    {
        fprintf(stderr, "[INTERNAL ERROR] Bitcode for %s cannot be parsed.\n", name.c_str());
        abort();
    }
    return module;
}
