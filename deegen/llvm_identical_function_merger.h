#pragma once

#include "common.h"
#include "misc_llvm_helper.h"

namespace dast {

// A simple wrapper around llvm::FunctionComparator that merges identical functions
//
// Note that this class is currently only used to merge return continuations, which are never directly referenced by outsider logic,
// so it simply deletes identical functions and RAUW all the uses inside the module (which is likely not good enough for general use case!).
//
struct LLVMIdenticalFunctionMerger
{
    void AddFunction(llvm::Function* func)
    {
        m_list.push_back(func);
    }

    // Identical functions with different section names will be assigned the section with highest priority
    //
    // It is caller's responsibility to not pass functions into this helper if the section name is required
    // for correctness and must not be changed.
    //
    void SetSectionPriority(const std::string& sectionName, uint32_t priority)
    {
        ReleaseAssert(!m_sectionPriorityMap.count(sectionName));
        m_sectionPriorityMap[sectionName] = priority;
    }

    void DoMerge();

private:
    std::vector<llvm::Function*> m_list;

    // Just as a precaution, use std::map to provide determinism even if the priority values are not distinct
    //
    std::map<std::string, int64_t> m_sectionPriorityMap;
};

}   // namespace dast
