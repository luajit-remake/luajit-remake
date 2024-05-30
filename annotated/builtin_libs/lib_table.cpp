#include "deegen_api.h"
#include "lualib_tonumber_util.h"
#include "runtime_utils.h"
#include "simple_string_stream.h"

// table.concat -- https://www.lua.org/manual/5.1/manual.html#pdf-table.concat
//
// table.concat (table [, sep [, i [, j]]])
// Given an array where all elements are strings or numbers, returns table[i]..sep..table[i+1] ··· sep..table[j].
// The default value for sep is the empty string, the default for i is 1, and the default for j is the length of the table.
// If i is greater than j, returns the empty string.
//
DEEGEN_DEFINE_LIB_FUNC(table_concat)
{
    size_t numArgs = GetNumArgs();
    if (unlikely(numArgs == 0))
    {
        ThrowError("bad argument #1 to 'concat' (table expected, got no value)");
    }
    if (!GetArg(0).Is<tTable>())
    {
        ThrowError("bad argument #1 to 'concat' (table expected)");
    }
    HeapPtr<TableObject> tab = GetArg(0).As<tTable>();

    VM* vm = VM::GetActiveVMForCurrentThread();

    char separatorN2SBuf[std::max(x_default_tostring_buffersize_double, x_default_tostring_buffersize_int)];
    const void* separator;
    uint32_t separatorLength;
    if (numArgs < 2 || GetArg(1).Is<tNil>())
    {
        separator = separatorN2SBuf;
        separatorLength = 0;
    }
    else
    {
        TValue tvSep = GetArg(1);
        if (tvSep.Is<tString>())
        {
            separator = TranslateToRawPointer(vm, tvSep.As<tString>()->m_string);
            separatorLength = tvSep.As<tString>()->m_length;
        }
        else if (tvSep.Is<tDouble>())
        {
            separator = separatorN2SBuf;
            separatorLength = static_cast<uint32_t>(StringifyDoubleUsingDefaultLuaFormattingOptions(separatorN2SBuf, tvSep.As<tDouble>()) - separatorN2SBuf);
        }
        else if (tvSep.Is<tInt32>())
        {
            separator = separatorN2SBuf;
            separatorLength = static_cast<uint32_t>(StringifyInt32UsingDefaultLuaFormattingOptions(separatorN2SBuf, tvSep.As<tInt32>()) - separatorN2SBuf);
        }
        else
        {
            ThrowError("bad argument #2 to 'concat' (string expected)");
        }
    }

    bool isEmptySeparator = (separatorLength == 0);

    int64_t start;
    if (numArgs < 3 || GetArg(2).Is<tNil>())
    {
        start = 1;
    }
    else
    {
        auto [success, val] = LuaLib_ToNumber(GetArg(2));
        if (unlikely(!success))
        {
            ThrowError("bad argument #3 to 'concat' (number expected)");
        }
        start = static_cast<int64_t>(val);
    }

    int64_t end;
    if (numArgs < 4 || GetArg(3).Is<tNil>())
    {
        end = TableObject::GetTableLengthWithLuaSemantics(tab);
    }
    else
    {
        auto [success, val] = LuaLib_ToNumber(GetArg(3));
        if (unlikely(!success))
        {
            ThrowError("bad argument #4 to 'concat' (number expected)");
        }
        end = static_cast<int64_t>(val);
    }

    if (start > end)
    {
        Return(TValue::Create<tString>(TranslateToHeapPtr(vm->m_emptyString)));
    }

    // Try to avoid temp buffer allocation if possible
    //
    constexpr size_t x_internalStringBufferLimit = 200;
    std::pair<const void* /*ptr*/, size_t /*len*/> internalStringBuffer[x_internalStringBufferLimit];
    std::pair<const void* /*ptr*/, size_t /*len*/>* strings = nullptr;

    // Real computation:
    //    numItems = (end - start + 1)
    //    if (!isEmptySeparator):
    //        numItems = numItems * 2 - 1
    //
    // However, directly doing the 'numItem' computation below may overflow size_t.
    // It seems like PUC Lua limits the maximum array size to about 2^27. LuaJIT has a even lower limit due to global 1GB mem limit,
    // and table.concat ignores metatable, so a too large 'numItem' is doomed to hit a non-existent key and error out.
    //
    // Furthermore, we pre-allocate a 'numItem'-sized array, which could directly result in an OOM and produce a different error from PUC Lua.
    //
    // To mimic PUC Lua behavior, we will scan for the non-existent key if 'numItem' is too large or the array allocation failed.
    //
    // P.S.
    //     It seems like PUC Lua / LuaJIT has weird behavior already (e.g., casting 'start' and 'end' to int32_t ignoring overflow).
    //     We do not attempt to mimic the behavior of that part as it seems like an unintentional bug.
    //
    size_t numItems = static_cast<size_t>(end) - static_cast<size_t>(start);
    if (numItems < (1ULL << 48))
    {
        // Cannot do +1 outside due to overflow
        //
        numItems += 1;
        if (!isEmptySeparator)
        {
            numItems = numItems * 2 - 1;
        }

        if (numItems > x_internalStringBufferLimit)
        {
            // 'strings' will be nullptr if allocation failed
            //
            strings = new (std::nothrow) std::pair<const void*, size_t>[numItems];
        }
        else
        {
            strings = internalStringBuffer;
        }
    }
    else
    {
        strings = nullptr;
    }

    // Note that if strings == nullptr, 'numItems' may not be valid!
    //
    if (unlikely(strings == nullptr))
    {
        // This table.concat cannot be accomplished due to OOM.
        // Try to produce a good error by finding a non-existent key, so we behave the same as PUC Lua.
        //
        GetByIntegerIndexICInfo info;
        TableObject::PrepareGetByIntegerIndex(tab, info /*out*/);
        for (int64_t i = start; i <= end; i++)
        {
            TValue val = TableObject::GetByIntegerIndex(tab, i, info);
            if (val.Is<tString>() || val.Is<tDouble>() || val.Is<tInt32>())
            {
                continue;
            }
            ThrowError("table contains invalid value for 'concat'");
        }

        // It seems like we really have an OOM condition
        //
        ThrowError("not enough memory");
    }

    size_t numNumbers = 0;
    {
        size_t ord = 0;
        GetByIntegerIndexICInfo info;
        TableObject::PrepareGetByIntegerIndex(tab, info /*out*/);
        for (int64_t i = start; i <= end; i++)
        {
            TValue val = TableObject::GetByIntegerIndex(tab, i, info);
            if (val.Is<tString>())
            {
                strings[ord].first = TranslateToRawPointer(vm, val.As<tString>()->m_string);
                strings[ord].second = val.As<tString>()->m_length;
            }
            else if (val.Is<tDouble>() || val.Is<tInt32>())
            {
                strings[ord].first = nullptr;
                strings[ord].second = val.m_value;
                numNumbers++;
            }
            else
            {
                if (strings != internalStringBuffer)
                {
                    delete [] strings;
                }
                ThrowError("table contains invalid value for 'concat'");
            }
            ord++;
            if (i < end && !isEmptySeparator)
            {
                strings[ord].first = separator;
                strings[ord].second = separatorLength;
                ord++;
            }
        }
        assert(ord == numItems);
    }

    // Stringify all the numbers in the table
    // Avoid creating heap objects for numbers stringified to string, as these objects won't be needed afterwards
    //
    SimpleTempStringStream tempBufferForStringifiedNumber;
    if (numNumbers > 0)
    {
        size_t spaceForOne = std::max(x_default_tostring_buffersize_double, x_default_tostring_buffersize_int);
        char* buf = tempBufferForStringifiedNumber.Reserve(spaceForOne * numNumbers);
        DEBUG_ONLY(size_t numbersFound = 0;)
        size_t gap = isEmptySeparator ? 1 : 2;
        for (size_t i = 0; i < numItems; i += gap)
        {
            if (strings[i].first == nullptr)
            {
                TValue tv; tv.m_value = strings[i].second;
                assert(tv.Is<tDouble>() || tv.Is<tInt32>());
                strings[i].first = buf;
                if (tv.Is<tDouble>())
                {
                    // note that 'newBuf' points at the '\0', same below
                    //
                    char* newBuf = StringifyDoubleUsingDefaultLuaFormattingOptions(buf, tv.As<tDouble>());
                    strings[i].second = static_cast<size_t>(newBuf - buf);
                    buf = newBuf + 1;
                }
                else
                {
                    char* newBuf = StringifyInt32UsingDefaultLuaFormattingOptions(buf, tv.As<tInt32>());
                    strings[i].second = static_cast<size_t>(newBuf - buf);
                    buf = newBuf + 1;
                }
                DEBUG_ONLY(numbersFound++;)
            }
        }
        assert(numbersFound == numNumbers);
        assert(buf <= tempBufferForStringifiedNumber.m_bufferEnd);
    }

    // Now, concat everything
    //
    HeapPtr<HeapString> result = vm->CreateStringObjectFromConcatenation(strings, numItems).As();

    if (strings != internalStringBuffer)
    {
        delete [] strings;
    }
    tempBufferForStringifiedNumber.Destroy();

    Return(TValue::Create<tString>(result));
}

