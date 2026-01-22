#!/bin/bash
# Git push review hook
# Warns before pushing to remote repository

set -euo pipefail

input=$(cat)

# Extract command from input
command=$(echo "$input" | jq -r '.tool_input.command // ""')

# Check for force push
if echo "$command" | grep -qE 'git push.*(--force|-f)'; then
  echo "⚠️  WARNING: Force push detected!" >&2
  echo "   Command: $command" >&2
  echo "   Force pushes can overwrite remote history." >&2
  echo "   Make sure this is intentional and coordinated with your team." >&2
fi

# Check for push to main/master
if echo "$command" | grep -qE 'git push.*\s(origin\s+)?(main|master)'; then
  echo "⚠️  WARNING: Pushing to main/master branch!" >&2
  echo "   Command: $command" >&2
  echo "   Direct pushes to main/master should be avoided." >&2
  echo "   Consider using a feature branch and creating a PR instead." >&2
fi

# Pass through input unchanged
echo "$input"
