#include "fps_main.h"
#include "deegen_postprocess_module_linker.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_dfg_jit_impl_creator.h"
#include "deegen_bytecode_ir_components.h"
#include "llvm_identical_function_merger.h"
#include "read_file.h"
#include "json_utils.h"
#include "json_parse_dump.h"
#include "transactional_output_file.h"
#include "base64_util.h"

using namespace dast;

void Fps_PostProcessLinkImplementations()
{
    using namespace llvm;
    std::unique_ptr<LLVMContext> llvmCtxHolder = std::make_unique<llvm::LLVMContext>();
    LLVMContext& ctx = *llvmCtxHolder.get();

    ReleaseAssert(cl_jsonInputFilename != "");
    json_t interpreterJson = ParseJsonFromFileName(cl_jsonInputFilename);

    ReleaseAssert(interpreterJson.count("all-bytecode-info"));
    json_t& bytecodeInfoListJson = interpreterJson["all-bytecode-info"];
    ReleaseAssert(bytecodeInfoListJson.is_array());

    ReleaseAssert(cl_jsonInputFilename2 != "");
    json_t baselineJitJson = ParseJsonFromFileName(cl_jsonInputFilename2);

    ReleaseAssert(cl_jsonInputFilename3 != "");
    json_t dfgJitJson = ParseJsonFromFileName(cl_jsonInputFilename3);

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
        json_t& j = bytecodeInfoListJson[bytecodeDefOrd];
        ReleaseAssert(j.count("bytecode_variant_definition"));
        json_t& bvd = j["bytecode_variant_definition"];

        std::string implFuncName = JSONCheckedGet<std::string>(bvd, "impl_function_name");
        bytecodeOriginalImplFunctionNames.push_back(implFuncName);
    }

    // Parse the origin module and each additional module generated from the interpreter lowering stage
    //
    auto getModuleFromBase64EncodedString = [&](const std::string& str)
    {
        return ParseLLVMModuleFromString(ctx, "bytecode_ir_component_module" /*moduleName*/, base64_decode(str));
    };

    std::vector<std::unique_ptr<Module>> moduleToLink;

    std::unique_ptr<Module> module = getModuleFromBase64EncodedString(JSONCheckedGet<std::string>(interpreterJson, "reference_module"));

    // Process interpreter modules
    //
    {
        ReleaseAssert(interpreterJson.count("bytecode_module_list"));
        json_t& listOfInterpreterLoweringModules = interpreterJson["bytecode_module_list"];
        ReleaseAssert(listOfInterpreterLoweringModules.is_array());
        for (size_t i = 0; i < listOfInterpreterLoweringModules.size(); i++)
        {
            json_t& val = listOfInterpreterLoweringModules[i];
            ReleaseAssert(val.is_string());
            std::unique_ptr<Module> m = getModuleFromBase64EncodedString(val.get<std::string>());
            moduleToLink.push_back(std::move(m));
        }

        // Add the list of return continuations from interpreter lowering to mergeable functions list
        //
        ReleaseAssert(interpreterJson.count("return-cont-name-list"));
        json_t& listOfReturnContFnNamesFromInterpreterLowering = interpreterJson["return-cont-name-list"];
        ReleaseAssert(listOfReturnContFnNamesFromInterpreterLowering.is_array());
        for (size_t i = 0; i < listOfReturnContFnNamesFromInterpreterLowering.size(); i++)
        {
            json_t& val = listOfReturnContFnNamesFromInterpreterLowering[i];
            ReleaseAssert(val.is_string());
            listOfFunctionsOkToMerge.push_back(val.get<std::string>());
        }
    }

    // Process baseline JIT and DFG JIT modules
    //
    {
        auto addCodegenModule = [&](const std::string& base64Module)
        {
            std::unique_ptr<Module> m = getModuleFromBase64EncodedString(base64Module);
            for (Function& fn : *m.get())
            {
                fnNamesNeedToBeMadeDsoLocal.insert(fn.getName().str());
                if (!fn.empty())
                {
                    ReleaseAssert(fn.hasExternalLinkage());
                }
            }
            moduleToLink.push_back(std::move(m));
        };

        auto addSlowPathModule = [&](const std::string& funcName, const std::string& base64Module)
        {
            std::unique_ptr<Module> m = getModuleFromBase64EncodedString(base64Module);
            Function* fn = m->getFunction(funcName);
            ReleaseAssert(fn != nullptr);
            ReleaseAssert(!fn->empty());
            ReleaseAssert(fn->hasExternalLinkage());
            for (Function& f : *m.get())
            {
                if (!f.empty())
                {
                    ReleaseAssert(&f == fn);
                }
            }
            moduleToLink.push_back(std::move(m));
        };

        auto addRetContModule = [&](const std::string& funcName, const std::string& base64Module)
        {
            listOfFunctionsOkToMerge.push_back(funcName);
            addSlowPathModule(funcName, base64Module);
        };

        auto handleJsonFile = [&](json_t& j)
        {
            {
                ReleaseAssert(j.count("all_codegen_modules"));
                json_t& list = j["all_codegen_modules"];
                ReleaseAssert(list.is_array());
                for (size_t i = 0; i < list.size(); i++)
                {
                    json_t& val = list[i];
                    ReleaseAssert(val.is_string());
                    addCodegenModule(val.get<std::string>());
                }
            }
            {
                ReleaseAssert(j.count("all_aot_slow_paths"));
                json_t& list = j["all_aot_slow_paths"];
                ReleaseAssert(list.is_array());
                for (size_t i = 0; i < list.size(); i++)
                {
                    json_t& element = list[i];
                    ReleaseAssert(element.is_object());
                    addSlowPathModule(
                        JSONCheckedGet<std::string>(element, "function_name"),
                        JSONCheckedGet<std::string>(element, "base64_module"));
                }
            }
            {
                ReleaseAssert(j.count("all_aot_return_continuations"));
                json_t& list = j["all_aot_return_continuations"];
                ReleaseAssert(list.is_array());
                for (size_t i = 0; i < list.size(); i++)
                {
                    json_t& element = list[i];
                    ReleaseAssert(element.is_object());
                    addRetContModule(
                        JSONCheckedGet<std::string>(element, "function_name"),
                        JSONCheckedGet<std::string>(element, "base64_module"));
                }
            }
        };

        handleJsonFile(baselineJitJson);
        handleJsonFile(dfgJitJson);
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

    // For sanity, add dso_local to all function names that we are certain to be generated by Deegen
    //
    for (Function& fn : *module.get())
    {
        std::string funcName = fn.getName().str();
        if (funcName.starts_with("__deegen_interpreter_") ||
            funcName.starts_with("__deegen_baseline_jit_") ||
            funcName.starts_with("__deegen_dfg_jit_") ||
            funcName.starts_with("deegen_dfg_rt_wrapper_"))
        {
            fn.setDSOLocal(true);
        }
    }

    // Try to merge identical return continuations
    //
    {
        LLVMIdenticalFunctionMerger ifm;
        ifm.SetSectionPriority(InterpreterBytecodeImplCreator::x_cold_code_section_name, 100);
        ifm.SetSectionPriority(BaselineJitImplCreator::x_slowPathSectionName, 110);
        ifm.SetSectionPriority(DfgJitImplCreator::x_slowPathSectionName, 120);
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

    std::string asmFileContents = CompileLLVMModuleToAssemblyFile(module.get(), llvm::Reloc::Static, llvm::CodeModel::Small);
    asmOutFile.write(asmFileContents);
    asmOutFile.Commit();
}
