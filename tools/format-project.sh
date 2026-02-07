#!/bin/sh

# Format the entire project with clang-format


# Check whether clang-format is installed
if ! command -v clang-format >/dev/null 2>&1; then
    echo "Error: clang-format is not installed or not in PATH." >&2
    exit 1
fi

# Collect source files, skipping ./build
FILES=$(find . -path ./build -prune -o \
        \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print)

# Check if formatting is needed
if clang-format --dry-run --Werror $FILES >/dev/null 2>&1; then
    echo "All files are already correctly formatted."
else
    echo "Formatting files..."
    clang-format -i $FILES
    echo "Formatting completed."
fi
