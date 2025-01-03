#include "fps_main.h"
#include "read_file.h"
#include "transactional_output_file.h"
#include "llvm/IRReader/IRReader.h"
#include "deegen_process_bytecode_for_baseline_jit.h"
#include "json_utils.h"
#include "drt/baseline_jit_codegen_helper.h"
#include "base64_util.h"
#include "deegen_postprocess_module_linker.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "llvm_identical_function_merger.h"
#include "deegen_global_bytecode_trait_accessor.h"
#include "deegen_jit_slow_path_data.h"
#include "json_parse_dump.h"

using namespace dast;

void FPS_ProcessBytecodeDefinitionForBaselineJit()
{
    using namespace llvm;
    std::unique_ptr<LLVMContext> llvmCtxHolder = std::make_unique<llvm::LLVMContext>();
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::string inputFileName = cl_jsonInputFilename;
    ReleaseAssert(inputFileName != "");

    json_t inputJson = ParseJsonFromFileName(cl_jsonInputFilename);

    BytecodeOpcodeRawValueMap byOpMap = BytecodeOpcodeRawValueMap::ParseFromCommandLineArgs();
    DeegenGlobalBytecodeTraitAccessor bcTraitAccessor = DeegenGlobalBytecodeTraitAccessor::ParseFromCommandLineArgs();

    ReleaseAssert(inputJson.count("all-bytecode-info"));
    json_t& bytecodeInfoListJson = inputJson["all-bytecode-info"];
    ReleaseAssert(bytecodeInfoListJson.is_array());

    ReleaseAssert(cl_jsonOutputFilename != "");
    TransactionalOutputFile jsonOutput(cl_jsonOutputFilename);

    ReleaseAssert(cl_headerOutputFilename != "");
    TransactionalOutputFile hdrOutput(cl_headerOutputFilename);
    FILE* hdrFp = hdrOutput.fp();
    FPS_EmitHeaderFileCommonHeader(hdrFp);
    fprintf(hdrFp, "#include \"drt/baseline_jit_codegen_helper.h\"\n\n");

    std::vector<std::unique_ptr<Module>> allCodegenModules;
    std::vector<std::pair<std::string /*funcName*/, std::unique_ptr<Module>>> allSlowPathModules;
    std::vector<std::pair<std::string /*funcName*/, std::unique_ptr<Module>>> allRetContModules;

    for (size_t bytecodeDefOrd = 0; bytecodeDefOrd < bytecodeInfoListJson.size(); bytecodeDefOrd++)
    {
        json_t& curBytecodeInfoJson = bytecodeInfoListJson[bytecodeDefOrd];

        BytecodeIrInfo bii(ctx, curBytecodeInfoJson);
        bii.m_bytecodeDef->ComputeBaselineJitSlowPathDataLayout();

        DeegenProcessBytecodeForBaselineJitResult res = DeegenProcessBytecodeForBaselineJitResult::Create(&bii, bcTraitAccessor);

        // Emit C++ code that populates the dispatch table entries
        //
        {
            std::string cgFnName = res.m_baselineJitInfo.m_resultFuncName;
            ReleaseAssert(cgFnName != "");
            size_t start = res.m_opcodeRawValue;
            size_t end = res.m_opcodeRawValue + res.m_opcodeNumFuseIcVariants + 1;
            ReleaseAssert(start < byOpMap.GetDispatchTableLength() && end <= byOpMap.GetDispatchTableLength());

            size_t bytecodeStructLength = res.m_bytecodeDef->GetBytecodeStructLength();

            fprintf(hdrFp, "extern \"C\" void %s();\n", cgFnName.c_str());
            fprintf(hdrFp, "namespace {\n\n");
            fprintf(hdrFp, "template<typename T> struct populate_baseline_jit_dispatch_table_%s {\n", res.m_bytecodeDef->GetBytecodeIdName().c_str());
            fprintf(hdrFp, "static consteval void run(T* p) {\n");
            for (size_t k = start; k < end; k++)
            {
                fprintf(hdrFp, "p->set(%llu, FOLD_CONSTEXPR(reinterpret_cast<void*>(&%s)));\n",
                        static_cast<unsigned long long>(k), cgFnName.c_str());
            }
            fprintf(hdrFp, "}\n};\n}\n\n");

            fprintf(hdrFp, "namespace {\n\n");
            fprintf(hdrFp, "template<typename T> struct populate_baseline_jit_bytecode_traits_%s {\n", res.m_bytecodeDef->GetBytecodeIdName().c_str());
            fprintf(hdrFp, "static consteval void run(T* p) {\n");
            fprintf(hdrFp, "constexpr BytecodeBaselineJitTraits x_traitValue = BytecodeBaselineJitTraits {\n");
            ReleaseAssert(res.m_baselineJitInfo.m_fastPathCodeLen <= 65535);
            fprintf(hdrFp, "    .m_fastPathCodeLen = %llu,\n", static_cast<unsigned long long>(res.m_baselineJitInfo.m_fastPathCodeLen));
            ReleaseAssert(res.m_baselineJitInfo.m_slowPathCodeLen <= 65535);
            fprintf(hdrFp, "    .m_slowPathCodeLen = %llu,\n", static_cast<unsigned long long>(res.m_baselineJitInfo.m_slowPathCodeLen));
            ReleaseAssert(res.m_baselineJitInfo.m_dataSectionCodeLen <= 65535);
            fprintf(hdrFp, "    .m_dataSectionCodeLen = %llu,\n", static_cast<unsigned long long>(res.m_baselineJitInfo.m_dataSectionCodeLen));
            ReleaseAssert(res.m_baselineJitInfo.m_slowPathDataLen <= 65535);
            fprintf(hdrFp, "    .m_slowPathDataLen = %llu,\n", static_cast<unsigned long long>(res.m_baselineJitInfo.m_slowPathDataLen));
            ReleaseAssert(res.m_baselineJitInfo.m_dataSectionAlignment <= x_jitMaxPossibleDataSectionAlignment);
            ReleaseAssert(is_power_of_2(res.m_baselineJitInfo.m_dataSectionAlignment));
            fprintf(hdrFp, "    .m_dataSectionAlignment = %llu,\n", static_cast<unsigned long long>(res.m_baselineJitInfo.m_dataSectionAlignment));
            ReleaseAssert(res.m_baselineJitInfo.m_numCondBrLatePatches <= 65535);
            ReleaseAssert(bytecodeStructLength <= 255);
            fprintf(hdrFp, "    .m_bytecodeLength = %llu,\n", static_cast<unsigned long long>(bytecodeStructLength));
            ReleaseAssert(res.m_baselineJitInfo.m_numCondBrLatePatches <= 255);
            fprintf(hdrFp, "    .m_numCondBrLatePatches = %llu,\n", static_cast<unsigned long long>(res.m_baselineJitInfo.m_numCondBrLatePatches));
            size_t numCallIcSites = res.m_bytecodeDef->GetNumCallICsInBaselineJitTier();
            ReleaseAssert(numCallIcSites <= 255);
            fprintf(hdrFp, "    .m_numCallIcSites = %llu,\n", static_cast<unsigned long long>(numCallIcSites));
            size_t callIcSiteOffsetInSlowPathData;
            if (numCallIcSites > 0)
            {
                callIcSiteOffsetInSlowPathData = res.m_bytecodeDef->GetBaselineJitSlowPathDataLayout()->m_callICs.GetOffsetForSite(0);
            }
            else
            {
                callIcSiteOffsetInSlowPathData = 0;
            }
            ReleaseAssert(callIcSiteOffsetInSlowPathData <= 65535);
            fprintf(hdrFp, "    .m_callIcSiteOffsetInSlowPathData = %llu\n,", static_cast<unsigned long long>(callIcSiteOffsetInSlowPathData));
            fprintf(hdrFp, "    .m_unused = 0\n");
            fprintf(hdrFp, "};\n");

            for (size_t k = start; k < end; k++)
            {
                fprintf(hdrFp, "p->set(%llu, x_traitValue);\n", static_cast<unsigned long long>(k));
            }
            fprintf(hdrFp, "}\n};\n}\n\n");
        }

        // Emit C++ code that populates the Call IC trait table entries
        //
        {
            using CallIcTraitDesc = JitCodeGenLogicCreator::CallIcTraitDesc;
            std::vector<CallIcTraitDesc> icTraitList = res.m_baselineJitInfo.m_allCallIcTraitDescs;
            size_t icTraitTableLength = bcTraitAccessor.GetJitCallIcTraitTableLength();
            for (CallIcTraitDesc& icTrait : icTraitList)
            {
                ReleaseAssert(icTrait.m_ordInTraitTable < icTraitTableLength);
                fprintf(hdrFp, "__attribute__((__section__(\"deegen_call_ic_trait_table_section\"))) constexpr JitCallInlineCacheTraitsHolder<%llu> ",
                        static_cast<unsigned long long>(icTrait.m_codePtrPatchRecords.size()));
                fprintf(hdrFp, "x_deegen_jit_call_ic_trait_ord_%llu(\n", static_cast<unsigned long long>(icTrait.m_ordInTraitTable));

                // If one IC has more than 8KB of code, probably something is seriously wrong...
                //
                ReleaseAssert(icTrait.m_allocationLength <= x_jit_mem_alloc_stepping_array[x_jit_mem_alloc_total_steppings - 1]);
                size_t icAllocationLengthStepping = GetJitMemoryAllocatorSteppingFromSmallAllocationSize(icTrait.m_allocationLength);
                ReleaseAssert(icAllocationLengthStepping < x_jit_mem_alloc_total_steppings);
                ReleaseAssert(x_jit_mem_alloc_stepping_array[icAllocationLengthStepping] >= icTrait.m_allocationLength);
                fprintf(hdrFp, "    %llu,\n", static_cast<unsigned long long>(icAllocationLengthStepping));
                fprintf(hdrFp, "    %s /*isDirectCallMode*/,\n", (icTrait.m_isDirectCall ? "true" : "false"));
                fprintf(hdrFp, "    std::array<JitCallInlineCacheTraits::PatchRecord, %llu> {", static_cast<unsigned long long>(icTrait.m_codePtrPatchRecords.size()));

                for (size_t i = 0; i < icTrait.m_codePtrPatchRecords.size(); i++)
                {
                    uint64_t offset = icTrait.m_codePtrPatchRecords[i].first;
                    bool is64 = icTrait.m_codePtrPatchRecords[i].second;
                    ReleaseAssert(offset <= 65535);
                    if (i > 0) { fprintf(hdrFp, ","); }
                    fprintf(hdrFp, "\n        JitCallInlineCacheTraits::PatchRecord {\n");
                    fprintf(hdrFp, "            .m_offset = %llu,\n", static_cast<unsigned long long>(offset));
                    fprintf(hdrFp, "            .m_is64 = %s\n", (is64 ? "true" : "false"));
                    fprintf(hdrFp, "        }");
                }
                fprintf(hdrFp, "\n    });\n\n");
            }
        }

        // Emit C++ code that populates the allocation length stepping table of generic IC
        //
        {
            using GenericIcTraitDesc = AstInlineCache::JitFinalLoweringResult::TraitDesc;

            std::vector<GenericIcTraitDesc> icTraitList = res.m_baselineJitInfo.m_allGenericIcTraitDescs;
            size_t icTraitTableLength = bcTraitAccessor.GetJitGenericIcEffectTraitTableLength();
            fprintf(hdrFp, "namespace {\n\n");
            fprintf(hdrFp, "template<typename T> struct populate_baseline_jit_generic_ic_allocation_length_stepping_table_%s {\n", res.m_bytecodeDef->GetBytecodeIdName().c_str());
            fprintf(hdrFp, "static consteval void run(T* p) {\n");
            fprintf(hdrFp, "std::ignore = p;\n");
            for (GenericIcTraitDesc& icTrait : icTraitList)
            {
                ReleaseAssert(icTrait.m_ordInTraitTable < icTraitTableLength);
                // If one IC has more than 8KB of code, probably something is seriously wrong...
                //
                ReleaseAssert(icTrait.m_allocationLength <= x_jit_mem_alloc_stepping_array[x_jit_mem_alloc_total_steppings - 1]);
                size_t icAllocationLengthStepping = GetJitMemoryAllocatorSteppingFromSmallAllocationSize(icTrait.m_allocationLength);
                ReleaseAssert(icAllocationLengthStepping < x_jit_mem_alloc_total_steppings);
                ReleaseAssert(x_jit_mem_alloc_stepping_array[icAllocationLengthStepping] >= icTrait.m_allocationLength);
                fprintf(hdrFp, "p->set(%llu, %llu);\n",
                        static_cast<unsigned long long>(icTrait.m_ordInTraitTable), static_cast<unsigned long long>(icAllocationLengthStepping));
            }
            fprintf(hdrFp, "}\n};\n}\n\n");
        }

        // Push generated modules to the list of modules to be linked, and also set section
        //
        for (auto& m : res.m_aotSlowPaths)
        {
            Function* fn = m.m_module->getFunction(m.m_funcName);
            ReleaseAssert(fn != nullptr);
            fn->setSection(BaselineJitImplCreator::x_slowPathSectionName);
            allSlowPathModules.push_back(std::make_pair(fn->getName().str(), std::move(m.m_module)));
        }

        for (auto& m : res.m_aotSlowPathReturnConts)
        {
            Function* fn = m.m_module->getFunction(m.m_funcName);
            ReleaseAssert(fn != nullptr);
            fn->setSection(BaselineJitImplCreator::x_slowPathSectionName);
            allRetContModules.push_back(std::make_pair(fn->getName().str(), std::move(m.m_module)));
        }

        ReleaseAssert(res.m_baselineJitInfo.m_cgMod->getFunction(res.m_baselineJitInfo.m_resultFuncName) != nullptr);

        // Compile the main codegen function module to ASM for audit purpose
        //
        std::string cgFnAsmForAudit = CompileLLVMModuleToAssemblyFile(res.m_baselineJitInfo.m_cgMod.get(), Reloc::Static, CodeModel::Small);

        allCodegenModules.push_back(std::move(res.m_baselineJitInfo.m_cgMod));

        // Write the codegen logic audit file
        //
        {
            std::string cgFnAuditFileContents = res.m_baselineJitInfo.m_disasmForAudit + cgFnAsmForAudit;
            std::string cgFnAuditFileName = res.m_bytecodeDef->GetBytecodeIdName() + ".s";

            std::string auditFilePath = FPS_GetAuditFilePathWithTwoPartName("baseline_jit" /*dirSuffix*/, cgFnAuditFileName);
            TransactionalOutputFile auditFile(auditFilePath);
            auditFile.write(cgFnAuditFileContents);
            auditFile.Commit();
        }

        // Write the verbose audit files
        //
        for (size_t i = 0; i < res.m_baselineJitInfo.m_implModulesForAudit.size(); i++)
        {
            Module* m = res.m_baselineJitInfo.m_implModulesForAudit[i].first.get();
            std::string fnName = res.m_baselineJitInfo.m_implModulesForAudit[i].second;
            std::string auditFileName = res.m_bytecodeDef->GetBytecodeIdName();
            if (i > 0)
            {
                size_t k = fnName.rfind("_retcont_");
                ReleaseAssert(k != std::string::npos);
                auditFileName += fnName.substr(k);
            }
            else
            {
                ReleaseAssert(fnName.rfind("_retcont_") == std::string::npos);
            }
            auditFileName += ".ll";
            std::string auditFileContents = DumpLLVMModuleAsString(m);

            std::string auditFilePath = FPS_GetAuditFilePathWithTwoPartName("baseline_jit_verbose" /*dirSuffix*/, auditFileName);
            TransactionalOutputFile auditFile(auditFilePath);
            auditFile.write(auditFileContents);
            auditFile.Commit();
        }

        for (auto& extraAuditFile : res.m_baselineJitInfo.m_extraAuditFiles)
        {
            std::string auditFilePath = FPS_GetAuditFilePathWithTwoPartName("baseline_jit" /*dirSuffix*/, extraAuditFile.first);
            TransactionalOutputFile auditFile(auditFilePath);
            auditFile.write(extraAuditFile.second);
            auditFile.Commit();
        }

        for (auto& verboseAuditFile : res.m_baselineJitInfo.m_extraVerboseAuditFiles)
        {
            std::string auditFilePath = FPS_GetAuditFilePathWithTwoPartName("baseline_jit_verbose" /*dirSuffix*/, verboseAuditFile.first);
            TransactionalOutputFile auditFile(auditFilePath);
            auditFile.write(verboseAuditFile.second);
            auditFile.Commit();
        }
    }

    // Store JSON output file that contains all the generated logic for baseline JIT
    //
    {
        json_t j = json_t::object();
        {
            json_t list = json_t::array();
            for (auto& m : allCodegenModules)
            {
                list.push_back(base64_encode(DumpLLVMModuleAsString(m.get())));
            }
            j["all_codegen_modules"] = std::move(list);
        }
        {
            json_t list = json_t::array();
            for (auto& it : allSlowPathModules)
            {
                json_t element = json_t::object();
                element["function_name"] = it.first;
                element["base64_module"] = base64_encode(DumpLLVMModuleAsString(it.second.get()));
                list.push_back(std::move(element));
            }
            j["all_aot_slow_paths"] = std::move(list);
        }
        {
            json_t list = json_t::array();
            for (auto& it : allRetContModules)
            {
                json_t element = json_t::object();
                element["function_name"] = it.first;
                element["base64_module"] = base64_encode(DumpLLVMModuleAsString(it.second.get()));
                list.push_back(std::move(element));
            }
            j["all_aot_return_continuations"] = std::move(list);
        }
        jsonOutput.write(SerializeJsonWithIndent(j, 4 /*indent*/));
    }

    hdrOutput.Commit();
    jsonOutput.Commit();
}

