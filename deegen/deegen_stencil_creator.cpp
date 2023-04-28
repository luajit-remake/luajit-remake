#include "deegen_stencil_creator.h"
#include "anonymous_file.h"
#include "deegen_stencil_lowering_pass.h"
#include "deegen_stencil_runtime_constant_insertion_pass.h"
#include "invoke_clang_helper.h"
#include "base64_util.h"
#include "deegen_stencil_reserved_placeholder_ords.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCRegisterInfo.h"

namespace dast {

using namespace llvm;
using namespace llvm::object;

std::string WARN_UNUSED StencilSharedConstantDataObject::PrintDeclaration()
{
    AnonymousFile file;
    FILE* fp = file.GetFStream("w");
    fprintf(fp, "struct __attribute__((__packed__, __aligned__(%llu))) deegen_stencil_constant_%llu_ty {\n",
            static_cast<unsigned long long>(GetAlignment()),
            static_cast<unsigned long long>(GetUniqueLabel()));
    for (size_t ord = 0; ord < m_valueDefs.size(); ord++)
    {
        Element& e  = m_valueDefs[ord];
        if (e.m_kind == Element::Kind::ByteConstant)
        {
            fprintf(fp, "    uint8_t e%llu;\n", static_cast<unsigned long long>(ord));
        }
        else
        {
            ReleaseAssert(e.m_kind == Element::Kind::PointerWithAddend);
            fprintf(fp, "    uint64_t e%llu;\n", static_cast<unsigned long long>(ord));
        }
        fprintf(fp, "\n");
    }
    fprintf(fp, "};\n");
    fprintf(fp, "static_assert(sizeof(deegen_stencil_constant_%llu_ty) == %llu);\n",
            static_cast<unsigned long long>(GetUniqueLabel()),
            static_cast<unsigned long long>(ComputeSizeWithPadding()));

    if (m_shouldForwardDeclare)
    {
        fprintf(fp, "extern const deegen_stencil_constant_%llu_ty deegen_stencil_constant_%llu;\n",
                static_cast<unsigned long long>(GetUniqueLabel()),
                static_cast<unsigned long long>(GetUniqueLabel()));
    }

    fclose(fp);
    return file.GetFileContents();
}

std::string WARN_UNUSED StencilSharedConstantDataObject::PrintDefinition()
{
    AnonymousFile file;
    FILE* fp = file.GetFStream("w");
    fprintf(fp, "[[maybe_unused]] constexpr deegen_stencil_constant_%llu_ty deegen_stencil_constant_%llu = {\n",
            static_cast<unsigned long long>(GetUniqueLabel()),
            static_cast<unsigned long long>(GetUniqueLabel()));

    for (size_t ord = 0; ord < m_valueDefs.size(); ord++)
    {
        Element& e  = m_valueDefs[ord];
        if (e.m_kind == Element::Kind::ByteConstant)
        {
            fprintf(fp, "    .e%llu = %llu", static_cast<unsigned long long>(ord), static_cast<unsigned long long>(e.m_byteValue));
            if (isalnum(e.m_byteValue) || static_cast<char>(e.m_byteValue) == ' ')
            {
                fprintf(fp, "\t/*'%c'*/", static_cast<char>(e.m_byteValue));
            }
        }
        else
        {
            ReleaseAssert(e.m_kind == Element::Kind::PointerWithAddend);
            fprintf(fp, "    .e%llu = FOLD_CONSTEXPR(reinterpret_cast<uint64_t>(&deegen_stencil_constant_%llu)) + %lluULL",
                    static_cast<unsigned long long>(ord),
                    static_cast<unsigned long long>(e.m_ptrValue->GetUniqueLabel()),
                    static_cast<unsigned long long>(e.m_addend));
        }
        if (ord + 1 < m_valueDefs.size())
        {
            fprintf(fp, ",");
        }
        fprintf(fp, "\n");
    }
    fprintf(fp, "};\n");

    fclose(fp);
    return file.GetFileContents();
}

static bool IsRelocAddressLess(RelocationRef A, RelocationRef B) {
    return A.getOffset() < B.getOffset();
}

// Logic stolen from llvm/tools/llvm-objdump/llvm-objdump.cpp
// Get a map from section to all its relocations
// tbh I have no idea why we have to call the 'getRelocatedSection'..
//
static std::map<SectionRef, std::vector<RelocationRef>> GetRelocationMap(ObjectFile const &Obj)
{
    std::map<SectionRef, std::vector<RelocationRef>> Ret;
    for (SectionRef Sec : Obj.sections())
    {
        Expected<section_iterator> RelocatedOrErr = Sec.getRelocatedSection();
        if (!RelocatedOrErr)
        {
            fprintf(stderr, "[INTERNAL ERROR] Failed to get a relocated section. Please report a bug.\n");
            abort();
        }

        section_iterator Relocated = *RelocatedOrErr;
        if (Relocated == Obj.section_end()) continue;
        std::vector<RelocationRef> &V = Ret[*Relocated];
        for (const RelocationRef &R : Sec.relocations())
        {
            V.push_back(R);
        }
        // Sort relocations by address.
        //
        llvm::stable_sort(V, IsRelocAddressLess);
    }

    for (SectionRef Sec : Obj.sections())
    {
        if (!Ret.count(Sec))
        {
            Ret[Sec] = std::vector<RelocationRef>();
        }
    }
    return Ret;
}

static SymbolRef WARN_UNUSED GetRelocationSymbol(ELFObjectFileBase* objBase, const RelocationRef& rref)
{
    ELF64LEObjectFile* obj = dyn_cast<ELF64LEObjectFile>(objBase);
    ReleaseAssert(obj != nullptr);
    symbol_iterator si = rref.getSymbol();
    if (si == obj->symbol_end())
    {
        fprintf(stderr, "[INTERNAL ERROR] Encountered a relocation record not associated with a symbol. Please report a bug.\n");
        abort();
    }
    return *si;
}

static int64_t WARN_UNUSED GetRelocationAddend(ELFObjectFileBase* objBase, const RelocationRef& rref)
{
    ELF64LEObjectFile* obj = dyn_cast<ELF64LEObjectFile>(objBase);
    ReleaseAssert(obj != nullptr);

    const ELFFile<ELF64LE>& EF = obj->getELFFile();
    DataRefImpl dri = rref.getRawDataRefImpl();
    auto SecOrErr = EF.getSection(dri.d.a);
    if (!SecOrErr)
    {
        fprintf(stderr, "[INTERNAL ERROR] Fail to get section from a relocation. Please report a bug.\n");
        abort();
    }

    if ((*SecOrErr)->sh_type != ELF::SHT_RELA)
    {
        fprintf(stderr, "[INTERNAL ERROR] Unexpected ELF SH_TYPE. Please report a bug.\n");
        abort();
    }

    const typename ELF64LE::Rela* rela = obj->getRela(dri);
    if (rela->getSymbol(false /*isMips64EL*/) == 0)
    {
        fprintf(stderr, "[INTERNAL ERROR] Encountered a relocation record not associated with a symbol. Please report a bug.\n");
        abort();
    }
    int64_t res = rela->r_addend;
    return res;
}

using ElfSymbol = typename ELF64LE::Sym;

static const ElfSymbol* WARN_UNUSED GetSymbolFromSymbolRef(ELFObjectFileBase* objBase, SymbolRef symRef)
{
    ELF64LEObjectFile* obj = dyn_cast<ELF64LEObjectFile>(objBase);
    ReleaseAssert(obj != nullptr);
    Expected<const ElfSymbol*> sym = obj->getSymbol(symRef.getRawDataRefImpl());
    if (!sym)
    {
        fprintf(stderr, "[INTERNAL ERROR] Failed to get symbol defintion from SymbolRef. Please report a bug.\n");
        abort();
    }
    return *sym;
}

DeegenStencil WARN_UNUSED DeegenStencil::ParseImpl(llvm::LLVMContext& ctx,
                                                   const std::string& objFile,
                                                   bool isExtractIcLogic,
                                                   SectionToPdoOffsetMapTy mainLogicPdoLayout)
{
    ELFObjectFileBase* obj = LoadElfObjectFile(ctx, objFile);
    std::map<SectionRef, std::vector<RelocationRef>> relocMap = GetRelocationMap(*obj);

    Triple triple = obj->makeTriple();

    SectionRef textSection;
    SectionRef textSlowSection;
    SectionRef textIcSection;
    for (SectionRef sec : obj->sections())
    {
        if (sec.isText())
        {
            Expected<StringRef> expectedName = sec.getName();
            ReleaseAssert(expectedName);
            std::string secName = expectedName->str();
            if (secName == ".text")
            {
                ReleaseAssert(textSection.getObject() == nullptr);
                textSection = sec;
            }
            else if (secName == ".text.deegen_slow")
            {
                ReleaseAssert(textSlowSection.getObject() == nullptr);
                textSlowSection = sec;
            }
            else
            {
                ReleaseAssert(secName == ".text.deegen_ic_logic");
                ReleaseAssert(textIcSection.getObject() == nullptr);
                textIcSection = sec;
            }
        }
    }
    // Note that '.text.deegen_slow' may not exist if the function does not have slow path
    //
    ReleaseAssert(textSection.getObject() == obj);

    // '.text.deegen_ic_logic' must exist if the object file is for IC extraction
    //
    ReleaseAssertImp(isExtractIcLogic, textIcSection.getObject() == obj);

    std::unordered_map<std::string, SectionRef> sectionNameToSectionRefMap;
    for (SectionRef sec : obj->sections())
    {
        Expected<StringRef> expectedName = sec.getName();
        ReleaseAssert(expectedName);
        std::string name = expectedName.get().str();
        ReleaseAssert(!sectionNameToSectionRefMap.count(name));
        sectionNameToSectionRefMap[name] = sec;
    }

    std::unordered_map<std::string, SymbolRef> symNameToSymRefMap;

    // symbol name -> the section where this symbol is defined
    //
    std::unordered_map<std::string, SectionRef> symNameToSectionMap;

    for (const SymbolRef &symRef : obj->symbols())
    {
        Expected<StringRef> exSymName = symRef.getName();
        if (!exSymName)
        {
            fprintf(stderr, "[INTERNAL ERROR] Fail to get a symbol name. Please report a bug.\n");
            abort();
        }
        std::string symName = exSymName->str();
        if (symName.empty())
        {
            continue;
        }
        const ElfSymbol* sym = GetSymbolFromSymbolRef(obj, symRef);
        if (sym->getType() == ELF::STT_SECTION)
        {
            continue;
        }

        ReleaseAssert(!symNameToSymRefMap.count(symName));
        symNameToSymRefMap[symName] = symRef;

        Expected<section_iterator> exSi = symRef.getSection();
        if (!exSi)
        {
            fprintf(stderr, "[INTERNAL ERROR] Failed to get symbol section from symbol. Please report a bug.\n");
            abort();
        }
        section_iterator si = *exSi;
        if (si == obj->section_end())
        {
            continue;
        }

        SectionRef secRef = *si;
        ReleaseAssert(!symNameToSectionMap.count(symName));
        symNameToSectionMap[symName] = secRef;
    }

    std::map<SectionRef, std::vector<std::string>> symbolsInSectionMap;
    for (auto& it : symNameToSectionMap)
    {
        std::string symName = it.first;
        SectionRef sec = it.second;
        symbolsInSectionMap[sec].push_back(symName);
    }

    // For normal extract, we want to extract the fast and slow text section; for extractIcLogic, we want to extract the IC text section
    //
    std::vector<SectionRef> textSectionExtractTargets;
    if (!isExtractIcLogic)
    {
        textSectionExtractTargets.push_back(textSection);
        if (textSlowSection.getObject() != nullptr)
        {
            textSectionExtractTargets.push_back(textSlowSection);
        }
    }
    else
    {
        textSectionExtractTargets.push_back(textIcSection);
    }

    // The set of sections that must be put in private data section because it directly or transitively
    // contains relocation symbols in the text section we want to extract
    //
    // Note that we currently only analyze at section level (instead of symbol level) for simplicity but
    // it should work equally well for the object files that we are targeting
    //
    std::set<SectionRef> privateDataSections;
    for (auto& section : textSectionExtractTargets)
    {
        ReleaseAssert(!privateDataSections.count(section));
        privateDataSections.insert(section);
    }

    // For simplicity, just O(n^2) naively propagate to fixpoint..
    //
    while (true)
    {
        bool changed = false;
        for (auto relocMapIter = relocMap.begin(); relocMapIter != relocMap.end(); relocMapIter++)
        {
            SectionRef sec = relocMapIter->first;
            if (privateDataSections.count(sec))
            {
                continue;
            }
            bool found = false;
            for (RelocationRef rref : relocMapIter->second)
            {
                SymbolRef symRef = GetRelocationSymbol(obj, rref);
                if (GetSymbolFromSymbolRef(obj, symRef)->getType() == ELF::STT_SECTION)
                {
                    // This is a section-based relocation
                    //
                    std::string secName = symRef.getName()->str();
                    ReleaseAssert(sectionNameToSectionRefMap.count(secName));
                    SectionRef defIn = sectionNameToSectionRefMap[secName];
                    if (privateDataSections.count(defIn))
                    {
                        found = true;
                        break;
                    }
                }
                else
                {
                    // This is a symbol-based relocation
                    //
                    std::string symName = symRef.getName()->str();
                    const ElfSymbol* sym = GetSymbolFromSymbolRef(obj, symRef);
                    // Don't bother if the symbol is undefined (it means the symbol refers to something external)
                    //
                    if (sym->isDefined())
                    {
                        ReleaseAssert(symNameToSectionMap.count(symName));
                        SectionRef defIn = symNameToSectionMap[symName];
                        if (privateDataSections.count(defIn))
                        {
                            found = true;
                            break;
                        }
                    }
                }
            }
            if (found)
            {
                changed = true;
                ReleaseAssert(!privateDataSections.count(sec));
                privateDataSections.insert(sec);
            }
        }
        if (!changed)
        {
            break;
        }
    }

    // Locate the main function, which should be the only defined function in the file
    //
    std::string mainFnName;
    for (const SymbolRef &symRef : obj->symbols())
    {
        const ElfSymbol* sym = GetSymbolFromSymbolRef(obj, symRef);
        if (sym->getType() == ELF::STT_FUNC && sym->isDefined())
        {
            std::string symName = symRef.getName()->str();
            ReleaseAssert(mainFnName == "");
            ReleaseAssert(symName != "");
            mainFnName = symName;
        }
    }
    ReleaseAssert(mainFnName != "");

    ReleaseAssert(symNameToSymRefMap.count(mainFnName));
    SymbolRef mainFnSym = symNameToSymRefMap[mainFnName];

    // Assert that the main function is the only symbol in the text section
    //
    ReleaseAssert(mainFnSym.getAddress());
    ReleaseAssert(*mainFnSym.getAddress() == textSection.getAddress());
    ReleaseAssert(symbolsInSectionMap[textSection].size() == 1);

    bool hasMergedPrivateDataSections = false;
    std::map<SectionRef, uint64_t /*offset*/> mergedPrivateDataSectionIndex;

    auto getSectionName = [&](SectionRef sec) WARN_UNUSED -> std::string
    {
        Expected<StringRef> expectedSecName = sec.getName();
        ReleaseAssert(expectedSecName);
        return expectedSecName->str();
    };

    // Note that this function does not populate 'm_sharedDataObject' field
    // We fixup this field in the end to avoid dependency loop issues
    //
    auto processRelocation = [&](RelocationRef rref) WARN_UNUSED -> RelocationRecord
    {
        RelocationRecord res;
        res.m_relocationType = rref.getType();
        res.m_offset = rref.getOffset();
        res.m_addend = GetRelocationAddend(obj, rref);
        res.m_sharedDataObject = nullptr;
        res.m_stencilHoleOrd = static_cast<size_t>(-1);

        SymbolRef sref = GetRelocationSymbol(obj, rref);
        std::string name = sref.getName()->str();
        const ElfSymbol* sym = GetSymbolFromSymbolRef(obj, sref);
        SectionRef sec;
        if (sym->getType() == ELF::STT_SECTION)
        {
            ReleaseAssert(sectionNameToSectionRefMap.count(name));
            sec = sectionNameToSectionRefMap[name];
        }
        else
        {
            if (sym->isDefined())
            {
                // The symbol is defined within the object file.
                // Normalize the record to "section + offset" form instead of "symbol + offset"
                //
                ReleaseAssert(symNameToSectionMap.count(name));
                sec = symNameToSectionMap[name];
                ReleaseAssert(sref.getAddress());
                size_t symAddr = *sref.getAddress();
                size_t secAddr = sec.getAddress();
                ReleaseAssert(secAddr <= symAddr && symAddr < secAddr + sec.getSize());
                res.m_addend += static_cast<int64_t>(symAddr - secAddr);
            }
            else
            {
                // An undefined symbol, it has to be either an external C symbol or a copy-and-patch placeholder
                //
                constexpr const char* x_placeholderPrefix = "__deegen_cp_placeholder_";
                if (name.starts_with(x_placeholderPrefix))
                {
                    int placeholderOrd = StoiOrFail(name.substr(strlen(x_placeholderPrefix)));
                    ReleaseAssert(placeholderOrd >= 0);
                    // There are two specially reserved placeholderOrd
                    // CP_PLACEHOLDER_STENCIL_SLOW_PATH_ADDR is used to represent the address of the slow path of this stencil
                    // CP_PLACEHOLDER_STENCIL_DATA_SEC_ADDR is used to represent the address of the data section of this stencil
                    // They should only be used by the main function, never the IC logic
                    //
                    if (placeholderOrd == CP_PLACEHOLDER_STENCIL_SLOW_PATH_ADDR)
                    {
                        ReleaseAssert(!isExtractIcLogic);
                        res.m_symKind = RelocationRecord::SymKind::SlowPathAddr;
                    }
                    else
                    {
                        res.m_symKind = RelocationRecord::SymKind::StencilHole;
                        res.m_stencilHoleOrd = static_cast<size_t>(placeholderOrd);
                    }
                }
                else
                {
                    res.m_symKind = RelocationRecord::SymKind::ExternalCSymbol;
                    res.m_symbolName = name;
                }
                return res;
            }
        }

        ReleaseAssert(sec.getObject() != nullptr);

        if (sec == textSection)
        {
            res.m_symKind = RelocationRecord::SymKind::FastPathAddr;
            return res;
        }

        if (sec == textSlowSection)
        {
            ReleaseAssert(textSlowSection.getObject() != nullptr);
            res.m_symKind = RelocationRecord::SymKind::SlowPathAddr;
            return res;
        }

        if (sec == textIcSection)
        {
            if (!isExtractIcLogic)
            {
                // When we process the main logic, we should never see a reference to IC section code
                // However, LLVM currently does not put individual jump tables into different sections,
                // and we are doing the extraction on a per-section (instead of per-symbol) basis,
                // so we might see false positive here.
                // This is fixable but requires some work, so simply fail here for simplicity.
                //
                ReleaseAssert(false && "see comments above");
            }
            res.m_symKind = RelocationRecord::SymKind::ICPathAddr;
            return res;
        }

        res.m_sectionRef = sec;
        if (privateDataSections.count(sec))
        {
            res.m_symKind = RelocationRecord::SymKind::PrivateDataAddr;
            if (hasMergedPrivateDataSections)
            {
                ReleaseAssert(mergedPrivateDataSectionIndex.count(sec));
                res.m_addend += static_cast<int64_t>(mergedPrivateDataSectionIndex[sec]);
            }
        }
        else
        {
            if (isExtractIcLogic && mainLogicPdoLayout.count(getSectionName(sec)))
            {
                // This is a section in the main logic's private data section
                //
                res.m_symKind = RelocationRecord::SymKind::MainLogicPrivateDataAddr;
                res.m_addend += static_cast<int64_t>(mainLogicPdoLayout[getSectionName(sec)]);
            }
            else
            {
                res.m_symKind = RelocationRecord::SymKind::SharedConstantDataObject;
            }
        }
        return res;
    };

    std::map<SectionRef, StencilSharedConstantDataObject*> neededSharedData;
    std::map<SectionRef, StencilPrivateDataObject*> neededPrivateData;

    std::function<void(SectionRef)> handleDataSection = [&](SectionRef sec) -> void
    {
        ReleaseAssert(sec.getObject() != nullptr);
        ReleaseAssert(sec != textSection && sec != textSlowSection && sec != textIcSection);
        if (neededSharedData.count(sec) || neededPrivateData.count(sec))
        {
            return;
        }

        if (isExtractIcLogic && mainLogicPdoLayout.count(getSectionName(sec)))
        {
            return;
        }

        Expected<StringRef> exContents = sec.getContents();
        ReleaseAssert(exContents);
        StringRef contents = *exContents;

        if (privateDataSections.count(sec))
        {
            StencilPrivateDataObject* pdo = new StencilPrivateDataObject();
            pdo->m_alignment = sec.getAlignment();

            for (RelocationRef rref : relocMap[sec])
            {
                pdo->m_relocations.push_back(processRelocation(rref));
            }

            pdo->m_bytes.resize(contents.size());
            memcpy(pdo->m_bytes.data(), contents.data(), contents.size());

            // Important to store into 'neededPrivateData' before recursing on the relocations to avoid deadloops
            //
            neededPrivateData[sec] = pdo;

            for (RelocationRecord& rr : pdo->m_relocations)
            {
                if (rr.m_symKind == RelocationRecord::SymKind::SharedConstantDataObject || rr.m_symKind == RelocationRecord::SymKind::PrivateDataAddr)
                {
                    handleDataSection(rr.m_sectionRef);
                }
            }
        }
        else
        {
            StencilSharedConstantDataObject* cdo = new StencilSharedConstantDataObject();
            cdo->m_alignment = sec.getAlignment();

            using Element = StencilSharedConstantDataObject::Element;
            std::map<uint64_t /*offset*/, Element> relocs;
            for (RelocationRef rref : relocMap[sec])
            {
                RelocationRecord rr = processRelocation(rref);
                // For now, only handle R_X86_64_64 for constant data. Other kinds of relocations should be really weird..
                //
                ReleaseAssert(rr.m_relocationType == ELF::R_X86_64_64);
                ReleaseAssert(rr.m_symKind == RelocationRecord::SymKind::SharedConstantDataObject);
                ReleaseAssert(rr.m_offset + 8 <= contents.size());
                ReleaseAssert(!relocs.count(rr.m_offset));
                relocs[rr.m_offset] = Element {
                    .m_kind = Element::Kind::PointerWithAddend,
                    .m_byteValue = 0,
                    .m_ptrValue = nullptr,
                    .m_sectionRef = rr.m_sectionRef,
                    .m_addend = rr.m_addend
                };
            }

            size_t i = 0;
            while (i < contents.size())
            {
                if (relocs.count(i))
                {
                    for (size_t k = 0; k < 8; k++)
                    {
                        ReleaseAssert(contents[i + k] == 0);
                        ReleaseAssertImp(k > 0, !relocs.count(i + k));
                    }
                    Element e = relocs[i];
                    cdo->m_valueDefs.push_back(e);
                    i += 8;
                }
                else
                {
                    uint8_t value = static_cast<uint8_t>(contents[i]);
                    cdo->m_valueDefs.push_back(Element {
                        .m_kind = Element::Kind::ByteConstant,
                        .m_byteValue = value
                    });
                    i += 1;
                }
            }

            ReleaseAssert(cdo->ComputeTrueSizeWithoutPadding() == contents.size());

            // Important to store into 'neededSharedData' before recursing on the relocations to avoid deadloops
            //
            neededSharedData[sec] = cdo;

            for (auto it : relocs)
            {
                Element e = it.second;
                ReleaseAssert(e.m_kind == Element::Kind::PointerWithAddend);
                handleDataSection(e.m_sectionRef);
            }
        }
    };

    // Collect and process all the data sections needed by the text sections we want to extract
    //
    for (SectionRef& sec : textSectionExtractTargets)
    {
        for (RelocationRef& rref : relocMap[sec])
        {
            RelocationRecord rr = processRelocation(rref);
            if (rr.m_symKind == RelocationRecord::SymKind::SharedConstantDataObject || rr.m_symKind == RelocationRecord::SymKind::PrivateDataAddr)
            {
                handleDataSection(rr.m_sectionRef);
            }
        }
    }

    // Fixup all the StencilSharedConstantDataObject fields
    //
    for (auto& it : neededSharedData)
    {
        StencilSharedConstantDataObject* cdo = it.second;
        using Element = StencilSharedConstantDataObject::Element;
        for (Element& e : cdo->m_valueDefs)
        {
            if (e.m_kind == Element::PointerWithAddend)
            {
                ReleaseAssert(e.m_ptrValue == nullptr);
                ReleaseAssert(neededSharedData.count(e.m_sectionRef));
                StencilSharedConstantDataObject* other = neededSharedData[e.m_sectionRef];
                e.m_ptrValue = other;
                other->m_shouldForwardDeclare = true;
            }
        }
    }

    for (auto& it : neededPrivateData)
    {
        StencilPrivateDataObject* pdo = it.second;
        for (RelocationRecord& rr : pdo->m_relocations)
        {
            if (rr.m_symKind == RelocationRecord::SymKind::SharedConstantDataObject)
            {
                ReleaseAssert(neededSharedData.count(rr.m_sectionRef));
                StencilSharedConstantDataObject* other = neededSharedData[rr.m_sectionRef];
                rr.m_sharedDataObject = other;
            }
        }
    }

    // Merge all the StencilPrivateDataObject together into one
    //
    // First, sort all the objects by alignment to minimize paddings
    //
    std::map<size_t /*alignment*/, std::vector<SectionRef>> sortedPdoList;
    for (auto& it : neededPrivateData)
    {
        SectionRef secRef = it.first;
        StencilPrivateDataObject* pdo = it.second;
        sortedPdoList[pdo->m_alignment].push_back(secRef);
    }

    StencilPrivateDataObject mergedPdo;
    mergedPdo.m_alignment = (sortedPdoList.empty() ? 1 : (--sortedPdoList.end())->first);
    uint64_t pdoSize = 0;
    for (auto& item : sortedPdoList)
    {
        size_t alignment = item.first;
        ReleaseAssert(alignment > 0);
        for (SectionRef secRef : item.second)
        {
            ReleaseAssert(neededPrivateData.count(secRef));
            StencilPrivateDataObject* pdo = neededPrivateData[secRef];
            size_t roundedOffset = (pdoSize + alignment - 1) / alignment * alignment;
            while (pdoSize != roundedOffset)
            {
                mergedPdo.m_bytes.push_back(0);
                pdoSize++;
            }
            ReleaseAssert(pdoSize == mergedPdo.m_bytes.size());
            ReleaseAssert(!mergedPrivateDataSectionIndex.count(secRef));
            mergedPrivateDataSectionIndex[secRef] = pdoSize;

            mergedPdo.m_bytes.insert(mergedPdo.m_bytes.end(), pdo->m_bytes.begin(), pdo->m_bytes.end());

            // All the relocation records in the merged PDO should have their offset updated
            //
            for (RelocationRecord rr : pdo->m_relocations)
            {
                rr.m_offset += pdoSize;
                mergedPdo.m_relocations.push_back(rr);
            }

            pdoSize += pdo->m_bytes.size();
            ReleaseAssert(pdoSize == mergedPdo.m_bytes.size());
        }
    }

    // For RelocationRecord::SymKind::PrivateDataAddr relocations, the addend also needs to be updated
    //
    ReleaseAssert(pdoSize == mergedPdo.m_bytes.size());
    for (RelocationRecord& rr : mergedPdo.m_relocations)
    {
        if (rr.m_symKind == RelocationRecord::SymKind::PrivateDataAddr)
        {
            SectionRef secRef = rr.m_sectionRef;
            ReleaseAssert(mergedPrivateDataSectionIndex.count(secRef));
            uint64_t offset = mergedPrivateDataSectionIndex[secRef];
            rr.m_addend += static_cast<int64_t>(offset);
            rr.m_sectionRef = SectionRef();
        }
    }

    hasMergedPrivateDataSections = true;

    for (RelocationRecord& rr : mergedPdo.m_relocations)
    {
        if (rr.m_symKind == RelocationRecord::SymKind::StencilHole && rr.m_stencilHoleOrd == CP_PLACEHOLDER_STENCIL_DATA_SEC_ADDR)
        {
            ReleaseAssert(!isExtractIcLogic);
            rr.m_symKind = RelocationRecord::SymKind::PrivateDataAddr;
        }
    }

    DeegenStencil ds;

    ds.m_sectionToPdoOffsetMap.clear();
    for (auto& it : mergedPrivateDataSectionIndex)
    {
        SectionRef sec = it.first;
        uint64_t offset = it.second;
        std::string secName = getSectionName(sec);
        ReleaseAssert(!ds.m_sectionToPdoOffsetMap.count(secName));
        ds.m_sectionToPdoOffsetMap[secName] = offset;
    }

    for (auto& it : neededSharedData)
    {
        StencilSharedConstantDataObject* cdo = it.second;
        ds.m_sharedDataObjs.push_back(cdo);
    }

    for (size_t ord = 0; ord < ds.m_sharedDataObjs.size(); ord++)
    {
        ds.m_sharedDataObjs[ord]->m_uniqueLabel = ord;
    }

    ds.m_privateDataObject = mergedPdo;

    ds.m_triple = triple;

    // Process the text sections (fast path and slow path)
    //
    auto getTextSectionPreFixupContents = [&](SectionRef sec) WARN_UNUSED -> std::vector<uint8_t>
    {
        ReleaseAssert(sec.getContents());
        StringRef contents = *sec.getContents();
        std::vector<uint8_t> data;
        data.resize(contents.size());
        memcpy(data.data(), contents.data(), contents.size());
        return data;
    };

    auto processTextSection = [&](SectionRef sec, std::vector<uint8_t>& data /*out*/, std::vector<RelocationRecord>& relos /*out*/)
    {
        ReleaseAssert(sec.getObject() == obj);
        data = getTextSectionPreFixupContents(sec);
        relos.clear();
        for (RelocationRef rref : relocMap[sec])
        {
            RelocationRecord rr = processRelocation(rref);
            if (rr.m_symKind == RelocationRecord::SymKind::SharedConstantDataObject)
            {
                ReleaseAssert(neededSharedData.count(rr.m_sectionRef));
                rr.m_sharedDataObject = neededSharedData[rr.m_sectionRef];
            }
            if (rr.m_symKind == RelocationRecord::SymKind::StencilHole && rr.m_stencilHoleOrd == CP_PLACEHOLDER_STENCIL_DATA_SEC_ADDR)
            {
                ReleaseAssert(!isExtractIcLogic);
                rr.m_symKind = RelocationRecord::SymKind::PrivateDataAddr;
            }
            relos.push_back(rr);
        }
    };

    ds.m_isForIcLogicExtraction = isExtractIcLogic;
    if (!isExtractIcLogic)
    {
        processTextSection(textSection, ds.m_fastPathCode /*out*/, ds.m_fastPathRelos /*out*/);
        if (textSlowSection.getObject() != nullptr)
        {
            processTextSection(textSlowSection, ds.m_slowPathCode /*out*/, ds.m_slowPathRelos /*out*/);
        }
    }
    else
    {
        // Still populate m_fastPathCode and m_slowPathCode for assertion purpose
        //
        ds.m_fastPathCode = getTextSectionPreFixupContents(textSection);
        if (textSlowSection.getObject() != nullptr)
        {
            ds.m_slowPathCode = getTextSectionPreFixupContents(textSlowSection);
        }
        processTextSection(textIcSection, ds.m_icPathCode /*out*/, ds.m_icPathRelos /*out*/);
    }

    // Retrieve the result of all the label distance computations
    //
    {
        ds.m_labelDistanceComputations.clear();
        for (SectionRef sec : obj->sections())
        {
            std::string secName = getSectionName(sec);
            if (secName.starts_with(".rodata.deegen_label_distance_computation_result_"))
            {
                std::string varName = secName.substr(strlen(".rodata."));
                std::vector<uint8_t> contents = getTextSectionPreFixupContents(sec);
                ReleaseAssert(contents.size() == 8);
                ReleaseAssert(relocMap[sec].empty());
                uint64_t resultValue = UnalignedLoad<uint64_t>(contents.data());
                ReleaseAssert(!ds.m_labelDistanceComputations.count(varName));
                ds.m_labelDistanceComputations[varName] = resultValue;
            }
        }
    }

    return ds;
}

// There is no way to determine the C++ type of the symbol from object file. Furthermore, simply giving it a
// fake type can cause conflict (e.g., if the symbol is 'memcpy'). So all the external symbols are printed
// as fake symbols (prefixed with 'deegen_fakeglobal_') in C++ code first, and then fixed up at LLVM level.
//
static std::string WARN_UNUSED PrintStencilFakeExternalSymbolDefs(const std::vector<RelocationRecord>& relocs)
{
    AnonymousFile file;
    FILE* fp = file.GetFStream("w");

    for (const RelocationRecord& rr : relocs)
    {
        if (rr.m_symKind == RelocationRecord::SymKind::ExternalCSymbol)
        {
            fprintf(fp, "extern \"C\" char deegen_fakeglobal_%s[];\n", rr.m_symbolName.c_str());
        }
    }

    fclose(fp);
    return file.GetFileContents();
}

struct PrintStencilCodegenLogicResult
{
    using CondBrLatePatchRecord = DeegenStencilCodegenResult::CondBrLatePatchRecord;

