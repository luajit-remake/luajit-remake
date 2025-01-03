#include "fps_main.h"
#include "deegen_bytecode_ir_components.h"
#include "read_file.h"
#include "deegen_dfg_jit_process_call_inlining.h"
#include "deegen_jit_slow_path_data.h"
#include "json_utils.h"
#include "json_parse_dump.h"
#include "transactional_output_file.h"
#include "deegen_jit_codegen_logic_creator.h"
#include "deegen_dfg_jit_impl_creator.h"
#include "base64_util.h"
#include "deegen_dfg_aot_slowpath_save_reg_stub.h"

using namespace dast;

void FPS_GenerateDfgSpecializedBytecodeInfo()
{
    using namespace llvm;
    std::unique_ptr<LLVMContext> llvmCtxHolder = std::make_unique<llvm::LLVMContext>();
    LLVMContext& ctx = *llvmCtxHolder.get();

    ReleaseAssert(cl_jsonInputFilename != "");
    json_t inputJson = ParseJsonFromFileName(cl_jsonInputFilename);

    BytecodeOpcodeRawValueMap byOpMap = BytecodeOpcodeRawValueMap::ParseFromCommandLineArgs();
    DeegenGlobalBytecodeTraitAccessor bcTraitAccessor = DeegenGlobalBytecodeTraitAccessor::ParseFromCommandLineArgs();

    ReleaseAssert(inputJson.count("all-bytecode-info"));
    json_t& bytecodeInfoListJson = inputJson["all-bytecode-info"];
    ReleaseAssert(bytecodeInfoListJson.is_array());

    ReleaseAssert(inputJson.count("all-dfg-variant-info"));
    json_t& dfgVariantListJson = inputJson["all-dfg-variant-info"];
    ReleaseAssert(dfgVariantListJson.is_array());

    ReleaseAssert(cl_headerOutputFilename != "");
    TransactionalOutputFile hdrOutput(cl_headerOutputFilename);
    FILE* hdrFp = hdrOutput.fp();
    FPS_EmitHeaderFileCommonHeader(hdrFp);

    ReleaseAssert(cl_cppOutputFilename != "");
    TransactionalOutputFile cppOutput(cl_cppOutputFilename);
    FILE* cppFp = cppOutput.fp();
    FPS_EmitCPPFileCommonHeader(cppFp);

    ReleaseAssert(cl_jsonOutputFilename != "");
    TransactionalOutputFile jsonOutput(cl_jsonOutputFilename);

    ReleaseAssert(cl_jsonOutputFilename2 != "");
    TransactionalOutputFile jsonOutput2(cl_jsonOutputFilename2);

    fprintf(hdrFp, "#include \"drt/bytecode_builder.h\"\n");

    fprintf(hdrFp, "#include \"drt/dfg_bytecode_speculative_inlining_trait.h\"\n");
    fprintf(hdrFp, "#include \"drt/constexpr_array_builder_helper.h\"\n");
    fprintf(hdrFp, "#include \"drt/dfg_variant_traits_internal.h\"\n");
    fprintf(hdrFp, "namespace dfg {\n");

    fprintf(cppFp, "#define DEEGEN_POST_FUTAMURA_PROJECTION\n\n");
    fprintf(cppFp, "#include \"drt/dfg_variant_traits.h\"\n");

    fprintf(cppFp, "namespace dfg {\n");

    for (size_t bytecodeDefOrd = 0; bytecodeDefOrd < bytecodeInfoListJson.size(); bytecodeDefOrd++)
    {
        json_t& curBytecodeInfoJson = bytecodeInfoListJson[bytecodeDefOrd];

        BytecodeIrInfo bii(ctx, curBytecodeInfoJson);

        bii.m_bytecodeDef->ComputeBaselineJitSlowPathDataLayout();

        size_t numJitCallICs = bii.m_bytecodeDef->GetNumCallICsInBaselineJitTier();
        std::vector<std::unique_ptr<DeegenOptJitSpeculativeInliningInfo>> callIcInfo;
        std::vector<std::string> itemNames;
        bool hasEligibleIcSite = false;
        for (size_t icSiteOrd = 0; icSiteOrd < numJitCallICs; icSiteOrd++)
        {
            callIcInfo.push_back(std::make_unique<DeegenOptJitSpeculativeInliningInfo>(bii.m_jitMainComponent.get(), icSiteOrd));
            DeegenOptJitSpeculativeInliningInfo* handler = callIcInfo.back().get();
            if (handler->TryGenerateInfo())
            {
                handler->PrintCppFile(hdrFp);
                itemNames.push_back("&" + handler->GetCppTraitObjectName());
                hasEligibleIcSite = true;
            }
            else
            {
                itemNames.push_back("nullptr");
            }
        }

        if (hasEligibleIcSite)
        {
            fprintf(hdrFp, "constexpr const BytecodeSpeculativeInliningTrait* x_deegen_dfg_speculative_inlining_trait_%s[%u] = {\n",
                    bii.m_bytecodeDef->GetBytecodeIdName().c_str(), static_cast<unsigned int>(numJitCallICs));

            ReleaseAssert(itemNames.size() == numJitCallICs);
            for (size_t icSiteOrd = 0; icSiteOrd < numJitCallICs; icSiteOrd++)
            {
                fprintf(hdrFp, "    %s%s\n",
                        itemNames[icSiteOrd].c_str(),
                        (icSiteOrd + 1 == numJitCallICs ? "" : ","));
            }
            fprintf(hdrFp, "};\n\n");
        }
        else
        {
            fprintf(hdrFp, "constexpr const BytecodeSpeculativeInliningTrait* const* x_deegen_dfg_speculative_inlining_trait_%s = nullptr;\n", bii.m_bytecodeDef->GetBytecodeIdName().c_str());
        }
        fprintf(hdrFp, "template<typename T>\n");
        fprintf(hdrFp, "struct build_bytecode_speculative_inline_info_array_%s {\n", bii.m_bytecodeDef->GetBytecodeIdName().c_str());
        fprintf(hdrFp, "    static consteval void run(T* impl) {\n");
        size_t absoluteOpcodeOrd = byOpMap.GetOpcode(bii.m_bytecodeDef->GetBytecodeIdName());
        size_t numFusedIcVariants = byOpMap.GetNumInterpreterFusedIcVariants(bii.m_bytecodeDef->GetBytecodeIdName());
        ReleaseAssert(numJitCallICs <= 255);
        size_t callIcSiteOffsetInSlowPathData = 0;
        if (numJitCallICs > 0)
        {
            callIcSiteOffsetInSlowPathData = bii.m_bytecodeDef->GetBaselineJitSlowPathDataLayout()->m_callICs.GetOffsetForSite(0);
        }
        ReleaseAssert(callIcSiteOffsetInSlowPathData <= 65535);
        fprintf(hdrFp, "        impl->set(%u, BytecodeSpeculativeInliningInfo(%u, %u, x_deegen_dfg_speculative_inlining_trait_%s));\n",
                static_cast<unsigned int>(absoluteOpcodeOrd),
                static_cast<unsigned int>(numJitCallICs),
                static_cast<unsigned int>(callIcSiteOffsetInSlowPathData),
                bii.m_bytecodeDef->GetBytecodeIdName().c_str());
        for (size_t k = absoluteOpcodeOrd + 1; k <= absoluteOpcodeOrd + numFusedIcVariants; k++)
        {
            fprintf(hdrFp, "        impl->set(%u, BytecodeSpeculativeInliningInfo());\n", static_cast<unsigned int>(k));
        }
        fprintf(hdrFp, "    }\n");
        fprintf(hdrFp, "};\n\n");
    }

    json_t allCodegenModules = json_t::array();
    std::vector<std::pair<std::string /*funcName*/, std::string /*llvmModuleBase64*/>> allSlowPathModules;
    std::vector<std::pair<std::string /*funcName*/, std::string /*llvmModuleBase64*/>> allRetContModules;

    std::vector<DfgRegAllocCCallWrapperRequest> allCCallWrapperRequests;

    // Process each DFG variant
    //
    for (json_t& bcInfo : dfgVariantListJson)
    {
        ReleaseAssert(bcInfo.is_object());

        std::string bcName;
        JSONCheckedGet(bcInfo, "bytecode_name", bcName);

        ReleaseAssert(bcInfo.count("dfg_variants"));
        json_t& bcDfgVariants = bcInfo["dfg_variants"];
        ReleaseAssert(bcDfgVariants.is_array());

        size_t numDfgVariantsInThisBC = bcDfgVariants.size();
        fprintf(hdrFp, "template<> struct NumDfgVariantsForBCKind<BCKind::%s> { static constexpr size_t value = %d; };\n\n",
                bcName.c_str(), static_cast<int>(numDfgVariantsInThisBC));

        size_t curDfgVariantOrd = 0;
        for (json_t& dfgVariantJson : bcDfgVariants)
        {
            Auto(curDfgVariantOrd++);

            BytecodeIrInfo bii(ctx, dfgVariantJson);

            std::unique_ptr<DfgJitImplCreator> j(new DfgJitImplCreator(&bii, *bii.m_jitMainComponent.get(), nullptr));

            std::unique_ptr<DfgNodeRegAllocRootInfo> raInfo = j->TryGenerateAllRegAllocVariants();
            bool regAllocSuccess = (raInfo.get() != nullptr);

            auto dumpAuditLog = [&](JitCodeGenLogicCreator& cgi, std::string regAllocSuffix)
            {
                {
                    std::string cgFnAsmForAudit = CompileLLVMModuleToAssemblyFile(cgi.m_cgMod.get(), Reloc::Static, CodeModel::Small);

                    std::string cgFnAuditFileContents = cgi.m_disasmForAudit + cgFnAsmForAudit;
                    std::string cgFnAuditFileName = bii.m_bytecodeDef->GetBytecodeIdName() + regAllocSuffix + ".s";

                    std::string auditFilePath = FPS_GetAuditFilePathWithTwoPartName("dfg_jit" /*dirSuffix*/, cgFnAuditFileName);
                    TransactionalOutputFile auditFile(auditFilePath);
                    auditFile.write(cgFnAuditFileContents);
                    auditFile.Commit();
                }

                for (size_t i = 0; i < cgi.m_implModulesForAudit.size(); i++)
                {
                    Module* m = cgi.m_implModulesForAudit[i].first.get();
                    std::string fnName = cgi.m_implModulesForAudit[i].second;
                    std::string auditFileName = bii.m_bytecodeDef->GetBytecodeIdName() + regAllocSuffix;
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

                    std::string auditFilePath = FPS_GetAuditFilePathWithTwoPartName("dfg_jit_verbose" /*dirSuffix*/, auditFileName);
                    TransactionalOutputFile auditFile(auditFilePath);
                    auditFile.write(auditFileContents);
                    auditFile.Commit();
                }

                for (auto& extraAuditFile : cgi.m_extraAuditFiles)
                {
                    std::string auditFilePath = FPS_GetAuditFilePathWithTwoPartName("dfg_jit" /*dirSuffix*/, extraAuditFile.first);
                    TransactionalOutputFile auditFile(auditFilePath);
                    auditFile.write(extraAuditFile.second);
                    auditFile.Commit();
                }

                for (auto& verboseAuditFile : cgi.m_extraVerboseAuditFiles)
                {
                    std::string auditFilePath = FPS_GetAuditFilePathWithTwoPartName("dfg_jit_verbose" /*dirSuffix*/, verboseAuditFile.first);
                    TransactionalOutputFile auditFile(auditFilePath);
                    auditFile.write(verboseAuditFile.second);
                    auditFile.Commit();
                }
            };

            {
                std::string regAllocLogFileName = bii.m_bytecodeDef->GetBytecodeIdName() + "_reg_alloc.log";
                std::string auditFilePath = FPS_GetAuditFilePathWithTwoPartName("dfg_jit" /*dirSuffix*/, regAllocLogFileName);
                TransactionalOutputFile auditFile(auditFilePath);
                auditFile.write(j->GetRegAllocInfoAuditLog().c_str());
                auditFile.Commit();
            }

            auto printGenericIcTraitInfo = [&](std::vector<AstInlineCache::JitFinalLoweringResult::TraitDesc> allTraits, size_t numTraits)
            {
                ReleaseAssert(numTraits == allTraits.size());
                std::vector<size_t> icSizes;
                icSizes.resize(numTraits, static_cast<size_t>(-1));
                for (auto& trait : allTraits)
                {
                    ReleaseAssert(trait.m_ordInTraitTable < icSizes.size());
                    ReleaseAssert(icSizes[trait.m_ordInTraitTable] == static_cast<size_t>(-1));
                    ReleaseAssert(trait.m_allocationLength != static_cast<size_t>(-1));
                    icSizes[trait.m_ordInTraitTable] = trait.m_allocationLength;
                }
                for (size_t val : icSizes) { ReleaseAssert(val != static_cast<size_t>(-1)); }

                fprintf(hdrFp, "    static constexpr size_t numGenericIcCases = %d;\n", static_cast<int>(numTraits));
                fprintf(hdrFp, "    static constexpr std::array<uint8_t, numGenericIcCases> genericIcStubAllocSteppings = { ");
                for (size_t idx = 0; idx < icSizes.size(); idx++)
                {
                    size_t size = icSizes[idx];
                    ReleaseAssert(size <= x_jit_mem_alloc_stepping_array[x_jit_mem_alloc_total_steppings - 1]);
                    size_t icAllocationLengthStepping = GetJitMemoryAllocatorSteppingFromSmallAllocationSize(size);
                    ReleaseAssert(icAllocationLengthStepping < x_jit_mem_alloc_total_steppings);
                    ReleaseAssert(x_jit_mem_alloc_stepping_array[icAllocationLengthStepping] >= size);

                    ReleaseAssert(icAllocationLengthStepping <= 255);
                    if (idx > 0) { fprintf(hdrFp, ", "); }
                    fprintf(hdrFp, "%d", static_cast<int>(icAllocationLengthStepping));
                }
                fprintf(hdrFp, "};\n");
            };

            auto printJitCodeSizeInfo = [&](JitCodeGenLogicCreator& cgi)
            {
                ReleaseAssert(cgi.m_fastPathCodeLen <= 65535);
                ReleaseAssert(cgi.m_slowPathCodeLen <= 65535);
                ReleaseAssert(cgi.m_dataSectionCodeLen <= 65535);
                ReleaseAssert(cgi.m_dataSectionAlignment <= x_jitMaxPossibleDataSectionAlignment && is_power_of_2(cgi.m_dataSectionAlignment));
                fprintf(hdrFp, "    static constexpr uint16_t fastPathLen = %d;\n", static_cast<int>(cgi.m_fastPathCodeLen));
                fprintf(hdrFp, "    static constexpr uint16_t slowPathLen = %d;\n", static_cast<int>(cgi.m_slowPathCodeLen));
                fprintf(hdrFp, "    static constexpr uint16_t dataSecLen = %d;\n", static_cast<int>(cgi.m_dataSectionCodeLen));
                fprintf(hdrFp, "    static constexpr uint16_t dataSecAlignment = %d;\n", static_cast<int>(cgi.m_dataSectionAlignment));
            };

            // The SlowPathDataLayout used by this DFG variant
            // This is created by the first DfgJitImplCreator, and the same point must be passed to each later DfgJitImplCreator
            //
            std::unique_ptr<DfgJitSlowPathDataLayout> slowPathDataLayout;

            if (regAllocSuccess)
            {
                bool bcHasRangeOperand = false;
                for (auto& op : bii.m_bytecodeDef->m_list)
                {
                    if (op->GetKind() == BcOperandKind::BytecodeRangeBase)
                    {
                        bcHasRangeOperand = true;
                    }
                }

                struct ImplCreatorInfo
                {
                    std::unique_ptr<DfgJitImplCreator> m_jitImplCreator;
                    std::unique_ptr<JitCodeGenLogicCreator> m_cgLogicCreator;
                };

                std::unordered_map<DfgNodeRegAllocVariant*, std::vector<std::unique_ptr<DfgNodeRegAllocSubVariant>>> subVariantMap;
                std::unordered_map<DfgNodeRegAllocSubVariant*, ImplCreatorInfo> svImplInfo;

                // Do lowering for the JIT component in each reg alloc variant
                //
                for (std::unique_ptr<DfgNodeRegAllocVariant>& variant : raInfo->m_variants)
                {
                    ReleaseAssert(!subVariantMap.count(variant.get()));
                    subVariantMap[variant.get()] = variant->GenerateSubVariants();
                    std::vector<std::unique_ptr<DfgNodeRegAllocSubVariant>>& r = subVariantMap[variant.get()];
                    for (std::unique_ptr<DfgNodeRegAllocSubVariant>& sv : r)
                    {
                        if (sv.get() != nullptr)
                        {
                            std::unique_ptr<DfgJitImplCreator> jic = j->GetImplCreatorForRegAllocSubVariant(sv.get());
                            if (slowPathDataLayout.get() != nullptr)
                            {
                                jic->SetDfgJitSlowPathDataLayout(slowPathDataLayout.get());
                            }
                            else
                            {
                                slowPathDataLayout = jic->ComputeDfgJitSlowPathDataLayout();
                                ReleaseAssert(slowPathDataLayout.get() != nullptr);
                            }

                            std::unique_ptr<JitCodeGenLogicCreator> cgi(new JitCodeGenLogicCreator());
                            cgi->SetBII(&bii);
                            cgi->DoAllLowering(jic.get());

                            ReleaseAssert(!svImplInfo.count(sv.get()));
                            svImplInfo[sv.get()] = {
                                .m_jitImplCreator = std::move(jic),
                                .m_cgLogicCreator = std::move(cgi)
                            };
                        }
                    }
                }

                // After lowering all JIT components, we can finalize the SlowPathDataLayout
                //
                ReleaseAssert(slowPathDataLayout.get() != nullptr);
                slowPathDataLayout->FinalizeLayout();

                // Generate the code generator logic for each reg alloc variant
                //
                size_t numSV = 0;
                size_t subVariantOrd = 0;
                std::vector<std::string> variantCppIdents;
                ReleaseAssert(raInfo->m_variants.size() > 0);
                for (std::unique_ptr<DfgNodeRegAllocVariant>& variant : raInfo->m_variants)
                {
                    Auto(subVariantOrd++);

                    ReleaseAssert(subVariantMap.count(variant.get()));
                    std::vector<std::unique_ptr<DfgNodeRegAllocSubVariant>>& r = subVariantMap[variant.get()];

                    size_t firstSVOrd = numSV;
                    for (std::unique_ptr<DfgNodeRegAllocSubVariant>& sv : r)
                    {
                        if (sv.get() != nullptr)
                        {
                            std::string fnSuffix = "_" + std::to_string(numSV);

                            ReleaseAssert(svImplInfo.count(sv.get()));
                            DfgJitImplCreator* jic = svImplInfo[sv.get()].m_jitImplCreator.get();
                            JitCodeGenLogicCreator* cgi = svImplInfo[sv.get()].m_cgLogicCreator.get();

                            cgi->GenerateLogic(jic, fnSuffix);

                            allCodegenModules.push_back(base64_encode(DumpLLVMModuleAsString(cgi->m_cgMod.get())));

                            allCCallWrapperRequests.insert(allCCallWrapperRequests.end(),
                                                           cgi->m_dfgCWrapperRequests.begin(),
                                                           cgi->m_dfgCWrapperRequests.end());

                            dumpAuditLog(*cgi, fnSuffix);

                            fprintf(hdrFp, "extern \"C\" void %s(PrimaryCodegenState*, NodeRegAllocInfo*, uint8_t*, RegAllocStateForCodeGen*);\n\n",
                                    cgi->m_resultFuncName.c_str());

                            fprintf(hdrFp, "template<> struct DfgCodegenFuncInfoFor<BCKind::%s, %d, %d> {\n",
                                    bcName.c_str(), static_cast<int>(curDfgVariantOrd), static_cast<int>(numSV));

                            // TODO implement
                            fprintf(hdrFp, "    static constexpr DfgVariantValidityCheckerFn checkValidFn = nullptr;\n");
                            fprintf(hdrFp, "    static constexpr CodegenImplFn codegenFn = &%s;\n", cgi->m_resultFuncName.c_str());
                            printGenericIcTraitInfo(cgi->m_allGenericIcTraitDescs, jic->GetTotalNumGenericIcCases());
                            printJitCodeSizeInfo(*cgi);
                            fprintf(hdrFp, "};\n\n");

                            numSV++;
                        }
                    }

                    // Each subvariant should have at least one valid codegen func created
                    //
                    ReleaseAssert(numSV > firstSVOrd);

                    // Print the C++ definition for this reg alloc variant
                    //
                    std::string cppNameIdent = bii.m_bytecodeDef->GetBytecodeIdName() + "_" + std::to_string(subVariantOrd);
                    std::string baseOrdExpr = "x_codegenFuncOrdBaseForDfgVariant<BCKind::" + bcName + ", " +
                        std::to_string(curDfgVariantOrd) + "> + " + std::to_string(firstSVOrd);

                    variantCppIdents.push_back(cppNameIdent);
                    variant->PrintCppDefinitions(r, hdrFp, cppFp, cppNameIdent, baseOrdExpr, bcHasRangeOperand);
                }
                ReleaseAssert(numSV > 0);
                ReleaseAssert(numSV == svImplInfo.size());

                // Print the C++ definition for this DFG variant
                //
                {
                    std::string cppNameIdent = bii.m_bytecodeDef->GetBytecodeIdName();
                    std::string baseOrdExpr = "x_codegenFuncOrdBaseForDfgVariant<BCKind::" + bcName + ", " + std::to_string(curDfgVariantOrd) + ">";
                    raInfo->PrintCppDefinitions(hdrFp,
                                                cppFp,
                                                cppNameIdent,
                                                variantCppIdents,
                                                baseOrdExpr,
                                                numSV);
                }

                fprintf(hdrFp, "template<> struct DfgVariantTraitFor<BCKind::%s, %d> {\n",
                        bcName.c_str(), static_cast<int>(curDfgVariantOrd));
                fprintf(hdrFp, "    static constexpr size_t numCodegenFuncs = %d;\n", static_cast<int>(numSV));
                fprintf(hdrFp, "    static constexpr const DfgVariantTraits* trait = &x_deegen_dfg_variant_%s;\n",
                        bii.m_bytecodeDef->GetBytecodeIdName().c_str());
                fprintf(hdrFp, "};\n");
            }
            else
            {
                slowPathDataLayout = j->ComputeDfgJitSlowPathDataLayout();

                JitCodeGenLogicCreator cgi;
                cgi.SetBII(&bii);
                cgi.DoAllLowering(j.get());

                slowPathDataLayout->FinalizeLayout();

                cgi.GenerateLogic(j.get());

                allCodegenModules.push_back(base64_encode(DumpLLVMModuleAsString(cgi.m_cgMod.get())));

                ReleaseAssert(cgi.m_dfgCWrapperRequests.size() == 0);

                dumpAuditLog(cgi, "" /*fnSuffix*/);

                fprintf(hdrFp, "extern \"C\" void %s(PrimaryCodegenState*, NodeRegAllocInfo*, uint8_t*, RegAllocStateForCodeGen*);\n\n",
                        cgi.m_resultFuncName.c_str());

                fprintf(hdrFp, "template<> struct DfgCodegenFuncInfoFor<BCKind::%s, %d, 0> {\n",
                        bcName.c_str(), static_cast<int>(curDfgVariantOrd));

                // TODO implement
                fprintf(hdrFp, "    static constexpr DfgVariantValidityCheckerFn checkValidFn = nullptr;\n");
                fprintf(hdrFp, "    static constexpr CodegenImplFn codegenFn = &%s;\n", cgi.m_resultFuncName.c_str());
                printGenericIcTraitInfo(cgi.m_allGenericIcTraitDescs, j->GetTotalNumGenericIcCases());
                printJitCodeSizeInfo(cgi);
                fprintf(hdrFp, "};\n\n");

                fprintf(hdrFp, "extern const DfgVariantTraits x_deegen_dfg_variant_%s;\n", bii.m_bytecodeDef->GetBytecodeIdName().c_str());

                fprintf(hdrFp, "template<> struct DfgVariantTraitFor<BCKind::%s, %d> {\n",
                        bcName.c_str(), static_cast<int>(curDfgVariantOrd));
                fprintf(hdrFp, "    static constexpr size_t numCodegenFuncs = 1;\n");
                fprintf(hdrFp, "    static constexpr const DfgVariantTraits* trait = &x_deegen_dfg_variant_%s;\n",
                        bii.m_bytecodeDef->GetBytecodeIdName().c_str());
                fprintf(hdrFp, "};\n");

                fprintf(cppFp, "constexpr DfgVariantTraits x_deegen_dfg_variant_%s(\n", bii.m_bytecodeDef->GetBytecodeIdName().c_str());
                fprintf(cppFp, "    x_codegenFuncOrdBaseForDfgVariant<BCKind::%s, %d>);\n\n", bcName.c_str(), static_cast<int>(curDfgVariantOrd));
            }

            auto processAOTComponent = [&](BytecodeIrComponent& component) -> std::pair<std::string /*fnName*/, std::string /*llvmModuleBase64*/>
            {
                auto getJIC = [&]()
                {
                    if (component.m_processKind == BytecodeIrComponentKind::ReturnContinuation)
                    {
                        return DfgJitImplCreator(BaselineJitImplCreator::SlowPathReturnContinuationTag(), &bii, component, nullptr);
                    }
                    else
                    {
                        return DfgJitImplCreator(&bii, component, nullptr);
                    }
                };

                DfgJitImplCreator jic = getJIC();
                jic.SetIsFastPathRegAllocAlwaysDisabled(!regAllocSuccess);
                jic.DisableRegAlloc(DfgJitImplCreator::RegAllocDisableReason::NotFastPath);
                ReleaseAssert(slowPathDataLayout.get() != nullptr);
                jic.SetDfgJitSlowPathDataLayout(slowPathDataLayout.get());
                jic.DoLowering(false /*forRegisterDemandTest*/);

                Module* m = jic.GetModule();

                std::string prefixToFind = "__deegen_bytecode_";
                std::string prefixToReplace = "__deegen_dfg_jit_op_";
                RenameAllFunctionsStartingWithPrefix(m, prefixToFind, prefixToReplace);

                std::string resultFnName = jic.GetResultFunctionName();
                ReleaseAssert(resultFnName.starts_with(prefixToFind));
                resultFnName = prefixToReplace + resultFnName.substr(prefixToFind.length());

                Function* resultFunction = m->getFunction(resultFnName);
                ReleaseAssert(resultFunction != nullptr);
                ReleaseAssert(!resultFunction->empty());
                resultFunction->setSection(DfgJitImplCreator::x_slowPathSectionName);

                return std::make_pair(resultFnName, base64_encode(DumpLLVMModuleAsString(m)));
            };

            auto generateSaveRegStubForSlowPath = [&](BytecodeIrComponent& component) -> std::pair<std::string /*fnName*/, std::string /*llvmModuleBase64*/>
            {
                std::string resultFuncName = DfgAotSlowPathSaveRegStubCreator::GetResultFunctionName(component);
                std::unique_ptr<Module> m = DfgAotSlowPathSaveRegStubCreator::Create(component);

                Function* resultFunction = m->getFunction(resultFuncName);
                ReleaseAssert(resultFunction != nullptr);
                ReleaseAssert(!resultFunction->empty());
                resultFunction->setSection(DfgJitImplCreator::x_slowPathSectionName);

                return std::make_pair(resultFuncName, base64_encode(DumpLLVMModuleAsString(m.get())));
            };

            // Generate all the AOT components (slow path and return continuation)
            //
            // Note that if reg alloc is enabled, we unconditionally generate all the SaveRegStub functions for simplicity.
            // If they end up not needed they will be dropped by the linker anyway.
            //
            for (auto& slowPathComponent : bii.m_slowPaths)
            {
                allSlowPathModules.push_back(processAOTComponent(*slowPathComponent.get()));
                if (regAllocSuccess)
                {
                    allSlowPathModules.push_back(generateSaveRegStubForSlowPath(*slowPathComponent.get()));
                }
            }
            if (bii.m_quickeningSlowPath.get() != nullptr)
            {
                allSlowPathModules.push_back(processAOTComponent(*bii.m_quickeningSlowPath.get()));
                if (regAllocSuccess)
                {
                    allSlowPathModules.push_back(generateSaveRegStubForSlowPath(*bii.m_quickeningSlowPath.get()));
                }
            }

            // We simply assume each return continuation could potentially be used by slow path,
            // because linker should be smart enough to drop unused functions
            //
            for (auto& retContComponent : bii.m_allRetConts)
            {
                allRetContModules.push_back(processAOTComponent(*retContComponent.get()));
            }
        }
        ReleaseAssert(curDfgVariantOrd == bcDfgVariants.size());
    }

    fprintf(hdrFp, "} // namespace dfg\n");
    fprintf(cppFp, "} // namespace dfg\n");

    {
        auto convertToJson = [&](std::vector<std::pair<std::string, std::string>>& data) -> json_t
        {
            json_t res = json_t::array();
            for (auto& it : data)
            {
                json_t element = json_t::object();
                element["function_name"] = std::move(it.first);
                element["base64_module"] = std::move(it.second);
                res.push_back(std::move(element));
            }
            return res;
        };

        json_t j = json_t::object();
        j["all_codegen_modules"] = std::move(allCodegenModules);
        j["all_aot_slow_paths"] = convertToJson(allSlowPathModules);
        j["all_aot_return_continuations"] = convertToJson(allRetContModules);

        jsonOutput.write(SerializeJsonWithIndent(j, 4 /*indent*/));
    }

    {
        json_t j = json_t::array();
        for (DfgRegAllocCCallWrapperRequest& item : allCCallWrapperRequests)
        {
            j.push_back(item.SaveToJSON());
        }
        jsonOutput2.write(SerializeJsonWithIndent(j, 4 /*indent*/));
    }

    cppOutput.Commit();
    hdrOutput.Commit();
    jsonOutput.Commit();
    jsonOutput2.Commit();
}

