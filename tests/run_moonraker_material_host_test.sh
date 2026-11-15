#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$(mktemp)"
trap 'rm -f "$out"' EXIT

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${HOST_TEST_CFLAGS:-} \
  -I"$root/tests/stubs" \
  -I"$root/components/dc_moonraker/include" \
  "$root/tests/dc_moonraker_material_host_test.c" \
  -o "$out"
"$out"
