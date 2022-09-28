#include "fps_main.h"
#include "transactional_output_file.h"
#include "llvm/IRReader/IRReader.h"
#include "deegen_process_bytecode_definition_for_interpreter.h"

using namespace dast;

void FPS_ProcessBytecodeDefinitionForInterpreter()
{
    using namespace llvm;
    std::unique_ptr<LLVMContext> llvmCtxHolder = std::make_unique<llvm::LLVMContext>();
    LLVMContext& ctx = *llvmCtxHolder.get();

    std::string inputFileName = cl_irInputFilename;
    ReleaseAssert(inputFileName != "");

    std::string outputFileName = cl_assemblyOutputFilename;
    ReleaseAssert(outputFileName != "");

    SMDiagnostic llvmErr;
    std::unique_ptr<Module> module = parseIRFile(inputFileName, llvmErr, ctx);
    if (module == nullptr)
    {
        fprintf(stderr, "[INTERNAL ERROR] Bitcode for %s cannot be read or parsed.\n", inputFileName.c_str());
        abort();
    }

    module = ProcessBytecodeDefinitionForInterpreter(std::move(module));

    std::string contents = CompileLLVMModuleToAssemblyFile(module.get(), llvm::Reloc::Static, llvm::CodeModel::Small);
    TransactionalOutputFile outFile(outputFileName);
    outFile.write(contents);
    outFile.Commit();
}
