#pragma once

#include "common.h"

// Check if the file matches the md5 checksum
// Returns true if the file is **unchanged**
//
inline bool CheckMd5Match(const std::string& file)
{
    std::string md5file = file + ".md5";
    std::string md5tmpfile = file + ".md5.tmp";

    std::string cmd = std::string("md5sum ") + file.c_str() + std::string(" > ") + md5tmpfile.c_str();

    {
        struct stat st;
        if (stat(file.c_str(), &st) != 0)
        {
            fprintf(stderr, "Failed to access file '%s', errno = %d (%s)\n",
                    file.c_str(), errno, strerror(errno));
            abort();
        }
    }

    int r = system(cmd.c_str());
    if (r != 0)
    {
        fprintf(stderr, "Command '%s' failed with return value %d\n", cmd.c_str(), r);
        abort();
    }

    std::string newMd5;
    static char buf[1000000];
    {
        FILE* fp = fopen(md5tmpfile.c_str(), "r");
        if (fp == nullptr)
        {
            fprintf(stderr, "Failed to open file '%s' for read, errno = %d (%s)\n",
                    md5tmpfile.c_str(), errno, strerror(errno));
            abort();
        }
        Auto(fclose(fp));

        ReleaseAssert(fscanf(fp, "%s", buf) == 1);
        newMd5 = buf;
        ReleaseAssert(newMd5.length() == 32);
    }

    std::string oldMd5;
    {
        struct stat st;
        if (stat(md5file.c_str(), &st) != 0)
        {
            // If there is an error, the error must that the file does not exist
            //
            ReleaseAssert(errno == ENOENT);
            return false;
        }

        FILE* fp = fopen(md5file.c_str(), "r");
        if (fp == nullptr)
        {
            fprintf(stderr, "Failed to open file '%s' for read, errno = %d (%s)\n",
                    md5file.c_str(), errno, strerror(errno));
            abort();
        }
        Auto(fclose(fp));

        ReleaseAssert(fscanf(fp, "%s", buf) == 1);
        oldMd5 = buf;
        ReleaseAssert(oldMd5.length() == 32);
    }
    return newMd5 == oldMd5;
}

// Update the md5checksum, must be called after CheckMd5Match()
//
inline void UpdateMd5Checksum(const std::string& file)
{
    std::string md5file = file + ".md5";
    std::string md5tmpfile = file + ".md5.tmp";

    int r = rename(md5tmpfile.c_str(), md5file.c_str());
    ReleaseAssert(r == 0 || r == -1);
    if (r == -1)
    {
        fprintf(stderr, "Failed to rename file '%s' into '%s', errno = %d (%s)\n",
                md5tmpfile.c_str(), md5file.c_str(), errno, strerror(errno));
        abort();
    }
}
