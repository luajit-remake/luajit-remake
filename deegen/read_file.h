#pragma once

#include "common.h"

inline std::string WARN_UNUSED ReadFileContentAsString(const std::string& filename)
{
    size_t size;
    char* data = nullptr;

    struct stat st;
    if (stat(filename.c_str(), &st) != 0)
    {
        fprintf(stderr, "Failed to access file '%s' for file size, errno = %d (%s)\n",
                filename.c_str(), errno, strerror(errno));
        abort();
    }
    size = static_cast<size_t>(st.st_size);

    data = new char[size + 1];

    FILE* fp = fopen(filename.c_str(), "r");
    if (fp == nullptr)
    {
        fprintf(stderr, "Failed to open file '%s' for read, errno = %d (%s)\n",
                filename.c_str(), errno, strerror(errno));
        abort();
    }

    size_t bytesRead = fread(data, sizeof(char), size, fp);
    ReleaseAssert(bytesRead == size);

    {
        // just to sanity check we have reached eof
        //
        uint8_t _c;
        ReleaseAssert(fread(&_c, sizeof(uint8_t), 1 /*numElements*/, fp) == 0);
        ReleaseAssert(feof(fp));
    }

    fclose(fp);

    std::string res = std::string(data, size);
    delete [] data;
    return res;
}
