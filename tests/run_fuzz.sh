#!/bin/sh
# Build the libFuzzer harnesses in tests/fuzz and run each one.
# Usage: tests/run_fuzz.sh [SECONDS]   (default 30 s for each harness)
# The compiler must be clang with libFuzzer. Set FUZZ_CC to select one.
# New inputs go to a work directory. The seed corpus in the repo stays the same.
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
seconds="${1:-30}"
cc="${FUZZ_CC:-$(command -v clang-23 || command -v clang || true)}"
if [ -z "$cc" ]; then
  echo "error: no clang found. Install clang and libclang-rt-dev, or set FUZZ_CC." >&2
  exit 2
fi
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

# shellcheck disable=SC2086
"$cc" $flags \
  -I"$root/components/dc_portal" \
  "$root/tests/fuzz/fuzz_portal_dns.c" \
  -o "$work/fuzz_portal_dns"

# Input size limits: RX_CAP of dc_bambu.c is 16384 (real Bambu reports are
# 10-15 KB), and the DNS receive buffer of dc_portal_dns.c is 512.
run() {
  name="$1"; harness="$2"; max_len="$3"; shift 3
  mkdir -p "$work/corpus-$name" "$work/artifacts-$name"
  echo "== $harness for ${seconds} s"
  "$work/$harness" \
    -max_total_time="$seconds" -timeout=10 -max_len="$max_len" -print_final_stats=1 \
    -artifact_prefix="$work/artifacts-$name/" "$@" \
    "$work/corpus-$name" "$root/tests/fuzz/corpus/$name"
}

run prusa fuzz_prusa_parse 16384 -dict="$root/tests/fuzz/json.dict"
run bambu fuzz_bambu_parse 16384 -dict="$root/tests/fuzz/json.dict"
run dns fuzz_portal_dns 512
