#include "anonymous_file.h"
#include "deegen_options.h"
#include "misc_llvm_helper.h"
#include "llvm_override_option.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"

namespace dast {

static std::string WARN_UNUSED CompileLLVMModuleImpl(llvm::Module* module,
                                                     llvm::CodeGenFileType outFileType,
                                                     llvm::Reloc::Model relocationModel,
                                                     llvm::CodeModel::Model codeModel,
                                                     const std::function<void(llvm::TargetOptions&)>& targetOptionsTweaker)
{
    using namespace llvm;
    std::string tripleStr = module->getTargetTriple();
    Triple triple(tripleStr);
    std::string err;
    const Target* target = TargetRegistry::lookupTarget(tripleStr, err /*out*/);
    if (target == nullptr)
    {
        fprintf(stderr, "llvm::TargetRegistry::lookupTarget failed, error message: %s\n", err.c_str());
        abort();
    }

    TargetOptions targetOptions;
    if (x_isDebugBuild)
    {
        targetOptions.TrapUnreachable = true;
    }
    targetOptions.MCOptions.AsmVerbose = false;
    targetOptions.ExceptionModel = ExceptionHandling::None;

    targetOptionsTweaker(targetOptions /*inout*/);

    ValidateLLVMModule(module);

    // Figure out the target CPU and featrues string from LLVM IR
    //
    bool foundNonEmptyFunction = false;
    std::string targetCpu;
    std::string targetFeatures;
    for (Function& f : *module)
    {
        if (f.empty())
        {
            continue;
        }

        // It is a bug to see a function that did not properly set up the target CPU/features attribute.
        // We cannot fix it by adding the attributes by ourselves, as the optimization passes (which have already been run)
        // have already been negatively affected by not having properly set up those attributes.
        // So we will simply report a bug and abort here.
        //
        ReleaseAssert(f.hasFnAttribute("target-cpu"));
        ReleaseAssert(f.hasFnAttribute("target-features"));

        Attribute tmp = f.getFnAttribute("target-cpu");
        ReleaseAssert(tmp.isStringAttribute());
        std::string targetCpuForThisFunc = tmp.getValueAsString().str();

        tmp = f.getFnAttribute("target-features");
        ReleaseAssert(tmp.isStringAttribute());
        std::string targetFeaturesForThisFunc = tmp.getValueAsString().str();

        if (foundNonEmptyFunction)
        {
            ReleaseAssert(targetCpu == targetCpuForThisFunc);
            ReleaseAssert(targetFeatures == targetFeaturesForThisFunc);
        }
        else
        {
            foundNonEmptyFunction = true;
            targetCpu = targetCpuForThisFunc;
            targetFeatures = targetFeaturesForThisFunc;
        }
    }

    if (!foundNonEmptyFunction)
    {
        // It seems like this IR module doesn't contain code, so target CPU/features should not matter here.
        // Just set up a safe x86-64 CPU so LLVM is happy.
        //
        targetCpu = "x86-64";
        targetFeatures = "+cx8,+fxsr,+mmx,+sse,+sse2,+x87";
    }

    std::unique_ptr<TargetMachine> targetMachine = std::unique_ptr<TargetMachine>(target->createTargetMachine(
        tripleStr, targetCpu, targetFeatures, targetOptions, relocationModel, codeModel, CodeGenOptLevel::Aggressive));
    ReleaseAssert(targetMachine != nullptr);

    // Logic below mostly copied from llvm/tools/llc/llc.cpp
    //

    AnonymousFile outFile;

    {
        // Some craziness: the destructor of 'MCAsmStreamer' (one of the classes owned by PM)
        // calls 'flush' on the raw_fd_ostream, so 'fdStream' must outlive 'PM'
        //
        // I think this can also be solved by manually calling 'flush' after running PM (as we already does below),
        // so that (as it seems from LLVM implementaion) the flush called in the destructor will not further call
        // into the already-destructed raw_fd_ostream's flush.
        // But let's better be safe than sorry and just declare 'fdStream' first...
        //
        raw_fd_ostream fdStream(outFile.GetUnixFd(), true /*shouldClose*/);
        legacy::PassManager PM;

        // Add an appropriate TargetLibraryInfo pass for the module's triple.
        //
        TargetLibraryInfoImpl TLII(triple);
        PM.add(new TargetLibraryInfoWrapperPass(TLII));

        // This function returns false on success
        //
        ReleaseAssert(targetMachine->addPassesToEmitFile(PM, fdStream, nullptr, outFileType) == false);

        PM.run(*module);

        fdStream.flush();

        // fdStream automatically closed here when it is destructed
        //
    }

    std::string s = outFile.GetFileContents();
    return s;
}

std::string WARN_UNUSED CompileLLVMModuleToAssemblyFile(llvm::Module* module, llvm::Reloc::Model relocationModel, llvm::CodeModel::Model codeModel, const std::function<void(llvm::TargetOptions&)>& targetOptionsTweaker)
{
    ScopeOverrideLLVMOption<bool> setOptionDoNotAvoid3OpsLea("no-tuning-slow-3ops-lea", x_finetune_llvm_do_not_avoid_3_ops_lea_inst);
    return CompileLLVMModuleImpl(module, llvm::CodeGenFileType::AssemblyFile, relocationModel, codeModel, targetOptionsTweaker);
}

std::string WARN_UNUSED CompileLLVMModuleToAssemblyFile(llvm::Module* module, llvm::Reloc::Model relocationModel, llvm::CodeModel::Model codeModel)
{
    return CompileLLVMModuleToAssemblyFile(module, relocationModel, codeModel, [](llvm::TargetOptions&) { });
}

std::string WARN_UNUSED CompileLLVMModuleToAssemblyFileForStencilGeneration(llvm::Module* module, llvm::Reloc::Model relocationModel, llvm::CodeModel::Model codeModel, const std::function<void(llvm::TargetOptions&)>& targetOptionsTweaker)
{
    ScopeOverrideLLVMOption<bool> setOptionAnnotateIBRDest("add-indirect-branch-dest-annotation-for-deegen", true);
    return CompileLLVMModuleToAssemblyFile(module, relocationModel, codeModel, [&](llvm::TargetOptions& targetOptions) {
        // Required for indirect branch dest annotation
        //
        targetOptions.MCOptions.AsmVerbose = true;
        // Required for calling convention register info annotation
        //
        targetOptions.EmitCallSiteInfo = true;
        targetOptionsTweaker(targetOptions);
    });
}

std::string WARN_UNUSED CompileLLVMModuleToAssemblyFileForStencilGeneration(llvm::Module* module, llvm::Reloc::Model relocationModel, llvm::CodeModel::Model codeModel)
{
    return CompileLLVMModuleToAssemblyFileForStencilGeneration(module, relocationModel, codeModel, [](llvm::TargetOptions&) { });
}

std::string WARN_UNUSED CompileLLVMModuleToElfObjectFile(llvm::Module* module, llvm::Reloc::Model relocationModel, llvm::CodeModel::Model codeModel, const std::function<void(llvm::TargetOptions&)>& targetOptionsTweaker)
{
    ScopeOverrideLLVMOption<bool> setOptionDoNotAvoid3OpsLea("no-tuning-slow-3ops-lea", x_finetune_llvm_do_not_avoid_3_ops_lea_inst);
    return CompileLLVMModuleImpl(module, llvm::CodeGenFileType::ObjectFile, relocationModel, codeModel, targetOptionsTweaker);
}

std::string WARN_UNUSED CompileLLVMModuleToElfObjectFile(llvm::Module* module, llvm::Reloc::Model relocationModel, llvm::CodeModel::Model codeModel)
{
    return CompileLLVMModuleToElfObjectFile(module, relocationModel, codeModel, [](llvm::TargetOptions&) { });
}

llvm::object::ELFObjectFileBase* LoadElfObjectFile(llvm::LLVMContext& ctx, const std::string& fileContent)
{
    using namespace llvm;
    using namespace llvm::object;
    MemoryBufferRef mb(fileContent, "elf_object");
    Expected<std::unique_ptr<Binary>> expectedBinary = llvm::object::createBinary(mb, &ctx);
    ReleaseAssert(expectedBinary);
    Binary* bin = expectedBinary.get().release();
    ReleaseAssert(bin != nullptr);
    ELFObjectFileBase* obj = dyn_cast<ELFObjectFileBase>(bin);
    ReleaseAssert(obj != nullptr);
    return obj;
}

}   // namespace dast
