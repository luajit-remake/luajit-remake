#pragma once

#include "tinysha1.h"
#include "common.h"

inline std::string WARN_UNUSED GetSHA1HashHex(const std::string& value)
{
    char hexresult[41];
    uint32_t digest[5];
    sha1::SHA1 s;
    s.processBytes(value.c_str(), value.size());
    s.getDigest(digest);
    snprintf(hexresult, 41, "%08x%08x%08x%08x%08x", digest[0], digest[1], digest[2], digest[3], digest[4]);
    std::string ret(hexresult);
    return ret;
}
