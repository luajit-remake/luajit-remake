#include "fps_main.h"
#include "json_parse_dump.h"
#include "read_file.h"
#include "transactional_output_file.h"
#include "deegen_dfg_builtin_nodes.h"
#include "llvm/Linker/Linker.h"
#include "drt/dfg_codegen_protocol.h"
#include "drt/jit_memory_allocator.h"
#include "drt/dfg_builtin_nodes.h"

using namespace dast;

struct DfgBuiltinNodeProcessorState
{
    DfgBuiltinNodeProcessorState(bool isCustomCgFnProtocol, FILE* hdrFp, FILE* cppFp)
        : m_isCustomCgFnProtocol(isCustomCgFnProtocol), m_hdrFp(hdrFp), m_cppFp(cppFp)
    {
        if (!m_isCustomCgFnProtocol)
        {
            m_standardNodeMainEntryTable.resize(dfg::x_numTotalDfgBuiltinNodeKinds);
            for (size_t i = 0; i < m_standardNodeMainEntryTable.size(); i++)
            {
                m_standardNodeMainEntryTable[i] = "nullptr";
            }
        }
    }

    struct CgFnInfo
    {
        dfg::CodegenFnJitCodeSizeInfo m_jitCodeSizeInfo;
        std::string m_cgFnName;
    };

    bool m_isCustomCgFnProtocol;
    FILE* m_hdrFp;
    FILE* m_cppFp;
    std::unique_ptr<llvm::Module> m_allCgModules;
    std::vector<DfgRegAllocCCallWrapperRequest> m_allCCallWrapperReqs;
    std::vector<CgFnInfo> m_cgFnInfo;
    std::vector<std::pair<std::string /*fileName*/, std::string /*content*/>> m_auditFiles;
    std::vector<std::pair<std::string /*fileName*/, std::string /*content*/>> m_extraAuditFiles;

    std::vector<std::string> m_standardNodeMainEntryTable;
    std::string m_customNodeMainEntryLambda;

    size_t GetNumCgFns()
    {
        return m_cgFnInfo.size();
    }

    void ProcessCgInfo(std::string nodeImplName, DfgBuiltinNodeVariantCodegenInfo& info)
    {
        using namespace llvm;
        {
            std::string auditFileName = "_builtin_" + nodeImplName + std::to_string(info.m_variantOrd) + ".s";
            std::string content = info.m_jitCodeAuditLog;
            content += CompileLLVMModuleToAssemblyFile(info.m_cgMod.get(), Reloc::Static, CodeModel::Small);
            m_auditFiles.push_back(std::make_pair(auditFileName, content));
        }
        {
            std::string auditFileName = "_builtin_" + nodeImplName + std::to_string(info.m_variantOrd) + ".ll";
            m_extraAuditFiles.push_back(std::make_pair(auditFileName, DumpLLVMModuleAsString(info.m_implMod.get())));
        }

        if (m_allCgModules.get() == nullptr)
        {
            m_allCgModules = std::move(info.m_cgMod);
        }
        else
        {
            Linker linker(*m_allCgModules.get());
            ReleaseAssert(linker.linkInModule(std::move(info.m_cgMod)) == false);
        }

        m_allCCallWrapperReqs.insert(m_allCCallWrapperReqs.end(),
                                     info.m_ccwRequests.begin(),
                                     info.m_ccwRequests.end());

        ReleaseAssert(info.m_fastPathLength <= 65535);
        ReleaseAssert(info.m_slowPathLength <= 65535);
        ReleaseAssert(info.m_dataSecLength <= 65535);
        ReleaseAssert(info.m_dataSecAlignment <= x_jitMaxPossibleDataSectionAlignment);

        m_cgFnInfo.push_back({
            .m_jitCodeSizeInfo = {
                .m_fastPathCodeLen = SafeIntegerCast<uint16_t>(info.m_fastPathLength),
                .m_slowPathCodeLen = SafeIntegerCast<uint16_t>(info.m_slowPathLength),
                .m_dataSecLen = SafeIntegerCast<uint16_t>(info.m_dataSecLength),
                .m_dataSecAlignment = SafeIntegerCast<uint16_t>(info.m_dataSecAlignment)
            },
            .m_cgFnName = info.m_cgFnName
        });
    }

