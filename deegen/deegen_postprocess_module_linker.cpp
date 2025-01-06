#include "deegen_postprocess_module_linker.h"
#include "deegen_bytecode_operand.h"
#include "llvm/Linker/Linker.h"

namespace dast {

std::unique_ptr<llvm::Module> WARN_UNUSED DeegenPostProcessModuleLinker::DoLinking()
{
    using namespace llvm;
    ReleaseAssert(!m_executed);
    m_executed = true;

    struct RestoreAction
    {
        bool m_isFunction;
        bool m_shallRemoveUseAttribute;
        bool m_shouldRestoreLinkage;
        llvm::GlobalValue::LinkageTypes m_originalLinkage;
    };

    std::unordered_map<std::string /*funcOrVarName*/, RestoreAction> changeMap;

    for (std::unique_ptr<Module>& m : m_moduleList)
    {
        for (Function& funcInExtracted : *m.get())
        {
            ReleaseAssert(funcInExtracted.hasExternalLinkage());
            std::string fnName = funcInExtracted.getName().str();
            if (funcInExtracted.empty())
            {
                // Ignore intrinsic functions
                //
                if (funcInExtracted.isIntrinsic())
                {
                    continue;
                }
                Function* funcInOriginal = m_originModule->getFunction(fnName);
                // It's possible that the lowering introduced new function declarations that we are not aware of.
                // We don't need to do anything for those functions.
                //
                if (funcInOriginal != nullptr)
                {
                    if (changeMap.count(fnName))
                    {
                        continue;
                    }
                    bool shouldRestoreLinkage = false;
                    GlobalValue::LinkageTypes originalLinkage = funcInOriginal->getLinkage();
                    if (funcInOriginal->hasLocalLinkage())
                    {
                        shouldRestoreLinkage = true;
                        funcInOriginal->setLinkage(GlobalValue::ExternalLinkage);
                    }
                    bool justAddedToUsed = AddUsedAttributeToGlobalValue(funcInOriginal);
                    changeMap[fnName] = RestoreAction {
                        .m_isFunction = true,
                        .m_shallRemoveUseAttribute = justAddedToUsed,
                        .m_shouldRestoreLinkage = shouldRestoreLinkage,
                        .m_originalLinkage = originalLinkage
                    };
                }
                else
                {
                    ReleaseAssert(m_originModule->getNamedValue(fnName) == nullptr);
                }
            }
            else
            {
                ReleaseAssert(m_originModule->getNamedValue(fnName) == nullptr);
            }
        }

        for (GlobalVariable& gvInExtracted : m->globals())
        {
            if (gvInExtracted.hasLocalLinkage())
            {
                // If the global has local linkage, just link them in. There isn't any downside other than we potentially get one more copy
                // if the variable already exists in the main module. But that's fine, because LLVM optimizer should usually be able to merge
                // identical copies of internal globals into one. We should not try to manually merge them because it's very tricky and risky.
                //
                // TODO: after some more investigation, it seems like LLVM does not merge identical internal globals with unnamed_addr on the spot.
                // It seems like some optimization pass is needed to merge them. We don't want to run the whole optimization pipeline again
                // since LLVM doesn't recommend that (it could bloat the code), so we should identify what pass does that and run that pass alone.
                // But it's not very important anyway.
                //
                continue;
            }

            std::string gvName = gvInExtracted.getName().str();

            GlobalVariable* gvInOriginal = m_originModule->getGlobalVariable(gvName);
            if (gvInOriginal == nullptr)
            {
                if (m_newlyIntroducedGvWhitelist.count(gvName))
                {
                    // This is a whitelisted global variable that we know could be introduced, it's fine that it's not
                    // in the original module, and we don't have to do anything about it
                    //
                    continue;
                }
                else
                {
                    // Currently we simply fail if the processed module introduced new external global variables that we do not know.
                    // We should be able to do better, but there is no use case for such scenario right now.
                    //
                    fprintf(stderr, "A post-processed module introduced unexpected external global variable %s!\n", gvName.c_str());
                    abort();
                }
            }

            if (changeMap.count(gvName))
            {
                continue;
            }

            bool shouldRestoreLinkage = false;
            GlobalValue::LinkageTypes originalLinkage = gvInOriginal->getLinkage();
            if (gvInOriginal->hasLocalLinkage())
            {
                shouldRestoreLinkage = true;
                if (gvInOriginal->isConstant() && gvInOriginal->hasInitializer())
                {
                    ReleaseAssert(gvInExtracted.getLinkage() == GlobalValue::LinkOnceODRLinkage || gvInExtracted.getLinkage() == GlobalValue::ExternalLinkage);
                    gvInOriginal->setLinkage(GlobalValue::LinkOnceODRLinkage);
                }
                else
                {
                    ReleaseAssert(!gvInExtracted.hasInitializer());
                    gvInOriginal->setLinkage(GlobalValue::ExternalLinkage);
                }
            }
            bool justAddedToUsed = AddUsedAttributeToGlobalValue(gvInOriginal);
            changeMap[gvName] = RestoreAction {
                .m_isFunction = false,
                .m_shallRemoveUseAttribute = justAddedToUsed,
                .m_shouldRestoreLinkage = shouldRestoreLinkage,
                .m_originalLinkage = originalLinkage
            };
        }
    }

    RunLLVMOptimizePass(m_originModule.get());

    // Check that the module is as expected after the optimization pass
    //
    // Assert that all the functions we expected to survive the optimization are still there
    //
    for (auto& it : changeMap)
    {
        if (it.second.m_isFunction)
        {
            ReleaseAssert(m_originModule->getFunction(it.first) != nullptr);
        }
        else
        {
            ReleaseAssert(m_originModule->getGlobalVariable(it.first) != nullptr);
        }
    }

    // Link in all the post-processed modules
    //
    for (size_t i = 0; i < m_moduleList.size(); i++)
    {
        Linker linker(*m_originModule.get());
        // linkInModule returns true on error
        //
        ReleaseAssert(linker.linkInModule(std::move(m_moduleList[i])) == false);
    }

    // Now, revert all the changes we did to the linkage, so that this module can be linked properly with other modules
    //
    for (auto& it : changeMap)
    {
        RestoreAction ra = it.second;
        if (ra.m_isFunction)
        {
            Function* func = m_originModule->getFunction(it.first);
            ReleaseAssert(func != nullptr);
            if (ra.m_shallRemoveUseAttribute)
            {
                RemoveGlobalValueUsedAttributeAnnotation(func);
            }
            if (ra.m_shouldRestoreLinkage)
            {
                func->setLinkage(ra.m_originalLinkage);
            }
        }
        else
        {
            GlobalVariable* gv = m_originModule->getGlobalVariable(it.first);
            ReleaseAssert(gv != nullptr);
            if (ra.m_shallRemoveUseAttribute)
            {
                RemoveGlobalValueUsedAttributeAnnotation(gv);
            }
            if (ra.m_shouldRestoreLinkage)
            {
                gv->setLinkage(ra.m_originalLinkage);
            }
        }
    }

    ValidateLLVMModule(m_originModule.get());

    // Set DSOLocal for all function marked to need to do so
    //
    for (const std::string& fnName : m_fnNamesNeedToBeMadeDsoLocal)
    {
        Function* fn = m_originModule->getFunction(fnName);
        ReleaseAssert(fn != nullptr);
        fn->setDSOLocal(true);
    }

    return std::move(m_originModule);
}

}   // namespace dast