// table.insert -- https://www.lua.org/manual/5.1/manual.html#pdf-table.insert
//
// table.insert (table, [pos,] value)
// Inserts element value at position pos in table, shifting up other elements to open space, if necessary.
// The default value for pos is n+1, where n is the length of the table (see §2.5.5), so that a call table.insert(t,x)
// inserts x at the end of table t.
//
DEEGEN_DEFINE_LIB_FUNC(table_insert)
{
    ThrowError("Library function 'table.insert' is not implemented yet!");
}

// table.maxn -- https://www.lua.org/manual/5.1/manual.html#pdf-table.maxn
//
// table.maxn (table)
// Returns the largest positive numerical index of the given table, or zero if the table has no positive numerical indices.
// (To do its job this function does a linear traversal of the whole table.)
//
DEEGEN_DEFINE_LIB_FUNC(table_maxn)
{
    ThrowError("Library function 'table.maxn' is not implemented yet!");
}

// table.remove -- https://www.lua.org/manual/5.1/manual.html#pdf-table.remove
//
// table.remove (table [, pos])
// Removes from table the element at position pos, shifting down other elements to close the space, if necessary.
// Returns the value of the removed element. The default value for pos is n, where n is the length of the table,
// so that a call table.remove(t) removes the last element of table t.
//
DEEGEN_DEFINE_LIB_FUNC(table_remove)
{
    ThrowError("Library function 'table.remove' is not implemented yet!");
}

