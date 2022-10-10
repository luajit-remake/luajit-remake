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

std::vector<std::string> WARN_UNUSED ParseCommaSeparatedFileList(const std::string& commaSeparatedFiles)
{
    if (commaSeparatedFiles == "")
    {
        return {};
    }
    std::vector<std::string> out;
    size_t curPos = 0;
    while (true)
    {
        size_t nextPos = commaSeparatedFiles.find(",", curPos);
        if (nextPos == std::string::npos)
        {
            ReleaseAssert(curPos < commaSeparatedFiles.length());
            out.push_back(commaSeparatedFiles.substr(curPos));
            break;
        }
        ReleaseAssert(curPos < nextPos);
        out.push_back(commaSeparatedFiles.substr(curPos, nextPos - curPos));
        curPos = nextPos + 1;
    }
    return out;
}

std::string WARN_UNUSED FPS_GetAuditFilePath(const std::string& filename)
{
    std::string auditDirPath = cl_auditDirPath;
    ReleaseAssert(auditDirPath != "");
    if (auditDirPath.ends_with("/"))
    {
        auditDirPath = auditDirPath.substr(0, auditDirPath.length() - 1);
    }
    ReleaseAssert(auditDirPath != "");

    {
        int status = mkdir(auditDirPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        if (status != 0)
        {
            int err = errno;
            if (err != EEXIST)
            {
                fprintf(stderr, "Failed to create audit file directory '%s', error = %d (%s)\n",
                        auditDirPath.c_str(), err, strerror(err));
                abort();
            }
        }
    }

    return auditDirPath + "/" + filename;
}