void FPS_GenerateDfgBytecodeInfoApiHeader()
{
    ReleaseAssert(cl_inputListFilenames != "");
    std::vector<std::string> hdrFileNameList = ParseCommaSeparatedFileList(cl_inputListFilenames);

    for (std::string& fileName : hdrFileNameList)
    {
        size_t loc = fileName.find_last_of("/");
        if (loc != std::string::npos)
        {
            fileName = fileName.substr(loc + 1);
        }
    }

    ReleaseAssert(cl_headerOutputFilename != "");
    TransactionalOutputFile hdrOutput(cl_headerOutputFilename);
    FILE* hdrFp = hdrOutput.fp();
    FPS_EmitHeaderFileCommonHeader(hdrFp);

    for (const std::string& fileName : hdrFileNameList)
    {
        fprintf(hdrFp, "#include \"%s\"\n", fileName.c_str());
    }

    BytecodeOpcodeRawValueMap byOpMap = BytecodeOpcodeRawValueMap::ParseFromCommandLineArgs();

    std::vector<std::string> allBytecodeVariants = byOpMap.GetPrimaryBytecodeList();

    fprintf(hdrFp, "\n\n#define GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_VARIANT_NAMES \\\n");
    {
        bool isFirst = true;
        for (const std::string& variantName : allBytecodeVariants)
        {
            fprintf(hdrFp, "%s%s \\\n", (isFirst ? "  " : ", "), variantName.c_str());
            isFirst = false;
        }
    }
    fprintf(hdrFp, "\n\n");

    hdrOutput.Commit();
}

