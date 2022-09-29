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

std::vector<std::string> WARN_UNUSED ParseSemicolonSeparatedFileList(const std::string& semicolonSeparatedFiles)
{
    if (semicolonSeparatedFiles == "")
    {
        return {};
    }
    std::vector<std::string> out;
    size_t curPos = 0;
    while (true)
    {
        size_t nextPos = semicolonSeparatedFiles.find(";", curPos);
        if (nextPos == std::string::npos)
        {
            ReleaseAssert(curPos < semicolonSeparatedFiles.length());
            out.push_back(semicolonSeparatedFiles.substr(curPos));
            break;
        }
        ReleaseAssert(curPos < nextPos);
        out.push_back(semicolonSeparatedFiles.substr(curPos, nextPos - curPos));
        curPos = nextPos + 1;
    }
    return out;
}

