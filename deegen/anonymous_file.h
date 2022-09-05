#pragma once

#include "common_utils.h"

// A wrapper to support a simple use case:
// Some writer writes into an anonymous file, and some reader reads its contents
//
class AnonymousFile
{
public:
    AnonymousFile()
    {
        m_fd = memfd_create("", 0);
        ReleaseAssert(m_fd != -1);
    }

    ~AnonymousFile()
    {
        close(m_fd);
    }

    // Return a dup of the UNIX file descriptor to access the file
    // The caller is responsible for closing the file descriptor
    // Should only be called once for the writer (who expects UNIX fd) to write into the file
    //
    int WARN_UNUSED GetUnixFd()
    {
        return dup(m_fd);
    }

    // Return a C FILE* to access the file
    // The caller is responsible for closing it
    // Should only be called once for the writer (who expects C FILE*) to write into the file
    //
    FILE* WARN_UNUSED GetFStream(const std::string& mode)
    {
        FILE* r = fdopen(GetUnixFd(), mode.c_str());
        ReleaseAssert(r != nullptr);
        return r;
    }

    // Should only be called once after the writer has finished writing and closed the file
    //
    std::string WARN_UNUSED GetFileContents()
    {
        size_t size;
        char* data = nullptr;

        int fd = GetUnixFd();

        struct stat st;
        if (fstat(fd, &st) != 0)
        {
            fprintf(stderr, "Failed to access file for file size, errno = %d (%s)\n", errno, strerror(errno));
            abort();
        }
        size = static_cast<size_t>(st.st_size);

        data = new char[size + 1];

        FILE* fp = fdopen(fd, "r"); // The responsibility to close 'fd' now goes to 'fp'
        if (fp == nullptr)
        {
            fprintf(stderr, "Failed to open file for read, errno = %d (%s)\n", errno, strerror(errno));
            abort();
        }

        rewind(fp);
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

private:
    int m_fd;
};
