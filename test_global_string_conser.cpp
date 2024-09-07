#include "runtime_utils.h"
#include "gtest/gtest.h"

namespace {

void CheckStringObjectIsAsExpected(UserHeapPointer<HeapString> p, const void* expectedStr, size_t expectedLen)
{
    uint64_t expectedHash = HashString(expectedStr, expectedLen);
    HeapString* s = p.As<HeapString>();
    ReleaseAssert(s->m_type == HeapEntityType::String);
    ReleaseAssert(static_cast<size_t>(s->m_length) == expectedLen);
    ReleaseAssert(s->m_hashHigh == static_cast<uint8_t>(expectedHash >> 56));
    ReleaseAssert(s->m_hashLow == static_cast<uint32_t>(expectedHash));
    ReleaseAssert(memcmp(s->m_string, expectedStr, expectedLen) == 0);
}

TEST(GlobalStringHashConser, Sanity)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());

    UserHeapPointer<HeapString> p1 = vm->CreateStringObjectFromRawString("abc", 3);
    CheckStringObjectIsAsExpected(p1, "abc", 3);
    UserHeapPointer<HeapString> p2 = vm->CreateStringObjectFromRawString("defg", 4);
    CheckStringObjectIsAsExpected(p2, "defg", 4);
    ReleaseAssert(p1 != p2);

    UserHeapPointer<HeapString> p3 = vm->CreateStringObjectFromRawString("abc", 3);
    ReleaseAssert(p1 == p3);
    UserHeapPointer<HeapString> p4 = vm->CreateStringObjectFromRawString("defg", 4);
    ReleaseAssert(p2 == p4);

    TValue vals[2] = { TValue::CreatePointer(p1), TValue::CreatePointer(p2) };
    UserHeapPointer<HeapString> p5 = vm->CreateStringObjectFromConcatenation(vals, 2);
    CheckStringObjectIsAsExpected(p5, "abcdefg", 7);

    UserHeapPointer<HeapString> p6 = vm->CreateStringObjectFromConcatenation(p4, vals, 2);
    CheckStringObjectIsAsExpected(p6, "defgabcdefg", 11);

    UserHeapPointer<HeapString> p7 = vm->CreateStringObjectFromConcatenation(p2, vals, 2);
    ReleaseAssert(p6 == p7);
}

TEST(GlobalStringHashConser, Stress)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());

    std::map<std::string, UserHeapPointer<HeapString>> expectedMap;
    std::vector<std::string> vec;

    for (int testcase = 0; testcase < 30000; testcase++)
    {
        std::string finalString;
        UserHeapPointer<HeapString> ptr;
        if (testcase < 100 || rand() % 10 != 0)
        {
            int length = rand() % 10 + 1;
            if (rand() % 1000 == 0) { length = 0; }
            finalString = "";
            for (int i = 0; i < length; i++) finalString += char('a' + rand() % 26);

            ptr = vm->CreateStringObjectFromRawString(finalString.c_str(), static_cast<uint32_t>(finalString.length()));
        }
        else
        {
            int len = rand() % 5 + 1;
            TValue v[5];
            finalString = "";
            for (int i = 0; i < len; i++)
            {
                std::string s;
                while (true)
                {
                    size_t ord = static_cast<size_t>(rand()) % vec.size();
                    if (vec[ord].length() > 20) continue;
                    s = vec[ord];
                    break;
                }
                ReleaseAssert(expectedMap.find(s) != expectedMap.end());
                v[i] = TValue::CreatePointer(expectedMap[s]);
                finalString += s;
            }

            if (rand() % 2 == 0)
            {
                ptr = vm->CreateStringObjectFromConcatenation(v, static_cast<size_t>(len));
            }
            else
            {
                ptr = vm->CreateStringObjectFromConcatenation(UserHeapPointer<HeapString> { v[0].AsPointer().As() }, v + 1, static_cast<size_t>(len - 1));
            }
        }

        if (expectedMap.find(finalString) == expectedMap.end())
        {
            CheckStringObjectIsAsExpected(ptr, finalString.c_str(), finalString.length());
            expectedMap[finalString] = ptr;
            vec.push_back(finalString);
        }
        else
        {
            ReleaseAssert(ptr == expectedMap[finalString]);
        }
    }

    for (auto& it : expectedMap)
    {
        std::string expectedString = it.first;
        UserHeapPointer<HeapString> expectedPtr = it.second;

        UserHeapPointer<HeapString> ptr = vm->CreateStringObjectFromRawString(expectedString.c_str(), static_cast<uint32_t>(expectedString.length()));
        ReleaseAssert(expectedPtr == ptr);
        CheckStringObjectIsAsExpected(ptr, expectedString.c_str(), expectedString.length());
    }

    ReleaseAssert(vec.size() == expectedMap.size());
}

}   // anonymous namespace
