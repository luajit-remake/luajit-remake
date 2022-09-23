#pragma once

#include "common.h"

// A simple wrapper that, instead of directly writing to an output file, it writes to a tmp file,
// and when the file is closed, it renames the tmp file to the true file name, giving an all-or-nothing guarantee
//
class TransactionalOutputFile
{
public:
    TransactionalOutputFile(const std::string& filename)
    {
        ReleaseAssert(filename != "");
        m_closed = false;
        m_fileName = filename;
        m_tmpName = filename + ".tmp";
        m_fp = fopen(m_tmpName.c_str(), "w");
        if (m_fp == nullptr)
        {
            fprintf(stderr, "Failed to open file '%s' for write, errno = %d (%s)\n",
                    m_tmpName.c_str(), errno, strerror(errno));
            abort();
        }
    }

    ~TransactionalOutputFile()
    {
        ReleaseAssert(m_closed);
    }

    FILE* fp() { return m_fp; }

    void write(const std::string& s)
    {
        ReleaseAssert(fwrite(s.data(), 1, s.length(), fp()) == s.length());
    }

    // Commit the transaction, the output file is overwritten with the new contents
    //
    void Commit()
    {
        ReleaseAssert(!m_closed);
        m_closed = true;
        fclose(m_fp);
        int r = rename(m_tmpName.c_str(), m_fileName.c_str());
        ReleaseAssert(r == 0 || r == -1);
        if (r == -1)
        {
            fprintf(stderr, "Failed to rename file '%s' into '%s', errno = %d (%s)\n",
                    m_tmpName.c_str(), m_fileName.c_str(), errno, strerror(errno));
            abort();
        }
    }

    // Roll back the transaction, the output file is untouched and keeps its old contents
    //
    void Rollback()
    {
        ReleaseAssert(!m_closed);
        m_closed = true;
        fclose(m_fp);
    }

private:
    FILE* m_fp;
    std::string m_tmpName;
    std::string m_fileName;
    bool m_closed;
};
