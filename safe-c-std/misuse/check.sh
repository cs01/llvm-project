#!/bin/bash
# Misuse of the wrappers: each is rejected, stopped, or harmless (never a HOLE).
H=$(cd "$(dirname "$0")" && pwd); export CC_SAFEC=${SAFEC:-$CC_SAFEC}; [ -n "$CC_SAFEC" ] || unset CC_SAFEC
out=$(bash "$H/../../clang/utils/safe-c/holes/blind3/judge.sh" "$H"/*.c 2>/dev/null </dev/null)
echo "$out" | awk '{print $1}' | sort | uniq -c
[ $(echo "$out" | grep -c "^REJECT\|^STOPPED") -gt 30 ] && [ $(echo "$out" | grep -c "^ACCEPT-OK") -ge 5 ] && ! echo "$out" | grep -E "^(HOLE|CHEAT|CRASH|NOASAN)"
