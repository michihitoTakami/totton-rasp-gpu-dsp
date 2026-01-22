#!/bin/bash
# Long-running command reminder hook
# Warns before executing potentially long-running commands

set -euo pipefail

input=$(cat)

# Extract command from input
command=$(echo "$input" | jq -r '.tool_input.command // ""')

# Check for long-running commands
if echo "$command" | grep -qE '(docker build|npm install|cmake --build|cargo build)'; then
  echo "ℹ️  INFO: Executing potentially long-running command" >&2
  echo "   Command: $command" >&2
  echo "   This may take several minutes to complete." >&2
fi

# Pass through input unchanged
echo "$input"
