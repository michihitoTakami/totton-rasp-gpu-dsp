#!/bin/bash
# Documentation file restriction hook
# Warns when creating unnecessary documentation files

set -euo pipefail

input=$(cat)

# Extract file path from input
file_path=$(echo "$input" | jq -r '.tool_input.file_path // ""')

# Skip if no file path
if [ -z "$file_path" ]; then
  echo "$input"
  exit 0
fi

# Check if file is a markdown or text file
if [[ "$file_path" =~ \.(md|txt)$ ]]; then
  # Allow specific documentation files
  allowed_files=(
    "README.md"
    "CLAUDE.md"
    "AGENTS.md"
    "CHANGELOG.md"
    "LICENSE.md"
    "CONTRIBUTING.md"
  )

  # Check if file is in .claude/ directory (allowed)
  if [[ "$file_path" =~ ^\.claude/ ]]; then
    echo "$input"
    exit 0
  fi

  # Check if file is in docs/ directory (allowed)
  if [[ "$file_path" =~ ^docs/ ]]; then
    echo "$input"
    exit 0
  fi

  # Check if file is in allowed list
  filename=$(basename "$file_path")
  for allowed in "${allowed_files[@]}"; do
    if [ "$filename" = "$allowed" ]; then
      echo "$input"
      exit 0
    fi
  done

  # Warn about potentially unnecessary documentation
  echo "⚠️  WARNING: Creating documentation file outside approved locations" >&2
  echo "   File: $file_path" >&2
  echo "   Documentation files should typically be in:" >&2
  echo "   - Root directory (README.md, CLAUDE.md, etc.)" >&2
  echo "   - docs/ directory" >&2
  echo "   - .claude/ directory" >&2
  echo "   If this is intentional, you can proceed." >&2
fi

# Pass through input unchanged
echo "$input"
