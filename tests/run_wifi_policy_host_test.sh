#!/bin/sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
out="${TMPDIR:-/tmp}/dragon-core-dc-wifi-policy-test"

# shellcheck disable=SC2086 # HOST_TEST_CFLAGS is a list of flags
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${HOST_TEST_CFLAGS:-} \
  -I"$root/tests/stubs" \
  -I"$root/components/dc_wifi/include" \
  -I"$root/components/dc_wifi" \
  "$root/components/dc_wifi/dc_wifi_policy.c" \
  "$root/tests/dc_wifi_policy_host_test.c" \
  -o "$out"

"$out"
