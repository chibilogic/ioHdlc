#!/bin/bash
# Build script for ioHdlc Linux tests
# Temporarily removes ARM toolchain from PATH to avoid conflicts

# Save original PATH
ORIGINAL_PATH="$PATH"

# Remove ARM toolchain directories from PATH
CLEAN_PATH=$(echo "$PATH" | tr ':' '\n' | grep -v "ARM" | grep -v "arm-none-eabi" | tr '\n' ':')

# Export clean PATH
export PATH="$CLEAN_PATH"

# Run make with all arguments
make "$@"

# Restore original PATH
export PATH="$ORIGINAL_PATH"