// Check that the metatable for string has no __lt metamethod
//
static bool LuaLibCheckStringHasNoExoticLtMetamethod(VM* vm)
{
    if (vm->m_metatableForString.m_value == 0)
    {
        return true;
    }

    TableObject* mt = TranslateToRawPointer(vm->m_metatableForString.As<TableObject>());
    assert(mt->m_type == HeapEntityType::Table);
    if (mt->m_hiddenClass.m_value == vm->m_initialHiddenClassOfMetatableForString.m_value)
    {
        return true;
    }
    else
    {
        return GetMetamethodFromMetatable(mt, LuaMetamethodKind::Lt).Is<tNil>();
    }
}

static void LuaLibTableSortDoubleContinuousArrayNoMM(TValue* arr, size_t n)
{
#ifndef NDEBUG
    for (size_t i = 1; i <= n; i++)
    {
        assert(arr[i].Is<tDouble>());
    }
#endif
    double* arrDbl = reinterpret_cast<double*>(arr);
    std::sort(arrDbl + 1, arrDbl + n + 1);
}

static void LuaLibTableSortDoubleNonContinuousArrayNoMM(HeapPtr<TableObject> tab, size_t n)
{
    constexpr size_t internalBufSize = 500;
    double buf[internalBufSize];
    double* ptr = nullptr;
    if (n <= internalBufSize)
    {
        ptr = buf;
    }
    else
    {
        ptr = new double[n];
    }

    {
        GetByIntegerIndexICInfo info;
        TableObject::PrepareGetByIntegerIndex(tab, info /*out*/);
        for (uint32_t i = 1; i <= n; i++)
        {
            TValue tv = TableObject::GetByIntegerIndex(tab, i, info);
            assert(tv.Is<tDouble>());
            ptr[i - 1] = tv.As<tDouble>();
        }
    }

    std::sort(ptr, ptr + n);

    for (uint32_t i = 1; i <= n; i++)
    {
        TValue val = TValue::Create<tDouble>(ptr[i - 1]);
        // TODO: we could have done better here
        //
        TableObject::RawPutByValIntegerIndex(tab, i, val);
    }

    if (ptr != buf)
    {
        delete [] ptr;
    }
}

static bool LuaLibTableSortStringComparator(TValue lhs, TValue rhs)
{
    assert(lhs.Is<tString>() && rhs.Is<tString>());
    HeapPtr<HeapString> lstr = lhs.As<tString>();
    HeapPtr<HeapString> rstr = rhs.As<tString>();
    if (lstr == rstr)
    {
        return false;
    }
    VM* vm = VM::GetActiveVMForCurrentThread();
    int cmpRes = TranslateToRawPointer(vm, lstr)->Compare(TranslateToRawPointer(vm, rstr));
    return cmpRes < 0;
}

static void LuaLibTableSortStringContinuousArrayNoMM(TValue* arr, size_t n)
{
#ifndef NDEBUG
    for (size_t i = 1; i <= n; i++)
    {
        assert(arr[i].Is<tString>());
    }
#endif
    // Use stable_sort because it generally produces less comparisons than std::sort (but at the cost of more iteration work).
    // For string sorting, comparison is the expensive part.
    //
    std::stable_sort(arr + 1, arr + n + 1, LuaLibTableSortStringComparator);
}

static void LuaLibTableSortStringNonContinuousArrayNoMM(HeapPtr<TableObject> tab, size_t n)
{
    constexpr size_t internalBufSize = 500;
    TValue buf[internalBufSize];
    TValue* ptr = nullptr;
    if (n <= internalBufSize)
    {
        ptr = buf;
    }
    else
    {
        ptr = new TValue[n];
    }

    {
        GetByIntegerIndexICInfo info;
        TableObject::PrepareGetByIntegerIndex(tab, info /*out*/);
        for (uint32_t i = 1; i <= n; i++)
        {
            ptr[i - 1] = TableObject::GetByIntegerIndex(tab, i, info);
            assert(ptr[i - 1].Is<tString>());
        }
    }

    std::stable_sort(ptr, ptr + n, LuaLibTableSortStringComparator);

    for (uint32_t i = 1; i <= n; i++)
    {
        TValue val = ptr[i - 1];
        // TODO: we could have done better here
        //
        TableObject::RawPutByValIntegerIndex(tab, i, val);
    }

    if (ptr != buf)
    {
        delete [] ptr;
    }
}

// Since we support truly fully-resumable VM, we support yielding from everywhere, including comparator functions and
// metamethods called by table.sort. So we have to write quicksort into continuation-passing style (or, a state machine),
// which is what this struct does. Every step, it takes in the previous comparison result, and produces the next pair of
// values to compare, until the sorting is complete.
//
struct QuickSortStateMachine
{
    struct Result
    {
        bool finish;
        TValue lhs;
        TValue rhs;
    };

