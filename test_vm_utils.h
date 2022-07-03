#pragma once

#include "bytecode.h"

namespace ToyLang
{

class VMOutputInterceptor
{
public:
    VMOutputInterceptor(VM* vm)
    {
        m_vm = vm;
        Reset(0);
        Reset(1);
    }

    ~VMOutputInterceptor()
    {
        m_vm->RedirectStdout(stdout);
        m_vm->RedirectStderr(stderr);
        fclose(m_fp[0]);
        fclose(m_fp[1]);
        close(m_fd[0]);
        close(m_fd[1]);
    }

    std::string GetAndResetStdOut() { return GetAndReset(0); }
    std::string GetAndResetStdErr() { return GetAndReset(1); }

private:
    void Reset(uint32_t ord)
    {
        ReleaseAssert(ord < 2);
        int fd = memfd_create("", 0);
        ReleaseAssert(fd != -1);

        m_fd[ord] = dup(fd);    // fdopen moves ownership of fd, so we must duplicate fd
        ReleaseAssert(m_fd[ord] != -1);

        m_fp[ord] = fdopen(fd, "w");
        ReleaseAssert(m_fp[ord] != nullptr);

        if (ord == 0)
        {
            m_vm->RedirectStdout(m_fp[ord]);
        }
        else
        {
            m_vm->RedirectStderr(m_fp[ord]);
        }
    }

    std::string GetAndReset(uint32_t ord)
    {
        ReleaseAssert(ord < 2);
        FILE* oldFp = m_fp[ord];
        int fd = m_fd[ord];
        Reset(ord);
        fclose(oldFp);

        FILE* fp = fdopen(fd, "rb");
        ReleaseAssert(fp != nullptr);

        ReleaseAssert(fseek(fp, 0, SEEK_END) == 0);
        ssize_t ssz = ftell(fp);
        ReleaseAssert(ssz >= 0);
        size_t sz = static_cast<size_t>(ssz);

        rewind(fp);
        std::string s;
        s.resize(sz, 0);
        ReleaseAssert(fread(s.data(), sizeof(char), sz, fp) == sz);
        fclose(fp);

        return s;
    }

    VM* m_vm;
    int m_fd[2];
    FILE* m_fp[2];
};

}   // namespace ToyLang
