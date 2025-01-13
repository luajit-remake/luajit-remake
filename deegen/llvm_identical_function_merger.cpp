#include "llvm_identical_function_merger.h"
#include "llvm/Transforms/Utils/FunctionComparator.h"

namespace dast {

void LLVMIdenticalFunctionMerger::DoMergeImpl(bool replaceWithAlias)
{
    using namespace llvm;
    if (m_list.size() == 0)
    {
        return;
    }

    Module* module = m_list[0]->getParent();
    ReleaseAssert(module != nullptr);

    // Sanity checks
    //
    {
        std::unordered_set<Function*> checkUnique;
        for (Function* fn : m_list)
        {
            ReleaseAssert(fn->getParent() == module);
            ReleaseAssert(!checkUnique.count(fn));
            checkUnique.insert(fn);

            std::string secName = fn->getSection().str();
            if (secName != "")
            {
                if (!m_sectionPriorityMap.count(secName))
                {
                    fprintf(stderr, "[ERROR] Function %s has unrecognized section name %s!", fn->getName().str().c_str(), secName.c_str());
                    abort();
                }
            }
        }
    }

    // For now, we simply use a n^2-comparison naive algorithm since performance should not matter here. This also allows us
    // to have some redundancy to be extra certain that llvm::FunctionComparator exhibits sane behavior.
    //
    llvm::GlobalNumberState gns;
    std::vector<std::vector<Function*>> equivGroups;
    for (Function* curFn : m_list)
    {
        ReleaseAssert(curFn != nullptr);
        ReleaseAssert(!curFn->empty());

        bool found = false;
        for (auto& grp : equivGroups)
        {
            ReleaseAssert(grp.size() > 0);
            Function* funcToCmp = grp[0];
            if (FunctionComparator(curFn, funcToCmp, &gns).compare() == 0)
            {
                found = true;
                for (size_t i = 1; i < grp.size(); i++)
                {
                    Function* other = grp[i];
                    ReleaseAssert(FunctionComparator(curFn, other, &gns).compare() == 0);
                }
                grp.push_back(curFn);
                break;
            }
        }

        if (!found)
        {
            equivGroups.push_back({ curFn });
        }
    }

    {
        size_t totalCount = 0;
        for (auto& eg : equivGroups)
        {
            totalCount += eg.size();
        }
        ReleaseAssert(totalCount == m_list.size());
    }

    // Now, merge all the equivalence groups
    //
    std::vector<std::string> fnNamesExpectedToExist;
    std::vector<std::string> fnNameExpectedToBeDeleted;
    for (auto& grp : equivGroups)
    {
        ReleaseAssert(grp.size() > 0);
        std::string mergedFnSecName = "";
        int64_t maxSecNamePriority = -1;    // all user-assigned priority are passed in as uint32_t so must be >= 0

        for (size_t i = 0; i < grp.size(); i++)
        {
            Function* func = grp[i];
            std::string secName = func->getSection().str();

            // If the secName is "" and the user did not assign priority for "", it is still possible that the secName doesn't exist in the map
            //
            if (m_sectionPriorityMap.count(secName))
            {
                int64_t prio = m_sectionPriorityMap[secName];
                if (prio > maxSecNamePriority)
                {
                    maxSecNamePriority = prio;
                    mergedFnSecName = secName;
                }
            }
            else
            {
                ReleaseAssert(secName == "");
            }
        }

        Function* fnToKeep = grp[0];
        fnNamesExpectedToExist.push_back(fnToKeep->getName().str());
        fnToKeep->setSection(mergedFnSecName);

        for (size_t i = 1; i < grp.size(); i++)
        {
            Function* fnToMerge = grp[i];
            if (replaceWithAlias)
            {
                // Logic stolen from LLVM MergeFunctions.cpp writeAlias
                //
                PointerType* PtrType = fnToMerge->getType();
                auto* GA = GlobalAlias::create(fnToMerge->getValueType(), PtrType->getAddressSpace(),
                                               fnToMerge->getLinkage(), "", fnToKeep, fnToMerge->getParent());

                const MaybeAlign FAlign = fnToKeep->getAlign();
                const MaybeAlign GAlign = fnToMerge->getAlign();
                if (FAlign || GAlign)
                    fnToKeep->setAlignment(std::max(FAlign.valueOrOne(), GAlign.valueOrOne()));
                else
                    fnToKeep->setAlignment(std::nullopt);

                GA->takeName(fnToMerge);
                GA->setVisibility(fnToMerge->getVisibility());
                GA->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

                fnToMerge->replaceAllUsesWith(GA);
                fnToMerge->eraseFromParent();
            }
            else
            {
                fnToMerge->replaceAllUsesWith(fnToKeep);
                fnToMerge->deleteBody();
                fnToMerge->setLinkage(GlobalValue::ExternalLinkage);
                fnNameExpectedToBeDeleted.push_back(fnToMerge->getName().str());
            }
        }
    }

    RunLLVMDeadGlobalElimination(module);

    // Assert everything is as expected
    //
    ValidateLLVMModule(module);
    for (auto& fnName : fnNamesExpectedToExist)
    {
        Function* fn = module->getFunction(fnName);
        ReleaseAssert(fn != nullptr);
        ReleaseAssert(!fn->empty());
    }

    for (auto& fnName : fnNameExpectedToBeDeleted)
    {
        ReleaseAssert(module->getNamedValue(fnName) == nullptr);
    }

    m_list.clear();
}

}   // namespace dast
