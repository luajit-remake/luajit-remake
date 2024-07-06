#pragma once

#include "misc_llvm_helper.h"
#include "tvalue.h"

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

    void AddTagRegister(llvm::Argument* arg, uint64_t value)
    {
        ReleaseAssert(!m_didOptimization);
        ReleaseAssert(arg->getParent() == m_target);
        ReleaseAssert(llvm_value_has_type<uint64_t>(arg));
        m_tagRegisterList.push_back(std::make_pair(arg, value));
    }

    void Run();

private:
    llvm::Function* m_target;
    bool m_didOptimization;
    std::vector<std::pair<llvm::Argument*, uint64_t>> m_tagRegisterList;
};

void VMBasePointerOptimization(llvm::Function* func);

inline void RunTagRegisterOptimizationPass(llvm::Function* func)
{
    TagRegisterOptimizationPass pass(func);
    pass.AddTagRegister(func->getArg(4), TValue::x_int32Tag);
    pass.AddTagRegister(func->getArg(9), TValue::x_mivTag);
    pass.Run();

    VMBasePointerOptimization(func);
}

}   // namespace dast
