#!/bin/bash
# Run a CI step; on failure, its last lines become annotations (readable through the API without the logs).
#   tools/ci_step.sh TITLE command args...
title=$1; shift
log=$(mktemp)
if "$@" >"$log" 2>&1; then cat "$log"; exit 0; fi
code=$?
cat "$log"
tail -n 25 "$log" | sed 's/%/%25/g' | while IFS= read -r line; do echo "::error title=$title::$line"; done
exit $code
