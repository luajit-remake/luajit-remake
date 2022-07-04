#!/bin/bash
rm luatests/*.lua.json
for file in `ls luatests/*.lua`
do
	echo "Generating JSON for $file..."
	./ljfrontend $file > "$file.json"
	if [ $? -ne 0 ]; then
		echo "Return value is not zero!"
		exit
	fi
done

