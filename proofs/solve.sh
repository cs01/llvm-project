#!/bin/sh
# Race the solvers instead of predicting which one wins.
#
#   solve.sh <goto-file> [extra cbmc args...]
#
# COST.md has a rule -- symbolic extents want an SMT solver, concrete extents
# being unwound want the bit-blasting default -- and it is a real effect, worth
# 20x in either direction. It is also not reliable enough to bet a 40-minute
# run on: the rule was derived from two harnesses, and picking by hand has
# burned hours on this branch. Every solver here is cheap to start, only one
# has to finish, and the loser costs nothing but a core.
#
# A solver "wins" only by returning a definitive verdict. UNKNOWN does not
# count: a solver that cannot decide a property has not answered, and taking
# its silence for success is the worst failure this pipeline could have.
set -u
GOTO=${1:?usage: solve.sh <goto-file> [cbmc args...]}
shift
DEADLINE=${DEADLINE:-1800}
LOG=${SOLVE_LOG:-}

WORK=$(mktemp -d)
trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$WORK"' EXIT INT TERM

# Only what is installed. An absent solver is not an error.
SOLVERS="default"
command -v bitwuzla >/dev/null 2>&1 && SOLVERS="$SOLVERS bitwuzla"
command -v z3       >/dev/null 2>&1 && SOLVERS="$SOLVERS z3"
command -v cvc5     >/dev/null 2>&1 && SOLVERS="$SOLVERS cvc5"

START=$(date +%s)
for S in $SOLVERS; do
  case $S in
    default) FLAG="" ;;
    *)       FLAG="--$S" ;;
  esac
  # shellcheck disable=SC2086
  ( timeout "$DEADLINE" cbmc "$GOTO" $FLAG "$@" > "$WORK/$S.out" 2>&1
    echo $? > "$WORK/$S.rc" ) &
done

WINNER=""
while [ -z "$WINNER" ]; do
  for S in $SOLVERS; do
    [ -f "$WORK/$S.rc" ] || continue
    # A verdict, not merely an exit. UNKNOWN properties mean no answer.
    if grep -q "VERIFICATION SUCCESSFUL\|VERIFICATION FAILED" "$WORK/$S.out" 2>/dev/null &&
       ! grep -q ": UNKNOWN" "$WORK/$S.out" 2>/dev/null; then
      WINNER=$S
      break
    fi
  done
  [ -n "$WINNER" ] && break
  # All finished without a verdict?
  DONE=1
  for S in $SOLVERS; do [ -f "$WORK/$S.rc" ] || DONE=0; done
  [ "$DONE" -eq 1 ] && break
  sleep 2
done

ELAPSED=$(( $(date +%s) - START ))
kill $(jobs -p) 2>/dev/null

if [ -z "$WINNER" ]; then
  echo "no solver returned a verdict in ${ELAPSED}s (deadline ${DEADLINE}s)" >&2
  for S in $SOLVERS; do
    printf '  %-9s %s\n' "$S" "$(grep -cE ': UNKNOWN' "$WORK/$S.out" 2>/dev/null) unknown" >&2
  done
  exit 2
fi

echo "== solved by ${WINNER} in ${ELAPSED}s =="
cat "$WORK/$WINNER.out"
[ -n "$LOG" ] && printf '%s\t%s\t%s\t%s\n' "$(date -u +%FT%TZ)" "$(basename "$GOTO")" "$WINNER" "$ELAPSED" >> "$LOG"
exit "$(cat "$WORK/$WINNER.rc")"
