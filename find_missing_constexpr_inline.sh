#!/bin/bash

# If a constexpr global variable is defined in a header file and not with "inline", e.g.,
#
#     constexpr static std::array<int, 2> arr = {1,2};
#
# It will have internal linkage, thus an internal copy every time it is included in a different CPP file.
# This is usually not the desired behavior, and especially problematic if the object is large.
#
# This script finds local constant symbols which name show up multiple times in the final executable 'luajitr'.
#
# This catches the issue above, but may have false positives when there are intended uses of static const 
# variables with the same name in different translational units. The whitelist regex below removes such false positives.
#
whitelist_regex="deegen_jit_stencil_shared_constant_data_object"

# We want to match nm output lines "[ADDR] r symbolName", where "r" means local read-only symbols
# 
echo "List of read-only symbols with local linkage that showed up multiple times:"
nm luajitr | grep '^[0-9]* r' | cut -d' ' -f 3 | sort | uniq --count --repeated | grep --invert "${whitelist_regex}"

