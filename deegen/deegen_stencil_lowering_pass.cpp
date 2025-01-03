#include "deegen_stencil_lowering_pass.h"
#include "deegen_recover_asm_cfg.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "deegen_call_inline_cache.h"
#include "deegen_stencil_inline_cache_extraction_pass.h"
#include "deegen_ast_inline_cache.h"

namespace dast {

// Split the function into a fast path part and a slow path part
//
// Note that after this pass, the ASM blocks in m_slowPath will already have been reordered to minimize fallthrough jmps
//
// The ASM blocks in m_fastPath retain the original order they show up in the input file
//
static void RunAsmHotColdSplittingPass(X64AsmFile* file /*inout*/,
                                       std::vector<llvm::BasicBlock*> llvmColdBlocks,
                                       std::unordered_set<std::string> forceFastPathBlocks,
                                       std::unordered_set<std::string> forceSlowPathBlocks)
{
    ReleaseAssert(file->m_slowpath.size() == 0);

    // Assert that 'forceFastPathBlocks' and 'forceSlowPathBlocks' should have no intersection
    //
    for (const std::string& label : forceFastPathBlocks)
    {
        ReleaseAssert(!forceSlowPathBlocks.count(label));
    }
    for (const std::string& label : forceSlowPathBlocks)
    {
        ReleaseAssert(!forceFastPathBlocks.count(label));
    }

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

        if (forceFastPathBlocks.count(block->m_normalizedLabelName))
        {
            list.push_back(block);
            continue;
        }

        if (forceSlowPathBlocks.count(block->m_normalizedLabelName))
        {
            ReleaseAssert(!isEntryBlock);
            list.push_back(block);
            coldAsmBlocks.push_back(block);
            continue;
        }

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
                bool canDetermineHotOrCold = false;
                bool isColdBlock = false;

                if (curBlock->m_lines[line].m_originCertain != nullptr)
                {
                    canDetermineHotOrCold = true;
                    llvm::BasicBlock* originBB = curBlock->m_lines[line].m_originCertain->getParent();
                    ReleaseAssert(originBB != nullptr);
                    isColdBlock = llvmColdBBSet.count(originBB);
                }

                if (line + 1 == curBlock->m_lines.size())
                {
                    X64AsmLine& asmLine = curBlock->m_lines[line];
                    if (asmLine.IsDirectUnconditionalJumpInst() && asmLine.HasDeegenTailCallAnnotation())
                    {
                        std::string tailCallDest = asmLine.GetWord(1);

                        // This is a tail call, try to figure out if this is a slow path tail call
                        // This is ugly, but for now..
                        //
                        if (tailCallDest.starts_with("__deegen_bytecode_") ||
                            tailCallDest.starts_with("__deegen_baseline_jit_") ||
                            tailCallDest.starts_with("__deegen_dfg_jit_"))
                        {
                            if (tailCallDest.find("_slow_path_") != std::string::npos ||
                                tailCallDest.find("_quickening_slowpath") != std::string::npos)
                            {
                                // If LLVM also has something to say, it must also be claiming that this is cold
                                //
                                ReleaseAssertImp(canDetermineHotOrCold, isColdBlock);
                                canDetermineHotOrCold = true;
                                isColdBlock = true;
                            }
                        }
                    }
                }

                if (canDetermineHotOrCold)
                {
                    if (isColdBlock)
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
                                                                    [[maybe_unused]] const std::string& fnNameForDebug,
                                                                    const std::string& fallthroughPlaceholderName,
                                                                    std::unordered_set<std::string> blocksMustNotSplit)
{
    if (locIdent == 0)
    {
        return;
    }

    if (file->m_blocks.size() == 0)
    {
        return;
    }

    // If the last line is already a "JMP fallthrough", nothing to do
    //
    {
        X64AsmBlock* block = file->m_blocks.back();
        ReleaseAssert(block->m_lines.size() > 0);
        if (block->m_lines.back().IsDirectUnconditionalJumpInst() && block->m_lines.back().GetWord(1) == fallthroughPlaceholderName)
        {
            return;
        }
    }

    X64AsmBlock* targetBlock = nullptr;
    size_t targetBlockOrd = static_cast<size_t>(-1);
    for (size_t i = 0; i < file->m_blocks.size(); i++)
    {
        X64AsmBlock* block = file->m_blocks[i];
        bool containsLocIdent = false;
        for (X64AsmLine& line : block->m_lines)
        {
            if (line.m_rawLocIdent == locIdent)
            {
                containsLocIdent = true;
                break;
            }
        }
        if (containsLocIdent)
        {
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

    // If we still can't find a candidate, try to look for a 'JCC fallthrough; JMP XXX' pattern.
    // If so, we can rewrite it to 'JNCC XXX; JMP fallthrough' and proceed with the transformation
    //
    // We record the place we did this rewrite, so if the transformation fails,
    // we can undo the rewrite so that we only modify the assembly if we are able to achieve something useful
    //
    size_t jccRewriteBlockOrd = static_cast<size_t>(-1);
    if (targetBlock == nullptr)
    {
        auto tryJccRewrite = [&](size_t blockOrd) WARN_UNUSED -> bool
        {
            ReleaseAssert(blockOrd < file->m_blocks.size());
            X64AsmBlock* block = file->m_blocks[blockOrd];
            if (block->m_lines.size() < 2)
            {
                return false;
            }
            X64AsmLine& terminator = block->m_lines.back();
            X64AsmLine& lineBeforeTerm = block->m_lines[block->m_lines.size() - 2];
            if (terminator.IsDirectUnconditionalJumpInst() &&
                lineBeforeTerm.IsConditionalJumpInst() &&
                lineBeforeTerm.GetWord(1) == fallthroughPlaceholderName)
            {
                ReleaseAssert(terminator.NumWords() == 2 && lineBeforeTerm.NumWords() == 2);
                // This rewrite is only correct if JMP follows immediately by JCC.
                // But the JMP instruction may have a pile of prefix texts (that are not normal assembly instructions).
                // I think this is normally harmless since these are usually harmless directives. I can only think of an edge case
                // where it contains ".byte" instructions (which we treat as opaque prefix bytes).
                // For sanity, check this case and do not proceed if the word ".byte" is found. Hopefully this is good enough..
                //
                if (terminator.m_prefixingText.find(".byte ") != std::string::npos)
                {
                    return false;
                }

                lineBeforeTerm.FlipConditionalJumpCondition();
                std::swap(lineBeforeTerm.GetWord(1), terminator.GetWord(1));

                block->m_endsWithJmpToLocalLabel = false;

                ReleaseAssert(jccRewriteBlockOrd == static_cast<size_t>(-1));
                ReleaseAssert(targetBlock == nullptr && targetBlockOrd == static_cast<size_t>(-1));
                jccRewriteBlockOrd = blockOrd;
                targetBlock = block;
                targetBlockOrd = blockOrd;

                file->Validate();

                return true;
            }
            return false;
        };

        // Try to use the block that corresponds to the line hint if possible
        //
        for (size_t i = 0; i < file->m_blocks.size(); i++)
        {
            X64AsmBlock* block = file->m_blocks[i];
            bool containsLocIdent = false;
            for (X64AsmLine& line : block->m_lines)
            {
                if (line.m_rawLocIdent == locIdent)
                {
                    containsLocIdent = true;
                    break;
                }
            }
            if (containsLocIdent)
            {
                if (tryJccRewrite(i))
                {
                    break;
                }
            }
        }

        // If the above fails, try any block
        //
        if (targetBlock == nullptr)
        {
            for (size_t i = 0; i < file->m_blocks.size(); i++)
            {
                if (tryJccRewrite(i))
                {
                    break;
                }
            }
        }

        ReleaseAssertIff(targetBlock != nullptr, jccRewriteBlockOrd != static_cast<size_t>(-1));
    }

    if (targetBlock == nullptr)
    {
        // If we still can't find a candidate, just skip this optimization.
        //
        //fprintf(stderr, "[NOTE] Failed to rewrite JIT stencil to eliminate jmp to fallthrough "
        //                "because we cannot find a 'JMP fallthrough' or a qualifying 'JCC fallthrough' instruction "
        //                "to do the rewrite (function name = %s).\n", fnNameForDebug.c_str());
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

        if (!blocksMustNotSplit.count(block->m_normalizedLabelName))
        {
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
        }

        if (curBlockOrd == 0)
        {
            //fprintf(stderr, "[NOTE] Failed to rewrite JIT stencil to eliminate jmp to fallthrough (function name = %s).\n", fnNameForDebug.c_str());
            break;
        }

        curBlockOrd--;
    }

    // Having reached here, the transform has failed
    // If we did the JCC rewrite, undo it so we don't change the assembly in any way
    //
    if (jccRewriteBlockOrd != static_cast<size_t>(-1))
    {
        ReleaseAssert(jccRewriteBlockOrd < file->m_blocks.size());
        X64AsmBlock* block = file->m_blocks[jccRewriteBlockOrd];
        ReleaseAssert(block->m_lines.size() >= 2);

        X64AsmLine& terminator = block->m_lines.back();
        X64AsmLine& lineBeforeTerm = block->m_lines[block->m_lines.size() - 2];
        ReleaseAssert(terminator.NumWords() == 2 && lineBeforeTerm.NumWords() == 2);
        ReleaseAssert(terminator.IsDirectUnconditionalJumpInst() && terminator.GetWord(1) == fallthroughPlaceholderName);
        ReleaseAssert(lineBeforeTerm.IsConditionalJumpInst());

        lineBeforeTerm.FlipConditionalJumpCondition();
        std::swap(lineBeforeTerm.GetWord(1), terminator.GetWord(1));

        if (file->m_labelNormalizer.QueryLabelExists(terminator.GetWord(1)))
        {
            block->m_endsWithJmpToLocalLabel = true;
            block->m_terminalJmpTargetLabel = file->m_labelNormalizer.GetNormalizedLabel(terminator.GetWord(1));
        }
        else
        {
            block->m_endsWithJmpToLocalLabel = false;
        }
    }
}

DeegenStencilLoweringPass WARN_UNUSED DeegenStencilLoweringPass::RunIrRewritePhase(
    llvm::Function* f, bool isLastStencilInBytecode, const std::string& fallthroughPlaceholderName)
{
    using namespace llvm;
    DeegenStencilLoweringPass r;

    ValidateLLVMFunction(f);

    DominatorTree dom(*f);
    LoopInfo li(dom);
    BranchProbabilityInfo bpi(*f, li);
    BlockFrequencyInfo bfi(*f, bpi, li);
    uint64_t entryFreq = bfi.getEntryFreq().getFrequency();
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

    r.m_isLastStencilInBytecode = isLastStencilInBytecode;
    r.m_shouldAssertNoGenericIcWithInlineSlab = false;
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

void DeegenStencilLoweringPass::ParseAsmFile(const std::string& asmFile)
{
    ReleaseAssert(m_rawInputFileForAudit.get() == nullptr && m_workFile.get() == nullptr);

    std::unique_ptr<X64AsmFile> fileHolder = X64AsmFile::ParseFile(asmFile, m_diInfo);
    X64AsmFile* file = fileHolder.get();
    m_rawInputFileForAudit = file->Clone();

    // AnalyzeIndirectBranch works by attempting to find mapping between ASM branches and LLVM CFG,
    // so it must be called before we do any transform to the ASM.
    //
    DeegenAsmCfg::AnalyzeIndirectBranch(file, m_diInfo.GetFunc(), false /*addDebugComment*/);

    file->Validate();

    m_workFile = std::move(fileHolder);
}

void DeegenStencilLoweringPass::RunAsmRewritePhase()
{
    X64AsmFile* file = m_workFile.get();
    ReleaseAssert(file != nullptr);

    // Lower the Call IC asm magic
    //
    using CallIcAsmTransformResult = DeegenCallIcLogicCreator::JitAsmTransformResult;
    std::vector<CallIcAsmTransformResult> callICs = DeegenCallIcLogicCreator::DoJitAsmTransform(file);

    file->Validate();

    // Lower the Generic IC asm magic
    //
    using GenericIcAsmTransformResult = AstInlineCache::JitAsmTransformResult;
    std::vector<GenericIcAsmTransformResult> genericICs = AstInlineCache::DoAsmTransformForJit(file);

    // Collect all the IC entry labels (Call IC and Generic IC)
    // Note that the order here must agree with the order we extract IC snippets later in this function!
    //
    std::vector<std::string> icEntryLabelList;
    for (CallIcAsmTransformResult& item : callICs)
    {
        icEntryLabelList.push_back(item.m_labelForDirectCallIc);
        icEntryLabelList.push_back(item.m_labelForClosureCallIc);
    }

    for (GenericIcAsmTransformResult& item : genericICs)
    {
        for (const std::string& label : item.m_labelForEffects)
        {
            icEntryLabelList.push_back(label);
        }
    }

    DeegenAsmCfg cfg = DeegenAsmCfg::GetCFG(file);

    std::vector<DeegenStencilExtractedICAsm> icAsmList = RunStencilInlineCacheLogicExtractionPass(file, cfg, icEntryLabelList);
    ReleaseAssert(icAsmList.size() == icEntryLabelList.size());

    // Run hot-cold splitting pass
    //
    {
        // Our IC creator requires for correctness that the patchable block is in the fast path, enforce it.
        //
        std::unordered_set<std::string> forceFastPathBlocks;
        for (CallIcAsmTransformResult& item : callICs)
        {
            ReleaseAssert(!forceFastPathBlocks.count(item.m_labelForSMCRegion));
            forceFastPathBlocks.insert(item.m_labelForSMCRegion);
        }
        for (GenericIcAsmTransformResult& item : genericICs)
        {
            ReleaseAssert(!forceFastPathBlocks.count(item.m_labelForSMCRegion));
            forceFastPathBlocks.insert(item.m_labelForSMCRegion);
        }

        // Unfortunately LLVM does not support BranchWeight metadata on CallBr, so it cannot understand by itself that
        // the IC slow path is cold. As an imperfect workaround, we forcefully put these blocks and any blocks dominated
        // by them into the slow path.
        //
        std::unordered_set<std::string> forceSlowPathBlocks;
        {
            std::vector<std::string> allIcSlowPathBlocks;
            for (CallIcAsmTransformResult& item : callICs)
            {
                allIcSlowPathBlocks.push_back(item.m_labelForCcIcMissLogic);
                allIcSlowPathBlocks.push_back(item.m_labelForDcIcMissLogic);
            }

            for (GenericIcAsmTransformResult& item : genericICs)
            {
                allIcSlowPathBlocks.push_back(item.m_labelForIcMissLogic);
            }

            std::vector<std::string> allForceSlowPathBlocksList = cfg.GetAllBlocksDominatedBy(allIcSlowPathBlocks,
                                                                                              file->m_blocks[0]->m_normalizedLabelName /*entryBlock*/);
            for (const std::string& label : allForceSlowPathBlocksList)
            {
                ReleaseAssert(!forceSlowPathBlocks.count(label));
                forceSlowPathBlocks.insert(label);
            }
            for (const std::string& label : allIcSlowPathBlocks)
            {
                ReleaseAssert(forceSlowPathBlocks.count(label));
            }

            for (const std::string& label : forceFastPathBlocks)
            {
                if (forceSlowPathBlocks.count(label))
                {
                    forceSlowPathBlocks.erase(forceSlowPathBlocks.find(label));
                }
            }
        }

        RunAsmHotColdSplittingPass(file /*inout*/, m_coldBlocks, forceFastPathBlocks, forceSlowPathBlocks);
    }

    file->Validate();

    // Reorder fast path blocks to maximize fallthroughs
    //
    ReleaseAssert(file->m_blocks.size() > 0);
    file->m_blocks = X64AsmBlock::ReorderBlocksToMaximizeFallthroughs(file->m_blocks, 0 /*entryOrd*/);

    file->Validate();

    // Having extracted IC, we can fix up the SMC region for the call ICs now
    //
    for (CallIcAsmTransformResult& item : callICs)
    {
        item.FixupSMCRegionAfterCFGAnalysis(file);
        file->Validate();
    }

    // If this is the last stencil in the bytecode, it is worth to attempt to transform the dispatch to next bytecode to a fallthrough. Note that:
    // 1. After this step the CFG is invalidated.
    // 2. This transform may need to split a block into two. We must not do this to any of the SMC regions of the ICs.
    //
    if (m_isLastStencilInBytecode && m_nextBytecodeFallthroughPlaceholderName != "")
    {
        std::unordered_set<std::string> blocksMustNotSplit;
        for (CallIcAsmTransformResult& item : callICs)
        {
            blocksMustNotSplit.insert(item.m_labelForSMCRegion);
        }
        for (GenericIcAsmTransformResult& item : genericICs)
        {
            blocksMustNotSplit.insert(item.m_labelForSMCRegion);
        }

        AttemptToTransformDispatchToNextBytecodeIntoFallthrough(file /*inout*/,
                                                                m_locIdentForJmpToFallthroughCandidate,
                                                                m_diInfo.GetFunc()->getName().str(),
                                                                m_nextBytecodeFallthroughPlaceholderName,
                                                                blocksMustNotSplit);
        file->Validate();

        ReleaseAssert(file->m_blocks.size() > 0);

        // Try to change all the "jmp fallthroughPlaceholder" to a direct jump to the end of the instruction stream
        //
        // Note that we cannot do this optimization if the bytecode has generic IC and the SMC region is at the tail position,
        // as in that case the generic IC may have inline slab, thus the length of the SMC region right now is not final and may be changed later.
        // This would change the length of the stencil and invalidate our jumps that hardcode the (wrong) length of the stencil!
        //
        bool mayHaveGenericIcWithInlineSlab = false;
        for (GenericIcAsmTransformResult& item : genericICs)
        {
            ReleaseAssert(file->m_labelNormalizer.QueryLabelExists(item.m_labelForSMCRegion));
            X64AsmBlock* smcBlock = file->FindBlockInFastPath(file->m_labelNormalizer.GetNormalizedLabel(item.m_labelForSMCRegion));
            ReleaseAssert(smcBlock != nullptr);

            X64AsmBlock* tailBlock = file->m_blocks.back();
            if (smcBlock == tailBlock)
            {
                mayHaveGenericIcWithInlineSlab = true;
            }
            else
            {
                // Ugly: even if smcBlock is not tail block, we still must check if there's only a "jmp fallthrough" instruction after smcBlock,
                // as it may be eliminated... Furthermore, only checking if there's one block exist after smcBlock is not correct as there may
                // be multiple blocks but each contains only a fallthrough to the next block...
                //
                size_t smcBlockOrd = static_cast<size_t>(-1);
                for (size_t blockOrd = 0; blockOrd < file->m_blocks.size(); blockOrd++)
                {
                    if (file->m_blocks[blockOrd] == smcBlock)
                    {
                        ReleaseAssert(smcBlockOrd == static_cast<size_t>(-1));
                        smcBlockOrd = blockOrd;
                    }
                }
                ReleaseAssert(smcBlockOrd != static_cast<size_t>(-1));
                ReleaseAssert(smcBlockOrd + 1 < file->m_blocks.size());

                size_t numEffectiveInsts = 0;
                for (size_t blockOrd = smcBlockOrd + 1; blockOrd < file->m_blocks.size(); blockOrd++)
                {
                    numEffectiveInsts += file->m_blocks[blockOrd]->m_lines.size();
                    if (file->IsTerminatorInstructionFallthrough(blockOrd)) { numEffectiveInsts--; }
                }
                ReleaseAssert(numEffectiveInsts > 0);
                if (numEffectiveInsts == 1)
                {
                    ReleaseAssert(tailBlock->m_lines.size() == 1);
                    if (tailBlock->m_lines.back().IsDirectUnconditionalJumpInst() &&
                        tailBlock->m_lines.back().GetWord(1) == m_nextBytecodeFallthroughPlaceholderName)
                    {
                        mayHaveGenericIcWithInlineSlab = true;
                    }
                }
            }
        }

        if (!mayHaveGenericIcWithInlineSlab)
        {
            // Caller logic will assert that the generic IC codegen agrees with us on this
            //
            m_shouldAssertNoGenericIcWithInlineSlab = true;

            // Note that we have to keep the tail fallthrough jump if it exists (even if it will be removed
            // by the later steps) to avoid edge cases such as empty blocks (or empty functions at all)..
            //
            // In fact, to simplify things (e.g., to avoid having jump to the end of the function, which our current X64AsmBlock model doesn't like),
            // we will always add a "jmp fallthroughPlaceholder" if the fast path not already ends with one,
            // then rewrite all the other "jmp fallthroughPlaceholder" to jump to that instruction instead.
            // After later passes eliminate the tail jump, all the jumps will correctly point to the end of the fast path, as desired.
            //
            X64AsmBlock* newTailBlock = X64AsmBlock::Create(file, m_nextBytecodeFallthroughPlaceholderName);
            ReleaseAssert(!newTailBlock->m_endsWithJmpToLocalLabel);
            file->m_blocks.push_back(newTailBlock);
            file->Validate();

            // Rewrite all existing "jmp fallthroughPlaceholder" instructions to jump to the new tail block instead
            //
            for (X64AsmBlock* block : file->m_blocks)
            {
                if (block == newTailBlock) { continue; }

                if (block->m_lines.back().IsDirectUnconditionalJumpInst() &&
                    block->m_lines.back().GetWord(1) == m_nextBytecodeFallthroughPlaceholderName)
                {
                    ReleaseAssert(!block->m_endsWithJmpToLocalLabel);
                    block->m_lines.back().GetWord(1) = newTailBlock->m_normalizedLabelName;
                    block->m_endsWithJmpToLocalLabel = true;
                    block->m_terminalJmpTargetLabel = newTailBlock->m_normalizedLabelName;
                }
            }
            file->Validate();
        }
    }

    cfg.m_cfg.clear();

    // The call IC and generic IC's SMC block ends with a jump. The jump must not be eliminated to a fallthrough even if it happens to
    // jump to the immediate following block, because the jump is only a placeholder.
    // We enforce this by figuring out all those jumps that happens to fallthrough, and adding a dummy block in between.
    // Note that this must happen after all the basic block orders has been finalized, as any further reordering can break this.
    //
    {
        std::unordered_set<std::string> preventFallthroughBlockSet;
        for (CallIcAsmTransformResult& item : callICs)
        {
            ReleaseAssert(!preventFallthroughBlockSet.count(item.m_labelForSMCRegion));
            preventFallthroughBlockSet.insert(item.m_labelForSMCRegion);
        }
        for (GenericIcAsmTransformResult& item : genericICs)
        {
            ReleaseAssert(!preventFallthroughBlockSet.count(item.m_labelForSMCRegion));
            preventFallthroughBlockSet.insert(item.m_labelForSMCRegion);
        }
        std::unordered_set<std::string> backupForAssertion = preventFallthroughBlockSet;

        std::vector<std::pair<X64AsmBlock*, X64AsmBlock*>> dummyBlockNeededList;
        for (size_t i = 0; i < file->m_blocks.size(); i++)
        {
            std::string label = file->m_blocks[i]->m_normalizedLabelName;
            if (preventFallthroughBlockSet.count(label))
            {
                preventFallthroughBlockSet.erase(preventFallthroughBlockSet.find(label));
                if (file->IsTerminatorInstructionFallthrough(i))
                {
                    dummyBlockNeededList.push_back(std::make_pair(file->m_blocks[i], file->m_blocks[i + 1]));
                }
            }
        }
        ReleaseAssert(preventFallthroughBlockSet.empty());

        for (auto& blockPair : dummyBlockNeededList)
        {
            X64AsmBlock* fromBlock = blockPair.first;
            X64AsmBlock* toBlock = blockPair.second;
            X64AsmBlock* dummyBlock = X64AsmBlock::Create(file, toBlock /*jmpDst*/);
            file->InsertBlocksAfter({ dummyBlock }, fromBlock /*insertAfter*/);
        }

        file->Validate();

        // Sanity check
        //
        for (size_t i = 0; i < file->m_blocks.size(); i++)
        {
            if (backupForAssertion.count(file->m_blocks[i]->m_normalizedLabelName))
            {
                ReleaseAssert(!file->IsTerminatorInstructionFallthrough(i));
            }
        }
    }

    // Emit label distance / offset information needed by Call IC
    // Similarly, this must happen after all the basic block orders has been finalized, as any further reordering can break this.
    //
    for (CallIcAsmTransformResult& item : callICs)
    {
        item.EmitComputeLabelOffsetAndLengthSymbol(file);
    }
    for (GenericIcAsmTransformResult& item : genericICs)
    {
        ReleaseAssert(file->FindBlockInFastPath(item.m_labelForSMCRegion) != nullptr);
        ReleaseAssert(item.m_symbolNameForSMCLabelOffset == "");
        item.m_symbolNameForSMCLabelOffset = file->EmitComputeLabelDistanceAsm(file->m_blocks[0]->m_normalizedLabelName, item.m_labelForSMCRegion);

        ReleaseAssert(file->FindBlockInSlowPath(item.m_labelForIcMissLogic) != nullptr);
        ReleaseAssert(item.m_symbolNameForIcMissLogicLabelOffset == "");
        item.m_symbolNameForIcMissLogicLabelOffset = file->EmitComputeLabelDistanceAsm(file->m_slowpath[0]->m_normalizedLabelName, item.m_labelForIcMissLogic);
    }

    file->Validate();
    file->AssertAllMagicRemoved();

    // Compute the code metrics
    // It should be better to move this pile of nonsense to the place that actually needs to compute it, but for now..
    //
    {
        m_numInstructionsInFastPath = file->GetNumAssemblyInstructionsInFastPath();
        m_numInstructionsInSlowPath = file->GetNumAssemblyInstructionsInSlowPath();

        // Check whether an instruction is a stack operation
        //
        auto isInstructionStackOperation = [&](X64AsmLine& line) -> bool
        {
            if (!line.IsInstruction())
            {
                return false;
            }
            // A push/pop is a stack operation
            //
            std::string& instOp = line.GetWord(0);
            if (instOp.starts_with("pop") && (instOp.length() <= 4 || instOp == "popfq"))
            {
                return true;
            }
            if (instOp.starts_with("push") && (instOp.length() <= 5 || instOp == "pushfq"))
            {
                return true;
            }
            // It turns out that LLVM will gracefully emit "X-byte Spill" / "X-byte Reload" comment
            // for stack spills and reloads, for now let's just use this as an indication for other stack operations
            //
            if (line.m_trailingComments.find("-byte Spill") != std::string::npos ||
                line.m_trailingComments.find("-byte Reload") != std::string::npos)
            {
                return true;
            }
            return false;
        };

        size_t numStackOps = 0;
        size_t numStackPopBeforeTailCall = 0;

        auto workForComponent = [&](std::vector<X64AsmBlock*>& blocks)
        {
            for (X64AsmBlock* block : blocks)
            {
                for (X64AsmLine& line : block->m_lines)
                {
                    if (isInstructionStackOperation(line))
                    {
                        numStackOps++;
                    }
                }

                if (block->m_lines.size() >= 2)
                {
                    // Check if the last inst is a tail call
                    //
                    X64AsmLine& line1 = block->m_lines[block->m_lines.size() - 1];
                    if ((line1.IsIndirectJumpInst() || line1.IsDirectUnconditionalJumpInst()) &&
                        line1.HasDeegenTailCallAnnotation())
                    {
                        // Check if the second last inst is a pop
                        //
                        X64AsmLine& line2 = block->m_lines[block->m_lines.size() - 2];
                        if (line2.GetWord(0) == "popq")
                        {
                            numStackPopBeforeTailCall++;
                        }
                    }
                }
            }
        };

        workForComponent(file->m_blocks);
        m_numStackOperationsInFastPath = numStackOps;
        m_numStackPopBeforeTailCallInFastPath = numStackPopBeforeTailCall;

        numStackOps = 0;
        numStackPopBeforeTailCall = 0;
        workForComponent(file->m_slowpath);
        m_numStackOperationsInSlowPath = numStackOps;
        m_numStackPopBeforeTailCallInSlowPath = numStackPopBeforeTailCall;

        numStackOps = 0;
        numStackPopBeforeTailCall = 0;
        workForComponent(file->m_icPath);
        m_numTotalStackOperationsInIc = numStackOps;
        m_numTotalStackPopBeforeTailCallInIc = numStackPopBeforeTailCall;
    }

    m_primaryPostTransformAsmFile = file->ToStringAndRemoveDebugInfo();

    auto getIcAsmFile = [&](std::vector<X64AsmBlock*> icLogic) WARN_UNUSED -> std::string
    {
        std::unique_ptr<X64AsmFile> icFile = file->Clone();
        icFile->m_icPath.clear();
        for (X64AsmBlock* block : icLogic)
        {
            icFile->m_icPath.push_back(block->Clone(icFile.get()));
        }
        icFile->Validate();
        icFile->AssertAllMagicRemoved();
        return icFile->ToStringAndRemoveDebugInfo();
    };

    // Important to extract IC after all transformation on primary ASM file has been executed,
    // as the IC ASM and the primary ASM's main logic must be EXACTLY in sync!
    //
    std::vector<DeegenCallIcLogicCreator::JitAsmLoweringResult> callIcInfos;

    // The order of extracting IC snippets must agree with the order we collected the IC labels!
    //
    size_t offsetInIcEntryLabelList = 0;
    for (CallIcAsmTransformResult& item : callICs)
    {
        ReleaseAssert(offsetInIcEntryLabelList + 2 <= icEntryLabelList.size());
        DeegenStencilExtractedICAsm& dcLogic = icAsmList[offsetInIcEntryLabelList];
        DeegenStencilExtractedICAsm& ccLogic = icAsmList[offsetInIcEntryLabelList + 1];
        offsetInIcEntryLabelList += 2;

        ReleaseAssert(dcLogic.m_blocks.size() > 0 && dcLogic.m_blocks[0]->m_normalizedLabelName == item.m_labelForDirectCallIc);
        ReleaseAssert(ccLogic.m_blocks.size() > 0 && ccLogic.m_blocks[0]->m_normalizedLabelName == item.m_labelForClosureCallIc);

        std::string dcAsm = getIcAsmFile(dcLogic.m_blocks);
        std::string ccAsm = getIcAsmFile(ccLogic.m_blocks);

        callIcInfos.push_back({
            .m_directCallLogicAsm = dcAsm,
            .m_closureCallLogicAsm = ccAsm,
            .m_directCallLogicAsmLineCount = dcLogic.GetNumAsmLines(),
            .m_closureCallLogicAsmLineCount = ccLogic.GetNumAsmLines(),
            .m_symbolNameForSMCLabelOffset = item.m_symbolNameForSMCLabelOffset,
            .m_symbolNameForSMCRegionLength = item.m_symbolNameForSMCRegionLength,
            .m_symbolNameForCcIcMissLogicLabelOffset = item.m_symbolNameForCcIcMissLogicLabelOffset,
            .m_symbolNameForDcIcMissLogicLabelOffset = item.m_symbolNameForDcIcMissLogicLabelOffset,
            .m_uniqueOrd = item.m_uniqueOrd
        });
    }

    std::vector<AstInlineCache::JitAsmLoweringResult> genericIcInfos;

    for (GenericIcAsmTransformResult& item : genericICs)
    {
        AstInlineCache::JitAsmLoweringResult info;
        for (const std::string& label : item.m_labelForEffects)
        {
            ReleaseAssert(offsetInIcEntryLabelList < icEntryLabelList.size());
            DeegenStencilExtractedICAsm& icLogic = icAsmList[offsetInIcEntryLabelList];
            offsetInIcEntryLabelList += 1;

            ReleaseAssert(icLogic.m_blocks.size() > 0 && icLogic.m_blocks[0]->m_normalizedLabelName == label);
            info.m_icLogicAsm.push_back(getIcAsmFile(icLogic.m_blocks));
            info.m_icLogicAsmLineCount.push_back(icLogic.GetNumAsmLines());
        }

        info.m_symbolNameForSMCLabelOffset = item.m_symbolNameForSMCLabelOffset;
        info.m_symbolNameForSMCRegionLength = item.m_symbolNameForSMCRegionLength;
        info.m_symbolNameForIcMissLogicLabelOffset = item.m_symbolNameForIcMissLogicLabelOffset;
        info.m_uniqueOrd = item.m_uniqueOrd;

        genericIcInfos.push_back(std::move(info));
    }

    ReleaseAssert(offsetInIcEntryLabelList == icAsmList.size());
    for (size_t i = 0; i < genericIcInfos.size(); i++)
    {
        ReleaseAssert(genericIcInfos[i].m_uniqueOrd == i);
    }

    m_callIcLoweringResults = std::move(callIcInfos);
    m_genericIcLoweringResults = std::move(genericIcInfos);
}

}   // namespace dast
