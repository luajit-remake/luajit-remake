#include "deegen_api.h"
#include "runtime_utils.h"

// Easy way to index tables
inline TValue IndexTable(VM* vm, HeapPtr<TableObject> tbl, std::string_view key)
{
    //TODO: many allocations for the keys, maybe better way?
    UserHeapPointer<HeapString> hs = vm->CreateStringObjectFromRawString(key.data(), static_cast<uint32_t>(key.length()));

    GetByIdICInfo icInfo;
    TableObject::PrepareGetById(tbl, hs, icInfo);
    return TableObject::GetById(tbl, hs.As<void>(), icInfo);
}

// TODO: Linker error caused on `ThrowError` call, so macro must be used
// `x = tbl[x] or error(msg)`
#define IndexValueOrError(vm, tbl, key, err) \
    ({ \
        TValue val = IndexTable(vm, tbl, key); \
        if (val.IsNil()) \
        { \
            ThrowError(err); \
        } \
        val;\
    })

#define OSResult(stat, fname) ({})

//In order to do an operation like `tbl[key] = x or default_value`
template<typename TValueType, typename T>
inline TValue IndexValueOr(VM* vm, HeapPtr<TableObject> tbl, std::string_view key, T defaultValue)
{
    TValue val = IndexTable(vm, tbl, key);
    if (val.IsNil())
    {
        return TValue::Create<TValueType>(defaultValue);
    }
    else
    {
        return val;
    }
}

template<typename TValueType, typename T>
inline void SetTableValue(VM* vm, HeapPtr<TableObject> tbl, std::string_view key, T value)
{
    UserHeapPointer<HeapString> hs = vm->CreateStringObjectFromRawString(key.data(), static_cast<uint32_t>(key.length()));

    PutByIdICInfo icInfo;
    TableObject::PreparePutById(tbl, hs, icInfo);
    TableObject::PutById(tbl, hs.As<void>(), TValue::Create<TValueType>(value), icInfo);
}

// os.clock -- https://www.lua.org/manual/5.1/manual.html#pdf-os.clock
//
// os.clock ()
// Returns an approximation of the amount in seconds of CPU time used by the program.
//
DEEGEN_DEFINE_LIB_FUNC(os_clock)
{
    // Direct rip from luajit: https://github.com/LuaJIT/LuaJIT/blob/v2.1/src/lib_os.c#L127
    Return(TValue::Create<tDouble>((clock()) * (1.0 / CLOCKS_PER_SEC)));
}

// os.date -- https://www.lua.org/manual/5.1/manual.html#pdf-os.date
//
// os.date ([format [, time]])
// Returns a string or a table containing date and time, formatted according to the given string format.
//
// If the time argument is present, this is the time to be formatted (see the os.time function for a description of this value).
// Otherwise, date formats the current time.
//
// If format starts with '!', then the date is formatted in Coordinated Universal Time. After this optional character, if format
// is the string "*t", then date returns a table with the following fields: year (four digits), month (1--12), day (1--31),
// hour (0--23), min (0--59), sec (0--61), wday (weekday, Sunday is 1), yday (day of the year), and isdst (daylight saving flag,
// a boolean).
//
// If format is not "*t", then date returns the date as a string, formatted according to the same rules as the C function strftime.
// When called without arguments, date returns a reasonable date and time representation that depends on the host system and on
// the current locale (that is, os.date() is equivalent to os.date("%c")).
//
DEEGEN_DEFINE_LIB_FUNC(os_date)
{
    VM* vm = VM::GetActiveVMForCurrentThread();

    //Could be a string literal, so has to be const
    const char *fmt;
    if (GetNumArgs() > 0)
    {
        if (unlikely(!GetArg(0).Is<tString>()))
        {
            ThrowError("bad argument #1 to 'date' (string expected)");
        }

        //m_string is a uint8_t[], it isn't the best to cast from unsigned to signed but we can assume the bytes will all be <127
        fmt = reinterpret_cast<const char *>(TranslateToRawPointer(vm, GetArg(0).As<tString>())->m_string);
    }
    else
    {
        fmt = "%c";
    }


    //2nd arg, current time used if not specified
    time_t t;
    if (GetNumArgs() > 1)
    {
        if (unlikely(!GetArg(1).Is<tInt32>()))
        {
            ThrowError("bad argument #2 to 'date' (number expected)");
        }

        t = GetArg(1).AsInt32();
    }
    else
    {
        t = time(nullptr);
    }

    struct tm *tm;
    //parsing the fmt
    if (fmt[0] == '!')
    {
        fmt++;
        tm = gmtime(&t);
    }
    else
    {
        tm = localtime(&t);
    }

    if (tm == nullptr)
    {
        Return(TValue::Create<tNil>());
    }
    else if (strcmp(fmt, "*t") == 0)
    {
        //table object length = day, month, year + optional: hour, min, sec, wday, yday, isdst, total: 9
        HeapPtr<TableObject> tbl = TableObject::CreateEmptyTableObject(vm, 9, 0);

        SetTableValue<tInt32>(vm, tbl, "day", tm->tm_mday);
        //0 indexed months and weekdays. Why, WG14?
        SetTableValue<tInt32>(vm, tbl, "month", tm->tm_mon + 1);
        // `gmtime` shows time since 1900, so add the 1900 missing years
        SetTableValue<tInt32>(vm, tbl, "year", tm->tm_year + 1900);
        SetTableValue<tInt32>(vm, tbl, "hour", tm->tm_hour);
        SetTableValue<tInt32>(vm, tbl, "min", tm->tm_min);
        SetTableValue<tInt32>(vm, tbl, "sec", tm->tm_sec);
        SetTableValue<tInt32>(vm, tbl, "wday", tm->tm_wday + 1);

        Return(TValue::Create<tTable>(tbl));
    }
    else
    {
        //calculating how much size is needed, in LuaJIT (and replicated here) this is done by iterating the format, and for every instance of `%`, adding 30
        //https://github.com/LuaJIT/LuaJIT/blob/v2.1/src/lib_os.c#L211
        size_t siz = 1;
        for (const char *fmt_ptr = fmt; *fmt; fmt++)
        {
            siz += *fmt == '%' ? 30 : 1;
        }

        auto buf = new char[siz];
        size_t len = strftime(buf, sizeof(buf), fmt, tm);
        if (len == 0) //Error
        {
            delete[] buf;
            Return(TValue::Create<tNil>());
        }
        else
        {
            auto hs = vm->CreateStringObjectFromRawString(buf, static_cast<uint32_t>(len)).As<HeapString>();
            delete[] buf;
            Return(TValue::Create<tString>(hs));
        }
    }
}

