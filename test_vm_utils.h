#pragma once

#include "bytecode.h"

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

inline TValue WARN_UNUSED GetGlobalVariable(VM* vm, const std::string& name)
{
    HeapPtr<TableObject> globalObject = vm->GetRootGlobalObject();
    UserHeapPointer<HeapString> hs = vm->CreateStringObjectFromRawString(name.c_str(), static_cast<uint32_t>(name.length()));

    GetByIdICInfo icInfo;
    TableObject::PrepareGetById(globalObject, hs, icInfo /*out*/);
    return TableObject::GetById(globalObject, hs.As<void>(), icInfo);
}

inline TableObject* AssertAndGetTableObject(TValue t)
{
    ReleaseAssert(t.IsPointer(TValue::x_mivTag) && t.AsPointer<UserHeapGcObjectHeader>().As()->m_type == HeapEntityType::TABLE);
    return TranslateToRawPointer(t.AsPointer<TableObject>().As());
}

inline Structure* AssertAndGetStructure(TableObject* obj)
{
    ReleaseAssert(TCGet(obj->m_hiddenClass).As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure);
    return TranslateToRawPointer(TCGet(obj->m_hiddenClass).As<Structure>());
}

using StringList = std::vector<UserHeapPointer<HeapString>>;

inline StringList GetStringList(VM* vm, size_t n)
{
    std::set<std::string> used;
    StringList result;
    std::set<int64_t> ptrSet;
    for (size_t i = 0; i < n; i++)
    {
        std::string s;
        while (true)
        {
            s = "";
            size_t len = static_cast<size_t>(rand() % 20);
            for (size_t k = 0; k < len; k++) s += 'a' + rand() % 26;
            if (!used.count(s))
            {
                break;
            }
        }
        used.insert(s);
        UserHeapPointer<HeapString> ptr = vm->CreateStringObjectFromRawString(s.c_str(), static_cast<uint32_t>(s.length()));
        result.push_back(ptr);
        ReleaseAssert(!ptrSet.count(ptr.m_value));
        ptrSet.insert(ptr.m_value);
    }
    ReleaseAssert(used.size() == n && ptrSet.size() == n && result.size() == n);
    return result;
}
