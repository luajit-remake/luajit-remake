#include "deegen_stencil_lowering_pass.h"
#include "deegen_recover_asm_cfg.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "deegen_call_inline_cache.h"
#include "deegen_stencil_inline_cache_extraction_pass.h"

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
                                                                    const std::string& fallthroughPlaceholderName,
                                                                    std::unordered_set<std::string> blocksMustNotSplit)
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

void DeegenStencilLoweringPass::RunAsmRewritePhase(const std::string& asmFile)
{
    std::unique_ptr<X64AsmFile> fileHolder = X64AsmFile::ParseFile(asmFile, m_diInfo);
    X64AsmFile* file = fileHolder.get();
    m_rawInputFileForAudit = file->Clone();

    // AnalyzeIndirectBranch works by attempting to find mapping between ASM branches and LLVM CFG,
    // so it must be called before we do any transform to the ASM.
    //
    DeegenAsmCfg::AnalyzeIndirectBranch(file, m_diInfo.GetFunc(), false /*addDebugComment*/);

    // Lower the Call IC asm magic
    //
    using CallIcAsmLoweringResult = DeegenCallIcLogicCreator::BaselineJitAsmLoweringResult;
    std::vector<CallIcAsmLoweringResult> callICs = DeegenCallIcLogicCreator::DoBaselineJitAsmLowering(file);

    file->Validate();

    std::vector<std::string> icEntryLabelList;
    for (CallIcAsmLoweringResult& item : callICs)
    {
        icEntryLabelList.push_back(item.m_labelForDirectCallIc);
        icEntryLabelList.push_back(item.m_labelForClosureCallIc);
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
        for (CallIcAsmLoweringResult& item : callICs)
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
            for (CallIcAsmLoweringResult& item : callICs)
            {
                allIcSlowPathBlocks.push_back(item.m_labelForCcIcMissLogic);
                allIcSlowPathBlocks.push_back(item.m_labelForDcIcMissLogic);
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
    for (CallIcAsmLoweringResult& item : callICs)
    {
        item.FixupSMCRegionAfterCFGAnalysis(file);
        file->Validate();
    }

    // Attempt to transform the dispatch to next bytecode to a fallthrough. Note that:
    // 1. After this step the CFG is invalidated.
    // 2. This transform may need to split a block into two. We must not do this to any of the SMC regions of the ICs.
    //
    {
        std::unordered_set<std::string> blocksMustNotSplit;
        for (CallIcAsmLoweringResult& item : callICs)
        {
            blocksMustNotSplit.insert(item.m_labelForSMCRegion);
        }

        AttemptToTransformDispatchToNextBytecodeIntoFallthrough(file /*inout*/,
                                                                m_locIdentForJmpToFallthroughCandidate,
                                                                m_diInfo.GetFunc()->getName().str(),
                                                                m_nextBytecodeFallthroughPlaceholderName,
                                                                blocksMustNotSplit);
        cfg.m_cfg.clear();
        file->Validate();
    }

    // The call IC's SMC block ends with a jump. The jump must not be eliminated to a fallthrough even if it happens to
    // jump to the immediate following block, because the jump is only a placeholder.
    // We enforce this by figuring out all those jumps that happens to fallthrough, and adding a dummy block in between.
    // Note that this must happen as the LAST transform, since any further reordering can break this.
    //
    {
        std::unordered_set<std::string> preventFallthroughBlockSet;
        for (CallIcAsmLoweringResult& item : callICs)
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

    file->Validate();
    file->AssertAllMagicRemoved();
    m_primaryPostTransformAsmFile = file->ToStringAndRemoveDebugInfo();

    // Important to extract IC after all transformation on primary ASM file has been executed,
    // as the IC ASM and the primary ASM's main logic must be EXACTLY in sync!
    //
    std::map<uint64_t, CallIcAsmInfo> callIcInfos;

    size_t offsetInIcEntryLabelList = 0;
    for (CallIcAsmLoweringResult& item : callICs)
    {
        ReleaseAssert(offsetInIcEntryLabelList + 2 <= icEntryLabelList.size());
        DeegenStencilExtractedICAsm& dcLogic = icAsmList[offsetInIcEntryLabelList];
        DeegenStencilExtractedICAsm& ccLogic = icAsmList[offsetInIcEntryLabelList + 1];
        offsetInIcEntryLabelList += 2;

        ReleaseAssert(dcLogic.m_blocks.size() > 0 && dcLogic.m_blocks[0]->m_normalizedLabelName == item.m_labelForDirectCallIc);
        ReleaseAssert(ccLogic.m_blocks.size() > 0 && ccLogic.m_blocks[0]->m_normalizedLabelName == item.m_labelForClosureCallIc);

        std::string dcAsm = getIcAsmFile(dcLogic.m_blocks);
        std::string ccAsm = getIcAsmFile(ccLogic.m_blocks);

        ReleaseAssert(!callIcInfos.count(item.m_uniqueOrd));
        callIcInfos[item.m_uniqueOrd] = {
            .m_directCallLogicAsm = dcAsm,
            .m_closureCallLogicAsm = ccAsm,
            .m_smcBlockLabel = item.m_labelForSMCRegion,
            .m_ccIcMissPathLabel = item.m_labelForCcIcMissLogic,
            .m_dcIcMissPathLabel = item.m_labelForDcIcMissLogic
        };
    }

    ReleaseAssert(offsetInIcEntryLabelList == icAsmList.size());
    m_callIcLogicAsmFiles = std::move(callIcInfos);
}

}   // namespace dast