    // The pseudo-code of the quicksort we implement (in imperative style) is the following:
    //
    // function partition(A, lo, hi)
    //     pivot_ord = (hi + lo) / 2
    //     pivot = A[pivot_ord]
    //     i = lo
    //     j = hi
    //     while true:
    //         while A[i] < pivot:
    //             i += 1
    //         while pivot < A[j]:
    //             j -= 1
    //         if i >= j then return j
    //         swap(A[i], A[j])
    //         i += 1
    //         j -= 1
    //
    // function insertion_sort(A, lo, hi)
    //     i = lo + 1
    //     while i <= hi:
    //         pivot = A[i]
    //         j = i
    //         while j > lo and pivot < A[j - 1]:
    //             j -= 1
    //         // A[j..i-1] should be moved by one slot, and A[i] should take position of A[j]
    //         if j < i:
    //             tmp = A[i]
    //             for k = i - 1 downto j:
    //                 A[k+1] = A[k]
    //             A[j] = tmp
    //         i += 1
    //
    // function qsort(A, n)
    //     std::stack<std::pair<int, int>> s;
    //     s.push(<1, n>)
    //     while !s.empty():
    //         lo, hi = s.back()
    //         if hi - lo + 1 > limit_for_ins_sort:
    //             pivot = partition(A, lo, hi)
    //             s.pop()
    //             if lo < pivot - 1:
    //                 s.push(<lo, pivot - 1>)
    //             if pivot + 1 < hi:
    //                 s.push(<pivot + 1, hi>)
    //         else:
    //             insertion_sort(A, lo, hi)
    //             s.pop()
    //
    // The physical stack (the Lua stack) is arranged as follows:
    // Slot 0: the table
    // Slot 1: the comparator, or nil if operator < is used
    // Slot 2: h (int32), the height of the stack 's' in qsort function
    // Slot 3: pivot
    // Slot 4: i
    // Slot 5: j
    // Slot 6: 0 if executing the 'while A[i] < pivot' loop,
    //         1 if executing the 'while pivot < A[j]' loop,
    //         2 if executing the insertion sort loop
    // Slot [7, 7 + 2 * h): the <lo, hi> of each item in 's'
    //
    static constexpr int32_t x_limit_for_ins_sort = 8;

    static QuickSortStateMachine WARN_UNUSED Init(TValue* stackBase, HeapPtr<TableObject> tableObj, int32_t n)
    {
        assert(n >= 2);
        GetByIntegerIndexICInfo info;
        TableObject::PrepareGetByIntegerIndex(tableObj, info /*out*/);
        if (n > x_limit_for_ins_sort)
        {
            TValue pivot = TableObject::GetByIntegerIndex(tableObj, (1 + n) / 2, info);
            stackBase[7] = TValue::Create<tInt32>(1);
            stackBase[8] = TValue::Create<tInt32>(n);
            return QuickSortStateMachine {
                .sb = stackBase,
                .tab = tableObj,
                .pivot = pivot,
                .h = 1,
                .i = 1,
                .j = n,
                .state = 0,
                .info = info
            };
        }
        else
        {
            TValue pivot = TableObject::GetByIntegerIndex(tableObj, 2, info);
            stackBase[7] = TValue::Create<tInt32>(1);
            stackBase[8] = TValue::Create<tInt32>(n);
            return QuickSortStateMachine {
                .sb = stackBase,
                .tab = tableObj,
                .pivot = pivot,
                .h = 1,
                .i = 2,
                .j = 2,
                .state = 2,
                .info = info
            };
        }
    }

    void PutToStack()
    {
        TValue* s = sb;
        s[2] = TValue::Create<tInt32>(h);
        s[3] = pivot;
        s[4] = TValue::Create<tInt32>(i);
        s[5] = TValue::Create<tInt32>(j);
        s[6] = TValue::Create<tInt32>(state);
    }

    static QuickSortStateMachine WARN_UNUSED GetFromStack(TValue* stackBase)
    {
        assert(stackBase[0].Is<tTable>());
        assert(stackBase[2].Is<tInt32>() && stackBase[2].As<tInt32>() > 0);
        assert(stackBase[4].Is<tInt32>());
        assert(stackBase[5].Is<tInt32>());
        assert(stackBase[6].Is<tInt32>() && 0 <= stackBase[6].As<tInt32>() && stackBase[6].As<tInt32>() <= 2);
        HeapPtr<TableObject> tableObj = stackBase[0].As<tTable>();
        GetByIntegerIndexICInfo info;
        TableObject::PrepareGetByIntegerIndex(tableObj, info /*out*/);
        return QuickSortStateMachine {
            .sb = stackBase,
            .tab = tableObj,
            .pivot = stackBase[3],
            .h = stackBase[2].As<tInt32>(),
            .i = stackBase[4].As<tInt32>(),
            .j = stackBase[5].As<tInt32>(),
            .state = stackBase[6].As<tInt32>(),
            .info = info
        };
    }

    // Advance the state machine for the first time.
    //
    Result WARN_UNUSED InitialAdvance()
    {
        assert(state == 0 || state == 2);
        if (state == 0)
        {
            return FirstLoopCompare();
        }
        else
        {
            return ThirdLoopCompare();
        }
    }

