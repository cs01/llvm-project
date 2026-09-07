#!/bin/sh
# Race every installed solver on the same goto binary and keep the first answer.
#
# COST.md measures up to 20x between the built-in SAT path and an SMT solver,
# in EITHER direction depending on whether the buffer extents are symbolic. That
# makes a static preference a coin flip, and a coin flip on a job that can run
# for an hour. Racing costs cores, which are cheap here, instead of wall time,
# which is not.
#
#   ./solve.sh [-t SECONDS] <goto-binary> [cbmc flags...]
#
# Exits with the winning cbmc's status (0 proved, 10 property failed), or 124 if
# every solver hit the deadline.
set -u
TIMEOUT=1800
while [ $# -gt 0 ]; do
  case "$1" in
    -t) TIMEOUT=$2; shift 2 ;;
    *)  break ;;
  esac
done
[ $# -ge 1 ] || { echo "usage: solve.sh [-t SECONDS] <goto-binary> [cbmc flags...]" >&2; exit 2; }
GOTO=$1; shift

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# The built-in path is bit-blast + MiniSat; it is named here so the log says
# which one won rather than "default".
CANDIDATES="sat:"
command -v z3       >/dev/null 2>&1 && CANDIDATES="$CANDIDATES z3:--z3"
command -v bitwuzla >/dev/null 2>&1 && CANDIDATES="$CANDIDATES bitwuzla:--bitwuzla"
command -v cvc5     >/dev/null 2>&1 && CANDIDATES="$CANDIDATES cvc5:--cvc5"

START=$(date +%s)
for C in $CANDIDATES; do
  NAME=${C%%:*}; FLAG=${C#*:}
  # shellcheck disable=SC2086
  ( cbmc "$GOTO" $FLAG "$@" > "$WORK/$NAME.log" 2>&1; echo $? > "$WORK/$NAME.rc" ) &
  echo "$!" > "$WORK/$NAME.pid"
done

# Poll rather than `wait -n`: this has to run under /bin/sh, and a loser that
# exits 6 (front-end error) must not be mistaken for the winner.
WINNER=""; RC=124
while [ $(( $(date +%s) - START )) -lt "$TIMEOUT" ]; do
  for C in $CANDIDATES; do
    NAME=${C%%:*}
    [ -f "$WORK/$NAME.rc" ] || continue
    R=$(cat "$WORK/$NAME.rc")
    # A solver that left properties UNKNOWN has not answered, and can still
    # exit 10. Taking that for a verdict is the worst failure this script could
    # have -- a homemade bitwuzla wrapper produced exactly it, twice -- so an
    # undecided run loses the race like any other.
    if grep -q ': UNKNOWN' "$WORK/$NAME.log" 2>/dev/null; then
      continue
    fi
    case "$R" in
      0|10) WINNER=$NAME; RC=$R; break ;;
      *) ;;   # front-end or solver error: let the others keep running
    esac
  done
  [ -n "$WINNER" ] && break
  # Every solver died without a verdict -- report rather than spin to deadline.
  DONE=$(ls "$WORK"/*.rc 2>/dev/null | wc -l | tr -d ' ')
  N=$(echo "$CANDIDATES" | wc -w | tr -d ' ')
  [ "$DONE" -ge "$N" ] && break
  sleep 2
done
ELAPSED=$(( $(date +%s) - START ))

for C in $CANDIDATES; do
  NAME=${C%%:*}
  [ "$NAME" = "$WINNER" ] && continue
  kill "$(cat "$WORK/$NAME.pid")" 2>/dev/null
  pkill -P "$(cat "$WORK/$NAME.pid")" 2>/dev/null
done

if [ -n "$WINNER" ]; then
  cat "$WORK/$WINNER.log"
  echo "== solved by $WINNER in ${ELAPSED}s (rc $RC)"
  # Set SOLVE_LOG to accumulate evidence for COST.md's rule instead of leaving
  # it a claim derived from two harnesses.
  [ -n "${SOLVE_LOG:-}" ] &&
    printf '%s\t%s\t%s\t%s\n' "$(date -u +%FT%TZ)" "$(basename "$GOTO")" \
           "$WINNER" "$ELAPSED" >> "$SOLVE_LOG"
  exit "$RC"
fi

# Nobody finished. Which phase each one reached says whether the solver was even
# the problem: a run still inside symbolic execution will not be rescued by a
# different solver, and needs a smaller harness instead.
echo "== no solver finished within ${TIMEOUT}s"
for C in $CANDIDATES; do
  NAME=${C%%:*}
  PHASE=$(grep -E "Starting Bounded Model Checking|converting SSA|Running|Passing problem" \
            "$WORK/$NAME.log" 2>/dev/null | tail -1)
  RCS=$( [ -f "$WORK/$NAME.rc" ] && cat "$WORK/$NAME.rc" || echo running )
  printf '   %-10s rc=%-8s last phase: %s\n' "$NAME" "$RCS" "${PHASE:-<none>}"
done
echo "   all still in Bounded Model Checking => symex-bound, not solver-bound: shrink the harness."
exit 124
