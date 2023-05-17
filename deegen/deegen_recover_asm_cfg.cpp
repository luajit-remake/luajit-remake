#include "deegen_recover_asm_cfg.h"
#include "deegen_magic_asm_helper.h"

namespace dast {

void DeegenAsmCfg::AnalyzeIndirectBranch(X64AsmFile* file, llvm::Function* /*func*/, bool addHumanDebugComment)
{
    using namespace llvm;

    file->Validate();

    ReleaseAssert(file->m_slowpath.empty());
    ReleaseAssert(file->m_icPath.empty());
    ReleaseAssert(!file->m_hasAnalyzedIndirectBranchTargets);
    file->m_hasAnalyzedIndirectBranchTargets = true;

    // Update m_indirectBranchTargets, and write comments for debug if requested
    //
    for (X64AsmBlock* block : file->m_blocks)
    {
        ReleaseAssert(block->m_indirectBranchTargets.empty());

        ReleaseAssert(!block->m_lines.empty());
        X64AsmLine& line = block->m_lines.back();
        if (line.IsIndirectJumpInst())
        {
            if (line.m_trailingComments.find("__deegen_asm_annotation_tailcall") != std::string::npos)
            {
                // This is a tail call, don't bother
                //
                if (addHumanDebugComment)
                {
                    line.m_trailingComments += "# [CFG]: tailcall";
                }
            }
            else
            {
                std::string hintStr = "__deegen_asm_annotation_indirectbr{";
                size_t offset = line.m_trailingComments.find(hintStr);
                if (offset == std::string::npos)
                {
                    fprintf(stderr, "An indirect branch is not annotated in the ASM! Is the annotation flag enabled when doing the compilation?\n");
                    abort();
                }

                std::string s = line.m_trailingComments.substr(offset + hintStr.length());
                size_t endOffset = s.find("}");
                ReleaseAssert(endOffset != std::string::npos);
                s = s.substr(0, endOffset);

                std::vector<std::string> allDestLabels;

                size_t cur = 0;
                while (cur < s.length())
                {
                    size_t next = s.find(",", cur);
                    ReleaseAssert(next != std::string::npos);
                    ReleaseAssert(cur < next);
                    std::string label = s.substr(cur, next - cur);
                    allDestLabels.push_back(label);
                    cur = next + 1;
                }
                ReleaseAssert(cur == s.length());

                std::set<std::string> allNormalizedDestLabels;
                for (std::string& label : allDestLabels)
                {
                    ReleaseAssert(file->m_labelNormalizer.QueryLabelExists(label));
                    std::string normalizedLabel = file->m_labelNormalizer.GetNormalizedLabel(label);
                    allNormalizedDestLabels.insert(normalizedLabel);
                }

                std::string humanDebugText = " # [CFG]: dest =";
                bool isFirst = true;
                for (const std::string& dstLabel : allNormalizedDestLabels)
                {
                    ReleaseAssert(file->m_labelNormalizer.QueryLabelExists(dstLabel));
                    ReleaseAssert(file->m_labelNormalizer.GetNormalizedLabel(dstLabel) == dstLabel);
                    block->m_indirectBranchTargets.push_back(dstLabel);

                    if (!isFirst) { humanDebugText += ","; }
                    isFirst = false;
                    humanDebugText += " " + dstLabel;
                }
                ReleaseAssert(humanDebugText.find("\n") == std::string::npos);
                if (addHumanDebugComment)
                {
                    line.m_trailingComments += humanDebugText;
                }
            }
        }
    }
}

DeegenAsmCfg WARN_UNUSED DeegenAsmCfg::GetCFG(X64AsmFile* file)
{
    file->Validate();
    ReleaseAssert(file->m_hasAnalyzedIndirectBranchTargets);

    // Build the final CFG graph
    //
    std::map<std::string, std::set<std::string>> cfg;

    for (X64AsmBlock* block : file->m_blocks)
    {
        ReleaseAssert(!cfg.count(block->m_normalizedLabelName));
        cfg[block->m_normalizedLabelName] = {};
    }

    for (X64AsmBlock* block : file->m_blocks)
    {
        std::string srcLabel = block->m_normalizedLabelName;
        ReleaseAssert(cfg.count(srcLabel));
        for (X64AsmLine& line : block->m_lines)
        {
            if (line.IsDirectUnconditionalJumpInst() || line.IsConditionalJumpInst())
            {
                ReleaseAssert(line.NumWords() == 2);
                std::string rawLabel = line.GetWord(1);
                if (file->m_labelNormalizer.QueryLabelExists(rawLabel))
                {
                    std::string dstLabel = file->m_labelNormalizer.GetNormalizedLabel(rawLabel);
                    ReleaseAssert(cfg.count(dstLabel));
                    cfg[srcLabel].insert(dstLabel);
                }
            }
        }

        if (block->m_lines.back().IsIndirectJumpInst())
        {
            for (const std::string& dstLabel : block->m_indirectBranchTargets)
            {
                ReleaseAssert(cfg.count(dstLabel));
                cfg[srcLabel].insert(dstLabel);
            }
        }
    }

    DeegenAsmCfg result;
    result.m_cfg = std::move(cfg);
    return result;
}

std::vector<std::string> WARN_UNUSED DeegenAsmCfg::GetAllBlocksDominatedBy(const std::vector<std::string>& dominatorBlockList, const std::string& entryBlock)
{
    ReleaseAssert(m_cfg.count(entryBlock));

    std::unordered_set<std::string> dominatorBlockSet;
    for (const std::string& label : dominatorBlockList)
    {
        ReleaseAssert(m_cfg.count(label));
        dominatorBlockSet.insert(label);
    }

    std::unordered_set<std::string> reachableFromEntryWithoutDominator;
    {
        std::function<void(const std::string&)> dfs = [&](const std::string& label)
        {
            ReleaseAssert(m_cfg.count(label));
            if (dominatorBlockSet.count(label))
            {
                return;
            }
            if (reachableFromEntryWithoutDominator.count(label))
            {
                return;
            }
            reachableFromEntryWithoutDominator.insert(label);

            for (const std::string& succ : SuccOf(label))
            {
                dfs(succ);
            }
        };
        dfs(entryBlock);
    }
    for (const std::string& label : dominatorBlockList)
    {
        ReleaseAssert(!reachableFromEntryWithoutDominator.count(label));
    }

    std::unordered_set<std::string> reachableFromEntry;
    {
        std::function<void(const std::string&)> dfs = [&](const std::string& label)
        {
            ReleaseAssert(m_cfg.count(label));
            if (reachableFromEntry.count(label))
            {
                return;
            }
            reachableFromEntry.insert(label);

            for (const std::string& succ : SuccOf(label))
            {
                dfs(succ);
            }
        };
        dfs(entryBlock);
    }

    for (const std::string& label : dominatorBlockList)
    {
        if (!reachableFromEntry.count(label))
        {
            fprintf(stderr, "Dominator block '%s' is not reachable from entry block '%s' in CFG!\n", label.c_str(), entryBlock.c_str());
            abort();
        }
    }

    for (const std::string& label : reachableFromEntryWithoutDominator)
    {
        ReleaseAssert(reachableFromEntry.count(label));
    }

    std::vector<std::string> result;
    for (const std::string& label : reachableFromEntry)
    {
        if (!reachableFromEntryWithoutDominator.count(label))
        {
            result.push_back(label);
        }
    }

    ReleaseAssert(result.size() + reachableFromEntryWithoutDominator.size() == reachableFromEntry.size());
    return result;
}

}   // namespace dast