    // Advance the state machine after the comparison complete.
    //
    Result WARN_UNUSED Advance(bool lessThanComparisonResult)
    {
        if (lessThanComparisonResult)
        {
            if (state == 0)
            {
                i += 1;
                return FirstLoopCompare();
            }
            else if (state == 1)
            {
                j -= 1;
                return SecondLoopCompare();
            }
            else
            {
                assert(state == 2);
                j -= 1;
                int32_t lo = sb[5 + 2 * h].As<tInt32>();
                assert(j >= lo);
                if (j > lo)
                {
                    return ThirdLoopCompare();
                }
                return ThirdInnerLoopFinished();
            }
        }
        else
        {
            if (state == 0)
            {
                return FirstPartitionInnerLoopFinished();
            }
            else if (state == 1)
            {
                return SecondPartitionInnerLoopFinished();
            }
            else
            {
                assert(state == 2);
                return ThirdInnerLoopFinished();
            }
        }
    }

    TValue* CallFrameEnd()
    {
        return sb + 7 + 2 * h;
    }

private:
    Result FirstLoopCompare()
    {
        assert(state == 0);
        return Result {
            .finish = false,
            .lhs = TableObject::GetByIntegerIndex(tab, i, info),
            .rhs = pivot
        };
    }

    Result SecondLoopCompare()
    {
        assert(state == 1);
        return Result {
            .finish = false,
            .lhs = pivot,
            .rhs = TableObject::GetByIntegerIndex(tab, j, info),
        };
    }

    Result ThirdLoopCompare()
    {
        assert(state == 2);
        return Result {
            .finish = false,
            .lhs = pivot,
            .rhs = TableObject::GetByIntegerIndex(tab, j - 1, info)
        };
    }

    Result FirstPartitionInnerLoopFinished()
    {
        assert(state == 0);
        state = 1;
        return SecondLoopCompare();
    }

    Result SecondPartitionInnerLoopFinished()
    {
        assert(state == 1);
        if (i >= j)
        {
            return PartitionFunctionFinished(j);
        }
        TValue ai = TableObject::GetByIntegerIndex(tab, i, info);
        TValue aj = TableObject::GetByIntegerIndex(tab, j, info);
        PutIndex(j, ai);
        PutIndex(i, aj);
        i++;
        j--;
        state = 0;
        return FirstLoopCompare();
    }

    Result ThirdInnerLoopFinished()
    {
        assert(state == 2);
        // A[j..i-1] should be moved by one slot, and A[i] should take position of A[j]
        //
        if (j < i)
        {
            TValue ai = TableObject::GetByIntegerIndex(tab, i, info);
            for (int32_t k = i - 1; k >= j; k--)
            {
                TValue ak = TableObject::GetByIntegerIndex(tab, k, info);
                PutIndex(k + 1, ak);
            }
            PutIndex(j, ai);
        }

        i += 1;
        int32_t hi = sb[6 + 2 * h].As<tInt32>();
        if (i <= hi)
        {
            pivot = TableObject::GetByIntegerIndex(tab, i, info);
            j = i;
            return ThirdLoopCompare();
        }
        else
        {
            assert(i == hi + 1);
            return InsertionSortFinished();
        }
    }

    Result PartitionFunctionFinished(int32_t pv)
    {
        assert(h > 0);
        assert(sb[5 + 2 * h].Is<tInt32>() && sb[6 + 2 * h].Is<tInt32>());
        int32_t lo = sb[5 + 2 * h].As<tInt32>();
        int32_t hi = sb[6 + 2 * h].As<tInt32>();
        assert(lo < hi);
        assert(lo <= pv && pv <= hi);
        h--;
        if (lo < pv)
        {
            h++;
            sb[5 + 2 * h] = TValue::Create<tInt32>(lo);
            sb[6 + 2 * h] = TValue::Create<tInt32>(pv);
        }
        if (pv + 1 < hi)
        {
            h++;
            sb[5 + 2 * h] = TValue::Create<tInt32>(pv + 1);
            sb[6 + 2 * h] = TValue::Create<tInt32>(hi);
        }

        return GetNextWorkItem();
    }

    Result InsertionSortFinished()
    {
        assert(state == 2);
        assert(h > 0);
        assert(sb[5 + 2 * h].Is<tInt32>() && sb[6 + 2 * h].Is<tInt32>());
        h--;
        return GetNextWorkItem();
    }

    Result GetNextWorkItem()
    {
        if (h == 0)
        {
            return Result {
                .finish = true
            };
        }

        assert(sb[5 + 2 * h].Is<tInt32>() && sb[6 + 2 * h].Is<tInt32>());
        int32_t lo = sb[5 + 2 * h].As<tInt32>();
        int32_t hi = sb[6 + 2 * h].As<tInt32>();
        assert(lo < hi);
        if (hi - lo + 1 > x_limit_for_ins_sort)
        {
            i = lo;
            j = hi;
            int32_t pivot_ord = (lo + hi) / 2;
            pivot = TableObject::GetByIntegerIndex(tab, pivot_ord, info);
            state = 0;
            return FirstLoopCompare();
        }
        else
        {
            i = lo + 1;
            j = i;
            pivot = TableObject::GetByIntegerIndex(tab, i, info);
            state = 2;
            return ThirdLoopCompare();
        }
    }

