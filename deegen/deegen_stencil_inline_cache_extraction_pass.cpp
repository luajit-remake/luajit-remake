#include "deegen_stencil_inline_cache_extraction_pass.h"
#include "deegen_recover_asm_cfg.h"

namespace dast {

std::vector<DeegenStencilExtractedICAsm> WARN_UNUSED RunStencilInlineCacheLogicExtractionPass(X64AsmFile* file /*inout*/,
                                                                                              DeegenAsmCfg cfg,
                                                                                              std::vector<std::string> icLabels)
{
    ReleaseAssert(file->m_blocks.size() > 0);

    std::unordered_map<std::string, X64AsmBlock*> labelToBlockMap;
    for (X64AsmBlock* block : file->m_blocks)
    {
        ReleaseAssert(!labelToBlockMap.count(block->m_normalizedLabelName));
        labelToBlockMap[block->m_normalizedLabelName] = block;
    }

    for (const std::string& label : icLabels)
    {
        ReleaseAssert(labelToBlockMap.count(label));
    }

    std::unordered_set<std::string> reachableFromEntry;

    {
        std::function<void(const std::string&)> dfs = [&](const std::string& label)
        {
            ReleaseAssert(labelToBlockMap.count(label));
            if (reachableFromEntry.count(label))
            {
                return;
            }
            reachableFromEntry.insert(label);
            for (const std::string& succ : cfg.SuccOf(label))
            {
                dfs(succ);
            }
        };
        dfs(file->m_blocks[0]->m_normalizedLabelName);
    }

    // Note that this returns the original pointer to X64AsmBlock, they are not cloned
    //
    auto extractIcBlocks = [&](std::string entry) WARN_UNUSED -> std::vector<X64AsmBlock*>
    {
        std::unordered_set<std::string> visited;
        std::function<void(const std::string&)> dfs = [&](const std::string& label)
        {
            ReleaseAssert(labelToBlockMap.count(label));
            if (reachableFromEntry.count(label))
            {
                return;
            }
            if (visited.count(label))
            {
                return;
            }
            visited.insert(label);
            for (const std::string& succ : cfg.SuccOf(label))
            {
                dfs(succ);
            }
        };

        if (reachableFromEntry.count(entry))
        {
            fprintf(stderr, "[ERROR] Unexpected: IC entry point is reachable from main entry point!\n");
            fprintf(stderr, "IC entry label: %s\n", entry.c_str());
            fprintf(stderr, "%s\n", file->ToString().c_str());
            abort();
        }

        dfs(entry);
        ReleaseAssert(visited.size() > 0);

        // Put the extracted blocks into the natural order they appear in ASM
        //
        std::vector<X64AsmBlock*> list;
        for (X64AsmBlock* block : file->m_blocks)
        {
            std::string label = block->m_normalizedLabelName;
            if (visited.count(label))
            {
                list.push_back(block);
            }
        }
        ReleaseAssert(list.size() == visited.size());

        {
            std::unordered_set<X64AsmBlock*> checkUnique;
            for (X64AsmBlock* block : list)
            {
                ReleaseAssert(!reachableFromEntry.count(block->m_normalizedLabelName));
                ReleaseAssert(!checkUnique.count(block));
                checkUnique.insert(block);
            }
        }

        // The entry block must come first in the reordered blocks. Note that this is not necessarily true in 'list'!
        //
        size_t entryOrd = static_cast<size_t>(-1);
        for (size_t i = 0; i < list.size(); i++)
        {
            if (list[i]->m_normalizedLabelName == entry)
            {
                entryOrd = i;
                break;
            }
        }
        ReleaseAssert(entryOrd != static_cast<size_t>(-1));

        // Reorder blocks to minimize jumps and to put the entry block at first
        //
        std::vector<X64AsmBlock*> result = X64AsmBlock::ReorderBlocksToMaximizeFallthroughs(list, entryOrd);
        ReleaseAssert(result[0] == list[entryOrd]);
        return result;
    };

    std::vector<DeegenStencilExtractedICAsm> r;
    for (const std::string& entryLabel : icLabels)
    {
        r.push_back({
            .m_blocks = extractIcBlocks(entryLabel),
            .m_owner = file
        });
    }

    std::vector<X64AsmBlock*> mainFnBlocks;
    for (X64AsmBlock* block : file->m_blocks)
    {
        if (reachableFromEntry.count(block->m_normalizedLabelName))
        {
            mainFnBlocks.push_back(block);
        }
    }

    // Sanity check that all blocks have shown up.
    // We require our caller to pass in all IC entry points. So if any block didn't show up, something is likely wrong with our CFG analysis.
    //
    {
        std::unordered_set<X64AsmBlock*> allBlocks;
        for (X64AsmBlock* block : mainFnBlocks)
        {
            allBlocks.insert(block);
        }
        for (DeegenStencilExtractedICAsm& item : r)
        {
            for (X64AsmBlock* block : item.m_blocks)
            {
                allBlocks.insert(block);
            }
        }
        for (X64AsmBlock* block : file->m_blocks)
        {
            if (!allBlocks.count(block))
            {
                fprintf(stderr, "[ERROR] Found a ASM block that is not used by the main logic nor any of the IC logic!\n");
                fprintf(stderr, "This is either because you didn't pass in all the IC entry points, or a CFG analysis bug.\n");
                fprintf(stderr, "Missing ASM block label: %s\n", block->m_normalizedLabelName.c_str());
                fprintf(stderr, "%s\n", file->ToString().c_str());
                abort();
            }
            allBlocks.erase(allBlocks.find(block));
        }
        ReleaseAssert(allBlocks.empty());
    }

    // Clone each extracted block, just to make sure all of them are safe to modify.
    // This must happen after the above sanity check.
    //
    for (DeegenStencilExtractedICAsm& item : r)
    {
        std::vector<X64AsmBlock*> clonedList;
        for (X64AsmBlock* block : item.m_blocks)
        {
            clonedList.push_back(block->Clone(file));
        }
        item.m_blocks = clonedList;
    }

    // Figure out the blocks that doesn't belong to 'mainFnBlocks' and move them to m_icPath
    //
    {
        std::unordered_set<X64AsmBlock*> mainFnBlockSet;
        for (X64AsmBlock* block : mainFnBlocks)
        {
            ReleaseAssert(!mainFnBlockSet.count(block));
            mainFnBlockSet.insert(block);
        }

        std::vector<X64AsmBlock*> icPathBlocks;
        for (X64AsmBlock* block : file->m_blocks)
        {
            if (!mainFnBlockSet.count(block))
            {
                icPathBlocks.push_back(block);
            }
        }
        ReleaseAssert(mainFnBlockSet.size() + icPathBlocks.size() == file->m_blocks.size());
        file->m_icPath = icPathBlocks;
    }

    // Update the main function block list to remove the blocks only used by IC
    //
    file->m_blocks = mainFnBlocks;

    file->Validate();

    return r;
}

}   // namespace dast
