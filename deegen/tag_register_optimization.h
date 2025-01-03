#pragma once

#include "misc_llvm_helper.h"
#include "tvalue.h"
#include "x64_register_info.h"

namespace dast {

class TagRegisterOptimizationPass
{
public:
    TagRegisterOptimizationPass(llvm::Function* target)
        : m_target(target)
        , m_didOptimization(false)
    {
        ReleaseAssert(m_target != nullptr);
    }

    void AddTagRegister(X64Reg reg, uint64_t value);

    void Run();

private:
    llvm::Function* m_target;
    bool m_didOptimization;
    std::vector<std::pair<llvm::Argument*, uint64_t>> m_tagRegisterList;
};

void RunTagRegisterOptimizationPass(llvm::Function* func);

}   // namespace dast