// os.difftime -- https://www.lua.org/manual/5.1/manual.html#pdf-os.difftime
//
// os.difftime (t2, t1)
// Returns the number of seconds from time t1 to time t2. In POSIX, Windows, and some other systems, this value is exactly t2-t1.
//
DEEGEN_DEFINE_LIB_FUNC(os_difftime)
{
    //On Lua 5.1 this is 0, later lua versions make the ommitiance of the 2nd arg to be an error
    double a2 = 0;
    if (GetNumArgs() > 1 && GetArg(1).IsDouble())
        a2 = GetArg(1).ViewAsDouble();

    Return(TValue::Create<tDouble>(difftime(GetArg(0).ViewAsDouble(), a2)));
}

// os.execute -- https://www.lua.org/manual/5.1/manual.html#pdf-os.execute
//
// os.execute ([command])
// This function is equivalent to the C function system. It passes command to be executed by an operating system shell. It returns
// a status code, which is system-dependent. If command is absent, then it returns nonzero if a shell is available and zero otherwise.
//
DEEGEN_DEFINE_LIB_FUNC(os_execute)
{
    VM* vm = VM::GetActiveVMForCurrentThread();
    if (unlikely(GetNumArgs()) == 0)
    {
        Return(TValue::Create<tBool>(system(nullptr)));
    }
    if (unlikely(!GetArg(0).Is<tString>()))
    {
        ThrowError("bad argument #1 to 'execute' (string expected)");
    }

    HeapString* hs = TranslateToRawPointer(vm, GetArg(0).As<tString>());
    Return(TValue::Create<tInt32>(system(reinterpret_cast<const char *>(hs->m_string))));
}

// os.exit -- https://www.lua.org/manual/5.1/manual.html#pdf-os.exit
//
// os.exit ([code])
// Calls the C function exit, with an optional code, to terminate the host program. The default value for code is the success code.
//
DEEGEN_DEFINE_LIB_FUNC(os_exit)
{
    int code = EXIT_SUCCESS;
    if (GetNumArgs() > 0)
    {
        if (unlikely(!GetArg(0).IsInt32()))
            ThrowError("bad argument #1 to 'exit' (number expected)");

        code = GetArg(0).AsInt32();
    }

    //TODO: Probably need to cleanup VM and other stuff, unless they are already set as destructors
    exit(code);
}