    void PutIndex(int32_t idx, TValue val)
    {
        if (unlikely(!TableObject::TryPutByValIntegerIndexFastNoIC(tab, idx, val)))
        {
            VM* vm = VM::GetActiveVMForCurrentThread();
            TableObject* obj = TranslateToRawPointer(vm, tab);
            obj->PutByIntegerIndexSlow(vm, idx, val);
            // If the user writes exotic code that changes the array in the comparator, it could result in ArrayType change.
            // Clearly this is undefined behavior, but we should not corrupt the VM. So compute GetByIntegerIndexICInfo again.
            //
            TableObject::PrepareGetByIntegerIndex(tab, info /*out*/);
        }
    }

public:
    TValue* sb;
    HeapPtr<TableObject> tab;
    TValue pivot;
    int32_t h;
    int32_t i;
    int32_t j;
    int32_t state;
    GetByIntegerIndexICInfo info;
};

enum class LessThanComparisonResult
{
    True,
    False,
    Error,
    MMCall
};

LessThanComparisonResult WARN_UNUSED LuaLibTableSortDoLessThanComparison(TValue lhs, TValue rhs, TValue& mm /*out*/)
{
    TValue metamethod;
    if (likely(lhs.Is<tHeapEntity>()))
    {
        if (unlikely(!rhs.Is<tHeapEntity>()))
        {
            return LessThanComparisonResult::Error;
        }
        HeapEntityType lhsTy = lhs.GetHeapEntityType();
        HeapEntityType rhsTy = rhs.GetHeapEntityType();
        if (unlikely(lhsTy != rhsTy))
        {
            return LessThanComparisonResult::Error;
        }

        if (lhs.Is<tTable>())
        {
            TableObject* lhsMetatable;
            {
                TableObject* tableObj = TranslateToRawPointer(lhs.As<tTable>());
                TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                if (result.m_result.m_value == 0)
                {
                    return LessThanComparisonResult::Error;
                }
                lhsMetatable = TranslateToRawPointer(result.m_result.As<TableObject>());
            }

            TableObject *rhsMetatable;
            {
                TableObject* tableObj = TranslateToRawPointer(rhs.As<tTable>());
                TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                if (result.m_result.m_value == 0)
                {
                    return LessThanComparisonResult::Error;
                }
                rhsMetatable = TranslateToRawPointer(result.m_result.As<TableObject>());
            }

            metamethod = GetMetamethodFromMetatableForComparisonOperation<false /*canQuicklyRuleOutMM*/>(lhsMetatable, rhsMetatable, LuaMetamethodKind::Lt);
            if (metamethod.IsNil())
            {
                return LessThanComparisonResult::Error;
            }

            mm = metamethod;
            return LessThanComparisonResult::MMCall;
        }

        if (lhs.Is<tString>())
        {
            HeapString* lhsString = TranslateToRawPointer(lhs.As<tString>());
            HeapString* rhsString = TranslateToRawPointer(rhs.As<tString>());
            return (lhsString->Compare(rhsString) < 0) ? LessThanComparisonResult::True : LessThanComparisonResult::False;
        }

        assert(!lhs.Is<tUserdata>() && "unimplemented");

        metamethod = GetMetamethodForValue(lhs, LuaMetamethodKind::Lt);
        if (metamethod.IsNil())
        {
            return LessThanComparisonResult::Error;
        }
        mm = metamethod;
        return LessThanComparisonResult::MMCall;
    }

    if (lhs.Is<tDouble>())
    {
        if (unlikely(!rhs.Is<tDouble>()))
        {
            return LessThanComparisonResult::Error;
        }
        return lhs.As<tDouble>() < rhs.As<tDouble>() ? LessThanComparisonResult::True : LessThanComparisonResult::False;
    }

    assert(!lhs.Is<tInt32>() && "unimplemented");

    assert(lhs.Is<tMIV>());
    if (!rhs.Is<tMIV>())
    {
        return LessThanComparisonResult::Error;
    }
    // Must be both 'nil', or both 'boolean', in order to consider metatable
    //
    if (lhs.Is<tNil>() != rhs.Is<tNil>())
    {
        return LessThanComparisonResult::Error;
    }
    metamethod = GetMetamethodForValue(lhs, LuaMetamethodKind::Lt);
    if (metamethod.IsNil())
    {
        return LessThanComparisonResult::Error;
    }
    mm = metamethod;
    return LessThanComparisonResult::MMCall;
}

