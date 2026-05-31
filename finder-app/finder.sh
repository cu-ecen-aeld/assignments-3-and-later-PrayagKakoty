#!/bin/sh

# Check that exactly two arguments are provided
if [ $# -ne 2 ]; then
	echo "Usage: $0: <src_dir> <string>"
	exit 1
fi

src_dir="$1"
string="$2"

# Check if src_dir exists
if [ ! -d "$src_dir" ]; then
	echo "Error: $src_dir not exist"
	exit 1
fi

num_files=$(grep -rl "$string" "$src_dir" | wc -l)
num_lines=$(grep -r "$string" "$src_dir" | wc -l)

echo "The number of files are $num_files and the number of matching lines are $num_lines"
exit 0
