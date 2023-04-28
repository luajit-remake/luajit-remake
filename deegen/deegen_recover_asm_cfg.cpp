#include "deegen_recover_asm_cfg.h"
#include "deegen_magic_asm_helper.h"

namespace dast {

void DeegenAsmCfg::AnalyzeIndirectBranch(X64AsmFile* file, llvm::Function* func, bool addHumanDebugComment)
{
    using namespace llvm;

    file->Validate();

    ReleaseAssert(file->m_slowpath.empty());
    ReleaseAssert(file->m_icPath.empty());
    ReleaseAssert(!file->m_hasAnalyzedIndirectBranchTargets);
    file->m_hasAnalyzedIndirectBranchTargets = true;

    std::unordered_set<BasicBlock*> allLLVMBBs;
    for (BasicBlock& bb : *func)
    {
        ReleaseAssert(!allLLVMBBs.count(&bb));
        allLLVMBBs.insert(&bb);
    }

    // Compute LLVM BB reachability graph
    //
    std::unordered_map<BasicBlock*, std::unordered_set<BasicBlock*>> reachability;
    for (BasicBlock& bb1 : *func)
    {
        std::unordered_set<BasicBlock*> reachableSet;
        std::queue<BasicBlock*> q;
        reachableSet.insert(&bb1);
        q.push(&bb1);
        while (!q.empty())
        {
            BasicBlock* bb = q.front();
            q.pop();
            for (BasicBlock* succ : successors(bb))
            {
                if (!reachableSet.count(succ))
                {
                    reachableSet.insert(succ);
                    q.push(succ);
                }
            }
        }
        reachability[&bb1] = reachableSet;
    }

    // We assume that LLVM debug info on instruction origin might be lost, but never inaccurate (because LLVM claims they are accurate).
    //
    // We use a stupid constraint solver to find an over-approximation of the CFG.
    //
    // Let 'l' represent an ASM label.
    // Let BB(l) denote the set of possible LLVM basic blocks that ASM label 'l' corresponds to.
    // Note that it is possible that BB(l) *actually* corresponds to multiple LLVM basic blocks, due to tail merge.
    // (And it is also possible for one LLVM BB to corresponds to multiple ASM blocks, due to LLVM lowering-introduced control flow,
    // e.g., SelectInst/SwitchInst, or tail duplication, but this doesn't matter for us).
    //
    // Let 'j' denote an indirect jump.
    // Let targetBB(j) denote the set of LLVM basic blocks that indirect jump j may jump to, we know this accurately from LLVM IR due to annotation
    //
    // Let S1, S2 be two sets of LLVM basic blocks, and reachable(S1, S2) := true if exists bb1 in S1 and bb2 in S2 s.t. bb1 can reach bb2
    //
    // For every ASM label 'l', we inspect the LLVM basic block origin of each instruction in 'l' until we hit a branchable instruction.
    // and initialize BB(l) to consist of these basic blocks. If none is found (which means the LLVM debug info is lost),
    // we initialize BB(l) to the full set.
    //
    // For every indirect jump 'j', we inspect its LLVM origin to figure out its possible target BB set 'targetBB(j)'.
    //
    // Then we can propagate to fixpoint using the rules below:
    // 1. A direct jmp inside ASM block 'l1' to ASM block 'l2' imposes a constraint of reachable(BB(l1), BB(l2)), we can use it to shrink BB(l1)
    // 2. An instruction with a known LLVM BB origin 'bb2' in ASM block 'l' imposes a constraint of reachable(BB(l), bb2), we can use it to shrink BB(l)
    // 3. An indirect jmp j inside ASM block 'l1', imposes a constraint of reachable(BB(l1), targetBB(j)), we can use it to shrink BB(l1)
    //

    std::unordered_map<std::string, std::vector<BasicBlock*>> BB;
    std::unordered_map<std::string, std::vector<BasicBlock*>> indirectBrTargetBB;

    for (X64AsmBlock* block : file->m_blocks)
    {
        std::unordered_set<BasicBlock*> originSet;
        if (block == file->m_blocks[0])
        {
            originSet.insert(&func->getEntryBlock());
        }
        else
        {
            for (X64AsmLine& line : block->m_lines)
            {
                ReleaseAssert(line.IsInstruction());
                if (line.m_originCertain != nullptr)
                {
                    BasicBlock* bb = line.m_originCertain->getParent();
                    ReleaseAssert(bb != nullptr);
                    ReleaseAssert(allLLVMBBs.count(bb));
                    originSet.insert(bb);
                }
                if (line.IsDirectUnconditionalJumpInst() || line.IsConditionalJumpInst() || line.IsIndirectJumpInst())
                {
                    break;
                }
            }

            if (originSet.empty())
            {
                originSet = allLLVMBBs;
            }
        }
        ReleaseAssert(!originSet.empty());

        std::string label = block->m_normalizedLabelName;
        for (BasicBlock* bb : originSet)
        {
            BB[label].push_back(bb);
        }
    }

    // If the block ends with a tail call, we can pinpoint it to be coming from the LLVM BBs ending with the same tail call,
    // then we know this ASM block must come from one of the LLVM BBs that can reach the terminal BB.
    // This allows us to further narrow down the BB set.
    //
    for (X64AsmBlock* block : file->m_blocks)
    {
        X64AsmLine& terminator = block->m_lines.back();
        std::unordered_set<BasicBlock*> termBBSet;
        if (terminator.IsIndirectJumpInst())
        {
            // We've asserted this when we parse the IndirectBr magic asm annotation, so should always be true here
            //
            ReleaseAssert(terminator.m_originCertainList.size() > 0);
            for (Instruction* origin : terminator.m_originCertainList)
            {
                ReleaseAssert(origin->getParent() != nullptr);
                termBBSet.insert(origin->getParent());
            }
        }
        else if (terminator.IsDirectUnconditionalJumpInst())
        {
            ReleaseAssert(terminator.NumWords() == 2);
            std::string label = terminator.GetWord(1);
            if (file->m_labelNormalizer.QueryLabelExists(label) || label.starts_with("."))
            {
                // Not an external function, don't bother
                //
                continue;
            }
            // Try to find all the LLVM BB that ends with tail call to that label name
            //
            for (BasicBlock& bb : *func)
            {
                CallInst* tc = bb.getTerminatingMustTailCall();
                if (tc != nullptr)
                {
                    Value* calledOperand = tc->getCalledOperand();
                    if (isa<GlobalValue>(calledOperand))
                    {
                        GlobalValue* gv = cast<GlobalValue>(calledOperand);
                        if (gv->getName() == label)
                        {
                            termBBSet.insert(&bb);
                        }
                    }
                }
            }
        }
        else
        {
            continue;
        }

        ReleaseAssert(!termBBSet.empty());
        std::vector<BasicBlock*> newList;
        for (BasicBlock* bb : BB[block->m_normalizedLabelName])
        {
            ReleaseAssert(reachability.count(bb));
            bool ok = false;
            for (BasicBlock* dst : termBBSet)
            {
                if (reachability[bb].count(dst))
                {
                    ok = true;
                    break;
                }
            }
            if (ok)
            {
                newList.push_back(bb);
            }
        }
        ReleaseAssert(newList.size() > 0);
        BB[block->m_normalizedLabelName] = newList;
    }

    // Initialize 'targetBB' for each indirect branch
    //
    for (X64AsmBlock* block : file->m_blocks)
    {
        X64AsmLine& terminator = block->m_lines.back();
        if (terminator.IsIndirectJumpInst())
        {
            ReleaseAssert(terminator.m_originCertainList.size() > 0);
            std::unordered_set<BasicBlock*> bbSet;
            bool isTailDispatch = false;
            bool isIndirectBr = false;
            for (Instruction* origin : terminator.m_originCertainList)
            {
                if (isa<CallInst>(origin))
                {
                    // This is a tail dispatch to somewhere outside the function, don't bother
                    //
                    ReleaseAssert(cast<CallInst>(origin)->isMustTailCall());
                    isTailDispatch = true;
                }
                else
                {
                    isIndirectBr = true;
                    if (isa<IndirectBrInst>(origin))
                    {
                        IndirectBrInst* ibi = cast<IndirectBrInst>(origin);
                        for (uint32_t i = 0; i < ibi->getNumDestinations(); i++)
                        {
                            bbSet.insert(ibi->getDestination(i));
                        }
                    }
                    else
                    {
                        ReleaseAssert(isa<SwitchInst>(origin));
                        SwitchInst* si = cast<SwitchInst>(origin);
                        bbSet.insert(si->getDefaultDest());
                        for (auto& caseIt : si->cases())
                        {
                            bbSet.insert(caseIt.getCaseSuccessor());
                        }
                    }
                }
            }

            if (isTailDispatch)
            {
                ReleaseAssert(!isIndirectBr);
                if (addHumanDebugComment)
                {
                    std::string humanDebugText = " # [CFG]: dest = <INDIRECT_TAILCALL>";
                    terminator.m_trailingComments += humanDebugText;
                }
            }
            else
            {
                ReleaseAssert(!isTailDispatch);
                ReleaseAssert(bbSet.size() > 0);
                std::vector<BasicBlock*> bbList;
                for (BasicBlock* bb : bbSet)
                {
                    bbList.push_back(bb);
                }
                indirectBrTargetBB[block->m_normalizedLabelName] = bbList;
            }
        }
    }

    auto removeBBInSet = [&](const std::vector<BasicBlock*>& input, const std::unordered_set<BasicBlock*>& removeSet) WARN_UNUSED -> std::vector<BasicBlock*>
    {
        std::vector<BasicBlock*> output;
        for (BasicBlock* bb : input)
        {
            if (!removeSet.count(bb))
            {
                output.push_back(bb);
            }
        }
        ReleaseAssert(input.size() == removeSet.size() + output.size());
        return output;
    };

    // Given ASM label 'l1', 'l2' and constraint reachable(BB(l1), BB(l2)), shrink BB(l1).
    // Return true if did something.
    //
    auto propagateConstraintLabelLabel = [&](const std::string& l1, const std::string& l2) WARN_UNUSED -> bool
    {
        bool didSomething = false;
        while (true)
        {
            std::unordered_set<BasicBlock*> removeSet;
            for (BasicBlock* bb1 : BB[l1])
            {
                bool ok = false;
                for (BasicBlock* bb2 : BB[l2])
                {
                    if (reachability[bb1].count(bb2))
                    {
                        ok = true;
                        break;
                    }
                }
                if (!ok)
                {
                    removeSet.insert(bb1);
                }
            }
            if (!removeSet.empty())
            {
                BB[l1] = removeBBInSet(BB[l1], removeSet);
                if (BB[l1].empty())
                {
                    func->getParent()->dump();
                    fprintf(stderr, "%s", file->ToString().c_str());
                    fprintf(stderr, "%s %s\n", l1.c_str(), l2.c_str());
                }
                ReleaseAssert(!BB[l1].empty());
                didSomething = true;
            }
            else
            {
                break;
            }
        }
        return didSomething;
    };

    // Given ASM label 'l1', LLVM Basic Block 'bb' and constraint reachable(BB(l1), bb), shrink BB(l1)
    // Return true if did something.
    //
    auto propagateConstraintLabelBB = [&](const std::string& l1, llvm::BasicBlock* bb2) WARN_UNUSED -> bool
    {
        std::unordered_set<BasicBlock*> removeSet;
        for (BasicBlock* bb1 : BB[l1])
        {
            if (!reachability[bb1].count(bb2))
            {
                removeSet.insert(bb1);
            }
        }
        if (!removeSet.empty())
        {
            BB[l1] = removeBBInSet(BB[l1], removeSet);
            ReleaseAssert(!BB[l1].empty());
            return true;
        }
        else
        {
            return false;
        }
    };

    // Figure out all the label->bb constraints
    //
    std::vector<std::pair<std::string, BasicBlock*>> labelBBConstraintList;
    for (X64AsmBlock* block : file->m_blocks)
    {
        std::unordered_set<BasicBlock*> bbSet;
        for (X64AsmLine& line : block->m_lines)
        {
            ReleaseAssert(line.IsInstruction());
            if (line.m_originCertain != nullptr)
            {
                BasicBlock* bb = line.m_originCertain->getParent();
                ReleaseAssert(bb != nullptr);
                bbSet.insert(bb);
            }
        }

        if (indirectBrTargetBB.count(block->m_normalizedLabelName))
        {
            ReleaseAssert(block->m_lines.back().IsIndirectJumpInst());
            for (BasicBlock* bb : indirectBrTargetBB[block->m_normalizedLabelName])
            {
                bbSet.insert(bb);
            }
        }

        for (BasicBlock* bb : bbSet)
        {
            labelBBConstraintList.push_back(std::make_pair(block->m_normalizedLabelName, bb));
        }
    }

    // Propagate to fixpoint using label->bb constraints
    // Note that this can be done independently from the label->label constraint as one can see from the rule
    //
    while (true)
    {
        bool didSomething = false;
        for (auto& constraint : labelBBConstraintList)
        {
            if (propagateConstraintLabelBB(constraint.first, constraint.second))
            {
                didSomething = true;
            }
        }
        if (!didSomething)
        {
            break;
        }
    }

    // Figure out all the label->label constraints
    //
    std::vector<std::pair<std::string, std::string>> labelLabelConstraintList;
    for (X64AsmBlock* block : file->m_blocks)
    {
        std::unordered_set<std::string> dstLabelSet;
        for (X64AsmLine& line : block->m_lines)
        {
            if (line.IsDirectUnconditionalJumpInst() || line.IsConditionalJumpInst())
            {
                std::string humanDebugText = " # [CFG]: dest = <DIRECT_TAILCALL>";
                ReleaseAssert(line.NumWords() == 2);
                std::string rawLabel = line.GetWord(1);

                // Don't bother if the label is not a local label
                //
                if (file->m_labelNormalizer.QueryLabelExists(rawLabel))
                {
                    std::string normalizedTargetLabel = file->m_labelNormalizer.GetNormalizedLabel(rawLabel);
                    ReleaseAssert(BB.count(normalizedTargetLabel));
                    dstLabelSet.insert(normalizedTargetLabel);
                    humanDebugText = " # [CFG]: dest = " + normalizedTargetLabel;
                    ReleaseAssert(humanDebugText.find("\n") == std::string::npos);
                }
                if (addHumanDebugComment)
                {
                    line.m_trailingComments += humanDebugText;
                }
            }
        }

        ReleaseAssert(BB.count(block->m_normalizedLabelName));
        for (const std::string& dstLabel : dstLabelSet)
        {
            labelLabelConstraintList.push_back(std::make_pair(block->m_normalizedLabelName, dstLabel));
        }
    }

    // Propagate to fixpoint
    //
    while (true)
    {
        bool didSomething = false;
        for (auto& constraint : labelLabelConstraintList)
        {
            if (propagateConstraintLabelLabel(constraint.first, constraint.second))
            {
                didSomething = true;
            }
        }
        if (!didSomething)
        {
            break;
        }
    }

    // Sanity check the result makes sense
    //
    for (X64AsmBlock* block : file->m_blocks)
    {
        ReleaseAssert(BB.count(block->m_normalizedLabelName));
        ReleaseAssert(!BB[block->m_normalizedLabelName].empty());
    }

    for (auto& constraint : labelBBConstraintList)
    {
        ReleaseAssert(!propagateConstraintLabelBB(constraint.first, constraint.second));
    }

    for (auto& constraint : labelLabelConstraintList)
    {
        ReleaseAssert(!propagateConstraintLabelLabel(constraint.first, constraint.second));
    }

    // Determine the potential destination labels of indirect jumps using the BB map
    //
    std::unordered_map<std::string, std::vector<std::string>> indirectBrTargetLabels;
    for (auto& indirectJmp : indirectBrTargetBB)
    {
        std::string fromBlockLabel = indirectJmp.first;
        std::vector<BasicBlock*> dstBBList = indirectJmp.second;
        std::unordered_set<BasicBlock*> dstBBSet;
        for (BasicBlock* bb : dstBBList)
        {
            dstBBSet.insert(bb);
        }

        std::vector<std::string> targetLabels;
        for (X64AsmBlock* block : file->m_blocks)
        {
            std::string label = block->m_normalizedLabelName;
            ReleaseAssert(BB.count(label));
            bool possible = false;
            for (BasicBlock* bb : BB[label])
            {
                if (dstBBSet.count(bb))
                {
                    possible = true;
                    break;
                }
            }
            if (possible)
            {
                targetLabels.push_back(label);
            }
        }
        ReleaseAssert(!indirectBrTargetLabels.count(fromBlockLabel));
        ReleaseAssert(targetLabels.size() > 0);
        indirectBrTargetLabels[fromBlockLabel] = targetLabels;
    }
    ReleaseAssert(indirectBrTargetLabels.size() == indirectBrTargetBB.size());

    // Update m_indirectBranchTargets, and write comments for debug if requested
    //
    for (X64AsmBlock* block : file->m_blocks)
    {
        std::string label = block->m_normalizedLabelName;
        if (indirectBrTargetLabels.count(label))
        {
            X64AsmLine& line = block->m_lines.back();
            ReleaseAssert(line.IsIndirectJumpInst());

            // Update m_indirectBranchTargets
            //
            ReleaseAssert(block->m_indirectBranchTargets.empty());
            block->m_indirectBranchTargets = indirectBrTargetLabels[label];

            // Write comments
            //
            std::string humanDebugText = " # [CFG]: dest =";
            bool isFirst = true;
            for (const std::string& dstLabel : indirectBrTargetLabels[label])
            {
                ReleaseAssert(file->m_labelNormalizer.QueryLabelExists(dstLabel));
                ReleaseAssert(file->m_labelNormalizer.GetNormalizedLabel(dstLabel) == dstLabel);
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
