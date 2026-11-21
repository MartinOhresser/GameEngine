#!/usr/bin/env bash
# Runs a command again when it fails, for the steps of the CI that depend on a server that
# sometimes says "too many requests" (Maven Central) or "unavailable" (apt mirrors).
#
# Usage: scripts/ci_retry.sh ATTEMPTS PAUSE_SECONDS COMMAND [ARGS...]
# The pause grows with each attempt (PAUSE, 2 x PAUSE, ...).
set -u

attempts="$1"
pause="$2"
shift 2

for attempt in $(seq 1 "$attempts"); do
  if "$@"; then
    exit 0
  fi
  if [ "$attempt" -lt "$attempts" ]; then
    wait=$((pause * attempt))
    echo "ci_retry: attempt $attempt of $attempts failed, trying again in ${wait} s" >&2
    sleep "$wait"
  fi
done
echo "ci_retry: still failing after $attempts attempts" >&2
exit 1
