#!/bin/sh

#Check argument count
if [ $# -ne 2 ]; then
	echo "Usage: $0 <file_path> <string>"
	exit 1
fi

writefile="$1"
writestr="$2"

# Extract the directory path
dirpath=$(dirname "$writefile")
mkdir -p "$dirpath"

echo "$writestr" > "$writefile"

# Check for write failure
if [ $? -ne 0 ]; then
	echo "Error: Could not write"
	exit 1
fi

exit 0
