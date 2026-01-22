#!/bin/bash
# PR auto-log hook
# Logs PR creation events to a file

set -euo pipefail

input=$(cat)

# Extract command from input
command=$(echo "$input" | jq -r '.tool_input.command // ""')

# Check if command is PR creation
if echo "$command" | grep -qE 'gh pr create'; then
  # Create log directory if it doesn't exist
  mkdir -p .claude/logs

  # Log PR creation event
  log_file=".claude/logs/pr-history.log"
  timestamp=$(date -Iseconds)

  echo "[$timestamp] PR created: $command" >> "$log_file"
  echo "📝 PR creation logged to $log_file" >&2
fi

# Pass through input unchanged
echo "$input"
