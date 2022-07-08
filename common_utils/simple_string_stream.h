#pragma once

#include "common.h"

namespace ToyLang
{

// A simple stream that destroys the buffer on class destruction
//
class SimpleTempStringStream
{
public:
    SimpleTempStringStream()
    {
        constexpr size_t initialSize = 128;
        m_bufferBegin = new uint8_t[initialSize];
        m_bufferCur = m_bufferBegin;
        m_bufferEnd = m_bufferBegin + initialSize;
    }

    ~SimpleTempStringStream()
    {
        if (m_bufferBegin != nullptr)
        {
            delete [] m_bufferBegin;
            m_bufferBegin = nullptr;
        }
    }

    uint8_t* Reserve(size_t size)
    {
        if (likely(m_bufferEnd - m_bufferCur >= static_cast<ssize_t>(size)))
        {
            return m_bufferCur;
        }
        size_t cap = static_cast<size_t>(m_bufferEnd - m_bufferBegin);
        size_t current = static_cast<size_t>(m_bufferCur - m_bufferBegin);
        size_t needed = current + size;
        while (cap < needed) { cap *= 2; }
        uint8_t* newArray = new uint8_t[cap];
        memcpy(newArray, m_bufferBegin, current);
        delete [] m_bufferBegin;
        m_bufferBegin = newArray;
        m_bufferCur = newArray + current;
        m_bufferEnd = newArray + cap;
        return m_bufferCur;
    }

    uint8_t* m_bufferBegin;
    uint8_t* m_bufferCur;
    uint8_t* m_bufferEnd;
};

}   // namespace ToyLang
