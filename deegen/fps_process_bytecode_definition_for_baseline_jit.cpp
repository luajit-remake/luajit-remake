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

using namespace dast;

constexpr const char* x_baselineJitSlowPathSectionName = "deegen_baseline_jit_slow_path_section";
constexpr const char* x_baselineJitCodegenFnSectionName = "deegen_baseline_jit_codegen_fn_section";

void FPS_ProcessBytecodeDefinitionForBaselineJit()
{
    using namespace llvm;
    using json_t = nlohmann::json;  // unfortunately the name 'json' collides with LLVM's json...
    std::unique_ptr<LLVMContext> llvmCtxHolder = std::make_unique<llvm::LLVMContext>();
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::string inputFileName = cl_jsonInputFilename;
    ReleaseAssert(inputFileName != "");

    json_t inputJson = json_t::parse(ReadFileContentAsString(cl_jsonInputFilename));

    BytecodeOpcodeRawValueMap byOpMap = BytecodeOpcodeRawValueMap::ParseFromCommandLineArgs();

    ReleaseAssert(inputJson.count("all-bytecode-info"));
    json_t& bytecodeInfoListJson = inputJson["all-bytecode-info"];
    ReleaseAssert(bytecodeInfoListJson.is_array());

    std::vector<std::pair<std::string, BytecodeBaselineJitTraits>> dispatchTableEntriesToPopulate;
    dispatchTableEntriesToPopulate.resize(byOpMap.GetDispatchTableLength());

    std::vector<std::unique_ptr<Module>> moduleToLink;

    ReleaseAssert(cl_headerOutputFilename != "");
    TransactionalOutputFile hdrOutput(cl_headerOutputFilename);
    FILE* hdrFp = hdrOutput.fp();
    FPS_EmitHeaderFileCommonHeader(hdrFp);
    fprintf(hdrFp, "#include \"drt/baseline_jit_codegen_helper.h\"\n\n");

    // The list of functions that are safe to merge if identical. Currently this only means return continuations.
    //
    // TODO: we can also merge identical bytecode main functions, but that would require a bit more work since:
    // (1) We need to make sure that the InterpreterDispatchTableBuilder is aware of this and builds the correct dispatch table.
    // (2) We want to handle the case where two functions are identical except that they store different return continuation
    //     functions (which is a common pattern), in which case we likely still want to merge the function, but we will need to
    //     do some transform so that the merged function picks up the correct return continuation based on the opcode.
    // This would require some work, so we leave it to the future.
    //
    std::vector<std::string> listOfFunctionsOkToMerge;

    // We need to make sure that all the external symbols used by the JIT logic are local to the linkage unit
    // (e.g., if the symbol is from a dynamic library, we must use its PLT address which resides in the first 2GB address range,
    // not the real address in the dynamic library) to satisfy our small CodeModel assumption.
    //
    // However, it turns out that LLVM optimizer can sometimes introduce new symbols that does not have dso_local set
    // (for example, it may rewrite 'fprintf' of a literal string to 'fwrite', and the 'fwrite' declaration is not dso_local).
    //
    // And it seems like if two LLVM modules are linked together using LLVM linker, a declaration would become non-dso_local
    // if *either* module's declaration is not dso_local.
    //
    // So we record all the symbols needed to be made dso_local here, and after all LLVM linker work finishes, we scan the
    // final module and change all those symbols dso_local.
    //
    std::unordered_set<std::string> fnNamesNeedToBeMadeDsoLocal;

    // For assertion purpose only
    //
    std::vector<std::string> bytecodeOriginalImplFunctionNames;

    for (size_t bytecodeDefOrd = 0; bytecodeDefOrd < bytecodeInfoListJson.size(); bytecodeDefOrd++)
    {
        json_t& curBytecodeInfoJson = bytecodeInfoListJson[bytecodeDefOrd];

        BytecodeIrInfo bii(ctx, curBytecodeInfoJson);
        bii.m_bytecodeDef->ComputeBaselineJitSlowPathDataLayout();

        DeegenProcessBytecodeForBaselineJitResult res = DeegenProcessBytecodeForBaselineJitResult::Create(&bii, byOpMap);

        bytecodeOriginalImplFunctionNames.push_back(bii.m_bytecodeDef->m_implFunctionName);

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
            ReleaseAssert(res.m_baselineJitInfo.m_dataSectionAlignment <= x_baselineJitMaxPossibleDataSectionAlignment);
            ReleaseAssert(is_power_of_2(res.m_baselineJitInfo.m_dataSectionAlignment));
            fprintf(hdrFp, "    .m_dataSectionAlignment = %llu,\n", static_cast<unsigned long long>(res.m_baselineJitInfo.m_dataSectionAlignment));
            ReleaseAssert(res.m_baselineJitInfo.m_numCondBrLatePatches <= 65535);
            fprintf(hdrFp, "    .m_numCondBrLatePatches = %llu,\n", static_cast<unsigned long long>(res.m_baselineJitInfo.m_numCondBrLatePatches));
            ReleaseAssert(res.m_baselineJitInfo.m_slowPathDataLen <= 65535);
            fprintf(hdrFp, "    .m_slowPathDataLen = %llu,\n", static_cast<unsigned long long>(res.m_baselineJitInfo.m_slowPathDataLen));
            ReleaseAssert(bytecodeStructLength <= 65535);
            fprintf(hdrFp, "    .m_bytecodeLength = %llu\n", static_cast<unsigned long long>(bytecodeStructLength));
            fprintf(hdrFp, "};\n");

            for (size_t k = start; k < end; k++)
            {
                fprintf(hdrFp, "p->set(%llu, x_traitValue);\n", static_cast<unsigned long long>(k));
            }
            fprintf(hdrFp, "}\n};\n}\n\n");
        }

        // Push generated modules to the list of modules to be linked, and also set section
        //
        for (auto& m : res.m_aotSlowPaths)
        {
            Function* fn = m.m_module->getFunction(m.m_funcName);
            ReleaseAssert(fn != nullptr);
            fn->setSection(x_baselineJitSlowPathSectionName);
            moduleToLink.push_back(std::move(m.m_module));
        }

        for (auto& m : res.m_aotSlowPathReturnConts)
        {
            Function* fn = m.m_module->getFunction(m.m_funcName);
            ReleaseAssert(fn != nullptr);
            fn->setSection(x_baselineJitSlowPathSectionName);
            // Return continuations are also mergeable if identical
            //
            listOfFunctionsOkToMerge.push_back(m.m_funcName);
            moduleToLink.push_back(std::move(m.m_module));
        }

        // Compile the main codegen function module to ASM for audit purpose
        //
        std::string cgFnAsmForAudit = CompileLLVMModuleToAssemblyFile(res.m_baselineJitInfo.m_cgMod.get(), Reloc::Static, CodeModel::Small);

        // Set up section name for main codegen function
        //
        {
            Function* fn = res.m_baselineJitInfo.m_cgMod->getFunction(res.m_baselineJitInfo.m_resultFuncName);
            ReleaseAssert(fn != nullptr);
            fn->setSection(x_baselineJitCodegenFnSectionName);
        }

        for (Function& fn : *res.m_baselineJitInfo.m_cgMod.get())
        {
            fnNamesNeedToBeMadeDsoLocal.insert(fn.getName().str());
        }

        moduleToLink.push_back(std::move(res.m_baselineJitInfo.m_cgMod));

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

    // Parse the origin module and each additional module generated from the interpreter lowering stage
    //
    auto getModuleFromBase64EncodedString = [&](const std::string& str)
    {
        return ParseLLVMModuleFromString(ctx, "bytecode_ir_component_module" /*moduleName*/, base64_decode(str));
    };

    std::unique_ptr<Module> module = getModuleFromBase64EncodedString(JSONCheckedGet<std::string>(inputJson, "reference_module"));

    ReleaseAssert(inputJson.count("bytecode_module_list"));
    json_t& listOfInterpreterLoweringModules = inputJson["bytecode_module_list"];
    ReleaseAssert(listOfInterpreterLoweringModules.is_array());
    for (size_t i = 0; i < listOfInterpreterLoweringModules.size(); i++)
    {
        json_t& val = listOfInterpreterLoweringModules[i];
        ReleaseAssert(val.is_string());
        std::unique_ptr<Module> m = getModuleFromBase64EncodedString(val.get<std::string>());
        moduleToLink.push_back(std::move(m));
    }

    // Remove the 'used' attribute of the bytecode definition globals, this should make all the implementation functions dead
    //
    BytecodeVariantDefinition::RemoveUsedAttributeOfBytecodeDefinitionGlobalSymbol(module.get());

    // Link in all the post-processed bytecode function modules
    //
    {
        DeegenPostProcessModuleLinker modLinker(std::move(module));
        for (size_t i = 0; i < moduleToLink.size(); i++)
        {
            modLinker.AddModule(std::move(moduleToLink[i]));
        }
        modLinker.AddWhitelistedNewlyIntroducedGlobalVar("__deegen_interpreter_dispatch_table");
        modLinker.AddWhitelistedNewlyIntroducedGlobalVar("__deegen_baseline_jit_codegen_dispatch_table");
        module = modLinker.DoLinking();
    }

    for (const std::string& fnName : fnNamesNeedToBeMadeDsoLocal)
    {
        Function* fn = module->getFunction(fnName);
        ReleaseAssert(fn != nullptr);
        fn->setDSOLocal(true);
    }

    // Add the list of return continuations from interpreter lowering to mergeable functions list
    //
    ReleaseAssert(inputJson.count("return-cont-name-list"));
    json_t& listOfReturnContFnNamesFromInterpreterLowering = inputJson["return-cont-name-list"];
    ReleaseAssert(listOfReturnContFnNamesFromInterpreterLowering.is_array());
    for (size_t i = 0; i < listOfReturnContFnNamesFromInterpreterLowering.size(); i++)
    {
        json_t& val = listOfReturnContFnNamesFromInterpreterLowering[i];
        ReleaseAssert(val.is_string());
        listOfFunctionsOkToMerge.push_back(val.get<std::string>());
    }

    // Try to merge identical return continuations
    //
    {
        LLVMIdenticalFunctionMerger ifm;
        ifm.SetSectionPriority(InterpreterBytecodeImplCreator::x_cold_code_section_name, 100);
        ifm.SetSectionPriority(x_baselineJitSlowPathSectionName, 110);
        ifm.SetSectionPriority("", 200);
        ifm.SetSectionPriority(InterpreterBytecodeImplCreator::x_hot_code_section_name, 300);
        for (const std::string& fnName : listOfFunctionsOkToMerge)
        {
            Function* fn = module->getFunction(fnName);
            ReleaseAssert(fn != nullptr);
            ifm.AddFunction(fn);
        }
        ifm.DoMerge();
    }

    // Assert that the bytecode definition symbols, and all the implementation functions are gone
    //
    BytecodeVariantDefinition::AssertBytecodeDefinitionGlobalSymbolHasBeenRemoved(module.get());
    for (auto& fnName : bytecodeOriginalImplFunctionNames)
    {
        ReleaseAssert(module->getNamedValue(fnName) == nullptr);
    }

    ReleaseAssert(cl_assemblyOutputFilename != "");
    TransactionalOutputFile asmOutFile(cl_assemblyOutputFilename);

    {
        std::string contents = CompileLLVMModuleToAssemblyFile(module.get(), llvm::Reloc::Static, llvm::CodeModel::Small);
        asmOutFile.write(contents);
    }

    hdrOutput.Commit();
    asmOutFile.Commit();
}

static std::unique_ptr<llvm::Module> WARN_UNUSED GenerateBaselineJitHelperLogic(llvm::LLVMContext& ctx)
{
    using namespace llvm;
    std::unique_ptr<Module> module = std::make_unique<Module>("baseline_jit_helper_logic", ctx);

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

    std::unique_ptr<llvm::Module> helperLogicModule = GenerateBaselineJitHelperLogic(ctx);
    std::string asmFileContents = CompileLLVMModuleToAssemblyFile(helperLogicModule.get(), llvm::Reloc::Static, llvm::CodeModel::Small);

    ReleaseAssert(cl_assemblyOutputFilename != "");
    TransactionalOutputFile asmOutputFile(cl_assemblyOutputFilename);
    asmOutputFile.write(asmFileContents);

    cppOutput.Commit();
    asmOutputFile.Commit();
}
