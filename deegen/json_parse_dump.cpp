#include "json_parse_dump.h"
#include "read_file.h"

json_t WARN_UNUSED ParseJson(const std::string& contents)
{
    return json_t::parse(contents);
}

json_t WARN_UNUSED ParseJsonFromFileName(const std::string& fileName)
{
    return json_t::parse(ReadFileContentAsString(fileName));
}

std::string WARN_UNUSED SerializeJsonWithIndent(const json_t& j, int indent)
{
    return j.dump(indent);
}
