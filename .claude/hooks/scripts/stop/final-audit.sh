#!/bin/bash
# Final audit hook
# Performs final checks before session ends

set -euo pipefail

echo "🔍 Performing final audit..." >&2

# Check for uncommitted changes
if [ -d .git ]; then
  if ! git diff-index --quiet HEAD -- 2>/dev/null; then
    echo "⚠️  WARNING: Uncommitted changes detected" >&2
    echo "   Run 'git status' to see changes" >&2
  fi
fi

# Check for Vulkan validation errors in recent logs (if applicable)
if [ -d logs ]; then
  if grep -r "VALIDATION ERROR" logs/ 2>/dev/null | tail -5; then
    echo "⚠️  WARNING: Vulkan validation errors found in logs" >&2
    echo "   Review and fix validation errors before deployment" >&2
  fi
fi

# Check for TODO comments in modified files
if [ -d .git ]; then
  modified_files=$(git diff --name-only HEAD 2>/dev/null || echo "")
  if [ -n "$modified_files" ]; then
    todo_count=$(echo "$modified_files" | xargs grep -l "TODO\|FIXME\|XXX" 2>/dev/null | wc -l || echo "0")
    if [ "$todo_count" -gt 0 ]; then
      echo "ℹ️  INFO: $todo_count file(s) with TODO/FIXME comments" >&2
    fi
  fi
fi

echo "✅ Final audit complete" >&2

# Pass through empty input
echo "{}"
