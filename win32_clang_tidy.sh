#!/usr/bin/env bash
set -euo pipefail

# This script can be used to run clang-tidy on the unity build files
# It constructs the compile_commands.json automatically which clang-tidy uses
# No absolute paths have to be set in the compile_commands.json manually
# This script has to be run from the project root
# WINDOWS ONLY FOR NOW AND MUST BE RUN FROM THE ROOT

# Get absolute path of the root directory (Windows only)
ROOT="$(pwd -W)"

# Add files here (platform files in addition to the game)
SRC_FILES=(
    "src/win32_compressor.cpp"
)

# removed: /I vendor/imgui because couldn't get to suppress warnings from imgui files
COMPILER_COMMAND="clang-cl -DCOMPRESSOR_WIN32=1 -DCOMPRESSOR_DEV=1 -DCOMPRESSOR_DEBUG=1 /I src /W4 /std:c++20"

# Start building the JSON, overwriting if already existed
echo "[" > "$ROOT/compile_commands.json"

for i in "${!SRC_FILES[@]}"; do
    FILE="${SRC_FILES[i]}"

    COMMA=","
    if [[ $i -eq $((${#SRC_FILES[@]} - 1)) ]]; then
        COMMA=""
    fi

    # Append the text between EOFs
    cat >> "$ROOT/compile_commands.json" <<EOF
    {
        "directory": "$ROOT",
        "command": "$COMPILER_COMMAND $FILE",
        "file": "$FILE"
    }$COMMA
EOF

done

echo "]" >> "$ROOT/compile_commands.json"

set -x

clang-tidy -p . "${SRC_FILES[@]}"