    std::string m_cppCode;
    // The offsets of the CondBr relocations, which destination is not yet populated
    //
    std::vector<CondBrLatePatchRecord> m_condBrFixupOffsets;
    std::vector<uint8_t> m_preFixupMachineCode;
    // Same length as m_preFixupMachineCode, marks whether each byte is part of some relocation
    // For printing audit dump purpose only
    //
    std::vector<bool> m_isPartOfReloc;
};

// This function prints a C++ snippet that assumes the following:
// 1. The C&P placeholder values are named 'deegen_stencil_patch_value_*'
// 2. The destination address is named 'deegen_dstAddr'
// 3. The external symbol names will be prefixed with 'deegen_fakeglobal_', for later fixup
// 4. The fast path, slow path, data section address are named 'deegen_fastPathAddr', 'deegen_slowPathAddr', 'deegen_dataSecAddr' respectively
//
static PrintStencilCodegenLogicResult WARN_UNUSED PrintStencilCodegenLogicImpl(
    const std::vector<uint8_t>& code,
    const std::vector<RelocationRecord>& relocs,
    const std::string& placeholderComputations,
    size_t placeholderOrdForFallthroughIfFastPath,  // -1 if not fast path or not exist
    size_t placeholderOrdForCondBranch,             // -1 if not exist
    bool isForIc)
{
    PrintStencilCodegenLogicResult res;

    AnonymousFile file;
    FILE* fp = file.GetFStream("w");

    bool finalJumpEliminated = false;
    size_t codeLen = code.size();

    // Assert that the relocation records do not overlap
    //
    {
        std::unordered_set<uint64_t> checkUnique;
        for (const RelocationRecord& rr : relocs)
        {
            size_t len;
            switch (rr.m_relocationType)
            {
            case ELF::R_X86_64_PLT32:
            case ELF::R_X86_64_PC32:
            case ELF::R_X86_64_32S:
            case ELF::R_X86_64_32:
            {
                len = 4;
                break;
            }
            case ELF::R_X86_64_64:
            {
                len = 8;
                break;
            }
            default:
            {
                fprintf(stderr, "Unhandled relocation type %llu\n", static_cast<unsigned long long>(rr.m_relocationType));
                abort();
            }
            }
            for (size_t k = rr.m_offset; k < rr.m_offset + len; k++)
            {
                ReleaseAssert(!checkUnique.count(k));
                checkUnique.insert(k);
            }
        }
    }

    // Attempt to eliminate the final jump to fallthrough
    //
    if (placeholderOrdForFallthroughIfFastPath != static_cast<size_t>(-1) && code.size() >= 5)
    {
        bool found = false;
        for (const RelocationRecord& rr : relocs)
        {
            if (rr.m_offset == code.size() - 4 &&
                (rr.m_relocationType == ELF::R_X86_64_PLT32 || rr.m_relocationType == ELF::R_X86_64_PC32) &&
                rr.m_symKind == RelocationRecord::SymKind::StencilHole &&
                rr.m_stencilHoleOrd == placeholderOrdForFallthroughIfFastPath)
            {
                ReleaseAssert(rr.m_addend == -4);
                ReleaseAssert(!found);
                found = true;
            }
        }
        if (found)
        {
            ReleaseAssert(code[code.size() - 5] == 0xe9 /*jmp*/);
            finalJumpEliminated = true;
            codeLen -= 5;
        }
    }

    fprintf(fp, "%s\n", placeholderComputations.c_str());

    size_t roundedLen = RoundUpToMultipleOf<8>(codeLen);
    // Use char* to avoid breaking C strict aliasing
    //
    char* buf = new char[roundedLen];
    Auto(delete [] buf);
    memcpy(buf, code.data(), codeLen);
    for (size_t i = codeLen; i < roundedLen; i++) { buf[i] = '\0'; }

    // Fix up the code to account for the addends, and the offset introduced by R_X86_64_PLT32 and R_X86_64_PC32 relocations.
    // (For R_X86_64_PLT32 R_X86_64_PC32 relocations, we pre-subtract the offset part so that they can be computed
    // later by S + A - startAddr instead of S + A - P)
    //
    for (const RelocationRecord& rr : relocs)
    {
        if (finalJumpEliminated && rr.m_offset == codeLen + 1)
        {
            continue;
        }

        switch (rr.m_relocationType)
        {
        case ELF::R_X86_64_PLT32:
        case ELF::R_X86_64_PC32:
        {
            ReleaseAssert(rr.m_offset + 4 <= codeLen);
            void* p = buf + rr.m_offset;
            uint32_t oldVal = UnalignedLoad<uint32_t>(p);
            uint32_t newVal = oldVal - static_cast<uint32_t>(rr.m_offset) + static_cast<uint32_t>(static_cast<int32_t>(rr.m_addend));
            UnalignedStore<uint32_t>(p, newVal);
            break;
        }
        case ELF::R_X86_64_32S:
        case ELF::R_X86_64_32:
        {
            ReleaseAssert(rr.m_offset + 4 <= codeLen);
            void* p = buf + rr.m_offset;
            uint32_t oldVal = UnalignedLoad<uint32_t>(p);
            uint32_t newVal = oldVal + static_cast<uint32_t>(static_cast<int32_t>(rr.m_addend));
            UnalignedStore<uint32_t>(p, newVal);
            break;
        }
        case ELF::R_X86_64_64:
        {
            ReleaseAssert(rr.m_offset + 8 <= codeLen);
            void* p = buf + rr.m_offset;
            uint64_t oldVal = UnalignedLoad<uint64_t>(p);
            uint64_t newVal = oldVal + static_cast<uint64_t>(rr.m_addend);
            UnalignedStore<uint64_t>(p, newVal);
            break;
        }
        default:
        {
            fprintf(stderr, "Unhandled relocation type %llu\n", static_cast<unsigned long long>(rr.m_relocationType));
            abort();
        }
        }
    }

    fprintf(fp, "static_assert(std::is_same_v<decltype(deegen_dstAddr), RestrictPtr<uint8_t>>);\n");
    fprintf(fp, "static_assert(std::is_same_v<decltype(deegen_fastPathAddr), uint64_t>);\n");
    fprintf(fp, "static_assert(std::is_same_v<decltype(deegen_slowPathAddr), uint64_t>);\n");
    fprintf(fp, "static_assert(std::is_same_v<decltype(deegen_icPathAddr), uint64_t>);\n");
    fprintf(fp, "static_assert(std::is_same_v<decltype(deegen_icDataSecAddr), uint64_t>);\n");
    fprintf(fp, "static_assert(std::is_same_v<decltype(deegen_dataSecAddr), uint64_t>);\n");

    /*
    fprintf(fp, "// Hexcode:\n// ");

    for (size_t i = 0; i < codeLen; i++)
    {
        uint64_t charVal = static_cast<uint8_t>(buf[i]);
        auto printDig = [&](uint64_t dig)
        {
            char c;
            if (dig < 10)
            {
                c = '0' + static_cast<char>(dig);
            }
            else
            {
                c = 'A' + static_cast<char>(dig - 10);
            }
            fprintf(fp, "%c", c);
        };
        printDig(charVal / 16);
        printDig(charVal % 16);
    }
    fprintf(fp, "\n//\n");

    fprintf(fp, "constexpr uint8_t deegen_code_seq[%llu] = {\n", static_cast<unsigned long long>(roundedLen));
    for (size_t i = 0; i < roundedLen; i++)
    {
        fprintf(fp, "%llu", static_cast<unsigned long long>(static_cast<uint8_t>(buf[i])));
        if (i + 1 < roundedLen) { fprintf(fp, ", "); }
        if (i % 16 == 15) { fprintf(fp, "\n"); }
    }
    fprintf(fp, "};\n\n");

    fprintf(fp, "deegen_cp_copy_code(deegen_dstAddr, deegen_code_seq, %llu);\n", static_cast<unsigned long long>(roundedLen));
    */

    // Emit a uint64_t variable 'deegen_patch_symval' that stores the value of the symbol
    //
    auto emitSymbolValue = [&](const RelocationRecord& rr)
    {
        switch (rr.m_symKind)
        {
        case RelocationRecord::SymKind::FastPathAddr:
        {
            fprintf(fp, "uint64_t deegen_patch_symval = deegen_fastPathAddr;\n");
            break;
        }
        case RelocationRecord::SymKind::SlowPathAddr:
        {
            fprintf(fp, "uint64_t deegen_patch_symval = deegen_slowPathAddr;\n");
            break;
        }
        case RelocationRecord::SymKind::ICPathAddr:
        {
            ReleaseAssert(isForIc);
            fprintf(fp, "uint64_t deegen_patch_symval = deegen_icPathAddr;\n");
            break;
        }
        case RelocationRecord::SymKind::MainLogicPrivateDataAddr:
        {
            ReleaseAssert(isForIc);
            fprintf(fp, "uint64_t deegen_patch_symval = deegen_dataSecAddr;\n");
            break;
        }
        case RelocationRecord::SymKind::PrivateDataAddr:
        {
            // PrivateDataAddr always refer to the private data section of this stencil itself
            // So if this stencil is IC logic, the PrivateDataAddr is 'icDataSecAddr'.
            // But if this stenil is main logic, the PrivateDataAddr is 'dataSecAddr'.
            //
            if (isForIc)
            {
                fprintf(fp, "uint64_t deegen_patch_symval = deegen_icDataSecAddr;\n");
            }
            else
            {
                fprintf(fp, "uint64_t deegen_patch_symval = deegen_dataSecAddr;\n");
            }
            break;
        }
        case RelocationRecord::SymKind::SharedConstantDataObject:
        {
            fprintf(fp, "uint64_t deegen_patch_symval = FOLD_CONSTEXPR(reinterpret_cast<uint64_t>(&deegen_stencil_constant_%llu));\n",
                    static_cast<unsigned long long>(rr.m_sharedDataObject->GetUniqueLabel()));
            break;
        }
        case RelocationRecord::SymKind::ExternalCSymbol:
        {
            fprintf(fp, "static_assert(std::is_array_v<decltype(deegen_fakeglobal_%s)>);\n",
                    rr.m_symbolName.c_str());
            fprintf(fp, "uint64_t deegen_patch_symval = FOLD_CONSTEXPR(reinterpret_cast<uint64_t>(deegen_fakeglobal_%s));\n",
                    rr.m_symbolName.c_str());
            break;
        }
        case RelocationRecord::SymKind::StencilHole:
        {
            if (rr.m_stencilHoleOrd == placeholderOrdForCondBranch)
            {
                if (!isForIc)
                {
                    // For bytecode, conditional branch targets are fixed up in the end
                    //
                    fprintf(fp, "uint64_t deegen_patch_symval = 0;\n");
                    bool is64 = (rr.m_relocationType == ELF::R_X86_64_64);
                    res.m_condBrFixupOffsets.push_back({ .m_offset = rr.m_offset, .m_is64Bit = is64 });
                }
                else
                {
                    // For IC, the conditional branch target is represented by a special ordinal
                    // If our caller failed to pass in this ordinal, we'll get a compile error so all is fine
                    //
                    fprintf(fp, "uint64_t deegen_patch_symval = deegen_stencil_patch_value_%llu;\n",
                            static_cast<unsigned long long>(CP_PLACEHOLDER_BYTECODE_CONDBR_DEST));
                }
            }
            else
            {
                fprintf(fp, "static_assert(std::is_same_v<decltype(deegen_stencil_patch_value_%llu), uint64_t>);\n",
                        static_cast<unsigned long long>(rr.m_stencilHoleOrd));
                fprintf(fp, "uint64_t deegen_patch_symval = deegen_stencil_patch_value_%llu;\n",
                        static_cast<unsigned long long>(rr.m_stencilHoleOrd));
            }
            break;
        }
        }
    };

    std::vector<bool> isPartOfRelocation;
    isPartOfRelocation.resize(codeLen);
    for (size_t i = 0; i < codeLen; i++) { isPartOfRelocation[i] = false; }

    auto markAsRelocBytes = [&](size_t start, size_t len)
    {
        ReleaseAssert(start < codeLen && start + len <= codeLen);
        for (size_t i = start; i < start + len; i++) { isPartOfRelocation[i] = true; }
    };

    for (const RelocationRecord& rr : relocs)
    {
        if (finalJumpEliminated && rr.m_offset == codeLen + 1)
        {
            continue;
        }

        switch (rr.m_relocationType)
        {
        case ELF::R_X86_64_PLT32:
        case ELF::R_X86_64_PC32:
        {
            markAsRelocBytes(rr.m_offset, 4);
            uint32_t oldVal = UnalignedLoad<uint32_t>(buf + rr.m_offset);
            fprintf(fp, "{\n");
            emitSymbolValue(rr);
            UnalignedStore<uint32_t>(buf + rr.m_offset, 0);
            fprintf(fp, "deegen_cp_store32(deegen_dstAddr + %llu, "
                        "static_cast<uint32_t>(%lluU) + "
                        "static_cast<uint32_t>(deegen_patch_symval) - "
                        "static_cast<uint32_t>(reinterpret_cast<uint64_t>(deegen_dstAddr)));\n",
                    static_cast<unsigned long long>(rr.m_offset),
                    static_cast<unsigned long long>(oldVal));
            fprintf(fp, "}\n");
            break;
        }
        case ELF::R_X86_64_32S:
        case ELF::R_X86_64_32:
        {
            markAsRelocBytes(rr.m_offset, 4);
            uint32_t oldVal = UnalignedLoad<uint32_t>(buf + rr.m_offset);
            fprintf(fp, "{\n");
            emitSymbolValue(rr);
            UnalignedStore<uint32_t>(buf + rr.m_offset, 0);
            fprintf(fp, "deegen_cp_store32(deegen_dstAddr + %llu, static_cast<uint32_t>(%lluU) + static_cast<uint32_t>(deegen_patch_symval));\n",
                    static_cast<unsigned long long>(rr.m_offset),
                    static_cast<unsigned long long>(oldVal));
            fprintf(fp, "}\n");
            break;
        }
        case ELF::R_X86_64_64:
        {
            markAsRelocBytes(rr.m_offset, 8);
            uint64_t oldVal = UnalignedLoad<uint64_t>(buf + rr.m_offset);
            int64_t oldValSigned = static_cast<int64_t>(oldVal);
            fprintf(fp, "{\n");
            emitSymbolValue(rr);
            if (std::numeric_limits<int32_t>::min() <= oldValSigned && oldValSigned <= std::numeric_limits<int32_t>::max())
            {
                UnalignedStore<uint64_t>(buf + rr.m_offset, 0);
                fprintf(fp, "deegen_cp_store64(deegen_dstAddr + %llu, static_cast<uint64_t>(%lluULL) + deegen_patch_symval);\n",
                        static_cast<unsigned long long>(rr.m_offset),
                        static_cast<unsigned long long>(oldVal));
            }
            else
            {
                fprintf(fp, "deegen_cp_add64(deegen_dstAddr + %llu, deegen_patch_symval);\n",
                        static_cast<unsigned long long>(rr.m_offset));
            }
            fprintf(fp, "}\n");
            break;
        }
        default:
        {
            fprintf(stderr, "Unhandled relocation type %llu\n", static_cast<unsigned long long>(rr.m_relocationType));
            abort();
        }
        }
    }

    fclose(fp);
    res.m_cppCode = file.GetFileContents();

    res.m_preFixupMachineCode.resize(codeLen);
    memcpy(res.m_preFixupMachineCode.data(), buf, codeLen);

    res.m_isPartOfReloc = isPartOfRelocation;

    return res;
}

DeegenStencilCodegenResult WARN_UNUSED DeegenStencil::PrintCodegenFunctions(
    bool mayAttemptToEliminateJmpToFallthrough,
    size_t numBytecodeOperands,
    const std::vector<CPRuntimeConstantNodeBase*>& placeholders,
    const std::vector<size_t>& extraPlaceholderOrds)
{
    ReleaseAssert(placeholders.size() < 10000);
    for (size_t ord : extraPlaceholderOrds) { ReleaseAssert(ord >= 10000); }

    AnonymousFile file;
    FILE* fp = file.GetFStream("w");

    // Must not include iostream! Our later processing stages do not expect the global init logic introduced by iostream, and will fire assertions.
    //
    fprintf(fp, "#include <cstdint>\n");
    fprintf(fp, "#include <cstring>\n");
    fprintf(fp, "#include <type_traits>\n\n");

    fprintf(fp, "#define FOLD_CONSTEXPR(...) (__builtin_constant_p(__VA_ARGS__) ? (__VA_ARGS__) : (__VA_ARGS__))\n");

    fprintf(fp, "template<typename T> using RestrictPtr = T* __restrict__;\n");

    fprintf(fp, "[[maybe_unused]] static void __attribute__((__always_inline__)) deegen_cp_store32(RestrictPtr<uint8_t> ptr, uint32_t val)"
                " { memcpy(ptr, &val, 4); }\n");
    fprintf(fp, "[[maybe_unused]] static void __attribute__((__always_inline__)) deegen_cp_store64(RestrictPtr<uint8_t> ptr, uint64_t val)"
                " { memcpy(ptr, &val, 8); }\n");
    fprintf(fp, "[[maybe_unused]] static void __attribute__((__always_inline__)) deegen_cp_add64(RestrictPtr<uint8_t> ptr, uint64_t val)"
                " { uint64_t oldVal; memcpy(&oldVal, ptr, 8); val += oldVal; memcpy(ptr, &val, 8); }\n");

    fprintf(fp, "[[maybe_unused]] static void __attribute__((__always_inline__)) deegen_cp_copy_code(RestrictPtr<uint8_t> ptr, RestrictPtr<const void> data, uint64_t size)"
                " { __builtin_memcpy(ptr, data, size); }");

    fprintf(fp, "\n");

    if (!m_isForIcLogicExtraction)
    {
        fprintf(fp, "%s", PrintStencilFakeExternalSymbolDefs(m_fastPathRelos).c_str());
        fprintf(fp, "%s", PrintStencilFakeExternalSymbolDefs(m_slowPathRelos).c_str());
    }
    else
    {
        fprintf(fp, "%s", PrintStencilFakeExternalSymbolDefs(m_icPathRelos).c_str());
    }
    fprintf(fp, "%s", PrintStencilFakeExternalSymbolDefs(m_privateDataObject.m_relocations).c_str());

    fprintf(fp, "\n");

    fprintf(fp, "namespace {\n");

    for (StencilSharedConstantDataObject* cdo : m_sharedDataObjs)
    {
        fprintf(fp, "%s", cdo->PrintDeclaration().c_str());
    }

    fprintf(fp, "\n");

    for (StencilSharedConstantDataObject* cdo : m_sharedDataObjs)
    {
        fprintf(fp, "%s", cdo->PrintDefinition().c_str());
    }

    fprintf(fp, "\n");

    auto printFunctionProto = [&](const std::string& funcName)
    {
        fprintf(fp, "extern \"C\" void %s(\n", funcName.c_str());
        fprintf(fp, "    [[maybe_unused]] RestrictPtr<uint8_t> deegen_dstAddr,\n");
        fprintf(fp, "    [[maybe_unused]] uint64_t deegen_fastPathAddr,\n");
        fprintf(fp, "    [[maybe_unused]] uint64_t deegen_slowPathAddr,\n");
        fprintf(fp, "    [[maybe_unused]] uint64_t deegen_icPathAddr,\n");
        fprintf(fp, "    [[maybe_unused]] uint64_t deegen_icDataSecAddr,\n");
        fprintf(fp, "    [[maybe_unused]] uint64_t deegen_dataSecAddr,\n");
        // This is hacky.. The conditional branch is fixed up in the end because we do not have a valid value until
        // after everything is generated. To ensure this, we do not provide deegen_rc_input_102 (the conditional
        // branch target input ordinal) so that we will get a compile error if it showed up unexpectingly...
        //
        // Note that deegen_rc_input_101 (fallthrough) is also needed: it points to the end of the fast path,
        // but the end of the fast path is NOT necessarily the end of this stencil, since the fast path may consist
        // of multiple stencils! So we must let the caller provide this value.
        //
        // Print the remaining special ordinals
        //
        fprintf(fp, "    [[maybe_unused]] int64_t deegen_rc_input_100,\n");
        fprintf(fp, "    [[maybe_unused]] int64_t deegen_rc_input_101,\n");
        fprintf(fp, "    [[maybe_unused]] int64_t deegen_rc_input_103,\n");
        fprintf(fp, "    [[maybe_unused]] int64_t deegen_rc_input_104\n");
        for (size_t i = 0; i < numBytecodeOperands; i++)
        {
            fprintf(fp, "    , [[maybe_unused]] int64_t deegen_rc_input_%llu\n", static_cast<unsigned long long>(i));
        }
        for (size_t ord : extraPlaceholderOrds)
        {
            ReleaseAssert(ord >= 10000);
            fprintf(fp, "    , [[maybe_unused]] uint64_t deegen_stencil_patch_value_%llu\n", static_cast<unsigned long long>(ord));
        }
        fprintf(fp, ") {\n");
    };

    size_t fallthroughPlaceholderOrd = static_cast<size_t>(-1);
    size_t condBrPlaceholderOrd = static_cast<size_t>(-1);

    for (size_t i = 0; i < placeholders.size(); i++)
    {
        if (placeholders[i]->IsRawRuntimeConstant())
        {
            size_t ord = assert_cast<CPRawRuntimeConstant*>(placeholders[i])->m_label;
            if (ord == 101)
            {
                ReleaseAssert(fallthroughPlaceholderOrd == static_cast<size_t>(-1));
                fallthroughPlaceholderOrd = i;
            }
            if (ord == 102)
            {
                ReleaseAssert(condBrPlaceholderOrd == static_cast<size_t>(-1));
                condBrPlaceholderOrd = i;
            }
        }
    }

    if (!mayAttemptToEliminateJmpToFallthrough)
    {
        fallthroughPlaceholderOrd = static_cast<size_t>(-1);
    }

    std::string placeholderComputations;
    {
        CpPlaceholderExprPrinter printer;
        for (size_t i = 0; i < placeholders.size(); i++)
        {
            ReleaseAssert(placeholders[i]->m_bitWidth == 64);
            if (i != condBrPlaceholderOrd)
            {
                std::string tmpVarName = placeholders[i]->PrintExpr(&printer);
                fprintf(printer.GetFd(), "[[maybe_unused]] uint64_t %s = %s;\n", printer.GetResultVarName(i).c_str(), tmpVarName.c_str());
            }
        }
        fclose(printer.GetFd());
        placeholderComputations = printer.GetFile().GetFileContents();
    }

    PrintStencilCodegenLogicResult fastPathInfo;
    PrintStencilCodegenLogicResult slowPathInfo;
    PrintStencilCodegenLogicResult icPathInfo;

    if (!m_isForIcLogicExtraction)
    {
        printFunctionProto(DeegenStencilCodegenResult::x_fastPathCodegenFuncName);
        fastPathInfo = PrintStencilCodegenLogicImpl(
            m_fastPathCode,
            m_fastPathRelos,
            placeholderComputations,
            fallthroughPlaceholderOrd,
            condBrPlaceholderOrd,
            false /*isForIc*/);

        fprintf(fp, "%s\n", fastPathInfo.m_cppCode.c_str());
        fprintf(fp, "}\n\n");

        printFunctionProto(DeegenStencilCodegenResult::x_slowPathCodegenFuncName);
        slowPathInfo = PrintStencilCodegenLogicImpl(
            m_slowPathCode,
            m_slowPathRelos,
            placeholderComputations,
            static_cast<size_t>(-1) /*fallthrough cannot be eliminated*/,
            condBrPlaceholderOrd,
            false /*isForIc*/);

        fprintf(fp, "%s\n", slowPathInfo.m_cppCode.c_str());
        fprintf(fp, "}\n\n");
    }
    else
    {
        printFunctionProto(DeegenStencilCodegenResult::x_icPathCodegenFuncName);
        icPathInfo = PrintStencilCodegenLogicImpl(
            m_icPathCode,
            m_icPathRelos,
            placeholderComputations,
            static_cast<size_t>(-1) /*fallthrough cannot be eliminated*/,
            condBrPlaceholderOrd,
            true /*isForIc*/);

        // For IC, CondBr placeholders are automatically redirected to CP_PLACEHOLDER_BYTECODE_CONDBR_DEST, no late patch should exist
        //
        ReleaseAssert(icPathInfo.m_condBrFixupOffsets.empty());

        fprintf(fp, "%s\n", icPathInfo.m_cppCode.c_str());
        fprintf(fp, "}\n\n");
    }

    printFunctionProto(DeegenStencilCodegenResult::x_dataSecCodegenFuncName);
    PrintStencilCodegenLogicResult dataSecInfo = PrintStencilCodegenLogicImpl(
        m_privateDataObject.m_bytes,
        m_privateDataObject.m_relocations,
        placeholderComputations,
        static_cast<size_t>(-1) /*fallthrough cannot be eliminated*/,
        condBrPlaceholderOrd,
        m_isForIcLogicExtraction /*isForIc*/);

    ReleaseAssertImp(m_isForIcLogicExtraction, dataSecInfo.m_condBrFixupOffsets.empty());

    fprintf(fp, "%s\n", dataSecInfo.m_cppCode.c_str());
    fprintf(fp, "}\n\n");

    fprintf(fp, "} /*namespace*/\n\n");
    fclose(fp);

    return DeegenStencilCodegenResult {
        .m_cppCode = file.GetFileContents(),
        .m_fastPathPreFixupCode = fastPathInfo.m_preFixupMachineCode,
        .m_slowPathPreFixupCode = slowPathInfo.m_preFixupMachineCode,
        .m_icPathPreFixupCode = icPathInfo.m_preFixupMachineCode,
        .m_dataSecPreFixupCode = dataSecInfo.m_preFixupMachineCode,
        .m_dataSecAlignment = m_privateDataObject.m_alignment,
        .m_condBrFixupOffsetsInFastPath = fastPathInfo.m_condBrFixupOffsets,
        .m_condBrFixupOffsetsInSlowPath = slowPathInfo.m_condBrFixupOffsets,
        .m_condBrFixupOffsetsInDataSec = dataSecInfo.m_condBrFixupOffsets,
        .m_fastPathRelocMarker = fastPathInfo.m_isPartOfReloc,
        .m_slowPathRelocMarker = slowPathInfo.m_isPartOfReloc,
        .m_icPathRelocMarker = icPathInfo.m_isPartOfReloc,
        .m_dataSecRelocMarker = dataSecInfo.m_isPartOfReloc,
        .m_isForIcLogicExtraction = m_isForIcLogicExtraction
    };
}

std::unique_ptr<llvm::Module> WARN_UNUSED DeegenStencilCodegenResult::GenerateCodegenLogicLLVMModule(llvm::Module* originModule, const std::string& cppStorePath)
{
    using namespace llvm;
    LLVMContext& ctx = originModule->getContext();

    std::string llvmBitcode = CompileCppFileToLLVMBitcode(m_cppCode, cppStorePath);
    std::unique_ptr<Module> cgMod = ParseLLVMModuleFromString(ctx, "stencil_jit_codegen_logic_module" /*moduleName*/, llvmBitcode);
    ReleaseAssert(cgMod.get() != nullptr);

    // Sanity check that the module is as expected
    //
    auto sanityCheckModule = [&](Module* m)
    {
        if (!m_isForIcLogicExtraction)
        {
            ReleaseAssert(m->getFunction(x_fastPathCodegenFuncName) != nullptr);
            ReleaseAssert(m->getFunction(x_slowPathCodegenFuncName) != nullptr);
            ReleaseAssert(m->getFunction(x_icPathCodegenFuncName) == nullptr);
            ReleaseAssert(m->getFunction(x_dataSecCodegenFuncName) != nullptr);
        }
        else
        {
            ReleaseAssert(m->getFunction(x_fastPathCodegenFuncName) == nullptr);
            ReleaseAssert(m->getFunction(x_slowPathCodegenFuncName) == nullptr);
            ReleaseAssert(m->getFunction(x_icPathCodegenFuncName) != nullptr);
            ReleaseAssert(m->getFunction(x_dataSecCodegenFuncName) != nullptr);
        }
    };

    sanityCheckModule(cgMod.get());

    constexpr const char* x_fakeGlobalPrefix = "deegen_fakeglobal_";

    // LLVM may lower llvm.XXX intrinsic calls to library function calls, thus introduces new globals that does not exist in the original module
    // This whitelist records the list of such globals, and how to correctly reproduce their declaration at LLVM IR level.
    //
    std::unordered_map<std::string, std::function<void(Module*)>> whitelistedGlobalNames;

    whitelistedGlobalNames["memcpy"] = [&](Module* m)
    {
        FunctionType* fty = FunctionType::get(llvm_type_of<void*>(ctx), { llvm_type_of<void*>(ctx), llvm_type_of<void*>(ctx), llvm_type_of<size_t>(ctx) }, false /*isVarArg*/);
        Function* fn = Function::Create(fty, GlobalValue::ExternalLinkage, "memcpy", m);
        ReleaseAssert(fn->getName() == "memcpy");
        fn->addParamAttr(0, Attribute::NoAlias);
        fn->addParamAttr(0, Attribute::NoCapture);
        fn->addParamAttr(0, Attribute::WriteOnly);
        fn->addParamAttr(1, Attribute::NoAlias);
        fn->addParamAttr(1, Attribute::NoCapture);
        fn->addParamAttr(1, Attribute::ReadOnly);
        fn->addFnAttr(Attribute::NoUnwind);
        fn->addFnAttr(Attribute::WillReturn);
        // Important to set DSOLocal so we can get the PLT address (which sits in the first 2GB),
        // instead of the actual address in the glibc shared library (which doesn't sit in the first 2GB and breaks our assumption!)
        //
        fn->setDSOLocal(true);
    };

    whitelistedGlobalNames["memmove"] = [&](Module* m)
    {
        FunctionType* fty = FunctionType::get(llvm_type_of<void*>(ctx), { llvm_type_of<void*>(ctx), llvm_type_of<void*>(ctx), llvm_type_of<size_t>(ctx) }, false /*isVarArg*/);
        Function* fn = Function::Create(fty, GlobalValue::ExternalLinkage, "memmove", m);
        ReleaseAssert(fn->getName() == "memmove");
        fn->addParamAttr(0, Attribute::NoCapture);
        fn->addParamAttr(0, Attribute::WriteOnly);
        fn->addParamAttr(1, Attribute::NoCapture);
        fn->addParamAttr(1, Attribute::ReadOnly);
        fn->addFnAttr(Attribute::NoUnwind);
        fn->addFnAttr(Attribute::WillReturn);
        // Important to set DSOLocal so we can get the PLT address (which sits in the first 2GB),
        // instead of the actual address in the glibc shared library (which doesn't sit in the first 2GB and breaks our assumption!)
        //
        fn->setDSOLocal(true);
    };

    for (GlobalValue& gv : cgMod->global_values())
    {
        std::string name = gv.getName().str();
        if (name.starts_with(x_fakeGlobalPrefix))
        {
            // All the fake globals should be external declarations
            //
            ReleaseAssert(isa<GlobalVariable>(gv));
            ReleaseAssert(gv.isDeclaration());
            ReleaseAssert(gv.hasExternalLinkage());

            // The real global should also be locatable and external in the original module
            //
            std::string realGlobalName = name.substr(strlen(x_fakeGlobalPrefix));
            if (!whitelistedGlobalNames.count(realGlobalName))
            {
                GlobalValue* realGv = originModule->getNamedValue(realGlobalName);
                if (realGv == nullptr)
                {
                    fprintf(stderr, "[ERROR] Stencil generation introduced unknown global '%s'!", realGlobalName.c_str());
                    abort();
                }
                if (realGv->isLocalLinkage(realGv->getLinkage()))
                {
                    fprintf(stderr, "[ERROR] Stencil generation introduced global '%s' with local linkage, which is unexpected!", realGlobalName.c_str());
                    abort();
                }
            }
        }
        else if (name != x_fastPathCodegenFuncName && name != x_slowPathCodegenFuncName && name != x_dataSecCodegenFuncName && name != x_icPathCodegenFuncName)
        {
            // All the other non-declaration globals should have local linkage
            // This must be enforced to make sure that we will not introduce link errors or ODR violations caused by name collision
            //
            if (!gv.isDeclaration())
            {
                ReleaseAssert(gv.isLocalLinkage(gv.getLinkage()));
                // They should also not be functions, because the CPP file should not contain any other functions
                //
                ReleaseAssert(!isa<Function>(gv));
            }
        }
        else
        {
            ReleaseAssert(!gv.isDeclaration() && gv.hasExternalLinkage());
        }
    }

    // Make a copy of the original module, remove all function bodies
    //
    std::unique_ptr<Module> module = CloneModule(*originModule);
    for (Function& func : module->functions())
    {
        if (!func.isDeclaration())
        {
            func.deleteBody();
            func.setLinkage(GlobalValue::ExternalLinkage);
        }
    }

    ValidateLLVMModule(module.get());

    // Link the codegen logic into the original module
    //
    {
        Linker linker(*module);
        // linkInModule returns true on error
        //
        ReleaseAssert(linker.linkInModule(std::move(cgMod)) == false);
    }

    ValidateLLVMModule(module.get());

    // Import the known global declarations if they don't exist
    //
    for (auto& it : whitelistedGlobalNames)
    {
        std::string gvName = it.first;
        auto& createDeclFunctor = it.second;
        if (module->getNamedValue(gvName) == nullptr)
        {
            createDeclFunctor(module.get());
        }
    }

    for (auto& it : whitelistedGlobalNames)
    {
        std::string gvName = it.first;
        ReleaseAssert(module->getFunction(gvName) != nullptr);
    }

    // Fixup the fakeGlobal references
    //
    {
        std::vector<GlobalValue*> fakeGvList;
        for (GlobalValue& gv : module->global_values())
        {
            std::string name = gv.getName().str();
            if (name.starts_with(x_fakeGlobalPrefix))
            {
                ReleaseAssert(isa<GlobalVariable>(gv));
                ReleaseAssert(gv.isDeclaration());
                ReleaseAssert(gv.hasExternalLinkage());
                fakeGvList.push_back(&gv);
            }
        }

        for (GlobalValue* gv : fakeGvList)
        {
            std::string name = gv->getName().str();
            ReleaseAssert(name.starts_with(x_fakeGlobalPrefix));
            std::string replaceName = name.substr(strlen(x_fakeGlobalPrefix));
            ReleaseAssert(!replaceName.starts_with(x_fakeGlobalPrefix));
            GlobalValue* replaceGv = module->getNamedValue(replaceName);
            ReleaseAssert(replaceGv != nullptr);
            ReleaseAssert(llvm_value_has_type<void*>(gv) && llvm_value_has_type<void*>(replaceGv));
            gv->replaceAllUsesWith(replaceGv);
        }
    }

    RunLLVMDeadGlobalElimination(module.get());

    sanityCheckModule(module.get());

    for (GlobalValue& gv : module->global_values())
    {
        ReleaseAssert(!gv.getName().startswith(x_fakeGlobalPrefix));
    }

    return module;
}

// Logic stolen from llvm-objdump.cpp
//
struct SimpleDisassembler
{
    SimpleDisassembler(llvm::Triple triple)
    {
        std::string err;
        target = TargetRegistry::lookupTarget(triple.getTriple(), err /*out*/);
        if (target == nullptr)
        {
            fprintf(stderr, "llvm::TargetRegistry::lookupTarget failed, error message: %s\n", err.c_str());
            abort();
        }

        SubtargetFeatures Features;
        Features.getDefaultSubtargetFeatures(triple);

        STI.reset(target->createMCSubtargetInfo(triple.getTriple(), "" /*MCPU*/, Features.getString()));
        if (!STI)
        {
            fprintf(stderr, "Failed to set up MCSubtargetInfo\n");
            abort();
        }

        MRI.reset(target->createMCRegInfo(triple.getTriple()));
        if (!MRI)
        {
            fprintf(stderr, "Failed to set up MCRegisterInfo\n");
            abort();
        }

        MII.reset(target->createMCInstrInfo());
        if (!MII)
        {
            fprintf(stderr, "Failed to set up MCInstrInfo\n");
            abort();
        }

        AsmInfo.reset(target->createMCAsmInfo(*MRI, triple.getTriple(), MCTargetOptions()));
        if (!AsmInfo)
        {
            fprintf(stderr, "Failed to set up MCAsmInfo\n");
            abort();
        }

        mcCtx.reset(new MCContext(triple, AsmInfo.get(), MRI.get(), STI.get()));
        disAsm.reset(target->createMCDisassembler(*STI, *mcCtx.get()));

        MIA.reset(target->createMCInstrAnalysis(MII.get()));
        if (!MIA)
        {
            fprintf(stderr, "Failed to set up MCInstrAnalysis\n");
            abort();
        }

        IP.reset(target->createMCInstPrinter(triple, AsmInfo->getAssemblerDialect(), *AsmInfo, *MII, *MRI));
        if (!IP)
        {
            fprintf(stderr, "Failed to set up MCInstPrinter\n");
            abort();
        }

        IP->setPrintImmHex(true);
        IP->setPrintBranchImmAsAddress(true);
        IP->setSymbolizeOperands(false);
        IP->setMCInstrAnalysis(MIA.get());
    }

