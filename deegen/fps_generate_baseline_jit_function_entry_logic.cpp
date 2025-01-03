#include "anonymous_file.h"
#include "fps_main.h"
#include "transactional_output_file.h"

#include "deegen_function_entry_logic_creator.h"
#include "llvm/Linker/Linker.h"
#include "drt/baseline_jit_codegen_helper.h"

namespace {

struct GenerateFnEntryLogicArrayResult
{
    std::unique_ptr<llvm::Module> m_module;
    std::vector<std::string> m_fnNameList;
    std::vector<BaselineJitFunctionEntryLogicTraits> m_infoList;
    std::string m_auditFileContents;
};

// Note that the specialization range is [0, specializationThreshold] inclusive!
//
GenerateFnEntryLogicArrayResult WARN_UNUSED GenerateBaselineJitFnEntryLogicArray(llvm::LLVMContext& ctx, bool takesVarArgs, size_t specializationThreshold)
{
    using namespace llvm;
    using namespace dast;
    AnonymousFile auditFile;
    FILE* fp = auditFile.GetFStream("w");

    GenerateFnEntryLogicArrayResult r;

    std::unique_ptr<Module> module;
    for (size_t k = 0; k <= specializationThreshold + 1; k++)
    {
        size_t numFixedArgs = k;
        if (k == specializationThreshold + 1)
        {
            numFixedArgs = static_cast<size_t>(-1);
        }

        DeegenFunctionEntryLogicCreator gen(ctx, DeegenEngineTier::BaselineJIT, takesVarArgs, numFixedArgs);
        auto res = gen.GetBaselineJitResult();

        fprintf(fp, "# ====== numFixedArgs = %lld, takesVarArgs = %s ======\n\n", static_cast<long long>(numFixedArgs), (takesVarArgs ? "true" : "false"));
        fprintf(fp, "%s\n\n", res.m_asmSourceForAudit.c_str());

        ReleaseAssert(res.m_fastPathLen <= 65535);
        ReleaseAssert(res.m_slowPathLen <= 65535);
        ReleaseAssert(res.m_dataSecLen <= 65535);
        r.m_infoList.push_back({
            .m_fastPathCodeLen = static_cast<uint16_t>(res.m_fastPathLen),
            .m_slowPathCodeLen = static_cast<uint16_t>(res.m_slowPathLen),
            .m_dataSecCodeLen = static_cast<uint16_t>(res.m_dataSecLen),
            .m_emitterFn = nullptr
        });
        r.m_fnNameList.push_back(res.m_patchFnName);

        if (module.get() == nullptr)
        {
            module = std::move(res.m_module);
        }
        else
        {
            Linker linker(*module.get());
            ReleaseAssert(linker.linkInModule(std::move(res.m_module)) == false);
        }
    }
    ReleaseAssert(module.get() != nullptr);
    ValidateLLVMModule(module.get());
    r.m_module = std::move(module);

    fclose(fp);
    r.m_auditFileContents = auditFile.GetFileContents();

    ReleaseAssert(r.m_fnNameList.size() == specializationThreshold + 2);
    ReleaseAssert(r.m_infoList.size() == specializationThreshold + 2);
    return r;
}

}   // anonymous namespace

