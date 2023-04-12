#pragma once

#include "common.h"
#include "misc_llvm_helper.h"

namespace dast {

// Deegen typically has the following workflow when processing a module:
// 1. Extract something from a module
// 2. Do some transform, typically producing a new function
//
// Since the new function may reference symbols in the original module, which could have internal linkage,
// in order for the newly-created function to work, we must link it back to the original module.
//
// However, we must take care when doing so, so that we do not introduce linkage problems
// (for example, we need to make sure all the symbols are referenced correctly, all the necessary symbols are existent,
// all the droppable symbols are dropped, and all the internal linkage symbols are kept internal)
//
// This helper class is responsible for correctly linking all the post-processed modules togther into the original module.
//
// It assumes that the post-processed modules only contain the generated functions, and all other functions
// have their bodies dropped and linkage changed to external, even if the linkage was internal in the original module.
//
// It does the following:
// (1) Iterate all the extracted modules 'E' to figure out the set of all needed functions 'D'.
// (2) Mark all functions in 'D' in our original module M as '__used__', change their linkage to external if they
//     have internal linkage.
// (3) Run optimization pass on M.
// (4) Link the extracted modules into our original module. The linking must succeed because all needed functions
//     have visible linkage and must exist in our original module (since they are marked used).
// (5) Restore the linkage of all functions in 'D' to their original linkage, and remove the 'used' annotations.
//     This allows our final produced module to be correctly linked against other modules.
//
// Note that during this process, LLVM standard optimization pipeline is only executed once on M, which is required
// to prevent unwanted code bloating caused by execessive inlining.
//
class DeegenPostProcessModuleLinker
{
public:
    DeegenPostProcessModuleLinker(std::unique_ptr<llvm::Module> originModule)
        : m_executed(false)
        , m_originModule(std::move(originModule))
        , m_moduleList()
    {
        ReleaseAssert(m_originModule.get() != nullptr);
    }

    // Add a post-processed module to be linked back into the original module
    //
    void AddModule(std::unique_ptr<llvm::Module> module)
    {
        ReleaseAssert(module.get() != nullptr);
        m_moduleList.push_back(std::move(module));
    }

    // We will fail if the post-processed module introduced new external-linkage global variables (not functions!),
    // because it generally indicates that something unexpected has happened. Unless it has been added to this whitelist.
    //
    void AddWhitelistedNewlyIntroducedGlobalVar(const std::string& gvName)
    {
        using namespace llvm;
        GlobalVariable* gv = m_originModule->getGlobalVariable(gvName);
        if (gv == nullptr)
        {
            ReleaseAssert(m_originModule->getNamedValue(gvName) == nullptr);
        }
        else
        {
            ReleaseAssert(!gv->hasLocalLinkage());
        }

        m_newlyIntroducedGvWhitelist.insert(gvName);
    }

    std::unique_ptr<llvm::Module> WARN_UNUSED DoLinking();

private:
    bool m_executed;
    std::unique_ptr<llvm::Module> m_originModule;
    std::vector<std::unique_ptr<llvm::Module>> m_moduleList;
    std::unordered_set<std::string> m_newlyIntroducedGvWhitelist;
};

}   // namespace dast