static std::unique_ptr<llvm::Module> WARN_UNUSED GenerateBaselineJitHelperLogic(llvm::LLVMContext& ctx)
{
    using namespace llvm;
    std::unique_ptr<Module> module = RegisterPinningScheme::CreateModule("baseline_jit_helper_logic", ctx);

    DeegenGenerateBaselineJitCompilerCppEntryFunction(module.get());
    DeegenGenerateBaselineJitCodegenFinishFunction(module.get());

    RunLLVMOptimizePass(module.get());
    return module;
}

void FPS_GenerateDispatchTableAndBytecodeTraitTableForBaselineJit()
{
    std::unique_ptr<llvm::LLVMContext> llvmCtxHolder = std::make_unique<llvm::LLVMContext>();
    llvm::LLVMContext& ctx = *llvmCtxHolder.get();

    ReleaseAssert(cl_inputListFilenames != "");
    std::vector<std::string> hdrFileNameList = ParseCommaSeparatedFileList(cl_inputListFilenames);

    ReleaseAssert(cl_cppOutputFilename != "");
    TransactionalOutputFile cppOutput(cl_cppOutputFilename);
    FILE* fp = cppOutput.fp();

    BytecodeOpcodeRawValueMap byOpMap = BytecodeOpcodeRawValueMap::ParseFromCommandLineArgs();
    DeegenGlobalBytecodeTraitAccessor bcTraitAccessor = DeegenGlobalBytecodeTraitAccessor::ParseFromCommandLineArgs();

    fprintf(fp, "#define DEEGEN_POST_FUTAMURA_PROJECTION\n");
    fprintf(fp, "#include \"drt/constexpr_array_builder_helper.h\"\n\n");

    for (auto& hdrFile : hdrFileNameList)
    {
        std::string filename = FPS_GetFileNameFromAbsolutePath(hdrFile);
        fprintf(fp, "#include \"generated/%s\"\n", filename.c_str());
    }

    fprintf(fp, "\n\n");

    size_t numEntries = byOpMap.GetDispatchTableLength();
    fprintf(fp, "using DispatchTableEntryT = void*;\n");

    fprintf(fp, "static constexpr auto deegen_cg_dt_contents = constexpr_multipart_array_builder_helper<DispatchTableEntryT, %llu\n",
            static_cast<unsigned long long>(numEntries));

    for (size_t i = 0; i < numEntries; i++)
    {
        std::string opName = byOpMap.GetBytecode(i);
        if (!byOpMap.IsFusedIcVariant(opName))
        {
            fprintf(fp, ", populate_baseline_jit_dispatch_table_%s\n", opName.c_str());
        }
    }
    fprintf(fp, ">::get();\n\n");

    fprintf(fp, "extern \"C\" void deegen_baseline_jit_codegen_finish();\n");

    // Note that the codegen dispatch table needs to have one extra "stopper function" entry in the end!
    //
    fprintf(fp, "extern \"C\" const DispatchTableEntryT __deegen_baseline_jit_codegen_dispatch_table[%llu];\n",
            static_cast<unsigned long long>(numEntries + 1));

    fprintf(fp, "constexpr DispatchTableEntryT __deegen_baseline_jit_codegen_dispatch_table[%llu] = {\n",
            static_cast<unsigned long long>(numEntries + 1));

    for (size_t i = 0; i < numEntries; i++)
    {
        fprintf(fp, "deegen_cg_dt_contents[%llu],\n", static_cast<unsigned long long>(i));
    }

    fprintf(fp, "FOLD_CONSTEXPR(reinterpret_cast<void*>(&deegen_baseline_jit_codegen_finish))\n};\n\n");

    fprintf(fp, "static constexpr auto deegen_by_trait_table_contents = constexpr_multipart_array_builder_helper<BytecodeBaselineJitTraits, %llu\n",
            static_cast<unsigned long long>(numEntries));

    for (size_t i = 0; i < numEntries; i++)
    {
        std::string opName = byOpMap.GetBytecode(i);
        if (!byOpMap.IsFusedIcVariant(opName))
        {
            fprintf(fp, ", populate_baseline_jit_bytecode_traits_%s\n", opName.c_str());
        }
    }
    fprintf(fp, ">::get();\n\n");

    fprintf(fp, "extern \"C\" const BytecodeBaselineJitTraits deegen_baseline_jit_bytecode_trait_table[%llu];\n",
            static_cast<unsigned long long>(numEntries));

    fprintf(fp, "constexpr BytecodeBaselineJitTraits deegen_baseline_jit_bytecode_trait_table[%llu] = {\n",
            static_cast<unsigned long long>(numEntries));

    for (size_t i = 0; i < numEntries; i++)
    {
        fprintf(fp, "deegen_by_trait_table_contents[%llu]", static_cast<unsigned long long>(i));
        if (i + 1 < numEntries)
        {
            fprintf(fp, ",");
        }
        fprintf(fp, "\n");
    }
    fprintf(fp, "};\n\n");

    // Generate the JIT Call IC trait table
    //
    {
        size_t callIcTraitTableLen = bcTraitAccessor.GetJitCallIcTraitTableLength();

        fprintf(fp, "constexpr const JitCallInlineCacheTraits* deegen_jit_call_inline_cache_trait_table[%llu] = {\n",
                static_cast<unsigned long long>(callIcTraitTableLen));

        for (size_t i = 0; i < callIcTraitTableLen; i++)
        {
            fprintf(fp, "    &x_deegen_jit_call_ic_trait_ord_%llu", static_cast<unsigned long long>(i));
            if (i + 1 < callIcTraitTableLen) { fprintf(fp, ","); }
            fprintf(fp, "\n");
        }
        fprintf(fp, "};\n\n");
    }

    // Generate the JIT generic IC allocation length stepping table
    //
    {
        size_t genericIcTraitTableLen = bcTraitAccessor.GetJitGenericIcEffectTraitTableLength();
        fprintf(fp, "static constexpr auto deegen_generic_ic_alloc_stepping_trait_table_contents = constexpr_multipart_array_builder_helper<uint8_t, %llu\n",
                static_cast<unsigned long long>(genericIcTraitTableLen));

        for (size_t i = 0; i < numEntries; i++)
        {
            std::string opName = byOpMap.GetBytecode(i);
            if (!byOpMap.IsFusedIcVariant(opName))
            {
                fprintf(fp, ", populate_baseline_jit_generic_ic_allocation_length_stepping_table_%s\n", opName.c_str());
            }
        }
        fprintf(fp, ">::get();\n\n");

        fprintf(fp, "extern \"C\" const uint8_t deegen_baseline_jit_generic_ic_jit_allocation_stepping_table[%llu];\n",
                static_cast<unsigned long long>(genericIcTraitTableLen));

        fprintf(fp, "constexpr uint8_t deegen_baseline_jit_generic_ic_jit_allocation_stepping_table[%llu] = {\n",
                static_cast<unsigned long long>(genericIcTraitTableLen));

        for (size_t i = 0; i < genericIcTraitTableLen; i++)
        {
            fprintf(fp, "deegen_generic_ic_alloc_stepping_trait_table_contents[%llu]", static_cast<unsigned long long>(i));
            if (i + 1 < genericIcTraitTableLen)
            {
                fprintf(fp, ",");
            }
            fprintf(fp, "\n");
        }
        fprintf(fp, "};\n\n");
    }

    std::unique_ptr<llvm::Module> helperLogicModule = GenerateBaselineJitHelperLogic(ctx);
    std::string asmFileContents = CompileLLVMModuleToAssemblyFile(helperLogicModule.get(), llvm::Reloc::Static, llvm::CodeModel::Small);

    ReleaseAssert(cl_assemblyOutputFilename != "");
    TransactionalOutputFile asmOutputFile(cl_assemblyOutputFilename);
    asmOutputFile.write(asmFileContents);

    cppOutput.Commit();
    asmOutputFile.Commit();
}

void FPS_GenerateBytecodeOpcodeTraitTable()
{
    ReleaseAssert(cl_inputListFilenames != "");
    std::vector<std::string> jsonFileNameList = ParseCommaSeparatedFileList(cl_inputListFilenames);

    BytecodeOpcodeRawValueMap byOpMap = BytecodeOpcodeRawValueMap::ParseFromCommandLineArgs();

    std::vector<json_t> allJsonInputs;
    for (auto& fileName : jsonFileNameList)
    {
        allJsonInputs.push_back(ParseJsonFromFileName(fileName));
    }

    DeegenGlobalBytecodeTraitAccessor gbta = DeegenGlobalBytecodeTraitAccessor::Build(byOpMap, allJsonInputs);

    ReleaseAssert(cl_jsonOutputFilename != "");
    TransactionalOutputFile outFile(cl_jsonOutputFilename);
    outFile.write(SerializeJsonWithIndent(gbta.SaveToJson(), 4 /*indent*/));
    outFile.Commit();
}