DEEGEN_DEFINE_LIB_FUNC_CONTINUATION(table_sort_no_comparator_continuation)
{
    bool cmpRes = false;
    if (GetNumReturnValues() > 0)
    {
        cmpRes = GetReturnValuesBegin()[0].IsTruthy();
    }

    TValue* sb = GetStackBase();
    QuickSortStateMachine qsm = QuickSortStateMachine::GetFromStack(sb);

    TValue lhs, rhs, mm;
    while (true)
    {
        QuickSortStateMachine::Result action = qsm.Advance(cmpRes);
        if (action.finish)
        {
            Return();
        }

        // Perform the comparison
        //
        lhs = action.lhs;
        rhs = action.rhs;
        LessThanComparisonResult r = LuaLibTableSortDoLessThanComparison(lhs, rhs, mm /*out*/);
        if (unlikely(r == LessThanComparisonResult::Error))
        {
            ThrowError("table.sort: Invalid types for comparison operator");
        }

        if (r != LessThanComparisonResult::MMCall)
        {
            cmpRes = (r == LessThanComparisonResult::True);
            continue;
        }
        else
        {
            // Do metamethod call
            //
            break;
        }
    }

    qsm.PutToStack();
    if (likely(mm.Is<tFunction>()))
    {
        TValue* callFrame = qsm.CallFrameEnd();
        reinterpret_cast<void**>(callFrame)[0] = TranslateToRawPointer(mm.As<tFunction>());
        callFrame[x_numSlotsForStackFrameHeader] = lhs;
        callFrame[x_numSlotsForStackFrameHeader + 1] = rhs;
        MakeInPlaceCall(callFrame + x_numSlotsForStackFrameHeader, 2 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(table_sort_no_comparator_continuation));
    }
    else
    {
        FunctionObject* callTarget = GetCallTargetViaMetatable(mm);
        if (unlikely(callTarget == nullptr))
        {
            ThrowError(MakeErrorMessageForUnableToCall(mm));
        }
        TValue* callFrame = qsm.CallFrameEnd();
        reinterpret_cast<void**>(callFrame)[0] = callTarget;
        callFrame[x_numSlotsForStackFrameHeader] = mm;
        callFrame[x_numSlotsForStackFrameHeader + 1] = lhs;
        callFrame[x_numSlotsForStackFrameHeader + 2] = rhs;
        MakeInPlaceCall(callFrame + x_numSlotsForStackFrameHeader, 3 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(table_sort_no_comparator_continuation));
    }
}

DEEGEN_DEFINE_LIB_FUNC_CONTINUATION(table_sort_usr_comparator_continuation)
{
    bool mmRes = false;
    if (GetNumReturnValues() > 0)
    {
        mmRes = GetReturnValuesBegin()[0].IsTruthy();
    }

    TValue* sb = GetStackBase();
    QuickSortStateMachine qsm = QuickSortStateMachine::GetFromStack(sb);
    QuickSortStateMachine::Result action = qsm.Advance(mmRes);
    if (action.finish)
    {
        Return();
    }

    TValue fn = sb[1];
    assert(fn.Is<tFunction>());
    qsm.PutToStack();
    TValue* callFrame = qsm.CallFrameEnd();
    reinterpret_cast<void**>(callFrame)[0] = TranslateToRawPointer(fn.As<tFunction>());
    callFrame[x_numSlotsForStackFrameHeader] = action.lhs;
    callFrame[x_numSlotsForStackFrameHeader + 1] = action.rhs;
    MakeInPlaceCall(callFrame + x_numSlotsForStackFrameHeader, 2 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(table_sort_usr_comparator_continuation));
}

