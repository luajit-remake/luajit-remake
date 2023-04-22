#include "deegen_stencil_lowering_pass.h"
#include "deegen_recover_asm_cfg.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/LoopInfo.h"

namespace dast {

// Split the function into a fast path part and a slow path part
//
// Note that the ASM blocks in m_slowPath will already have been reordered to minimize fallthrough jmps
// Also, the first ASM block has been prefixed with ".deegen_slow" section annotation,
// so one is not supposed to reorder the blocks in m_slowPath any more
//
// The ASM blocks in m_fastPath retain the original order they show up in the input file
//
static void RunAsmHotColdSplittingPass(X64AsmFile* file /*inout*/, std::vector<llvm::BasicBlock*> llvmColdBlocks)
{
    ReleaseAssert(file->m_slowpath.size() == 0);

    std::unordered_set<llvm::BasicBlock*> llvmColdBBSet;
    for (llvm::BasicBlock* bb : llvmColdBlocks)
    {
        llvmColdBBSet.insert(bb);
    }

    std::vector<X64AsmBlock*> list;
    std::vector<X64AsmBlock*> coldAsmBlocks;
    for (X64AsmBlock* block : file->m_blocks)
    {
        bool isEntryBlock = (block == file->m_blocks[0]);

        // We find all the transition lines from hot to cold or from cold to hot.
        // For each transition line, search backward for a conditional jump instruction, and split the block there
        //
        X64AsmBlock* curBlock = block;
        while (true)
        {
            ReleaseAssert(curBlock->m_lines.size() > 0);

            bool shouldSplit = false;
            bool seenColdBB = false;
            bool seenHotBB = false;
            size_t line = 0;
retry:
            while (line < curBlock->m_lines.size())
            {
                if (curBlock->m_lines[line].m_originCertain != nullptr)
                {
                    llvm::BasicBlock* originBB = curBlock->m_lines[line].m_originCertain->getParent();
                    ReleaseAssert(originBB != nullptr);
                    if (llvmColdBBSet.count(originBB))
                    {
                        if (seenHotBB)
                        {
                            shouldSplit = true;
                            break;
                        }
                        seenColdBB = true;
                    }
                    else
                    {
                        if (seenColdBB)
                        {
                            shouldSplit = true;
                            break;
                        }
                        seenHotBB = true;
                    }
                }
                line++;
            }

            if (!shouldSplit)
            {
                ReleaseAssert(line == curBlock->m_lines.size());
                list.push_back(curBlock);
                if (seenColdBB)
                {
                    ReleaseAssert(!isEntryBlock);
                    coldAsmBlocks.push_back(curBlock);
                }
                break;
            }

            ReleaseAssert(line < curBlock->m_lines.size());

            size_t oldLine = line;

            ReleaseAssert(line > 0);
            while (line > 0)
            {
                if (curBlock->m_lines[line - 1].IsConditionalJumpInst())
                {
                    break;
                }
                line--;
            }

            if (line == 0)
            {
                // It seems like we have a straightline sequence of instruction, but we believed part of it is cold and part of it is hot
                // The likely reason is LLVM tail-duplicated a hot BB to a cold BB.
                //
                // This assert is not needed for correctness, just interesting to know the case where the above theory fails
                //
                ReleaseAssert(seenColdBB);

                // This is a bit hacky, but for now don't bother too much, just scan down as if the hot-cold transition didn't happen
                //
                line = oldLine + 1;
                shouldSplit = false;
                goto retry;
            }

            X64AsmBlock* b1 = nullptr;
            X64AsmBlock* b2 = nullptr;
            curBlock->SplitAtLine(file, line, b1 /*out*/, b2 /*out*/);
            ReleaseAssert(b1 != nullptr && b2 != nullptr);

            list.push_back(b1);
            if (seenColdBB)
            {
                ReleaseAssert(!isEntryBlock);
                coldAsmBlocks.push_back(b1);
            }

            curBlock = b2;
            isEntryBlock = false;
        }
    }

    std::set<X64AsmBlock*> coldAsmBlockSet;
    for (X64AsmBlock* block : coldAsmBlocks)
    {
        ReleaseAssert(!coldAsmBlockSet.count(block));
        coldAsmBlockSet.insert(block);
    }

    std::vector<X64AsmBlock*> hotAsmBlocks;
    ReleaseAssert(list.size() > 0 && !coldAsmBlockSet.count(list[0]));
    for (X64AsmBlock* block: list)
    {
        if (!coldAsmBlockSet.count(block))
        {
            hotAsmBlocks.push_back(block);
        }
    }

    ReleaseAssert(hotAsmBlocks.size() + coldAsmBlocks.size() == list.size());
    ReleaseAssert(hotAsmBlocks.size() > 0);
    ReleaseAssert(hotAsmBlocks[0] == list[0]);

    if (coldAsmBlocks.size() > 0)
    {
        // Layout the cold ASM blocks to minimize jumps
        //
        coldAsmBlocks = X64AsmBlock::ReorderBlocksToMaximizeFallthroughs(coldAsmBlocks, 0 /*entryOrd*/);

        // Put the cold BBs into the slow path section
        // Very important to put the directive BEFORE m_prefixText, not after, because m_prefixText contains labels!
        //
        {
            std::string sectionDirective = "\t.section\t.text.deegen_slow,\"ax\",@progbits\n";
            coldAsmBlocks[0]->m_prefixText = sectionDirective + coldAsmBlocks[0]->m_prefixText;
        }
    }

    // Reset the section directive at function end
    // Similarly, important to append before m_fileFooter, not after
    // TODO: the section directive should be printed by ToString(), not appended by us
    //
    {
        std::string sectionDirective = "\t.section\t.text,\"ax\",@progbits\n";
        file->m_fileFooter = sectionDirective + file->m_fileFooter;
    }

    file->m_blocks = hotAsmBlocks;
    file->m_slowpath = coldAsmBlocks;
}

// A very crude pass that attempts to reorder code so that the fast path can fallthrough to
// the next bytecode, thus saving a jmp instruction
//
// It first find the desired jmp to move by looking at the magic asm pattern injected earlier.
// From that jmp, it searches backward until it finds a JCC or JMP instruction. Then:
//
// For JCC instruction: if it is jumping backward, ignore. Otherwise:
//  /- jcc ...
//  |  XXXXX
//  |  jmp next_bytecode
//  |  YYYYY
//  \->ZZZZZ
//
// If YYYYY fallthroughs to ZZZZZ, ignore. Otherwise we can rewrite it to:
//  /- jncc ...
//  |  ZZZZZ
//  |  YYYYY
//  \->XXXXX
//     jmp next_bytecode
//
// For JMP instruction:
//     jmp ...
//     XXXXX
//     jmp next_bytecode
//     YYYYY
//
// We can simply rewrite it to:
//     jmp ...
//     YYYYY
//     XXXXX
//     jmp next_bytecode
//
// Note that one common pattern is
//  /- jcc ...
//  |  jmp ... -|
//  \->XXXXX    |
//     jmp next |
//     YYYYY  <-|
// After the rewrite the jmp-to-YYYYY would be unnecessary.
// Fortunately due to how we canonicalize the ASM blocks, those jumps will be automatically removed in the end
//
static void AttemptToTransformDispatchToNextBytecodeIntoFallthrough(X64AsmFile* file /*inout*/,
                                                                    uint32_t locIdent,
                                                                    const std::string& fnNameForDebug,
                                                                    const std::string& fallthroughPlaceholderName)
{
    if (locIdent == 0)
    {
        return;
    }

    X64AsmBlock* targetBlock = nullptr;
    size_t targetBlockOrd = static_cast<size_t>(-1);
    for (size_t i = 0; i < file->m_blocks.size(); i++)
    {
        X64AsmBlock* block = file->m_blocks[i];
        for (X64AsmLine& line : block->m_lines)
        {
            if (line.m_rawLocIdent == locIdent)
            {
                targetBlock = block;
                targetBlockOrd = i;
                break;
            }
        }
    }

    if (targetBlock == nullptr)
    {
        // In rare cases, it's possible that the debug info for the tail jmp is simply lost..
        // In that case, simply pick an arbitrary tail jmp to 'fallthroughPlaceholderName' and do the optimization:
        // for normal use cases this should just work equally well.
        //
        for (size_t i = 0; i < file->m_blocks.size(); i++)
        {
            X64AsmBlock* block = file->m_blocks[i];
            ReleaseAssert(block->m_lines.size() > 0);
            if (block->m_lines.back().IsDirectUnconditionalJumpInst() && block->m_lines.back().GetWord(1) == fallthroughPlaceholderName)
            {
                targetBlock = block;
                targetBlockOrd = i;
                break;
            }
        }
    }

    if (targetBlock == nullptr)
    {
        // If we still can't find a candidate, all of the candidates should have been moved to slow path.
        // This is kinda weird but theoretically possible. In this case, just skip this optimization.
        //
        bool foundInSlowPath = false;
        for (size_t i = 0; i < file->m_slowpath.size(); i++)
        {
            X64AsmBlock* block = file->m_slowpath[i];
            ReleaseAssert(block->m_lines.size() > 0);
            if (block->m_lines.back().IsDirectUnconditionalJumpInst() && block->m_lines.back().GetWord(1) == fallthroughPlaceholderName)
            {
                foundInSlowPath = true;
                break;
            }
        }
        ReleaseAssert(foundInSlowPath);
        return;
    }

    ReleaseAssert(targetBlock != nullptr);
    ReleaseAssert(targetBlock->m_lines.size() > 0);
    ReleaseAssert(targetBlock->m_lines.back().IsDirectUnconditionalJumpInst());
    ReleaseAssert(targetBlock->m_lines.back().GetWord(1) == fallthroughPlaceholderName);

    if (targetBlock == file->m_blocks.back())
    {
        // Already last line, nothing to do
        //
        return;
    }

    std::unordered_map<std::string, size_t /*blockOrd*/> labelMap;
    for (size_t i = 0; i < file->m_blocks.size(); i++)
    {
        X64AsmBlock* block = file->m_blocks[i];
        ReleaseAssert(!labelMap.count(block->m_normalizedLabelName));
        labelMap[block->m_normalizedLabelName] = i;
    }

    size_t curBlockOrd = targetBlockOrd;
    while (true)
    {
        // Attempt to do the rewrite using the terminator barrier instruction of 'block'
        //
        if (curBlockOrd < targetBlockOrd)
        {
            // No merit to do the rewrite using a fallthrough: doing that is just introducing a new jump to remove another jump
            //
            if (!file->IsTerminatorInstructionFallthrough(curBlockOrd))
            {
                // This is a barrier, we can do rewrite
                //
                //     jmp ...                       jmp ...
                //     XXXXX                ===>     YYYYY
                //     jmp next_bytecode             XXXXX
                //     YYYYY                         jmp next_bytecode
                //
                std::vector<X64AsmBlock*> newList;
                for (size_t i = 0; i <= curBlockOrd; i++)
                {
                    newList.push_back(file->m_blocks[i]);
                }
                for (size_t i = targetBlockOrd + 1; i < file->m_blocks.size(); i++)
                {
                    newList.push_back(file->m_blocks[i]);
                }
                for (size_t i = curBlockOrd + 1; i <= targetBlockOrd; i++)
                {
                    newList.push_back(file->m_blocks[i]);
                }
                ReleaseAssert(newList.size() == file->m_blocks.size());
                file->m_blocks = newList;
                return;
            }
        }

        X64AsmBlock* block = file->m_blocks[curBlockOrd];

        // Reverse scan for JCC instruction
        //
        for (size_t instOrd = block->m_lines.size(); instOrd-- > 0; /*no-op*/)
        {
            if (block->m_lines[instOrd].IsConditionalJumpInst())
            {
                std::string dstLabel = block->m_lines[instOrd].GetWord(1);
                // If the label doesn't exist, it might be a symbol or a label in slow path, don't bother
                //
                if (labelMap.count(dstLabel))
                {
                    size_t definedBlockOrd = labelMap[dstLabel];
                    ReleaseAssert(file->m_blocks[definedBlockOrd]->m_normalizedLabelName == dstLabel);
                    if (definedBlockOrd > targetBlockOrd)
                    {
                        // We can do the rewrite if 'definedLine' cannot be reached by fallthrough
                        //
                        ReleaseAssert(definedBlockOrd > 0);
                        bool dstLabelCanBeReachedByFallthrough = file->IsTerminatorInstructionFallthrough(definedBlockOrd - 1);
                        if (!dstLabelCanBeReachedByFallthrough)
                        {
                            //  /- jcc ...                jncc   -|
                            //  |  XXXXX                  ZZZZZ   |
                            //  |  jmp next_bc    ===>    YYYYY   |
                            //  |  YYYYY                  XXXXX <--
                            //  \->ZZZZZ                  jmp next_bc
                            //
                            std::vector<X64AsmBlock*> newList;
                            for (size_t i = 0; i < curBlockOrd; i++)
                            {
                                newList.push_back(file->m_blocks[i]);
                            }

                            // Split 'block' after JCC
                            //
                            X64AsmBlock* p1 = nullptr;
                            X64AsmBlock* p2 = nullptr;
                            block->SplitAtLine(file, instOrd + 1, p1 /*out*/, p2 /*out*/);
                            ReleaseAssert(p1 != nullptr && p2 != nullptr);

                            ReleaseAssert(p1->m_lines.size() == instOrd + 2);

                            ReleaseAssert(p1->m_lines[instOrd].IsConditionalJumpInst());
                            ReleaseAssert(p1->m_lines[instOrd].GetWord(1) == file->m_blocks[definedBlockOrd]->m_normalizedLabelName);
                            ReleaseAssert(p1->m_lines[instOrd + 1].IsDirectUnconditionalJumpInst());
                            ReleaseAssert(p1->m_lines[instOrd + 1].GetWord(1) == p2->m_normalizedLabelName);

                            p1->m_lines[instOrd].FlipConditionalJumpCondition();
                            p1->m_lines[instOrd].GetWord(1) = p2->m_normalizedLabelName;
                            p1->m_lines[instOrd + 1].GetWord(1) = file->m_blocks[definedBlockOrd]->m_normalizedLabelName;
                            ReleaseAssert(p1->m_endsWithJmpToLocalLabel);
                            p1->m_terminalJmpTargetLabel = file->m_blocks[definedBlockOrd]->m_normalizedLabelName;

                            newList.push_back(p1);

                            for (size_t i = definedBlockOrd; i < file->m_blocks.size(); i++)
                            {
                                newList.push_back(file->m_blocks[i]);
                            }

                            for (size_t i = targetBlockOrd + 1; i < definedBlockOrd; i++)
                            {
                                newList.push_back(file->m_blocks[i]);
                            }

                            newList.push_back(p2);

                            for (size_t i = curBlockOrd + 1; i <= targetBlockOrd; i++)
                            {
                                newList.push_back(file->m_blocks[i]);
                            }

                            ReleaseAssert(newList.size() == file->m_blocks.size() + 1);
                            file->m_blocks = newList;
                            return;
                        }
                    }
                }
            }
        }

        if (curBlockOrd == 0)
        {
            fprintf(stderr, "[NOTE] Failed to rewrite JIT stencil to eliminate jmp to fallthrough (function name = %s).\n", fnNameForDebug.c_str());
            return;
        }

        curBlockOrd--;
    }
}

DeegenStencilLoweringPass WARN_UNUSED DeegenStencilLoweringPass::RunIrRewritePhase(llvm::Function* f, const std::string& fallthroughPlaceholderName)
{
    using namespace llvm;
    DeegenStencilLoweringPass r;

    ValidateLLVMFunction(f);

    DominatorTree dom(*f);
    LoopInfo li(dom);
    BranchProbabilityInfo bpi(*f, li);
    BlockFrequencyInfo bfi(*f, bpi, li);
    uint64_t entryFreq = bfi.getEntryFreq();
    ReleaseAssert(entryFreq != 0);

    auto getBBFreq = [&](BasicBlock* bb) WARN_UNUSED -> double
    {
        uint64_t bbFreq = bfi.getBlockFreq(bb).getFrequency();
        return static_cast<double>(bbFreq) / static_cast<double>(entryFreq);
    };

    // Figure out all the cold basic blocks
    // TODO: we also need to add the IC slowpath, which is not known by LLVM to be cold because CallBr doesn't support branch weight metadata
    //
    for (BasicBlock& bb : *f)
    {
        if (bb.empty())
        {
            continue;
        }
        double bbFreq = getBBFreq(&bb);
        if (bbFreq < 0.02)
        {
            r.m_coldBlocks.push_back(&bb);
        }
    }

    r.m_diInfo = InjectedMagicDiLocationInfo::RunOnFunction(f);

    r.m_locIdentForJmpToFallthroughCandidate = 0;
    r.m_nextBytecodeFallthroughPlaceholderName = fallthroughPlaceholderName;

    // If requested, find the most frequent BB that fallthroughs to the next bytecode,
    // and record it for future jmp-to-fallthrough transform.
    //
    if (fallthroughPlaceholderName != "")
    {
        double maxFreq = -1;
        BasicBlock* selectedBB = nullptr;
        for (BasicBlock& bb : *f)
        {
            if (bb.empty()) { continue; }
            CallInst* ci = bb.getTerminatingMustTailCall();
            if (ci != nullptr)
            {
                // Must not use 'getCalledFunction', because our callee is forcefully casted from a non-function symbol!
                //
                Value* callee = ci->getCalledOperand();
                if (callee != nullptr && isa<GlobalVariable>(callee) && cast<GlobalVariable>(callee)->getName() == fallthroughPlaceholderName)
                {
                    double bbFreq = getBBFreq(&bb);
                    if (bbFreq > maxFreq)
                    {
                        maxFreq = bbFreq;
                        selectedBB = &bb;
                    }
                }
            }
        }
        if (selectedBB != nullptr && maxFreq >= 0.02)
        {
            CallInst* ci = selectedBB->getTerminatingMustTailCall();
            ReleaseAssert(ci != nullptr);
            MDNode* md = ci->getMetadata(LLVMContext::MD_dbg);
            ReleaseAssert(md != nullptr && isa<DILocation>(md));
            DILocation* dil = cast<DILocation>(md);
            r.m_locIdentForJmpToFallthroughCandidate = dil->getLine();
        }
        else
        {
            r.m_locIdentForJmpToFallthroughCandidate = 0;
            /*
            fprintf(stderr, "[NOTE] Jmp-to-fallthrough rewrite is requested, but %s! Code generation will continue without this rewrite.\n",
                    (selectedBB == nullptr ? "no dispatch to the next bytecode can be found" : "the dispatch to next bytecode is in the slow path"));
            */
        }
    }

    return r;
}

std::string WARN_UNUSED DeegenStencilLoweringPass::RunAsmRewritePhase(const std::string& asmFile)
{
    std::unique_ptr<X64AsmFile> fileHolder = X64AsmFile::ParseFile(asmFile, m_diInfo);
    X64AsmFile* file = fileHolder.get();
    m_rawInputFileForAudit = file->Clone();

    DeegenAsmCfgAnalyzer dummy = DeegenAsmCfgAnalyzer::DoAnalysis(file, m_diInfo.GetFunc());

    RunAsmHotColdSplittingPass(file /*inout*/, m_coldBlocks);

    file->Validate();

    AttemptToTransformDispatchToNextBytecodeIntoFallthrough(file /*inout*/,
                                                            m_locIdentForJmpToFallthroughCandidate,
                                                            m_diInfo.GetFunc()->getName().str(),
                                                            m_nextBytecodeFallthroughPlaceholderName);

    file->Validate();

    return file->ToStringAndRemoveDebugInfo();
}

}   // namespace dast
