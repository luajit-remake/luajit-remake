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
        FPS_GenerateInterpreterFunctionEntryLogic();
        break;
    }
    case FpsCommand_ProcessUserBuiltinLib:
    {
        FPS_ProcessUserBuiltinLib();
        break;
    }
    case FpsCommand_ProcessBytecodeDefinitionForInterpreter:
    {
        FPS_ProcessBytecodeDefinitionForInterpreter();
        break;
    }
    case FpsCommand_GenerateBytecodeBuilderApiHeader:
    {
        FPS_GenerateBytecodeBuilderAPIHeader();
        break;
    }
    case FpsCommand_ProcessBytecodeDefinitionForBaselineJit:
    {
        FPS_ProcessBytecodeDefinitionForBaselineJit();
        break;
    }
    case FpsCommand_GenerateBaselineJitDispatchAndBytecodeTraitTable:
    {
        FPS_GenerateDispatchTableAndBytecodeTraitTableForBaselineJit();
        break;
    }
    case FpsCommand_GenerateBaselineJitFunctionEntryLogic:
    {
        FPS_GenerateJitFunctionEntryLogic(true /*forBaselineJIT*/);
        break;
    }
    case FpsCommand_GenerateBytecodeOpcodeTraitTable:
    {
        FPS_GenerateBytecodeOpcodeTraitTable();
        break;
    }
    case FpsCommand_GenerateDfgJitSpecializedBytecodeInfo:
    {
        FPS_GenerateDfgSpecializedBytecodeInfo();
        break;
    }
    case FpsCommand_GenerateDfgJitBytecodeInfoApiHeader:
    {
        FPS_GenerateDfgBytecodeInfoApiHeader();
        break;
    }
    case FpsCommand_ProcessDfgJitBuiltinNodes:
    {
        FPS_ProcessDfgBuiltinNodes();
        break;
    }
    case FpsCommand_GenerateDfgJitCCallWrapperStubs:
    {
        Fps_GenerateDfgJitCCallWrapperStubs();
        break;
    }
    case FpsCommand_GenerateDfgJitFunctionEntryLogic:
    {
        FPS_GenerateJitFunctionEntryLogic(false /*forBaselineJIT*/);
        break;
    }
    case FpsCommand_PostProcessLinkImplementations:
    {
        Fps_PostProcessLinkImplementations();
        break;
    }
    }   /* switch cl_mainCommand */

    return 0;
}
