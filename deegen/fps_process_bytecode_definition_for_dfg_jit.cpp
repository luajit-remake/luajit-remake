#include "fps_main.h"
#include "deegen_bytecode_ir_components.h"
#include "read_file.h"
#include "deegen_dfg_jit_process_call_inlining.h"
#include "deegen_jit_slow_path_data.h"
#include "json_utils.h"
#include "transactional_output_file.h"

using namespace dast;

void FPS_GenerateDfgSpecializedBytecodeInfo()
{
    using namespace llvm;
    using json_t = nlohmann::json;
    std::unique_ptr<LLVMContext> llvmCtxHolder = std::make_unique<llvm::LLVMContext>();
    LLVMContext& ctx = *llvmCtxHolder.get();

    ReleaseAssert(cl_jsonInputFilename != "");
    json_t inputJson = json_t::parse(ReadFileContentAsString(cl_jsonInputFilename));

    BytecodeOpcodeRawValueMap byOpMap = BytecodeOpcodeRawValueMap::ParseFromCommandLineArgs();
    DeegenGlobalBytecodeTraitAccessor bcTraitAccessor = DeegenGlobalBytecodeTraitAccessor::ParseFromCommandLineArgs();

    ReleaseAssert(inputJson.count("all-bytecode-info"));
    json_t& bytecodeInfoListJson = inputJson["all-bytecode-info"];
    ReleaseAssert(bytecodeInfoListJson.is_array());

    ReleaseAssert(cl_headerOutputFilename != "");
    TransactionalOutputFile hdrOutput(cl_headerOutputFilename);
    FILE* hdrFp = hdrOutput.fp();
    FPS_EmitHeaderFileCommonHeader(hdrFp);

    ReleaseAssert(cl_cppOutputFilename != "");
    TransactionalOutputFile cppOutput(cl_cppOutputFilename);
    FILE* cppFp = cppOutput.fp();
    FPS_EmitCPPFileCommonHeader(cppFp);

    fprintf(hdrFp, "#include \"drt/bytecode_builder.h\"\n");

    fprintf(hdrFp, "#include \"drt/dfg_bytecode_speculative_inlining_trait.h\"\n");
    fprintf(hdrFp, "#include \"drt/constexpr_array_builder_helper.h\"\n");
    fprintf(hdrFp, "namespace dfg {\n");

    for (size_t bytecodeDefOrd = 0; bytecodeDefOrd < bytecodeInfoListJson.size(); bytecodeDefOrd++)
    {
        json_t& curBytecodeInfoJson = bytecodeInfoListJson[bytecodeDefOrd];

        BytecodeIrInfo bii(ctx, curBytecodeInfoJson);

        bii.m_bytecodeDef->ComputeBaselineJitSlowPathDataLayout();

        size_t numJitCallICs = bii.m_bytecodeDef->GetNumCallICsInJitTier();
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

    fprintf(hdrFp, "} // namespace dfg\n");

    cppOutput.Commit();
    hdrOutput.Commit();
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