// table.sort -- https://www.lua.org/manual/5.1/manual.html#pdf-table.sort
//
// table.sort (table [, comp])
// Sorts table elements in a given order, in-place, from table[1] to table[n], where n is the length of the table.
// If comp is given, then it must be a function that receives two table elements, and returns true when the first is less
// than the second (so that not comp(a[i+1],a[i]) will be true after the sort). If comp is not given, then the standard
// Lua operator < is used instead.
//
// The sort algorithm is not stable; that is, elements considered equal by the given order may have their relative
// positions changed by the sort.
//
DEEGEN_DEFINE_LIB_FUNC(table_sort)
{
    size_t numArgs = GetNumArgs();
    if (unlikely(numArgs == 0))
    {
        ThrowError("bad argument #1 to 'sort' (table expected, got no value)");
    }
    if (unlikely(!GetArg(0).Is<tTable>()))
    {
        ThrowError("bad argument #1 to 'sort' (table expected)");
    }
    HeapPtr<TableObject> tab = GetArg(0).As<tTable>();
    VM* vm = VM::GetActiveVMForCurrentThread();

    if (numArgs > 1)
    {
        TValue cmpFn = GetArg(1);
        if (cmpFn.Is<tNil>())
        {
            goto no_usr_comparator;
        }
        if (!cmpFn.Is<tFunction>())
        {
            ThrowError("bad argument #2 to 'sort' (function expected)");
        }
        HeapPtr<FunctionObject> func = GetArg(1).As<tFunction>();

        size_t n = TableObject::GetTableLengthWithLuaSemantics(tab);
        if (n < 2)
        {
            Return();
        }

        TValue* sb = GetStackBase();
        QuickSortStateMachine qsm = QuickSortStateMachine::Init(sb, tab, static_cast<int32_t>(n));
        QuickSortStateMachine::Result action = qsm.InitialAdvance();
        assert(!action.finish);

        qsm.PutToStack();
        TValue* callFrame = qsm.CallFrameEnd();
        reinterpret_cast<uint64_t*>(callFrame)[0] = reinterpret_cast<uint64_t>(TranslateToRawPointer(func));
        callFrame[x_numSlotsForStackFrameHeader] = action.lhs;
        callFrame[x_numSlotsForStackFrameHeader + 1] = action.rhs;
        MakeInPlaceCall(callFrame + x_numSlotsForStackFrameHeader, 2 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(table_sort_usr_comparator_continuation));
    }
    else
    {
no_usr_comparator:
        uint32_t n = TableObject::GetTableLengthWithLuaSemantics(tab);
        if (n < 2)
        {
            Return();
        }

        // No function, we should use the built-in less-than comparator
        //
        // We implement fastpath for the good case: all values are double, or all values are string (and that
        // no exotic __lt metamethod exists for the double/string type)
        // In these cases, we can be certain that no metamethod will be called, so we can simply use std::sort
        //
        GetByIntegerIndexICInfo info;
        TableObject::PrepareGetByIntegerIndex(tab, info /*out*/);
        if (info.m_isContinuous)
        {
            TValue* arr = reinterpret_cast<TValue*>(tab->m_butterfly);
            if (TCGet(tab->m_arrayType).ArrayKind() == ArrayType::Kind::Double)
            {
                if (unlikely(vm->m_metatableForNumber.m_value != 0))
                {
                    goto slowpath;
                }
                LuaLibTableSortDoubleContinuousArrayNoMM(arr, n);
                Return();
            }

            if (arr[1].Is<tDouble>())
            {
                if (unlikely(vm->m_metatableForNumber.m_value != 0))
                {
                    goto slowpath;
                }
                for (size_t i = 2; i <= n; i++)
                {
                    if (unlikely(!arr[i].Is<tDouble>()))
                    {
                        goto slowpath;
                    }
                }
                LuaLibTableSortDoubleContinuousArrayNoMM(arr, n);
                Return();
            }
            else if (arr[1].Is<tString>())
            {
                if (unlikely(!LuaLibCheckStringHasNoExoticLtMetamethod(vm)))
                {
                    goto slowpath;
                }
                for (size_t i = 2; i <= n; i++)
                {
                    if (unlikely(!arr[i].Is<tString>()))
                    {
                        goto slowpath;
                    }
                }
                LuaLibTableSortStringContinuousArrayNoMM(arr, n);
                Return();
            }
            else
            {
                goto slowpath;
            }
        }
        else
        {
            TValue elementOne = TableObject::GetByIntegerIndex(tab, 1, info);
            if (elementOne.Is<tDouble>())
            {
                if (unlikely(vm->m_metatableForNumber.m_value != 0))
                {
                    goto slowpath;
                }
                for (uint32_t i = 2; i <= n; i++)
                {
                    if (unlikely(!TableObject::GetByIntegerIndex(tab, i, info).Is<tDouble>()))
                    {
                        goto slowpath;
                    }
                }
                LuaLibTableSortDoubleNonContinuousArrayNoMM(tab, n);
                Return();
            }
            else if (elementOne.Is<tString>())
            {
                if (unlikely(!LuaLibCheckStringHasNoExoticLtMetamethod(vm)))
                {
                    goto slowpath;
                }
                for (uint32_t i = 2; i <= n; i++)
                {
                    if (unlikely(!TableObject::GetByIntegerIndex(tab, i, info).Is<tString>()))
                    {
                        goto slowpath;
                    }
                }
                LuaLibTableSortStringNonContinuousArrayNoMM(tab, n);
                Return();
            }
            else
            {
                goto slowpath;
            }
        }
slowpath:
        TValue* sb = GetStackBase();
        QuickSortStateMachine qsm = QuickSortStateMachine::Init(sb, tab, static_cast<int32_t>(n));
        QuickSortStateMachine::Result action = qsm.InitialAdvance();
        assert(!action.finish);

        TValue lhs, rhs, mm;
        while (true)
        {
            // Perform the comparison
            //
            lhs = action.lhs;
            rhs = action.rhs;
            LessThanComparisonResult cmpRes = LuaLibTableSortDoLessThanComparison(lhs, rhs, mm /*out*/);
            if (unlikely(cmpRes == LessThanComparisonResult::Error))
            {
                ThrowError("table.sort: Invalid types for comparison operator");
            }

            if (cmpRes != LessThanComparisonResult::MMCall)
            {
                bool cmpIsTrue = (cmpRes == LessThanComparisonResult::True);
                action = qsm.Advance(cmpIsTrue);
                if (action.finish)
                {
                    Return();
                }
            }
            else
            {
                // Do metamethod call
                //
                break;
            }
        }

        qsm.PutToStack();
        if (likely(mm.Is<tFunction>()))
        {
            TValue* callFrame = qsm.CallFrameEnd();
            reinterpret_cast<void**>(callFrame)[0] = TranslateToRawPointer(mm.As<tFunction>());
            callFrame[x_numSlotsForStackFrameHeader] = lhs;
            callFrame[x_numSlotsForStackFrameHeader + 1] = rhs;
            MakeInPlaceCall(callFrame + x_numSlotsForStackFrameHeader, 2 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(table_sort_no_comparator_continuation));
        }
        else
        {
            FunctionObject* callTarget = GetCallTargetViaMetatable(mm);
            if (unlikely(callTarget == nullptr))
            {
                ThrowError(MakeErrorMessageForUnableToCall(mm));
            }
            TValue* callFrame = qsm.CallFrameEnd();
            reinterpret_cast<void**>(callFrame)[0] = callTarget;
            callFrame[x_numSlotsForStackFrameHeader] = mm;
            callFrame[x_numSlotsForStackFrameHeader + 1] = lhs;
            callFrame[x_numSlotsForStackFrameHeader + 2] = rhs;
            MakeInPlaceCall(callFrame + x_numSlotsForStackFrameHeader, 3 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(table_sort_no_comparator_continuation));
        }
    }
}

DEEGEN_END_LIB_FUNC_DEFINITIONS
