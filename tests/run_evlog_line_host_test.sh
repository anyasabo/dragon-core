#!/bin/sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
out="${TMPDIR:-/tmp}/dragon-core-evlog-line-test"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${HOST_TEST_CFLAGS:-} \
  -DDC_EVLOG_LINE_ASSEMBLY=1 \
  -I"$root/tests/stubs" \
  -I"$root/components/dc_evlog/include" \
  "$root/tests/dc_evlog_line_host_test.c" \
  "$root/components/dc_evlog/dc_evlog.c" \
  -o "$out"

"$out"
