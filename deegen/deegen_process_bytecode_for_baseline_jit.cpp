#include "deegen_process_bytecode_for_baseline_jit.h"

namespace dast {

DeegenProcessBytecodeForBaselineJitResult WARN_UNUSED DeegenProcessBytecodeForBaselineJitResult::Create(BytecodeIrInfo* bii, const BytecodeOpcodeRawValueMap& byOpMap)
{
    using namespace llvm;
    DeegenProcessBytecodeForBaselineJitResult res;
    res.m_bii = bii;
    res.m_bytecodeDef = bii->m_bytecodeDef;

    // Create the main JIT logic
    //
    res.m_baselineJitInfo = DeegenBytecodeBaselineJitInfo::Create(*bii, byOpMap);

    // Process each slow path and return continuation
    // We simply assume each return continuation could potentially be used by slow path,
    // because linker should be smart enough to drop unused functions (even if not it's not a big deal at all)
    //
    for (size_t i = 0; i < bii->m_slowPaths.size(); i++)
    {
        BaselineJitImplCreator jic(*(bii->m_slowPaths[i].get()));
        jic.DoLowering();
        std::unique_ptr<llvm::Module> module = CloneModule(*jic.GetModule());
        res.m_aotSlowPaths.push_back({ std::move(module), jic.GetResultFunctionName() });
    }

    if (bii->m_quickeningSlowPath.get() != nullptr)
    {
        BaselineJitImplCreator jic(*(bii->m_quickeningSlowPath.get()));
        jic.DoLowering();
        std::unique_ptr<llvm::Module> module = CloneModule(*jic.GetModule());
        res.m_aotSlowPaths.push_back({ std::move(module), jic.GetResultFunctionName() });
    }

    for (size_t i = 0; i < bii->m_allRetConts.size(); i++)
    {
        BaselineJitImplCreator jic(BaselineJitImplCreator::SlowPathReturnContinuationTag(), *(bii->m_allRetConts[i].get()));
        jic.DoLowering();
        std::unique_ptr<llvm::Module> module = CloneModule(*jic.GetModule());
        res.m_aotSlowPathReturnConts.push_back({ std::move(module), jic.GetResultFunctionName() });
    }

    // Figure out the range to populate in the dispatch table
    //
    std::string opName = res.m_bytecodeDef->GetBytecodeIdName();
    size_t opOrd = byOpMap.GetOpcode(opName);

    // This check is really.. hacky.. but for now let's just make things simple..
    //
    size_t numFusedIcVariants = 0;
    while (opOrd + numFusedIcVariants + 1 < byOpMap.GetDispatchTableLength())
    {
        if (byOpMap.GetBytecode(opOrd + numFusedIcVariants + 1) == opName + "_fused_ic_" + std::to_string(numFusedIcVariants))
        {
            numFusedIcVariants++;
        }
        else
        {
            break;
        }
    }

    res.m_opcodeRawValue = opOrd;
    res.m_opcodeNumFuseIcVariants = numFusedIcVariants;

    // Some simple sanity checks
    //
    {
        std::unordered_set<std::string> funcNameSet;

        auto validate = [&](const std::vector<SlowPathInfo>& list)
        {
            for (const auto& item : list)
            {
                Function* func = item.m_module->getFunction(item.m_funcName);
                ReleaseAssert(func != nullptr);
                ReleaseAssert(func->hasExternalLinkage());

                ReleaseAssert(!funcNameSet.count(item.m_funcName));
                funcNameSet.insert(item.m_funcName);
            }
        };

        validate(res.m_aotSlowPaths);
        validate(res.m_aotSlowPathReturnConts);
    }

    // Rename the generic prefix '__deegen_bytecode_' to '__deegen_baseline_jit_op_'
    //
    {
        std::string prefixToFind = "__deegen_bytecode_";
        std::string prefixToReplace = "__deegen_baseline_jit_op_";

        auto renameFunctions = [&](Module* module)
        {
            std::vector<Function*> fnsToRename;
            for (Function& fn : *module)
            {
                if (fn.getName().startswith(prefixToFind))
                {
                    fnsToRename.push_back(&fn);
                }
            }
            for (Function* fn : fnsToRename)
            {
                std::string oldName = fn->getName().str();
                ReleaseAssert(oldName.starts_with(prefixToFind));
                std::string newName = prefixToReplace + oldName.substr(prefixToFind.length());
                fn->setName(newName);
                ReleaseAssert(fn->getName() == newName);
            }
        };

        auto renameSlowPathInfo = [&](std::vector<SlowPathInfo>& list /*inout*/)
        {
            for (auto& m : list)
            {
                renameFunctions(m.m_module.get());
                ReleaseAssert(m.m_funcName.starts_with(prefixToFind));
                m.m_funcName = prefixToReplace + m.m_funcName.substr(prefixToFind.length());
            }
        };

        renameSlowPathInfo(res.m_aotSlowPaths);
        renameSlowPathInfo(res.m_aotSlowPathReturnConts);
        renameFunctions(res.m_baselineJitInfo.m_cgMod.get());
    }

    return res;
}

}   // namespace dast