    void PrintCodegenFnArray(FILE* fp)
    {
        for (size_t idx = 0; idx < m_cgFnInfo.size(); idx++)
        {
            ReleaseAssert(m_cgFnInfo[idx].m_cgFnName != "");
            fprintf(fp, "    %s", m_cgFnInfo[idx].m_cgFnName.c_str());
            if (idx + 1 < m_cgFnInfo.size()) { fprintf(fp, ","); }
            fprintf(fp, "\n");
        }
    }

    void PrintJitCodeSizeArray(FILE* fp)
    {
        for (size_t idx = 0; idx < m_cgFnInfo.size(); idx++)
        {
            dfg::CodegenFnJitCodeSizeInfo info = m_cgFnInfo[idx].m_jitCodeSizeInfo;
            fprintf(fp, "    CodegenFnJitCodeSizeInfo {\n");
            fprintf(fp, "        .m_fastPathCodeLen = %d,\n", static_cast<int>(info.m_fastPathCodeLen));
            fprintf(fp, "        .m_slowPathCodeLen = %d,\n", static_cast<int>(info.m_slowPathCodeLen));
            fprintf(fp, "        .m_dataSecLen = %d,\n", static_cast<int>(info.m_dataSecLen));
            fprintf(fp, "        .m_dataSecAlignment = %d\n", static_cast<int>(info.m_dataSecAlignment));
            fprintf(fp, "    }");
            if (idx + 1 < m_cgFnInfo.size()) { fprintf(fp, ","); }
            fprintf(fp, "\n");
        }
    }

    void RegisterImpl(DfgBuiltinNodeCodegenProcessorBase& impl)
    {
        if (m_isCustomCgFnProtocol)
        {
            AddCustomNodeMainEntry(impl);
        }
        else
        {
            SetStandardNodeMainEntry(impl);
        }
        if (impl.m_isRegAllocEnabled)
        {
            std::string auditFileName = "_builtin_" + impl.NodeName() + "_reg_alloc.log";
            m_auditFiles.push_back(std::make_pair(auditFileName, impl.m_regAllocAuditLog));
        }
    }

private:
    void SetStandardNodeMainEntry(DfgBuiltinNodeCodegenProcessorBase& impl)
    {
        ReleaseAssert(!m_isCustomCgFnProtocol);
        ReleaseAssert(!dfg::DfgBuiltinNodeUseCustomCodegenImpl(impl.AssociatedNodeKind()));
        size_t nodeKind = static_cast<size_t>(impl.AssociatedNodeKind());
        ReleaseAssert(nodeKind < m_standardNodeMainEntryTable.size());
        ReleaseAssert(m_standardNodeMainEntryTable[nodeKind] == "nullptr");
        m_standardNodeMainEntryTable[nodeKind] = "&x_deegen_dfg_variant__dfg_builtin_" + impl.NodeName();
    }

    void AddCustomNodeMainEntry(DfgBuiltinNodeCodegenProcessorBase& impl)
    {
        ReleaseAssert(m_isCustomCgFnProtocol);
        ReleaseAssert(dfg::DfgBuiltinNodeUseCustomCodegenImpl(impl.AssociatedNodeKind()));
        m_customNodeMainEntryLambda += "    ReleaseAssert(arr[static_cast<size_t>(DfgBuiltinNodeCustomCgFn::" + impl.NodeName() + ")] == nullptr);\n";
        m_customNodeMainEntryLambda += "    arr[static_cast<size_t>(DfgBuiltinNodeCustomCgFn::" + impl.NodeName() + ")] = &x_deegen_dfg_variant__dfg_builtin_" + impl.NodeName() + ";\n";
    }
};

