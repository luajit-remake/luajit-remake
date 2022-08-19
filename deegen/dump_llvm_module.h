#pragma once

#include "llvm/Bitcode/BitcodeWriter.h"

inline void DumpLLVMModuleForDebug(llvm::Module* module)
{
    std::string _dst;
    llvm::raw_string_ostream rso(_dst /*target*/);
    module->print(rso, nullptr);
    std::string& dump = rso.str();
    printf("%s\n", dump.c_str());
}
