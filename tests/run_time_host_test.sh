#!/bin/sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
out="${TMPDIR:-/tmp}/dragon-core-dc-time-test"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${HOST_TEST_CFLAGS:-} \
  -I"$root/components/dc_time/include" \
  "$root/components/dc_time/dc_time_format.c" \
  "$root/tests/dc_time_host_test.c" \
  -o "$out"

"$out"