void Fps_GenerateDfgJitCCallWrapperStubs()
{
    ReleaseAssert(cl_inputListFilenames != "");
    std::vector<std::string> jsonFileNameList = ParseCommaSeparatedFileList(cl_inputListFilenames);

    ReleaseAssert(cl_assemblyOutputFilename != "");
    TransactionalOutputFile asmOutput(cl_assemblyOutputFilename);
    FILE* asmFp = asmOutput.fp();

    fprintf(asmFp, "\t.text\n");
    fprintf(asmFp, "\t.file\t\"deegen_dfg_jit_all_c_call_wrapper_stubs.c\"\n\n");

    std::vector<DfgRegAllocCCallWrapperRequest> allRequests;
    for (std::string& jsonFile : jsonFileNameList)
    {
        json_t j = ParseJsonFromFileName(jsonFile);
        ReleaseAssert(j.is_array());
        for (size_t idx = 0; idx < j.size(); idx++)
        {
            allRequests.push_back(DfgRegAllocCCallWrapperRequest::LoadFromJSON(j[idx]));
        }
    }

    {
        // Check that all final function names are distinct
        //
        std::unordered_set<std::string> fnNames;
        for (DfgRegAllocCCallWrapperRequest& req : allRequests)
        {
            std::string name = req.GetFuncName();
            ReleaseAssert(!fnNames.count(name) && "Duplicated stub names! Maybe something is wrong with DfgJitImplCreator::GetCCallWrapperPrefix.");
            fnNames.insert(name);
        }
    }

    {
        // Check that all instances of DfgCCallFuncInfo of one function agree with each other
        //
        std::unordered_map<std::string, DfgCCallFuncInfo> funcInfoMap;
        for (DfgRegAllocCCallWrapperRequest& req : allRequests)
        {
            if (funcInfoMap.count(req.m_info.m_fnName))
            {
                ReleaseAssert(req.m_info == funcInfoMap[req.m_info.m_fnName]);
            }
            else
            {
                funcInfoMap[req.m_info.m_fnName] = req.m_info;
            }
        }
    }

    // If two requests have identical implementation, we can make them point to the same
    // address (using an alias) to decrease code footprint
    //
    std::map<DfgRegAllocCCallWrapperRequest::KeyTy, std::string /*aliasName*/> aliasMap;
    for (DfgRegAllocCCallWrapperRequest& req : allRequests)
    {
        DfgRegAllocCCallWrapperRequest::KeyTy key = req.GetKey();
        if (aliasMap.count(key))
        {
            req.PrintAliasImpl(asmFp, aliasMap[key]);
        }
        else
        {
            aliasMap[key] = req.GetFuncName();
            req.PrintAssemblyImpl(asmFp);
        }
    }

    fprintf(asmFp, "\t.section\t\".note.GNU-stack\",\"\",@progbits\n\n");
    asmOutput.Commit();
}
