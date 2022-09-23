#include "fps_main.h"

#include "common_utils.h"

void FPS_EmitHeaderFileCommonHeader(FILE* fp)
{
    fprintf(fp, "// Generated, do not edit!\n//\n\n#pragma once\n\n");

    fprintf(fp, "#ifndef DEEGEN_POST_FUTAMURA_PROJECTION\n");
    fprintf(fp, "#error \"This file may only be included by code built after the futamura projection stage!\"\n");
    fprintf(fp, "#endif\n\n");

    fprintf(fp, "#include \"common_utils.h\"\n\n");
}

void FPS_EmitCPPFileCommonHeader(FILE* fp)
{
    fprintf(fp, "// Generated, do not edit!\n//\n\n");
    fprintf(fp, "#include \"common_utils.h\"\n\n");
}
