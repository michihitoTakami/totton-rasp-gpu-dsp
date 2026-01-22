#!/bin/bash
# Python auto-format hook
# Automatically formats Python files after editing with ruff

set -euo pipefail

input=$(cat)

# Extract file path from input
file_path=$(echo "$input" | jq -r '.tool_input.file_path // ""')

# Skip if no file path
if [ -z "$file_path" ]; then
  echo "$input"
  exit 0
fi

# Check if file is a Python file
if [[ "$file_path" =~ \.py$ ]] && [ -f "$file_path" ]; then
  echo "🔧 Auto-formatting Python file: $file_path" >&2

  # Check if ruff is available
  if command -v ruff &> /dev/null; then
    # Format the file
    ruff format "$file_path" 2>&1 | head -5 >&2 || true

    # Run linter with auto-fix
    ruff check --fix "$file_path" 2>&1 | head -5 >&2 || true

    echo "✅ Formatted successfully" >&2
  else
    echo "⚠️  ruff not found. Skipping auto-format." >&2
    echo "   Install with: uv tool install ruff" >&2
  fi
fi

# Pass through input unchanged
echo "$input"
