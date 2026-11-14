#!/bin/sh
# Build the libFuzzer harnesses in tests/fuzz and run each one.
# Usage: tests/run_fuzz.sh [SECONDS]   (default 30 s for each harness)
# The compiler must be clang with libFuzzer. Set FUZZ_CC to select one.
# New inputs go to a work directory. The seed corpus in the repo stays the same.
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
seconds="${1:-30}"
cc="${FUZZ_CC:-$(command -v clang-23 || command -v clang)}"
work="${TMPDIR:-/tmp}/dragon-core-fuzz"
flags="-std=c11 -g -O1 -Wall -Wextra -Werror -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all"

mkdir -p "$work"

# shellcheck disable=SC2086
"$cc" $flags \
  -I"$root/components/dc_prusa/include" \
  -I"$root/tests/vendor/cjson" \
  "$root/tests/fuzz/fuzz_prusa_parse.c" \
  "$root/tests/vendor/cjson/cJSON.c" \
  -o "$work/fuzz_prusa_parse"

# shellcheck disable=SC2086
"$cc" $flags \
  -I"$root/components/dc_bambu/include" \
  "$root/tests/fuzz/fuzz_bambu_parse.c" \
  -o "$work/fuzz_bambu_parse"

for name in prusa bambu; do
  mkdir -p "$work/corpus-$name" "$work/artifacts-$name"
  echo "== fuzz_${name}_parse for ${seconds} s"
  "$work/fuzz_${name}_parse" \
    -max_total_time="$seconds" -timeout=10 -print_final_stats=1 \
    -artifact_prefix="$work/artifacts-$name/" \
    "$work/corpus-$name" "$root/tests/fuzz/corpus/$name"
done