static void ProcessDfgBuiltinNode(DfgBuiltinNodeProcessorState& state /*inout*/, std::unique_ptr<DfgBuiltinNodeCodegenProcessorBase> implHolder)
{
    ReleaseAssert(implHolder.get() != nullptr);
    DfgBuiltinNodeCodegenProcessorBase& impl = *implHolder.get();
    ReleaseAssert(impl.IsProcessed());

    state.RegisterImpl(impl);

    std::string implName = impl.NodeName();
    FILE* hdrFp = state.m_hdrFp;
    FILE* cppFp = state.m_cppFp;

    size_t startCgFnOrd = state.GetNumCgFns();

    if (impl.m_isRegAllocEnabled)
    {
        size_t curSvOrd = 0;
        std::vector<std::string> variantCppIdents;
        for (size_t variantIdx = 0; variantIdx < impl.m_rootInfo->m_variants.size(); variantIdx++)
        {
            DfgNodeRegAllocVariant* variant = impl.m_rootInfo->m_variants[variantIdx].get();

            ReleaseAssert(impl.m_svMap.count(variant));
            std::vector<std::unique_ptr<DfgNodeRegAllocSubVariant>>& r = impl.m_svMap[variant];

            size_t variantStartCgFnOrd = state.GetNumCgFns();
            for (std::unique_ptr<DfgNodeRegAllocSubVariant>& sv : r)
            {
                if (sv.get() != nullptr)
                {
                    Auto(curSvOrd++);
                    ReleaseAssert(impl.m_cgInfoMap.count(sv.get()));
                    DfgBuiltinNodeVariantCodegenInfo& cgInfo = impl.m_cgInfoMap[sv.get()];

                    ReleaseAssert(cgInfo.m_variantOrd == curSvOrd);
                    state.ProcessCgInfo(impl.NodeName(), cgInfo);

                    if (state.m_isCustomCgFnProtocol)
                    {
                        fprintf(hdrFp, "extern \"C\" void %s(PrimaryCodegenState*, BuiltinNodeOperandsInfoBase*, uint64_t*, RegAllocStateForCodeGen*);\n\n",
                                cgInfo.m_cgFnName.c_str());
                    }
                    else
                    {
                        fprintf(hdrFp, "extern \"C\" void %s(PrimaryCodegenState*, NodeRegAllocInfo*, uint8_t*, RegAllocStateForCodeGen*);\n\n",
                                cgInfo.m_cgFnName.c_str());
                    }
                }
            }

            // Print the C++ definition for this reg alloc variant
            //
            std::string cppNameIdent = "_dfg_builtin_" + impl.NodeName() + "_" + std::to_string(variantIdx);
            std::string baseOrdExpr = std::to_string(variantStartCgFnOrd);
            if (!state.m_isCustomCgFnProtocol)
            {
                baseOrdExpr = "x_totalNumDfgUserNodeCodegenFuncs + " + baseOrdExpr;
            }

            variantCppIdents.push_back(cppNameIdent);
            variant->PrintCppDefinitions(r, hdrFp, cppFp, cppNameIdent, baseOrdExpr, false /*hasRangeOperand*/);
        }

        {
            std::string cppNameIdent = "_dfg_builtin_" + impl.NodeName();
            std::string baseOrdExpr = std::to_string(startCgFnOrd);
            if (!state.m_isCustomCgFnProtocol)
            {
                baseOrdExpr = "x_totalNumDfgUserNodeCodegenFuncs + " + baseOrdExpr;
            }

            impl.m_rootInfo->PrintCppDefinitions(hdrFp,
                                                 cppFp,
                                                 cppNameIdent,
                                                 variantCppIdents,
                                                 baseOrdExpr,
                                                 curSvOrd);

            fprintf(hdrFp, "\n");
        }
        ReleaseAssert(state.GetNumCgFns() == startCgFnOrd + curSvOrd);
    }
    else
    {
        ReleaseAssert(impl.m_cgInfoForRegDisabled.get() != nullptr);
        DfgBuiltinNodeVariantCodegenInfo& cgInfo = *impl.m_cgInfoForRegDisabled.get();

        ReleaseAssert(cgInfo.m_variantOrd == 0);
        state.ProcessCgInfo(impl.NodeName(), cgInfo);

        if (state.m_isCustomCgFnProtocol)
        {
            fprintf(hdrFp, "extern \"C\" void %s(PrimaryCodegenState*, BuiltinNodeOperandsInfoBase*, uint64_t*, RegAllocStateForCodeGen*);\n\n",
                    cgInfo.m_cgFnName.c_str());
        }
        else
        {
            fprintf(hdrFp, "extern \"C\" void %s(PrimaryCodegenState*, NodeRegAllocInfo*, uint8_t*, RegAllocStateForCodeGen*);\n\n",
                    cgInfo.m_cgFnName.c_str());
        }

        fprintf(hdrFp, "extern const DfgVariantTraits x_deegen_dfg_variant__dfg_builtin_%s;\n", impl.NodeName().c_str());

        std::string cgFnOrdExpr = std::to_string(startCgFnOrd);
        if (!state.m_isCustomCgFnProtocol)
        {
            cgFnOrdExpr = "x_totalNumDfgUserNodeCodegenFuncs + " + cgFnOrdExpr;
        }

        fprintf(cppFp, "constexpr DfgVariantTraits x_deegen_dfg_variant__dfg_builtin_%s(%s);\n\n",
                impl.NodeName().c_str(), cgFnOrdExpr.c_str());

        ReleaseAssert(state.GetNumCgFns() == startCgFnOrd + 1);
    }
}

