# lower.sh IN.c OUT.flat [clang flags]: C to flat generic ClangIR
B=$(cd "$(dirname "$0")/../../../.." && pwd)/build.noindex/bin; in=$1 out=$2; shift 2
$B/clang "$@" -fclangir -emit-cir -Xclang -clangir-disable-passes -w -isysroot "$(xcrun --show-sdk-path)" "$in" -o "$out.cir" 2>"$out.err" &&
$B/cir-opt "$out.cir" --cir-flatten-cfg --cir-goto-solver --mlir-print-op-generic --mlir-print-debuginfo -o "$out" 2>>"$out.err"
