#include "fps_main.h"

#include "init_llvm_helper.h"

#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"

int main(int argc, char** argv)
{
    llvm::InitLLVM X(argc, argv);
    LLVMInitializeEverything();

    cl::HideUnrelatedOptions(FPSOptions);
    cl::ParseCommandLineOptions(argc, argv, "The Futamura projection stage of Deegen that generates the various components of the compiler.\n");

    switch (cl_mainCommand)
    {
    case BadFpsCommand:
    {
        fprintf(stderr, "Error: main command not specified, run 'fps --help' for list of options.\n");
        return 1;
    }
    case FpsCommand_GenerateInterpreterFunctionEntryLogic:
    {

        break;
    }
    }   /* switch cl_mainCommand */

    return 0;
}