void FPS_ProcessDfgBuiltinNodes()
{
    using namespace llvm;
    std::unique_ptr<LLVMContext> llvmCtxHolder = std::make_unique<llvm::LLVMContext>();
    LLVMContext& ctx = *llvmCtxHolder.get();

    TransactionalOutputFile hdrOutFile(cl_headerOutputFilename);
    FPS_EmitHeaderFileCommonHeader(hdrOutFile.fp());

    TransactionalOutputFile cppOutFile(cl_cppOutputFilename);
    FPS_EmitCPPFileCommonHeader(cppOutFile.fp());

    FILE* hdrFp = hdrOutFile.fp();
    FILE* cppFp = cppOutFile.fp();

    fprintf(hdrFp, "#include \"drt/dfg_variant_traits_internal.h\"\n");

    fprintf(cppFp, "#define DEEGEN_POST_FUTAMURA_PROJECTION\n\n");
    fprintf(cppFp, "#include \"drt/dfg_variant_traits.h\"\n");
    fprintf(cppFp, "#include \"generated/deegen_dfg_jit_builtin_node_codegen_info.h\"\n\n");

    fprintf(hdrFp, "namespace dfg {\n");
    fprintf(cppFp, "namespace dfg {\n");

    DfgBuiltinNodeProcessorState standardState(false /*isCustomCgFnProtocol*/, hdrFp, cppFp);
    DfgBuiltinNodeProcessorState customState(true /*isCustomCgFnProtocol*/, hdrFp, cppFp);

    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplConstant>(ctx));
    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplUnboxedConstant>(ctx));
    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplArgument>(ctx));
    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplGetNumVariadicArgs>(ctx));
    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplGetKthVariadicArg>(ctx));
    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplGetFunctionObject>(ctx));
    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplGetLocal>(ctx));
    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplSetLocal>(ctx));
    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplCreateCapturedVar>(ctx));
    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplGetCapturedVar>(ctx));
    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplSetCapturedVar>(ctx));
    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplGetKthVariadicRes>(ctx));
    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplGetNumVariadicRes>(ctx));
    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplCheckU64InBound>(ctx));
    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplI64SubSaturateToZero>(ctx));
    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplGetImmutableUpvalue>(ctx));
    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplGetMutableUpvalue>(ctx));
    ProcessDfgBuiltinNode(standardState, std::make_unique<DfgBuiltinNodeImplSetUpvalue>(ctx));

    ProcessDfgBuiltinNode(customState, std::make_unique<DfgBuiltinNodeImplCreateVariadicRes_StoreInfo>(ctx));
    ProcessDfgBuiltinNode(customState, std::make_unique<DfgBuiltinNodeImplPrependVariadicRes_MoveAndStoreInfo>(ctx));
    ProcessDfgBuiltinNode(customState, std::make_unique<DfgBuiltinNodeImplCreateFunctionObject_AllocAndSetup>(ctx));
    ProcessDfgBuiltinNode(customState, std::make_unique<DfgBuiltinNodeImplCreateFunctionObject_BoxFunctionObject>(ctx));
    ProcessDfgBuiltinNode(customState, std::make_unique<DfgBuiltinNodeImplReturn_MoveVariadicRes>(ctx));
    ProcessDfgBuiltinNode(customState, std::make_unique<DfgBuiltinNodeImplReturn_RetWithVariadicRes>(ctx));
    ProcessDfgBuiltinNode(customState, std::make_unique<DfgBuiltinNodeImplReturn_WriteNil>(ctx));
    ProcessDfgBuiltinNode(customState, std::make_unique<DfgBuiltinNodeImplReturn_RetNoVariadicRes>(ctx));
    ProcessDfgBuiltinNode(customState, std::make_unique<DfgBuiltinNodeImplReturn_Ret1>(ctx));
    ProcessDfgBuiltinNode(customState, std::make_unique<DfgBuiltinNodeImplReturn_Ret0>(ctx));

    // Make sure we provided implementations for all standard nodes
    //
    for (size_t idx = 0; idx < standardState.m_standardNodeMainEntryTable.size(); idx++)
    {
        dfg::NodeKind nodeKind = static_cast<dfg::NodeKind>(idx);
        if (dfg::DfgBuiltinNodeUseCustomCodegenImpl(nodeKind))
        {
            if (standardState.m_standardNodeMainEntryTable[idx] != "nullptr")
            {
                fprintf(stderr, "DFG Builtin node %s is marked to use custom codegen, but a standard codegen is provided!\n",
                        dfg::GetDfgBuiltinNodeKindName(nodeKind));
                abort();
            }
        }
        else
        {
            if (standardState.m_standardNodeMainEntryTable[idx] == "nullptr")
            {
                fprintf(stderr, "DFG Builtin node %s is marked to use standard codegen, but no codegen implementation is provided!\n",
                        dfg::GetDfgBuiltinNodeKindName(nodeKind));
                abort();
            }
        }
    }

    fprintf(hdrFp, "constexpr std::array<const DfgVariantTraits*, x_numTotalDfgBuiltinNodeKinds> x_dfg_builtin_node_standard_codegen_handler = {\n");
    for (size_t idx = 0; idx < dfg::x_numTotalDfgBuiltinNodeKinds; idx++)
    {
        ReleaseAssert(idx < standardState.m_standardNodeMainEntryTable.size());
        fprintf(hdrFp, "    %s", standardState.m_standardNodeMainEntryTable[idx].c_str());
        if (idx + 1 != dfg::x_numTotalDfgBuiltinNodeKinds) { fprintf(hdrFp, ","); }
        fprintf(hdrFp, "\n");
    }
    fprintf(hdrFp, "};\n\n");

    fprintf(hdrFp, "constexpr std::array<const DfgVariantTraits*, static_cast<size_t>(DfgBuiltinNodeCustomCgFn::X_END_OF_ENUM)> x_dfg_builtin_node_custom_codegen_handler = []() {\n");
    fprintf(hdrFp, "    constexpr size_t n = static_cast<size_t>(DfgBuiltinNodeCustomCgFn::X_END_OF_ENUM);\n");
    fprintf(hdrFp, "    std::array<const DfgVariantTraits*, n> arr;\n");
    fprintf(hdrFp, "    for (size_t i = 0; i < n; i++) { arr[i] = nullptr; }\n");
    fprintf(hdrFp, "%s", customState.m_customNodeMainEntryLambda.c_str());
    fprintf(hdrFp, "    for (size_t i = 0; i < n; i++) { ReleaseAssert(arr[i] != nullptr); }\n");
    fprintf(hdrFp, "    return arr;\n");
    fprintf(hdrFp, "}();\n\n");

    fprintf(hdrFp, "constexpr size_t x_totalDfgBuiltinNodeStandardCgFns = %d;\n\n", static_cast<int>(standardState.GetNumCgFns()));
    fprintf(hdrFp, "constexpr size_t x_totalDfgBuiltinNodeCustomCgFns = %d;\n\n", static_cast<int>(customState.GetNumCgFns()));

    fprintf(hdrFp, "constexpr std::array<CodegenImplFn, x_totalDfgBuiltinNodeStandardCgFns> x_dfgBuiltinNodeStandardCgFnArray = {\n");
    standardState.PrintCodegenFnArray(hdrFp);
    fprintf(hdrFp, "};\n");

    fprintf(hdrFp, "constexpr std::array<CodegenFnJitCodeSizeInfo, x_totalDfgBuiltinNodeStandardCgFns> x_dfgBuiltinNodeStandardCgFnJitCodeSizeArray = {\n");
    standardState.PrintJitCodeSizeArray(hdrFp);
    fprintf(hdrFp, "};\n");

    fprintf(hdrFp, "constexpr std::array<CustomBuiltinNodeCodegenImplFn, x_totalDfgBuiltinNodeCustomCgFns> x_dfgBuiltinNodeCustomCgFnArray = {\n");
    customState.PrintCodegenFnArray(hdrFp);
    fprintf(hdrFp, "};\n");

    fprintf(hdrFp, "constexpr std::array<CodegenFnJitCodeSizeInfo, x_totalDfgBuiltinNodeCustomCgFns> x_dfgBuiltinNodeCustomCgFnJitCodeSizeArray = {\n");
    customState.PrintJitCodeSizeArray(hdrFp);
    fprintf(hdrFp, "};\n");

    fprintf(hdrFp, "} // namespace dfg\n");
    fprintf(cppFp, "} // namespace dfg\n");

    {
        ReleaseAssert(standardState.m_allCgModules.get() != nullptr && customState.m_allCgModules.get() != nullptr);
        Linker linker(*standardState.m_allCgModules.get());
        ReleaseAssert(linker.linkInModule(std::move(customState.m_allCgModules)) == false);
    }

    standardState.m_auditFiles.insert(standardState.m_auditFiles.end(),
                                      customState.m_auditFiles.begin(),
                                      customState.m_auditFiles.end());

    standardState.m_extraAuditFiles.insert(standardState.m_extraAuditFiles.end(),
                                           customState.m_extraAuditFiles.begin(),
                                           customState.m_extraAuditFiles.end());

    standardState.m_allCCallWrapperReqs.insert(standardState.m_allCCallWrapperReqs.end(),
                                               customState.m_allCCallWrapperReqs.begin(),
                                               customState.m_allCCallWrapperReqs.end());

    TransactionalOutputFile asmOutFile(cl_assemblyOutputFilename);
    asmOutFile.write(CompileLLVMModuleToAssemblyFile(standardState.m_allCgModules.get(), Reloc::Static, CodeModel::Small));

    TransactionalOutputFile jsonOutFile(cl_jsonOutputFilename);
    {
        json_t j = json_t::array();
        for (DfgRegAllocCCallWrapperRequest& item : standardState.m_allCCallWrapperReqs)
        {
            j.push_back(item.SaveToJSON());
        }
        jsonOutFile.write(SerializeJsonWithIndent(j, 4 /*indent*/));
    }

    ReleaseAssert(cl_auditDirPath != "");

    {
        std::unordered_set<std::string> checkUnique;
        for (auto& item : standardState.m_auditFiles)
        {
            ReleaseAssert(!checkUnique.count(item.first));
            checkUnique.insert(item.first);
            std::string auditFilePath = FPS_GetAuditFilePathWithTwoPartName("dfg_jit" /*dirSuffix*/, item.first);
            TransactionalOutputFile auditFile(auditFilePath);
            auditFile.write(item.second);
            auditFile.Commit();
        }
    }

    {
        std::unordered_set<std::string> checkUnique;
        for (auto& item : standardState.m_extraAuditFiles)
        {
            ReleaseAssert(!checkUnique.count(item.first));
            checkUnique.insert(item.first);
            std::string auditFilePath = FPS_GetAuditFilePathWithTwoPartName("dfg_jit_verbose" /*dirSuffix*/, item.first);
            TransactionalOutputFile auditFile(auditFilePath);
            auditFile.write(item.second);
            auditFile.Commit();
        }
    }

    hdrOutFile.Commit();
    cppOutFile.Commit();
    asmOutFile.Commit();
    jsonOutFile.Commit();
}