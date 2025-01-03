#pragma once

#include "json_utils.h"

// We compile these time-consuming functions always using -O3 to speed up build time, since we know they won't be buggy anyway
//
json_t WARN_UNUSED ParseJson(const std::string& contents);
json_t WARN_UNUSED ParseJsonFromFileName(const std::string& fileName);
std::string WARN_UNUSED SerializeJsonWithIndent(const json_t& j, int indent = -1);
