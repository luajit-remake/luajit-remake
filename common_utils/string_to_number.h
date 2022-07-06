#include "common.h"

namespace ToyLang
{

/* Returned format. */
// DEVNOTE: Don't touch this enum! LuaJIT has fragile code that relies on the ordering of this enum...
//
enum StrScanFmt
{
    STRSCAN_ERROR,
    STRSCAN_NUM,
    STRSCAN_IMAG,
    STRSCAN_INT,
    STRSCAN_U32,
    STRSCAN_I64,
    STRSCAN_U64,
} ;

// x64 cc can return two registers, so returning this struct is fine
//
struct StrScanResult
{
    StrScanFmt fmt;
    union {
        int32_t i32;
        uint64_t u64;
        double d;
    };
};
static_assert(sizeof(StrScanResult) == 16);

// The returned 'fmt' must be STRSCAN_ERROR or STRSCAN_NUM
//
StrScanResult WARN_UNUSED TryConvertStringToDoubleWithLuaSemantics(const void* str, size_t len);

// The returned 'fmt' must be STRSCAN_ERROR or STRSCAN_NUM or STRSCAN_INT
//
StrScanResult WARN_UNUSED TryConvertStringToDoubleOrInt32WithLuaSemantics(const void* str, size_t len);

}   // namespace ToyLang
