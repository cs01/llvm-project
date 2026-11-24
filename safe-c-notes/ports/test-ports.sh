#!/bin/bash
# Builds and runs each port's tests under Safe C (default and with the M2
# ownership rules), the system clang, and ASan+UBSan. Prints one line per
# configuration; exits non-zero if any test fails or does not build.
set -u
here=$(cd "$(dirname "$0")" && pwd)
safec=${SAFEC:-$here/../../build.noindex/bin/clang}
sdk=$(xcrun --show-sdk-path)
out=$(mktemp -d)
fail=0
# cJSON's circular-reference test recurses 10000 deep on purpose; ASan's
# larger frames need more than the default 8 MB stack.
ulimit -s 65520
# Unity (the test framework) is not ported code: build it once, unchecked.
/usr/bin/clang -c -o "$out/unity.o" "$here/cjson/tests/unity/src/unity.c" || exit 1
/usr/bin/clang -fsanitize=address,undefined -c -o "$out/unity-asan.o" \
  "$here/cjson/tests/unity/src/unity.c" || exit 1

configs=(
  "safec|$safec -fsafe-c -isysroot $sdk"
  "safec-own|$safec -fsafe-c -mllvm -safe-c-ownership -isysroot $sdk"
  "system|/usr/bin/clang"
  "asan|/usr/bin/clang -fsanitize=address,undefined -fno-sanitize-recover=all"
)

run() { # name, compiler command, sources...
  local name=$1 cc=$2; shift 2
  local pass=0 total=0
  for t in "$@"; do
    total=$((total + 1))
    local exe=$out/$name-$(basename "$t" .c)
    # shellcheck disable=SC2086
    if $cc -I"$port" -I"$port/tests" -I"$port/test" -I"$port/tests/unity/src" -o "$exe" $lib "$t" 2>"$exe.log" && (cd "$(dirname "$t")" && perl -e 'alarm 60; exec @ARGV' "$exe" </dev/null >"$exe.out" 2>&1); then
      pass=$((pass + 1))
    else
      echo "  FAIL $name $(basename "$t"): $exe.log"
    fi
  done
  echo "$label $name: $pass/$total"
  [ "$pass" = "$total" ] || fail=1
}

for spec in "${configs[@]}"; do
  name=${spec%%|*}; cc=${spec#*|}
  # cJSON_Utils is not ported, so its three tests are left out; unity_setup.c
  # is a helper, not a test.
  port=$here/cjson; label=cjson
  # The tests #include cJSON.c themselves.
  lib=$out/unity.o; [ "$name" = asan ] && lib=$out/unity-asan.o
  run "$name" "$cc" $(ls "$port"/tests/*.c | grep -vE 'utils_tests|json_patch|unity_setup')
  port=$here/jsmn; label=jsmn; lib=
  run "$name" "$cc" "$port"/test/tests.c
done
exit $fail
