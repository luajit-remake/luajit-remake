#pragma once

#include "common.h"

namespace CommonUtils
{

// Error reporting utility
//
struct ErrorContext
{
    // The first parameter is the hidden 'this' pointer, so 'const char* format' is the 5th param, not 4th
    //
    __attribute__((__format__ (__printf__, 5, 6)))
    void SetError(const char *file, unsigned int line, const char *function, const char* format, ...)
    {
        if (!m_hasError)
        {
            m_hasError = true;
            m_errorFile = file;
            m_errorLine = line;
            m_errorFunction = function;
            va_list args;
            va_start(args, format);
            vsnprintf(m_errorMsg, x_max_errmsg_len, format, args);
            va_end(args);
        }
    }

    bool HasError() const { return m_hasError; }

    void PrintError() const
    {
        TestAssert(m_hasError);
        printf("%s:%u: %s: %s.\n", m_errorFile, m_errorLine, m_errorFunction, m_errorMsg);
    }

    static const size_t x_max_errmsg_len = 2000;

    bool m_hasError;
    char m_errorMsg[x_max_errmsg_len];
    const char* m_errorFile;
    unsigned int m_errorLine;
    const char* m_errorFunction;
};

inline thread_local ErrorContext* thread_errorContext = nullptr;

class AutoThreadErrorContext
{
public:
    AutoThreadErrorContext()
    {
        TestAssert(thread_errorContext == nullptr);
        m_contextPtr = new ErrorContext();
        ReleaseAssert(m_contextPtr != nullptr);
        thread_errorContext = m_contextPtr;
    }

    ~AutoThreadErrorContext()
    {
        TestAssert(thread_errorContext == m_contextPtr);
        delete m_contextPtr;
        thread_errorContext = nullptr;
    }

private:
    ErrorContext* m_contextPtr;
};
#define AutoThreadErrorContext(...) static_assert(false, "Wrong use of 'auto'-pattern!");

#define REPORT_ERR(...) \
    thread_errorContext->SetError(__FILE__, __LINE__, __extension__ __PRETTY_FUNCTION__, __VA_ARGS__)

#define CHECK_REPORT_ERR(expr, ...) do { \
    if (unlikely(!(expr))) { \
        thread_errorContext->SetError(__FILE__, __LINE__, __extension__ __PRETTY_FUNCTION__, __VA_ARGS__); \
        return FalseOrNullptr(); \
    } \
} while (false)

#define REPORT_BUG(...) do { \
    TestAssert(false);  \
    thread_errorContext->SetError(__FILE__, __LINE__, __extension__ __PRETTY_FUNCTION__, __VA_ARGS__); \
} while (false)

#define CHECK_REPORT_BUG(expr, ...) do { \
    if (unlikely(!(expr))) { \
        TestAssert(false);  \
        thread_errorContext->SetError(__FILE__, __LINE__, __extension__ __PRETTY_FUNCTION__, __VA_ARGS__); \
        return FalseOrNullptr(); \
    } \
} while (false)

}   // namespace CommonUtils
