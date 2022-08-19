#pragma once

#include "common.h"

#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

// Provides the functionality for the unit test to access the LLVM IR of the CPP files in this directory
//

extern std::unordered_map<std::string, const std::pair<const char* /*buffer*/, size_t /*length*/>*> g_deegenUnitTestLLVMIRMap;

// 'name' should be a CPP file name in this directory (without .cpp)
//
inline std::unique_ptr<llvm::Module> WARN_UNUSED GetDeegenUnitTestLLVMIR(llvm::LLVMContext& ctx, const std::string& name)
{
    ReleaseAssert(g_deegenUnitTestLLVMIRMap.count(name));
    auto it = g_deegenUnitTestLLVMIRMap.find(name);

    const std::string& persistentName = it->first;
    std::pair<const char*, size_t> buf = *(it->second);

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
