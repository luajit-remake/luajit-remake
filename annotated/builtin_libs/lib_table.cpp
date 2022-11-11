#include "deegen_api.h"
#include "runtime_utils.h"

// table.concat -- https://www.lua.org/manual/5.1/manual.html#pdf-table.concat
//
// table.concat (table [, sep [, i [, j]]])
// Given an array where all elements are strings or numbers, returns table[i]..sep..table[i+1] ··· sep..table[j].
// The default value for sep is the empty string, the default for i is 1, and the default for j is the length of the table.
// If i is greater than j, returns the empty string.
//
DEEGEN_DEFINE_LIB_FUNC(table_concat)
{
    ThrowError("Library function 'table.concat' is not implemented yet!");
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
    ThrowError("Library function 'table.sort' is not implemented yet!");
}

DEEGEN_END_LIB_FUNC_DEFINITIONS
