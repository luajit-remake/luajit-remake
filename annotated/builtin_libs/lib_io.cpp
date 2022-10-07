#include "deegen_api.h"
#include "runtime_utils.h"

// Lua standard library io.write
//
DEEGEN_DEFINE_LIB_FUNC(io_write)
{
    VM* vm = VM::GetActiveVMForCurrentThread();
    FILE* fp = vm->GetStdout();
    size_t numElementsToPrint = GetNumArgs();
    bool success = true;
    for (uint32_t i = 0; i < numElementsToPrint; i++)
    {
        TValue val = GetArg(i);
        if (val.Is<tInt32>())
        {
            char buf[x_default_tostring_buffersize_int];
            char* bufEnd = StringifyInt32UsingDefaultLuaFormattingOptions(buf /*out*/, val.As<tInt32>());
            size_t len = static_cast<size_t>(bufEnd - buf);
            size_t written = fwrite(buf, 1, len, fp);
            if (unlikely(len != written)) { success = false; break; }
        }
        else if (val.Is<tDouble>())
        {
            double dbl = val.As<tDouble>();
            char buf[x_default_tostring_buffersize_double];
            char* bufEnd = StringifyDoubleUsingDefaultLuaFormattingOptions(buf /*out*/, dbl);
            size_t len = static_cast<size_t>(bufEnd - buf);
            size_t written = fwrite(buf, 1, len, fp);
            if (unlikely(len != written)) { success = false; break; }
        }
        else if (val.Is<tString>())
        {
            HeapString* hs = TranslateToRawPointer(vm, val.As<tString>());
            size_t written = fwrite(hs->m_string, sizeof(char), hs->m_length /*length*/, fp);
            if (unlikely(hs->m_length != written)) { success = false; break; }
        }
        else
        {
            // TODO: make error message consistent with Lua
            ThrowError("bad argument to 'write' (string expected)");
        }
    }

    if (likely(success))
    {
        Return(TValue::Create<tBool>(true));
    }
    else
    {
        int err = errno;
        const char* errstr = strerror(err);
        TValue tvErrStr = TValue::CreatePointer(vm->CreateStringObjectFromRawString(errstr, static_cast<uint32_t>(strlen(errstr))));
        Return(TValue::Create<tNil>(), tvErrStr, TValue::Create<tDouble>(err));
    }
}

DEEGEN_END_LIB_FUNC_DEFINITIONS
