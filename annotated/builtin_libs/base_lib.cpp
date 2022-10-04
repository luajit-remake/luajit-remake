#include "deegen_api.h"
#include "bytecode.h"

// Lua standard library base.print
//
DEEGEN_DEFINE_LIB_FUNC(base_print)
{
    FILE* fp = VM::GetActiveVMForCurrentThread()->GetStdout();
    size_t numArgs = GetNumArgs();
    for (size_t i = 0; i < numArgs; i++)
    {
        if (i > 0)
        {
            fprintf(fp, "\t");
        }
        PrintTValue(fp, GetArg(i));
    }
    fprintf(fp, "\n");
    Return();
}

DEEGEN_END_LIB_FUNC_DEFINITIONS
