#!/bin/sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
out="${TMPDIR:-/tmp}/dragon-core-dc-syslog-test"

# Pure parts: line parse and RFC 5424 format.
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${HOST_TEST_CFLAGS:-} \
  -I"$root/components/dc_syslog/include" \
  "$root/components/dc_syslog/dc_syslog_format.c" \
  "$root/tests/dc_syslog_host_test.c" \
  -o "$out"
"$out"

# Hook, queue and sender task, with FreeRTOS, esp_netif and lwIP stubs.
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${HOST_TEST_CFLAGS:-} -D_DEFAULT_SOURCE \
  -I"$root/tests/stubs_dc_syslog" \
  -I"$root/components/dc_syslog/include" \
  "$root/components/dc_syslog/dc_syslog.c" \
  "$root/components/dc_syslog/dc_syslog_format.c" \
  "$root/tests/dc_syslog_task_host_test.c" \
  -o "$out-task"
"$out-task"