void FPS_GenerateBaselineJitFunctionEntryLogic()
{
    using namespace llvm;
    using namespace dast;

    std::unique_ptr<LLVMContext> llvmCtxHolder = std::make_unique<LLVMContext>();
    LLVMContext& ctx = *llvmCtxHolder.get();

    GenerateFnEntryLogicArrayResult novaRes = GenerateBaselineJitFnEntryLogicArray(ctx, false /*takesVarArgs*/, x_baselineJitFunctionEntrySpecializeThresholdForNonVarargsFunction);
    GenerateFnEntryLogicArrayResult vaRes = GenerateBaselineJitFnEntryLogicArray(ctx, true /*takesVarArgs*/, x_baselineJitFunctionEntrySpecializeThresholdForVarargsFunction);

    {
        std::unordered_set<std::string> chkUnique;
        for (auto& s : novaRes.m_fnNameList)
        {
            ReleaseAssert(!chkUnique.count(s));
            chkUnique.insert(s);
        }
        for (auto& s : vaRes.m_fnNameList)
        {
            ReleaseAssert(!chkUnique.count(s));
            chkUnique.insert(s);
        }
    }

    std::unique_ptr<Module> module = std::move(novaRes.m_module);
    {
        Linker linker(*module.get());
        ReleaseAssert(linker.linkInModule(std::move(vaRes.m_module)) == false);
    }

    ValidateLLVMModule(module.get());

    ReleaseAssert(cl_cppOutputFilename != "");
    TransactionalOutputFile cppOutputFile(cl_cppOutputFilename);
    FILE* cppFp = cppOutputFile.fp();
    fprintf(cppFp, "#include \"drt/baseline_jit_codegen_helper.h\"\n\n");

    auto printCppFile = [&](const std::string& varName, GenerateFnEntryLogicArrayResult& r)
    {
        size_t len = r.m_infoList.size();
        ReleaseAssert(len == r.m_fnNameList.size());
        for (auto& s : r.m_fnNameList)
        {
            fprintf(cppFp, "extern \"C\" void %s(void*, void*, void*);\n", s.c_str());
        }
        fprintf(cppFp, "extern \"C\" const BaselineJitFunctionEntryLogicTraits %s[%llu];\n", varName.c_str(), static_cast<unsigned long long>(len));
        fprintf(cppFp, "constexpr BaselineJitFunctionEntryLogicTraits %s[%llu] = {\n", varName.c_str(), static_cast<unsigned long long>(len));
        for (size_t i = 0; i < len; i++)
        {
            fprintf(cppFp, "    BaselineJitFunctionEntryLogicTraits {\n");
            fprintf(cppFp, "        .m_fastPathCodeLen = %llu,\n", static_cast<unsigned long long>(r.m_infoList[i].m_fastPathCodeLen));
            fprintf(cppFp, "        .m_slowPathCodeLen = %llu,\n", static_cast<unsigned long long>(r.m_infoList[i].m_slowPathCodeLen));
            fprintf(cppFp, "        .m_dataSecCodeLen = %llu,\n", static_cast<unsigned long long>(r.m_infoList[i].m_dataSecCodeLen));
            fprintf(cppFp, "        .m_emitterFn = &%s,\n", r.m_fnNameList[i].c_str());
            fprintf(cppFp, "    }");
            if (i + 1 < len)
            {
                fprintf(cppFp, ",");
            }
            fprintf(cppFp, "\n");
        }
        fprintf(cppFp, "};\n\n");
    };

    ReleaseAssert(novaRes.m_fnNameList.size() == x_baselineJitFunctionEntrySpecializeThresholdForNonVarargsFunction + 2);
    printCppFile("deegen_baseline_jit_function_entry_logic_trait_table_nova", novaRes);

    ReleaseAssert(vaRes.m_fnNameList.size() == x_baselineJitFunctionEntrySpecializeThresholdForVarargsFunction + 2);
    printCppFile("deegen_baseline_jit_function_entry_logic_trait_table_va", vaRes);

    ReleaseAssert(cl_assemblyOutputFilename != "");
    TransactionalOutputFile asmOutputFile(cl_assemblyOutputFilename);

    std::string asmFileContents = CompileLLVMModuleToAssemblyFile(module.get(), Reloc::Static, CodeModel::Small);
    asmOutputFile.write(asmFileContents);

    std::string auditFilePath = FPS_GetAuditFilePathWithTwoPartName("baseline_jit", "deegen_fn_entry_logic.s");
    TransactionalOutputFile auditFile(auditFilePath);
    auditFile.write(novaRes.m_auditFileContents + vaRes.m_auditFileContents);

    auditFile.Commit();
    cppOutputFile.Commit();
    asmOutputFile.Commit();
}