// os.getenv -- https://www.lua.org/manual/5.1/manual.html#pdf-os.getenv
//
// os.getenv (varname)
// Returns the value of the process environment variable varname, or nil if the variable is not defined.
//
DEEGEN_DEFINE_LIB_FUNC(os_getenv)
{
    if (unlikely(GetNumArgs() == 0))
    {
        ThrowError("bad argument #1 to 'getenv' (string expected, got no value)");
    }

    if (unlikely(!GetArg(0).Is<tString>()))
    {
        ThrowError("bad argument #1 to 'getenv' (string expected)");
    }

    VM* vm = VM::GetActiveVMForCurrentThread();

    HeapString* hs = TranslateToRawPointer(vm, GetArg(0).As<tString>());
    const char* env = getenv(reinterpret_cast<const char *>(hs->m_string));
    if (env == nullptr)
    {
        Return(TValue::Create<tNil>());
    }
    else
    {
        // the result of getenv doesn't need to be freed
        Return(TValue::Create<tString>(vm->CreateStringObjectFromRawCString(env)));
    }
}

// os.remove -- https://www.lua.org/manual/5.1/manual.html#pdf-os.remove
//
// os.remove (filename)
// Deletes the file or directory with the given name. Directories must be empty to be removed. If this function fails, it returns nil,
// plus a string describing the error.
//
DEEGEN_DEFINE_LIB_FUNC(os_remove)
{
    // ThrowError("Library function 'os.remove' is not implemented yet!");
    if (unlikely(GetNumArgs() == 0))
    {
        ThrowError("bad argument #1 to 'remove' (string expected, got no value)");
    }

    if (unlikely(!GetArg(0).Is<tString>()))
    {
        ThrowError("bad argument #1 to 'remove' (string expected)");
    }

    VM* vm = VM::GetActiveVMForCurrentThread();

    HeapString* hs = TranslateToRawPointer(vm, GetArg(0).As<tString>());

    //suprisingly a C89 function, same with rename
    int ret = remove(reinterpret_cast<char *>(hs->m_string));

    if (ret != 0)
    {
        //using the format of luaL_fileresult (as found here: https://github.com/LuaJIT/LuaJIT/blob/v2.1/src/lib_aux.c#L32)
        int en = errno;
        Return(TValue::Create<tNil>(), TValue::Create<tString>(vm->CreateStringObjectFromRawCString(strerror(en))), TValue::Create<tInt32>(en));
    }
    else
    {
        Return(TValue::Create<tBool>(true));
    }
}

// os.rename -- https://www.lua.org/manual/5.1/manual.html#pdf-os.rename
//
// os.rename (oldname, newname)
// Renames file or directory named oldname to newname. If this function fails, it returns nil, plus a string describing the error.
//
DEEGEN_DEFINE_LIB_FUNC(os_rename)
{
    if (unlikely(GetNumArgs() < 2))
    {
        ThrowError("bad argument #1 to 'rename' (string expected, got no value)");
    }

    if (unlikely(!GetArg(0).Is<tString>()))
    {
        ThrowError("bad argument #1 to 'rename' (string expected)");
    }

    if (unlikely(!GetArg(1).Is<tString>()))
    {
        ThrowError("bad argument #2 to 'rename' (string expected)");
    }

    VM* vm = VM::GetActiveVMForCurrentThread();

    HeapString* from = TranslateToRawPointer(vm, GetArg(0).As<tString>());
    HeapString* to = TranslateToRawPointer(vm, GetArg(1).As<tString>());

    int ret = rename(reinterpret_cast<const char *>(from->m_string), reinterpret_cast<char *>(to->m_string));

    if (ret != 0)
    {
        int en = errno;
        Return(TValue::Create<tNil>(), TValue::Create<tString>(vm->CreateStringObjectFromRawCString(strerror(errno))), TValue::Create<tInt32>(en));
    }
    else
    {
        Return(TValue::Create<tBool>(true));
    }
}

