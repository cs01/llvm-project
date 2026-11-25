#!/bin/bash
# Builds and runs each port's tests under Safe C (ownership off: not ported to it yet), the system clang, and ASan+UBSan. Prints one line per
# configuration; exits non-zero if any test fails or does not build.
set -u
here=$(cd "$(dirname "$0")" && pwd)
safec=${SAFEC:-$here/../../build.noindex/bin/clang}
sdk=$(xcrun --show-sdk-path)
out=$(mktemp -d)
fail=0
# The vendored safec.h copies (for compilers without Safe C) must match the
# real one, or the non-Safe-C builds test a different header.
for copy in "$here"/cjson/safec.h "$here"/jsmn/safec.h "$here"/lz4/safec.h "$here"/inih/safec.h "$here"/parson/safec.h "$here"/../../safe-c-std/safec.h; do
  cmp -s "$copy" "$here/../../clang/lib/Headers/safec.h" ||
    { echo "stale: $copy (cp clang/lib/Headers/safec.h there)"; fail=1; }
done
# cJSON's circular-reference test recurses 10000 deep on purpose; ASan's
# larger frames need more than the default 8 MB stack.
ulimit -s 65520
# Unity (the test framework) is not ported code: build it once, unchecked.
/usr/bin/clang -c -o "$out/unity.o" "$here/cjson/tests/unity/src/unity.c" || exit 1
/usr/bin/clang -fsanitize=address,undefined -c -o "$out/unity-asan.o" \
  "$here/cjson/tests/unity/src/unity.c" || exit 1

configs=(
  "safec|$safec -fsafe-c -mllvm -safe-c-ownership=false -isysroot $sdk"
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

# inih's tests print what the handler saw. Each build must exit 0 and print
# exactly upstream's checked-in baseline; inih/tests/unittest.sh lists the same
# builds (it regenerates the baselines, this compares against them).
run_inih() { # name, compiler command
  local name=$1 cc=$2 port=$here/inih pass=0 total=0 base src flags
  while read -r base src flags; do
    total=$((total + 1))
    local exe=$out/$name-inih-$base
    # shellcheck disable=SC2086
    if $cc $flags -o "$exe" "$port/ini.c" "$port/tests/$src" 2>"$exe.log" &&
      (cd "$port/tests" && perl -e 'alarm 60; exec @ARGV' "$exe" </dev/null >"$exe.out" 2>&1) &&
      cmp -s "$port/tests/baseline_$base.txt" "$exe.out"; then
      pass=$((pass + 1))
    else
      echo "  FAIL $name inih $base: $exe.log $exe.out"
    fi
  done <<'EOF'
multi unittest.c
multi_max_line unittest.c -DINI_MAX_LINE=20
single unittest.c -DINI_ALLOW_MULTILINE=0
disallow_inline_comments unittest.c -DINI_ALLOW_INLINE_COMMENTS=0
stop_on_first_error unittest.c -DINI_STOP_ON_FIRST_ERROR=1
handler_lineno unittest.c -DINI_HANDLER_LINENO=1
string unittest_string.c -DINI_MAX_LINE=20
heap unittest.c -DINI_USE_STACK=0
heap_max_line unittest.c -DINI_USE_STACK=0 -DINI_MAX_LINE=20 -DINI_INITIAL_ALLOC=20
heap_realloc unittest.c -DINI_USE_STACK=0 -DINI_ALLOW_REALLOC=1 -DINI_INITIAL_ALLOC=5
heap_realloc_max_line unittest.c -DINI_USE_STACK=0 -DINI_MAX_LINE=20 -DINI_ALLOW_REALLOC=1 -DINI_INITIAL_ALLOC=5
heap_string unittest_string.c -DINI_USE_STACK=0 -DINI_MAX_LINE=20 -DINI_INITIAL_ALLOC=20
call_handler_on_new_section unittest.c -DINI_CALL_HANDLER_ON_NEW_SECTION=1
allow_no_value unittest.c -DINI_ALLOW_NO_VALUE=1
alloc unittest_alloc.c -DINI_CUSTOM_ALLOCATOR=1 -DINI_USE_STACK=0 -DINI_ALLOW_REALLOC=1 -DINI_INITIAL_ALLOC=12
file_error unittest_file_error.c
EOF
  echo "inih $name: $pass/$total"
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
  # test.c reads lz4.c and lz4.h from its own directory
  port=$here/lz4; label=lz4; lib=$port/lz4.c
  run "$name" "$cc" "$port"/test.c
  run_inih "$name" "$cc"
  # tests.c runs every suite in one program and exits 1 if any check failed. The
  # second build puts every key in one hash chain, as upstream's Makefile does.
  # It reads tests/ and leaves two files there (it removes them by the wrong name).
  port=$here/parson; label=parson; lib="-DTESTS_MAIN $port/parson.c"
  run "$name" "$cc" "$port"/tests.c
  label=parson-collisions; lib="$lib -DPARSON_FORCE_HASH_COLLISIONS"
  run "$name" "$cc" "$port"/tests.c
  rm -f "$port"/tests/*_serialized*.txt
  port=$here/../../safe-c-std; label=std; lib=
  run "$name" "$cc" "$port"/tests/*.c "$port"/examples/*.c
done
exit $fail
