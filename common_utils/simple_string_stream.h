#pragma once

#include "common.h"

// A simple stream
// One must call Destroy() manually to free the stream.
// This is not done in the destructor due to a limitation of our framework (since our
// framework use NO_RETURN function calls to model tail dispatches, destructors of the
// currently alive local variables won't run, so having non-trivial destructor can be fragile.)
//
// TODO: this file should be moved to the 'runtime' folder
//
class SimpleTempStringStream
{
    MAKE_NONCOPYABLE(SimpleTempStringStream);
    MAKE_NONMOVABLE(SimpleTempStringStream);
public:
    SimpleTempStringStream()
    {
        m_bufferBegin = m_internalBuffer;
        m_bufferCur = m_bufferBegin;
        m_bufferEnd = m_bufferBegin + x_internalBufferSize;
    }

    void Destroy()
    {
        if (unlikely(m_bufferBegin != m_internalBuffer))
        {
            delete [] m_bufferBegin;
            m_bufferBegin = m_internalBuffer;
            m_bufferCur = m_bufferBegin;
            m_bufferEnd = m_bufferBegin + x_internalBufferSize;
        }
    }

    void Update(char* newCurPtr)
    {
        assert(m_bufferCur <= newCurPtr && newCurPtr <= m_bufferEnd);
        m_bufferCur = newCurPtr;
    }

    char* Reserve(size_t size)
    {
        if (likely(m_bufferEnd - m_bufferCur >= static_cast<ssize_t>(size)))
        {
            return m_bufferCur;
        }
        size_t cap = static_cast<size_t>(m_bufferEnd - m_bufferBegin);
        size_t current = static_cast<size_t>(m_bufferCur - m_bufferBegin);
        size_t needed = current + size;
        while (cap < needed) { cap *= 2; }
        char* newArray = new char[cap];
        memcpy(newArray, m_bufferBegin, current);
        if (m_bufferBegin != m_internalBuffer)
        {
            delete [] m_bufferBegin;
        }
        m_bufferBegin = newArray;
        m_bufferCur = newArray + current;
        m_bufferEnd = newArray + cap;
        return m_bufferCur;
    }

    char* Begin()
    {
        return m_bufferBegin;
    }

    size_t Len()
    {
        assert(m_bufferCur >= m_bufferBegin);
        return static_cast<size_t>(m_bufferCur - m_bufferBegin);
    }

    void Clear()
    {
        m_bufferCur = m_bufferBegin;
    }

    char* m_bufferBegin;
    char* m_bufferCur;
    char* m_bufferEnd;

    static constexpr size_t x_internalBufferSize = 2000;
    char m_internalBuffer[x_internalBufferSize];
};