// os.setlocale -- https://www.lua.org/manual/5.1/manual.html#pdf-os.setlocale
//
// os.setlocale (locale [, category])
// Sets the current locale of the program. locale is a string specifying a locale; category is an optional string describing which
// category to change: "all", "collate", "ctype", "monetary", "numeric", or "time"; the default category is "all". The function returns
// the name of the new locale, or nil if the request cannot be honored.
//
// If locale is the empty string, the current locale is set to an implementation-defined native locale. If locale is the string "C",
// the current locale is set to the standard C locale.
//
// When called with nil as the first argument, this function only returns the name of the current locale for the given category.
//
DEEGEN_DEFINE_LIB_FUNC(os_setlocale)
{
    VM* vm = VM::GetActiveVMForCurrentThread();

    size_t numArgs = GetNumArgs();
    if (unlikely(numArgs == 0))
    {
        ThrowError("bad argument #1 to 'setlocale' (string expected, got no value)");
    }

    if (unlikely(!GetArg(0).Is<tString>()))
    {
        ThrowError("bad argument #1 to 'setlocale' (string expected)");
    }

    HeapString* lcl = TranslateToRawPointer(VM::GetActiveVMForCurrentThread(), GetArg(0).As<tString>());

    int opt = LC_ALL;
    if (numArgs > 1)
    {
        if (unlikely(!GetArg(1).Is<tString>()))
        {
            ThrowError("bad argument #2 to 'setlocale' (string expected)");
        }

        HeapString* catHs = TranslateToRawPointer(vm, GetArg(1).As<tString>());
        const char *cat = reinterpret_cast<char *>(catHs->m_string);
        if (strcmp(cat, "all") == 0)
        {
            opt = LC_ALL;
        }
        else if (strcmp(cat, "collate") == 0)
        {
            opt = LC_COLLATE;
        }
        else if (strcmp(cat, "ctype") == 0)
        {
            opt = LC_CTYPE;
        }
        else if (strcmp(cat, "monetary") == 0)
        {
            opt = LC_MONETARY;
        }
        else if (strcmp(cat, "numeric") == 0)
        {
            opt = LC_NUMERIC;
        }
        else if (strcmp(cat, "time") == 0)
        {
            opt = LC_TIME;
        }
        else
        {
            ThrowError("bad argument #2 to 'setlocale' (invalid category)");
        }
    }

    char *ret = setlocale(opt, reinterpret_cast<char *>(lcl->m_string));
    if (ret == nullptr)
    {
        Return(TValue::Create<tNil>());
    }
    else
    {
        Return(TValue::Create<tString>(vm->CreateStringObjectFromRawCString(ret)));
    }
}

// os.time -- https://www.lua.org/manual/5.1/manual.html#pdf-os.time
//
// os.time ([table])
// Returns the current time when called without arguments, or a time representing the date and time specified by the given table.
// This table must have fields year, month, and day, and may have fields hour, min, sec, and isdst (for a description of these fields,
// see the os.date function).
//
// The returned value is a number, whose meaning depends on your system. In POSIX, Windows, and some other systems, this number counts
// the number of seconds since some given start time (the "epoch"). In other systems, the meaning is not specified, and the number
// returned by time can be used only as an argument to date and difftime.
//
DEEGEN_DEFINE_LIB_FUNC(os_time)
{
    VM* vm = VM::GetActiveVMForCurrentThread();

    time_t t;
    if (GetNumArgs() == 0)
    {
        t = time(nullptr);
    }
    // time table
    else
    {
        if (unlikely(!GetArg(0).Is<tTable>()))
        {
            ThrowError("bad argument #1 to 'time' (table expected)");
        }

        HeapPtr<TableObject> tbl = GetArg(0).As<tTable>();

        struct tm tm;
        //day month year are manditory
        tm.tm_mday = IndexValueOrError(vm, tbl, "day", "field 'day' missing in date table").AsInt32();
        tm.tm_mon = IndexValueOrError(vm, tbl, "month", "field 'month' missing in date table").AsInt32() - 1;
        tm.tm_year = IndexValueOrError(vm, tbl, "year", "field 'year' missing in date table").AsInt32() - 1900;

        //rest are optional
        tm.tm_hour = IndexValueOr<tInt32>(vm, tbl, "hour", 12).AsInt32();
        tm.tm_min = IndexValueOr<tInt32>(vm, tbl, "min", 0).AsInt32();
        tm.tm_sec = IndexValueOr<tInt32>(vm, tbl, "sec", 0).AsInt32();
        tm.tm_isdst = IndexValueOr<tBool>(vm, tbl, "isdst", false).As<tBool>();

        t = mktime(&tm);
    }

    Return(TValue::Create<tInt32>(static_cast<int32_t>(t)));
}

// os.tmpname -- https://www.lua.org/manual/5.1/manual.html#pdf-os.tmpname
//
// os.tmpname ()
// Returns a string with a file name that can be used for a temporary file. The file must be explicitly opened before its use and
// explicitly removed when no longer needed.
// On some systems (POSIX), this function also creates a file with that name, to avoid security risks. (Someone else might create
// the file with wrong permissions in the time between getting the name and creating the file.) You still have to open the file
// to use it and to remove it (even if you do not use it).
// When possible, you may prefer to use io.tmpfile, which automatically removes the file when the program ends.
//
DEEGEN_DEFINE_LIB_FUNC(os_tmpname)
{
    VM* vm = VM::GetActiveVMForCurrentThread();

    char buf[15+1];
    strcpy(buf, "/tmp/lua_XXXXXX");
    int fp = mkstemp(buf);
    if (fp != -1)
    {
        close(fp);
    }
    else
    {
        ThrowError("unable to create temporary file");
    }


    Return(TValue::Create<tString>(vm->CreateStringObjectFromRawCString(buf)));
}

DEEGEN_END_LIB_FUNC_DEFINITIONS
