#include "deegen_api.h"
#include "runtime_utils.h"

// io.close -- https://www.lua.org/manual/5.1/manual.html#pdf-io.close
//
// io.close ([file])
// Equivalent to file:close(). Without a file, closes the default output file.
//
DEEGEN_DEFINE_LIB_FUNC(io_close)
{
    ThrowError("Library function 'io.close' is not implemented yet!");
}

// io.flush -- https://www.lua.org/manual/5.1/manual.html#pdf-io.flush
//
// io.flush ()
// Equivalent to file:flush over the default output file.
//
DEEGEN_DEFINE_LIB_FUNC(io_flush)
{
    ThrowError("Library function 'io.flush' is not implemented yet!");
}

// io.input -- https://www.lua.org/manual/5.1/manual.html#pdf-io.input
//
// io.input ([file])
// When called with a file name, it opens the named file (in text mode), and sets its handle as the default input file.
// When called with a file handle, it simply sets this file handle as the default input file.
// When called without parameters, it returns the current default input file.
//
// In case of errors this function raises the error, instead of returning an error code.
//
DEEGEN_DEFINE_LIB_FUNC(io_input)
{
    ThrowError("Library function 'io.input' is not implemented yet!");
}

// 'buf' must be 'limit + 1' bytes long
// If -1 is returned, it means 'limit' bytes have been read but end-of-line is still not reached.
// If -2 is returned, it means the file hit EOF without reading in any bytes
// Otherwise, returns the length of the line, minus the '\n' if it exists.
//
static size_t WARN_UNUSED TryReadLineOnce(FILE* fp, char* buf, size_t limit)
{
    // Set up a sentry value so we know if 'fgets' read exactly 'limit' bytes
    //
    buf[limit] = 1;

    // Let fgets read at most 'limit' bytes and append the '\0'
    //
    char* fgetsRet = fgets(buf, static_cast<int>(limit + 1), fp);
    if (unlikely(fgetsRet == nullptr))
    {
        // EOF without any bytes read, or error. For error just treat it as EOF for now.
        //
        return static_cast<size_t>(-2);
    }

    // If our sentry value 1 is not overwritten, it means 'fgets' stopped short before reading 'x_internalBufferSize' characters.
    // So a newline (or EOF) must have been encountered. We can quit now.
    //
    if (buf[limit] != '\0')
    {
        size_t len = strlen(buf);
        Assert(len < limit);
        if (len >= 1 && buf[len - 1] == '\n')
        {
            len--;
        }
        return len;
    }

    // Now we know fgets has read exactly 'limit' characters.
    //
    if (buf[limit - 1] == '\n')
    {
        // The line (including '\n') happens to have exactly 'limit' bytes.
        //
        return limit - 1;
    }

    // The line might have more to read.
    // (Note that it is possible that we have reached EOF here and the file doesn't end with a newline,
    // but we won't know until we call fgets again).
    //
    return static_cast<size_t>(-1);
}

static HeapPtr<HeapString> NO_INLINE ReadLinesSlowPath(FILE* fp, VM* vm, char* firstChunk, size_t firstChunkLen)
{
    // Just use a vector for simplicity now
    //
    std::vector<std::pair<const void*, size_t>> chunkList;
    chunkList.push_back(std::make_pair(firstChunk, firstChunkLen));

    constexpr size_t x_chunkSize = 65280;
    while (true)
    {
        char* buf = new char[x_chunkSize + 1];
        size_t len = TryReadLineOnce(fp, buf, x_chunkSize);
        if (len == static_cast<size_t>(-2))
        {
            // It means we reached EOF and the file did not end with a newline, we are done.
            // We didn't read in anything useful in this iteration.
            //
            chunkList.push_back(std::make_pair(buf, 0));
            break;
        }
        if (len == static_cast<size_t>(-1))
        {
            // We read in full x_chunkSize bytes but still may have more to read.
            //
            chunkList.push_back(std::make_pair(buf, x_chunkSize));
            continue;
        }

        Assert(len < x_chunkSize);
        // We found a newline or EOF, we are done.
        //
        chunkList.push_back(std::make_pair(buf, len));
        break;
    }

    HeapPtr<HeapString> result = vm->CreateStringObjectFromConcatenation(chunkList.data(), chunkList.size()).As();

    // The first element in 'chunkList' is the internal buffer, we must not free it. Free everything else.
    //
    for (size_t i = 1; i < chunkList.size(); i++)
    {
        char* ptr = const_cast<char*>(reinterpret_cast<const char*>(chunkList[i].first));
        delete [] ptr;
    }

    return result;
}