    std::pair<size_t /*numBytes*/, std::string> WARN_UNUSED Disassemble(const std::vector<uint8_t>& data, size_t offset)
    {
        SmallString<40> Comments;
        raw_svector_ostream CommentStream(Comments);

        ReleaseAssert(offset < data.size());

        MCInst inst;
        uint64_t size = 0;
        bool disassembled = disAsm->getInstruction(inst /*out*/,
                                                   size /*out*/,
                                                   ArrayRef<uint8_t>(data.data() + offset, data.data() + data.size()),
                                                   offset /*thisAddr*/,
                                                   CommentStream);
        ReleaseAssert(disassembled);
        ReleaseAssert(size != 0);

        SmallString<40> output;
        raw_svector_ostream outputSS(output);

        ReleaseAssert(offset + size <= data.size());

        IP->printInst(&inst, offset + size /*instEndAddr*/, "", *STI.get(), outputSS);

        return std::make_pair(size, output.str().str());
    }

    const Target* target;
    std::unique_ptr<const MCSubtargetInfo> STI;
    std::unique_ptr<const MCRegisterInfo> MRI;
    std::unique_ptr<const MCInstrInfo> MII;
    std::unique_ptr<const MCAsmInfo> AsmInfo;
    std::unique_ptr<MCContext> mcCtx;
    std::unique_ptr<MCDisassembler> disAsm;
    std::unique_ptr<const MCInstrAnalysis> MIA;
    std::unique_ptr<MCInstPrinter> IP;
};

std::string WARN_UNUSED DumpStencilDisassemblyForAuditPurpose(
    llvm::Triple triple,
    bool isDataSection,
    const std::vector<uint8_t>& preFixupCode,
    const std::vector<bool>& isPartOfReloc,
    const std::string& linePrefix)
{
    using namespace llvm;
    AnonymousFile file;
    FILE* fp = file.GetFStream("w");

    ReleaseAssert(preFixupCode.size() == isPartOfReloc.size());

    SimpleDisassembler disas(triple);

    auto printByteAtOffset = [&](uint64_t offset)
    {
        ReleaseAssert(offset < preFixupCode.size());
        if (isPartOfReloc[offset])
        {
            fprintf(fp, " **");
        }
        else
        {
            fprintf(fp, " %1x%1x", static_cast<int>(preFixupCode[offset] / 16), static_cast<int>(preFixupCode[offset] % 16));
        }
    };

    uint64_t maxBytesPerLine = (isDataSection ? 16 : 8);
    auto printAndAlignOneLineOfBytes = [&](uint64_t offset, uint64_t len)
    {
        ReleaseAssert(len <= maxBytesPerLine);
        for (size_t i = 0; i < len; i++)
        {
            printByteAtOffset(offset + i);
        }
        for (size_t i = len; i < maxBytesPerLine; i++)
        {
            fprintf(fp, "   ");
        }
    };

    if (!isDataSection)
    {
        uint64_t offset = 0;
        while (offset < preFixupCode.size())
        {
            auto [size, asmStr] = disas.Disassemble(preFixupCode, offset);
            ReleaseAssert(offset + size <= preFixupCode.size());

            fprintf(fp, "%s%3llx:", linePrefix.c_str(), static_cast<unsigned long long>(offset));

            printAndAlignOneLineOfBytes(offset, std::min(maxBytesPerLine, size));

            fprintf(fp, "%s\n", asmStr.c_str());

            if (size > maxBytesPerLine)
            {
                uint64_t curOffset = offset + maxBytesPerLine;
                uint64_t remainingBytes = size - maxBytesPerLine;
                while (remainingBytes > 0)
                {
                    uint64_t numBytesToPrint = std::min(remainingBytes, maxBytesPerLine);
                    fprintf(fp, "%s    ", linePrefix.c_str());
                    printAndAlignOneLineOfBytes(curOffset, numBytesToPrint);
                    remainingBytes -= numBytesToPrint;
                    curOffset += numBytesToPrint;
                    fprintf(fp, "\n");
                }
            }

            offset += size;
            ReleaseAssert(offset <= preFixupCode.size());
        }
    }
    else
    {
        uint64_t offset = 0;
        uint64_t remainingBytes = preFixupCode.size();
        while (remainingBytes > 0)
        {
            uint64_t numBytesToPrint = std::min(remainingBytes, maxBytesPerLine);
            fprintf(fp, "%s    ", linePrefix.c_str());
            printAndAlignOneLineOfBytes(offset, numBytesToPrint);
            remainingBytes -= numBytesToPrint;
            offset += numBytesToPrint;
            fprintf(fp, "\n");
        }
    }

    fclose(fp);
    return file.GetFileContents();
}

}   // namespace dast
