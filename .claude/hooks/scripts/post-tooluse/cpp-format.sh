#!/bin/bash
# C++ auto-format hook
# Automatically formats C++ files after editing with clang-format

set -euo pipefail

input=$(cat)

# Extract file path from input
file_path=$(echo "$input" | jq -r '.tool_input.file_path // ""')

# Skip if no file path
if [ -z "$file_path" ]; then
  echo "$input"
  exit 0
fi

# Check if file is a C++ file
if [[ "$file_path" =~ \.(cpp|cu|h|cuh|hpp)$ ]] && [ -f "$file_path" ]; then
  echo "🔧 Auto-formatting C++ file: $file_path" >&2

  # Check if clang-format is available
  if command -v clang-format &> /dev/null; then
    # Format the file in place
    clang-format -i "$file_path" 2>&1 | head -5 >&2 || true
    echo "✅ Formatted successfully" >&2
  else
    echo "⚠️  clang-format not found. Skipping auto-format." >&2
    echo "   Install with: sudo apt install clang-format" >&2
  fi
fi

# Pass through input unchanged
echo "$input"
