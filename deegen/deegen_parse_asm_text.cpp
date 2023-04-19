#include "deegen_parse_asm_text.h"
#include "anonymous_file.h"
#include "llvm/IR/DIBuilder.h"

namespace dast {

InjectedMagicDiLocationInfo WARN_UNUSED InjectedMagicDiLocationInfo::RunOnFunction(llvm::Function* func)
{
    using namespace llvm;
    InjectedMagicDiLocationInfo r;
    r.m_hasUpdatedAfterCodegen = false;
    r.m_func = func;
    r.m_list.clear();

    Module* module = func->getParent();
    LLVMContext& ctx = module->getContext();

    module->addModuleFlag(Module::Warning, "Debug Info Version", DEBUG_METADATA_VERSION);

    std::string srcFileName = module->getSourceFileName();
    if (srcFileName == "")
    {
        srcFileName = "unknown.cpp";
    }

    DIBuilder db(*module);
    DIFile* diFile = db.createFile(srcFileName, "");

    db.createCompileUnit(dwarf::DW_LANG_C_plus_plus_14,
                         diFile,
                         "fake debug info producer",
                         true /*isOptimized*/,
                         "" /*flags*/,
                         0 /*runtimeVersion*/);

    DISubroutineType* diSt = db.createSubroutineType({});

    DISubprogram* diSp = db.createFunction(diFile, func->getName(), func->getName(), diFile, 1 /*lineNo*/, diSt, 1 /*scopeLine*/, DINode::FlagPrototyped, DISubprogram::SPFlagDefinition);
    ReleaseAssert(func->getSubprogram() == nullptr);
    func->setSubprogram(diSp);

    // We blend a random (but deterministic) values into the DILocation line number as a
    // santiy check that nothing is unexpected after compilation to ASM
    //
    std::mt19937 rng;
    for (BasicBlock& bb : *func)
    {
        for (Instruction& inst : bb)
        {
            uint32_t ord = static_cast<uint32_t>(r.m_list.size());
            ReleaseAssert(ord < 100000);
            uint32_t randVal = rng() % (x_lineInc / 3) + (x_lineInc / 3);
            uint32_t line = x_lineBase + ord * x_lineInc + randVal;
            r.m_list.push_back(randVal);

            DILocation* dil = DILocation::get(ctx, line /*line*/, 0 /*col*/, diSp);
            ReleaseAssert(inst.getMetadata(LLVMContext::MD_dbg) == nullptr);
            inst.setMetadata(LLVMContext::MD_dbg, dil);
        }
    }

    db.finalize();

    return r;
}

llvm::Instruction* WARN_UNUSED InjectedMagicDiLocationInfo::GetCertainInstructionOriginMaybeNullFromDILoc(uint32_t line)
{
    using namespace llvm;
    if (!m_hasUpdatedAfterCodegen)
    {
        UpdateAfterCodegen();
        ReleaseAssert(m_hasUpdatedAfterCodegen);
    }
    if (line < x_lineBase)
    {
        ReleaseAssert(!m_mappingCertain.count(line));
        return nullptr;
    }
    if (!m_mappingCertain.count(line))
    {
        return nullptr;
    }
    return m_mappingCertain[line];
}

llvm::Instruction* WARN_UNUSED InjectedMagicDiLocationInfo::GetMaybeInstructionOriginMaybeNullFromDILoc(uint32_t line)
{
    using namespace llvm;
    if (!m_hasUpdatedAfterCodegen)
    {
        UpdateAfterCodegen();
        ReleaseAssert(m_hasUpdatedAfterCodegen);
    }
    if (line < x_lineBase)
    {
        ReleaseAssert(!m_mappingMaybe.count(line));
        return nullptr;
    }
    if (!m_mappingMaybe.count(line))
    {
        return nullptr;
    }
    return m_mappingMaybe[line];
}

void InjectedMagicDiLocationInfo::UpdateAfterCodegen()
{
    using namespace llvm;
    ReleaseAssert(!m_hasUpdatedAfterCodegen);
    m_hasUpdatedAfterCodegen = true;

    for (BasicBlock& bb : *m_func)
    {
        for (Instruction& inst : bb)
        {
            MDNode* md = inst.getMetadata(LLVMContext::MD_dbg);
            if (md == nullptr || !isa<DILocation>(md))
            {
                continue;
            }
            DILocation* dil = cast<DILocation>(md);
            uint32_t line = dil->getLine();
            if (line < x_lineBase)
            {
                continue;
            }

            uint32_t ord = (line - x_lineBase) / x_lineInc;
            uint32_t rem = (line - x_lineBase) % x_lineInc;
            ReleaseAssert(ord < m_list.size());
            ReleaseAssert(m_list[ord] == rem);

            m_mappingMaybe[line] = &inst;
            if (!m_mappingCertain.count(line))
            {
                m_mappingCertain[line] = &inst;
            }
            else
            {
                // This instruction seems to have been cloned during LLVM codegen..
                // Mark it as nullptr to invalidate it (important to not delete it, so that mapping.count still returns true)
                //
                m_mappingCertain[line] = nullptr;
            }
        }
    }
}

X64AsmLine WARN_UNUSED X64AsmLine::Parse(std::string line)
{
    X64AsmLine res;
    if (line.length() == 0)
    {
        return res;
    }

    {
        size_t commentStart = line.find("#");
        if (commentStart != std::string::npos)
        {
            res.m_trailingComments = line.substr(commentStart);
            line = line.substr(0, commentStart);
        }

        // Remove the comment if it is '#APP' and '#NO_APP' because they are
        // special comments that might break the preprocessor if used incorrectly, but harmless if simply removed
        // See: https://stackoverflow.com/questions/53959565/what-does-app-in-the-assembly-file-generated-by-compiler-mean
        //
        if (res.m_trailingComments.starts_with("#APP"))
        {
            if (res.m_trailingComments == "#APP")
            {
                res.m_trailingComments = "";
            }
            else
            {
                res.m_trailingComments = "# <removed_sharp_app_comment>" + res.m_trailingComments.substr(strlen("#APP"));
            }
        }
        else if (res.m_trailingComments.starts_with("#NO_APP"))
        {
            if (res.m_trailingComments == "#NO_APP")
            {
                res.m_trailingComments = "";
            }
            else
            {
                res.m_trailingComments = "# <removed_sharp_no_app_comment>" + res.m_trailingComments.substr(strlen("#NO_APP"));
            }
        }
    }

    bool isSpace = std::isspace(line[0]);
    std::string curStr = "";
    for (size_t i = 0; i < line.size(); i++)
    {
        bool b = std::isspace(line[i]);
        if (b != isSpace)
        {
            res.m_components.push_back(curStr);
            if (!isSpace) { res.m_nonWhiteSpaceIdx.push_back(res.m_components.size() - 1); }
            curStr = line[i];
            isSpace = b;
        }
        else
        {
            curStr += line[i];
        }
    }
    res.m_components.push_back(curStr);
    if (!isSpace) { res.m_nonWhiteSpaceIdx.push_back(res.m_components.size() - 1); }
    return res;
}

void X64AsmBlock::SplitAtLine(X64AsmFile* owner, size_t line, X64AsmBlock*& part1 /*out*/, X64AsmBlock*& part2 /*out*/)
{
    ReleaseAssert(0 < line && line < m_lines.size());

    std::string uniqLabel = owner->m_labelNormalizer.GetUniqueLabel();

    std::unique_ptr<X64AsmBlock> p1 = std::make_unique<X64AsmBlock>();
    p1->m_prefixText = m_prefixText;
    p1->m_normalizedLabelName = m_normalizedLabelName;
    p1->m_endsWithJmpToLocalLabel = true;
    p1->m_terminalJmpTargetLabel = uniqLabel;
    for (size_t i = 0; i < line; i++)
    {
        p1->m_lines.push_back(m_lines[i]);
    }
    p1->m_lines.push_back(X64AsmLine::Parse("\tjmp\t" + p1->m_terminalJmpTargetLabel));

    std::unique_ptr<X64AsmBlock> p2 = std::make_unique<X64AsmBlock>();
    p2->m_prefixText = uniqLabel + ":\n";
    p2->m_normalizedLabelName = uniqLabel;
    p2->m_endsWithJmpToLocalLabel = m_endsWithJmpToLocalLabel;
    p2->m_terminalJmpTargetLabel = m_terminalJmpTargetLabel;
    for (size_t i = line; i < m_lines.size(); i++)
    {
        p2->m_lines.push_back(m_lines[i]);
    }

    part1 = p1.get();
    part2 = p2.get();

    owner->m_blockHolders.push_back(std::move(p1));
    owner->m_blockHolders.push_back(std::move(p2));
}

std::unique_ptr<X64AsmFile> WARN_UNUSED X64AsmFile::ParseFile(std::string fileContents, InjectedMagicDiLocationInfo diInfo)
{
    using namespace llvm;

    std::unique_ptr<X64AsmFile> resHolder = std::make_unique<X64AsmFile>();
    X64AsmFile* r = resHolder.get();

    Function* func = diInfo.GetFunc();

    // Split the file into lines
    //
    std::vector<std::string> lines;
    {
        std::stringstream ss(fileContents);
        std::string line;
        while (std::getline(ss, line))
        {
            lines.push_back(line);
        }
    }

    // Find the start of the function (the line for label '<function_name>:')
    //
    size_t funcStartLine = static_cast<size_t>(-1);
    {
        std::string funcName = func->getName().str();
        ReleaseAssert(funcName != "");
        std::string expectedContent = funcName + ":";
        for (size_t i = 0; i < lines.size(); i++)
        {
            X64AsmLine line = X64AsmLine::Parse(lines[i]);
            if (line.NumWords() == 1 && line.GetWord(0)  == expectedContent)
            {
                ReleaseAssert(funcStartLine == static_cast<size_t>(-1));
                funcStartLine = i;
            }
        }
        ReleaseAssert(funcStartLine != static_cast<size_t>(-1));
    }

    // Find the end of the function (the line '.Lfunc_endXXX:')
    //
    size_t funcEndLine = static_cast<size_t>(-1);
    {
        for (size_t i = funcStartLine + 1; i < lines.size(); i++)
        {
            if (lines[i].starts_with(".Lfunc_end"))
            {
                funcEndLine = i;
                break;
            }
        }
    }
    ReleaseAssert(funcEndLine != static_cast<size_t>(-1));

    // Find the last instruction in the function, which is the true funcEnd
    //
    {
        while (true)
        {
            funcEndLine--;
            ReleaseAssert(funcEndLine > funcStartLine);
            X64AsmLine asmLine = X64AsmLine::Parse(lines[funcEndLine]);
            if (asmLine.IsInstruction())
            {
                break;
            }
        }
        funcEndLine++;
    }

    // Set up the preheader and footer part
    //
    {
        std::string s = "";
        for (size_t i = 0; i < funcStartLine; i++)
        {
            s += lines[i] + "\n";
        }
        r->m_filePreheader = s;
    }

    {
        std::string s = "";
        for (size_t i = funcEndLine; i < lines.size(); i++)
        {
            s += lines[i] + "\n";
        }
        r->m_fileFooter = s;
    }

    std::vector<X64AsmLine> asmLines;
    for (size_t i = funcStartLine; i < funcEndLine; i++)
    {
        asmLines.push_back(X64AsmLine::Parse(lines[i]));
    }

    // Split the function into chunks by labels
    //
    {
        size_t i = asmLines.size();
        size_t nextChunkStart = i;
        while (i > 0)
        {
            i--;

            if (asmLines[i].IsLocalLabel() || i == 0)
            {
                // We found a chunk [i + 1, nextChunkStart)
                // Scan backward until we find an instruction
                //
                size_t firstInstLine = i + 1;
                size_t lastInstLine = nextChunkStart;
                ReleaseAssert(firstInstLine < lastInstLine);

                std::string label;
                if (asmLines[i].IsLocalLabel())
                {
                    ReleaseAssert(i > 0);
                    label = asmLines[i].ParseLabel();
                }
                else
                {
                    ReleaseAssert(i == 0);
                    ReleaseAssert(asmLines[i].NumWords() == 1);
                    ReleaseAssert(asmLines[i].GetWord(0) == func->getName().str() + ":");
                    label = func->getName().str();
                }

                r->m_labelNormalizer.AddLabel(label);
                while (i > 0)
                {
                    i--;
                    if (asmLines[i].IsInstruction())
                    {
                        break;
                    }
                    if (asmLines[i].IsLocalLabel())
                    {
                        std::string otherLabel = asmLines[i].ParseLabel();
                        r->m_labelNormalizer.AddLabel(otherLabel);
                        r->m_labelNormalizer.AddEquivalenceRelation(label, otherLabel);
                    }
                }

                size_t labelRangeStart;
                if (i == 0)
                {
                    ReleaseAssert(!asmLines[i].IsLocalLabel());
                    ReleaseAssert(asmLines[i].NumWords() == 1);
                    ReleaseAssert(asmLines[i].GetWord(0) == func->getName().str() + ":");
                    if (firstInstLine > 1)
                    {
                        std::string otherLabel = func->getName().str();
                        r->m_labelNormalizer.AddLabel(otherLabel);
                        r->m_labelNormalizer.AddEquivalenceRelation(label, otherLabel);
                    }
                    nextChunkStart = 0;
                    labelRangeStart = 0;
                }
                else
                {
                    ReleaseAssert(asmLines[i].IsInstruction());
                    nextChunkStart = i + 1;
                    labelRangeStart = i + 1;
                }

                // [labelRangeStart, firstInstLine) should be the prefix for this AsmBlock
                //
                std::unique_ptr<X64AsmBlock> block = std::make_unique<X64AsmBlock>();
                ReleaseAssert(labelRangeStart < firstInstLine);
                for (size_t k = labelRangeStart; k < firstInstLine; k++)
                {
                    block->m_prefixText += asmLines[k].ToString();
                }

                block->m_normalizedLabelName = r->m_labelNormalizer.GetNormalizedLabel(label);
                for (size_t k = firstInstLine; k < lastInstLine; k++)
                {
                    block->m_lines.push_back(asmLines[k]);
                }

                r->m_blockHolders.push_back(std::move(block));
            }
        }

        std::reverse(r->m_blockHolders.begin(), r->m_blockHolders.end());
    }

    for (auto& blockIter : r->m_blockHolders)
    {
        r->m_blocks.push_back(blockIter.get());
    }
    ReleaseAssert(r->m_blocks.size() == r->m_blockHolders.size());

    for (X64AsmBlock* block : r->m_blocks)
    {
        ReleaseAssert(r->m_labelNormalizer.GetNormalizedLabel(block->m_normalizedLabelName) == block->m_normalizedLabelName);
    }
    ReleaseAssert(r->m_labelNormalizer.QueryLabelExists(func->getName().str()));

    // Normalize all implicit fallthroughs to explicit jmp
    //
    for (size_t i = 0; i < r->m_blocks.size(); i++)
    {
        X64AsmBlock* block = r->m_blocks[i];
        ReleaseAssert(block->m_lines.size() > 0);
        X64AsmLine& line = block->m_lines.back();
        ReleaseAssert(line.IsInstruction());
        block->m_endsWithJmpToLocalLabel = false;
        if (line.IsDirectJumpInst())
        {
            ReleaseAssert(line.NumWords() == 2);
            std::string label = line.GetWord(1);
            bool isLocalLabelOrSelfFunctionLabel = r->m_labelNormalizer.QueryLabelExists(label);
            ReleaseAssertImp(!isLocalLabelOrSelfFunctionLabel, !label.starts_with("."));
            if (isLocalLabelOrSelfFunctionLabel)
            {
                block->m_endsWithJmpToLocalLabel = true;
                block->m_terminalJmpTargetLabel = r->m_labelNormalizer.GetNormalizedLabel(label);
            }
        }

        if (!block->m_endsWithJmpToLocalLabel)
        {
            // If the last instruction is not a barrier, this means the end of this asm block
            // may implicitly fallthrough to the next asm block. We normalize by manually adding a
            // jmp to the next asm block: this is OK because we will remove unnecessary jmps in the end
            //
            if (!line.IsDefinitelyBarrierInst())
            {
                if (i == r->m_blocks.size() - 1)
                {
                    fprintf(stderr, "Unrecognized barrier instruction at the end of the function:\n '%s'\n", line.ToString().c_str());
                    abort();
                }

                // Append a 'jmp targetLabel'
                //
                std::string targetLabel = r->m_blocks[i + 1]->m_normalizedLabelName;
                block->m_endsWithJmpToLocalLabel = true;
                block->m_terminalJmpTargetLabel = targetLabel;
                block->m_lines.push_back(X64AsmLine::Parse("\tjmp\t" + targetLabel));
            }
            else
            {
                // The last instruction is a recognized barrier, nothing to do
                //
            }
        }

        ReleaseAssert(block->m_lines.back().IsDefinitelyBarrierInst());
        ReleaseAssertImp(block->m_endsWithJmpToLocalLabel,
                         block->m_lines.back().IsDirectJumpInst() && block->m_terminalJmpTargetLabel == block->m_lines.back().GetWord(1));
    }

    // Sanity check that all the label names make sense
    //
    {
        std::unordered_set<std::string /*label*/> chkUnique;
        for (X64AsmBlock* block : r->m_blocks)
        {
            ReleaseAssert(!chkUnique.count(block->m_normalizedLabelName));
            chkUnique.insert(block->m_normalizedLabelName);
            ReleaseAssert(r->m_labelNormalizer.GetNormalizedLabel(block->m_normalizedLabelName) == block->m_normalizedLabelName);
        }

        for (X64AsmBlock* block : r->m_blocks)
        {
            if (block->m_endsWithJmpToLocalLabel)
            {
                ReleaseAssert(chkUnique.count(block->m_terminalJmpTargetLabel));
                ReleaseAssert(r->m_labelNormalizer.GetNormalizedLabel(block->m_terminalJmpTargetLabel) == block->m_terminalJmpTargetLabel);
            }
        }
    }

    // Convert all the '.loc' directives to InstructionOrigin information
    // Fold all the other misc directives and comments into the instruction's prefixText
    //
    for (X64AsmBlock* block : r->m_blocks)
    {
        std::vector<X64AsmLine> list;
        {
            ReleaseAssert(block->m_lines.size() > 0);
            size_t i = block->m_lines.size() - 1;
            while (true)
            {
                ReleaseAssert(block->m_lines[i].IsInstruction());
                int64_t prevInstLine = static_cast<int64_t>(i) - 1;
                while (prevInstLine >= 0)
                {
                    if (block->m_lines[static_cast<uint64_t>(prevInstLine)].IsInstruction())
                    {
                        break;
                    }
                    else
                    {
                        prevInstLine--;
                    }
                }

                ReleaseAssert(prevInstLine == -1 || block->m_lines[static_cast<uint64_t>(prevInstLine)].IsInstruction());

                // [limit, i) is the range of non-instruction lines
                //
                uint64_t limit = static_cast<uint64_t>(prevInstLine + 1);

                X64AsmLine resLine = block->m_lines[i];
                Instruction* originInstCertain = nullptr;
                Instruction* originInstMaybe = nullptr;
                uint32_t locIdent = 0;
                for (uint64_t k = limit; k < i; k++)
                {
                    X64AsmLine& niLine = block->m_lines[k];
                    ReleaseAssert(!niLine.IsInstruction());
                    resLine.m_prefixingText += niLine.ToString();
                    if (niLine.IsDiLocDirective())
                    {
                        uint32_t lineNumber = niLine.ParseLineNumberFromDiLocDirective();
                        if (lineNumber != 0)
                        {
                            locIdent = lineNumber;
                        }
                        {
                            Instruction* inst = diInfo.GetCertainInstructionOriginMaybeNullFromDILoc(lineNumber);
                            if (inst != nullptr)
                            {
                                originInstCertain = inst;
                            }
                        }
                        {
                            Instruction* inst = diInfo.GetMaybeInstructionOriginMaybeNullFromDILoc(lineNumber);
                            if (inst != nullptr)
                            {
                                originInstMaybe = inst;
                            }
                        }
                    }
                }
                resLine.m_originCertain = originInstCertain;
                resLine.m_originMaybe = originInstMaybe;
                resLine.m_rawLocIdent = locIdent;

                list.push_back(std::move(resLine));

                if (limit == 0)
                {
                    break;
                }

                i = limit - 1;
            }
        }
        std::reverse(list.begin(), list.end());

        // Sanity assertions:
        // 1. The block should be non-empty and consists of only instructions
        // 2. The last instruction should be a known barrier instruction
        // 3. Every other instruction should not be a known barrier instruction
        //
        ReleaseAssert(list.size() > 0);
        for (auto& line : list)
        {
            ReleaseAssert(line.IsInstruction());
        }
        ReleaseAssert(list.back().IsDefinitelyBarrierInst());
        for (size_t i = 0; i + 1 < list.size(); i++)
        {
            ReleaseAssert(!list[i].IsDefinitelyBarrierInst());
        }

        block->m_lines = std::move(list);
    }

    // Detect and fold magic asm snippets
    //
    for (X64AsmBlock* block : r->m_blocks)
    {
        std::vector<X64AsmLine> list;
        {
            auto checkIsIntInst = [&](X64AsmLine& asmLine) WARN_UNUSED -> bool
            {
                ReleaseAssert(asmLine.IsInstruction());
                if (asmLine.NumWords() != 2) { return false; }
                if (asmLine.GetWord(0) != "int") { return false; }
                ReleaseAssert(asmLine.GetWord(1).starts_with("$"));
                return true;
            };

            auto getIntInstOpVal = [&](X64AsmLine& asmLine) WARN_UNUSED -> uint32_t
            {
                ReleaseAssert(checkIsIntInst(asmLine));
                ReleaseAssert(asmLine.NumWords() == 2);
                std::string strVal = asmLine.GetWord(1).substr(1);
                int val = -1;
                try
                {
                    val = std::stoi(strVal);
                }
                catch (...)
                {
                    fprintf(stderr, "Failed to parse int instruction opvalue! Instruction:\n%s\n", asmLine.ToString().c_str());
                    abort();
                }
                ReleaseAssert(0 <= val && val <= 255);
                return static_cast<uint32_t>(val);
            };

            size_t i = 0;
            while (i < block->m_lines.size())
            {
                if (!block->m_lines[i].IsMagicInstruction())
                {
                    list.push_back(block->m_lines[i]);
                    i++;
                }
                else
                {
                    ReleaseAssert(i + 1 < block->m_lines.size());
                    ReleaseAssert(checkIsIntInst(block->m_lines[i + 1]));
                    uint32_t intInstOpval = getIntInstOpVal(block->m_lines[i + 1]);
                    AsmMagicPayload* payload = new AsmMagicPayload();
                    ReleaseAssert(100 <= intInstOpval && intInstOpval < 100 + static_cast<uint32_t>(MagicAsmKind::X_END_OF_ENUM));
                    payload->m_kind = static_cast<MagicAsmKind>(intInstOpval - 100);

                    ReleaseAssert(block->m_lines[i + 1].m_prefixingText == "");

                    size_t payloadEnd = i + 2;
                    while (true)
                    {
                        ReleaseAssert(payloadEnd < block->m_lines.size());
                        if (checkIsIntInst(block->m_lines[payloadEnd]))
                        {
                            ReleaseAssert(getIntInstOpVal(block->m_lines[payloadEnd]) == intInstOpval);
                            break;
                        }
                        payloadEnd++;
                    }

                    // [i+2, payloadEnd) is the range of the asm payload
                    //
                    for (size_t k = i + 2; k < payloadEnd; k++)
                    {
                        payload->m_lines.push_back(block->m_lines[k]);
                    }

                    ReleaseAssert(block->m_lines[payloadEnd].m_prefixingText == "");
                    ReleaseAssert(payloadEnd + 1 < block->m_lines.size());
                    ReleaseAssert(block->m_lines[payloadEnd + 1].IsMagicInstruction());
                    ReleaseAssert(block->m_lines[payloadEnd + 1].m_prefixingText == "");

                    list.push_back(block->m_lines[i]);
                    ReleaseAssert(list.back().m_magicPayload == nullptr);
                    list.back().m_magicPayload = payload;

                    i = payloadEnd + 2;
                }
            }
            ReleaseAssert(i == block->m_lines.size());
        }

        ReleaseAssert(list.size() > 0);
        for (auto& line : list)
        {
            ReleaseAssert(line.IsInstruction());
        }
        ReleaseAssert(list.back().IsDefinitelyBarrierInst());
        for (size_t i = 0; i + 1 < list.size(); i++)
        {
            ReleaseAssert(!list[i].IsDefinitelyBarrierInst());
            ReleaseAssertIff(list[i].IsMagicInstruction(), list[i].m_magicPayload != nullptr);
        }

        block->m_lines = std::move(list);
    }

    r->Validate();

    return resHolder;
}

void X64AsmFile::Validate()
{
    std::unordered_set<X64AsmBlock*> heldPtrs;
    for (auto& it : m_blockHolders)
    {
        X64AsmBlock* p = it.get();
        ReleaseAssert(!heldPtrs.count(p));
        heldPtrs.insert(p);
    }

    auto validateBlocks = [&](const std::vector<X64AsmBlock*>& blocks)
    {
        for (size_t i = 0; i < blocks.size(); i++)
        {
            X64AsmBlock* block = blocks[i];
            ReleaseAssert(heldPtrs.count(block));
            ReleaseAssert(block->m_lines.size() > 0);
            for (size_t k = 0; k + 1 < block->m_lines.size(); k++)
            {
                ReleaseAssert(!block->m_lines[k].IsDefinitelyBarrierInst());
            }
            ReleaseAssert(block->m_lines.back().IsDefinitelyBarrierInst());

            ReleaseAssert(m_labelNormalizer.QueryLabelExists(block->m_normalizedLabelName));
            ReleaseAssert(block->m_prefixText.find(block->m_normalizedLabelName + ":") != std::string::npos);

            if (block->m_lines.back().IsDirectJumpInst())
            {
                std::string label = block->m_lines.back().GetWord(1);
                if (m_labelNormalizer.QueryLabelExists(label))
                {
                    ReleaseAssert(block->m_endsWithJmpToLocalLabel);
                    ReleaseAssert(block->m_terminalJmpTargetLabel == m_labelNormalizer.GetNormalizedLabel(label));
                }
                else
                {
                    ReleaseAssert(!block->m_endsWithJmpToLocalLabel);
                }
            }
            else
            {
                ReleaseAssert(!block->m_endsWithJmpToLocalLabel);
            }
        }
    };

    validateBlocks(m_blocks);
    validateBlocks(m_slowpath);
}

std::string WARN_UNUSED X64AsmFile::ToString()
{
    Validate();

    AnonymousFile file;
    FILE* fp = file.GetFStream("w");

    fprintf(fp, "%s", m_filePreheader.c_str());

    auto printSection = [&](std::vector<X64AsmBlock*> blockList)
    {
        for (size_t i = 0; i < blockList.size(); i++)
        {
            X64AsmBlock* block = blockList[i];
            bool elideTailJmp = false;
            if (i + 1 < blockList.size() && block->m_endsWithJmpToLocalLabel)
            {
                elideTailJmp = (block->m_terminalJmpTargetLabel == blockList[i + 1]->m_normalizedLabelName);
            }

            fprintf(fp, "%s", block->m_prefixText.c_str());

            for (size_t k = 0; k < block->m_lines.size(); k++)
            {
                X64AsmLine& line = block->m_lines[k];
                if (k == block->m_lines.size() - 1 && elideTailJmp)
                {
                    ReleaseAssert(line.IsDirectJumpInst());
                    fprintf(fp, "%s", line.m_prefixingText.c_str());
                    fprintf(fp, "%s\n", line.m_trailingComments.c_str());
                }
                else
                {
                    fprintf(fp, "%s", line.ToString().c_str());
                }
            }
        }
    };

    printSection(m_blocks);
    printSection(m_slowpath);

    fprintf(fp, "%s", m_fileFooter.c_str());
    fclose(fp);
    return file.GetFileContents();
}

std::string WARN_UNUSED X64AsmFile::ToStringAndRemoveDebugInfo()
{
    std::string originalOutputContents = ToString();
    AnonymousFile file;
    FILE* fp = file.GetFStream("w");

    std::vector<std::string> lines;
    {
        std::stringstream ss(originalOutputContents);
        std::string line;
        while (std::getline(ss, line))
        {
            lines.push_back(line);
        }
    }

    for (std::string& line : lines)
    {
        X64AsmLine asmLine = X64AsmLine::Parse(line);
        bool shouldCommentOut = false;
        if (asmLine.IsDirective())
        {
            ReleaseAssert(asmLine.NumWords() > 0);
            if (asmLine.IsDiLocDirective())
            {
                shouldCommentOut = true;
            }
            else if (asmLine.GetWord(0).starts_with(".cfi"))
            {
                shouldCommentOut = true;
            }
        }
        /*
        if (shouldCommentOut)
        {
            fprintf(fp, "# ");
        }
        fprintf(fp, "%s\n", line.c_str());
        */

        if (!shouldCommentOut)
        {
            fprintf(fp, "%s\n", line.c_str());
        }
    }

    fclose(fp);
    return file.GetFileContents();
}

}   // namespace dast