DEEGEN_DEFINE_LIB_FUNC(io_lines_iter)
{
    VM* vm = VM::GetActiveVMForCurrentThread();
    constexpr size_t x_internalBufferSize = 8192;
    char internalBuf[x_internalBufferSize + 1];
    size_t len = TryReadLineOnce(stdin, internalBuf, x_internalBufferSize);
    if (len == static_cast<size_t>(-2))
    {
        Return(TValue::Create<tNil>());
    }
    if (len != static_cast<size_t>(-1))
    {
        Assert(len < x_internalBufferSize);
        Return(TValue::Create<tString>(vm->CreateStringObjectFromRawString(internalBuf, static_cast<uint32_t>(len)).As()));
    }
    HeapPtr<HeapString> result = ReadLinesSlowPath(stdin, vm, internalBuf, x_internalBufferSize);
    Return(TValue::Create<tString>(result));
}

// io.lines -- https://www.lua.org/manual/5.1/manual.html#pdf-io.lines
//
// io.lines ([filename])
// Opens the given file name in read mode and returns an iterator function that, each time it is called, returns a new line
// from the file. Therefore, the construction
//     for line in io.lines(filename) do body end
// will iterate over all lines of the file. When the iterator function detects the end of file, it returns nil (to finish the
// loop) and automatically closes the file.
//
// The call io.lines() (with no file name) is equivalent to io.input():lines(); that is, it iterates over the lines of the
// default input file. In this case it does not close the file when the loop ends.
//
// TODO: This is a makeshift implementation. This is not standard conforming.
// Currently we are always read from stdin and does not support file input.
//
DEEGEN_DEFINE_LIB_FUNC(io_lines)
{
    if (GetNumArgs() > 0 && !GetArg(0).Is<tNil>())
    {
        ThrowError("Library function 'io.lines' with file input is not implemented yet!");
    }
    Return(VM_GetLibFunctionObject<VM::LibFn::IoLinesIter>());
}

// io.open -- https://www.lua.org/manual/5.1/manual.html#pdf-io.open
//
// io.open (filename [, mode])
// This function opens a file, in the mode specified in the string mode. It returns a new file handle, or, in case of errors,
// nil plus an error message.
//
// The mode string can be any of the following:
//     "r": read mode (the default);
//     "w": write mode;
//     "a": append mode;
//     "r+": update mode, all previous data is preserved;
//     "w+": update mode, all previous data is erased;
//     "a+": append update mode, previous data is preserved, writing is only allowed at the end of file.
// The mode string can also have a 'b' at the end, which is needed in some systems to open the file in binary mode. This string
// is exactly what is used in the standard C function fopen.
//
DEEGEN_DEFINE_LIB_FUNC(io_open)
{
    ThrowError("Library function 'io.open' is not implemented yet!");
}

// io.output -- https://www.lua.org/manual/5.1/manual.html#pdf-io.output
//
// io.output ([file])
// Similar to io.input, but operates over the default output file.
//
DEEGEN_DEFINE_LIB_FUNC(io_output)
{
    ThrowError("Library function 'io.output' is not implemented yet!");
}

// io.popen -- https://www.lua.org/manual/5.1/manual.html#pdf-io.popen
//
// io.popen (prog [, mode])
// Starts program prog in a separated process and returns a file handle that you can use to read data from this program (if mode is
// "r", the default) or to write data to this program (if mode is "w").
//
// This function is system dependent and is not available on all platforms.
//
DEEGEN_DEFINE_LIB_FUNC(io_popen)
{
    ThrowError("Library function 'io.popen' is not implemented yet!");
}

// io.read -- https://www.lua.org/manual/5.1/manual.html#pdf-io.read
//
// io.read (···)
// Equivalent to io.input():read.
//
DEEGEN_DEFINE_LIB_FUNC(io_read)
{
    ThrowError("Library function 'io.read' is not implemented yet!");
}

// io.tmpfile -- https://www.lua.org/manual/5.1/manual.html#pdf-io.tmpfile
//
// io.tmpfile ()
// Returns a handle for a temporary file. This file is opened in update mode and it is automatically removed when the program ends.
//
DEEGEN_DEFINE_LIB_FUNC(io_tmpfile)
{
    ThrowError("Library function 'io.tmpfile' is not implemented yet!");
}

// io.type -- https://www.lua.org/manual/5.1/manual.html#pdf-io.type
//
// io.type (obj)
// Checks whether obj is a valid file handle. Returns the string "file" if obj is an open file handle, "closed file" if obj is a
// closed file handle, or nil if obj is not a file handle.
//
DEEGEN_DEFINE_LIB_FUNC(io_type)
{
    ThrowError("Library function 'io.type' is not implemented yet!");
}

// io.write -- https://www.lua.org/manual/5.1/manual.html#pdf-io.write
//
// io.write (···)
// Equivalent to io.output():write.
//
// TODO: This is a makeshift implementation. This is not standard conforming.
// Currently we are always printing to stdout.
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
#if 0
        if (val.Is<tInt32>())
        {
            char buf[x_default_tostring_buffersize_int];
            char* bufEnd = StringifyInt32UsingDefaultLuaFormattingOptions(buf /*out*/, val.As<tInt32>());
            size_t len = static_cast<size_t>(bufEnd - buf);
            size_t written = fwrite(buf, 1, len, fp);
            if (unlikely(len != written)) { success = false; break; }
        }
        else
#endif
        if (val.Is<tDouble>())
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
