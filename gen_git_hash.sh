#!/bin/bash
OUTPUT=$1
DIRTY_FLAG="-dirty"
if [ -z "$(git status --porcelain)" ]; then 
  DIRTY_FLAG=""
fi
 
GIT_HASH=`git log -1 --format=%h`

# Write to a tmp file first
#
echo "extern const char* const x_git_commit_hash; constexpr const char* x_git_commit_hash=\"${GIT_HASH}${DIRTY_FLAG}\";" > "${OUTPUT}.tmp"

# Do not overwrite the file and change the file timestamp if the contents are identical, 
# so that we won't recompile unless truly needed
#
if [ -f "$OUTPUT" ]; then
    diff -q "${OUTPUT}.tmp" "${OUTPUT}" > /dev/null 2>&1
    if [ $? -ne 0 ]; then
    	mv "${OUTPUT}.tmp" "${OUTPUT}"
    fi
else
    mv "${OUTPUT}.tmp" "${OUTPUT}"
fi

