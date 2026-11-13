#!/usr/bin/env bash
#
# Run all test suites sequentially.
#
# Allow unicode quotes:
# shellcheck disable=SC1111
# Do not assume environment variables presence:
# shellcheck enable=check-unassigned-uppercase
# Set directory to project root:
# shellcheck source-path=SCRIPTDIR/..

set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

SCRIPT_DIR=$(dirname -- "${BASH_SOURCE[0]}")
cd -- "$SCRIPT_DIR/.." # Switch to project root

source tests/dev-scripts/colors.sh

passed=0
failed=0

echo '=== Running examples tests ============================================='
if (set -o xtrace; tests/run-examples.sh); then
  printf '%s[PASS]%s Examples tests passed\n' "$O_PASS_COLOR" "$O_RESET_COLOR"
  (( passed += 1 ))
else
  >&2 printf '%s[FAIL]%s Examples tests failed\n' "$E_FAIL_COLOR" "$E_RESET_COLOR"
  (( failed += 1 ))
fi

echo '=== Running examples time and memory measurements ======================'
if (set -o xtrace; tests/measure-examples.sh); then
  printf '%s[PASS]%s Examples time and memory measurements passed\n' "$O_PASS_COLOR" "$O_RESET_COLOR"
  (( passed += 1 ))
else
  >&2 printf '%s[FAIL]%s Examples time and memory measurements failed\n' "$E_FAIL_COLOR" "$E_RESET_COLOR"
  (( failed += 1 ))
fi

echo '=== Running C tests ===================================================='
if (set -o xtrace; tests/run-c.sh); then
  printf '%s[PASS]%s C tests passed\n' "$O_PASS_COLOR" "$O_RESET_COLOR"
  (( passed += 1 ))
else
  >&2 printf '%s[FAIL]%s C tests failed\n' "$E_FAIL_COLOR" "$E_RESET_COLOR"
  (( failed += 1 ))
fi

if (( failed != 0 )); then
  PASS_HL=$O_SECONDARY_COLOR
  FAIL_HL=$O_FAIL_COLOR
else
  PASS_HL=$O_PASS_COLOR
  FAIL_HL=$O_SECONDARY_COLOR
fi

echo '========================================================================'

printf 'Test suites summary: %sPassed: %d%s, %sFailed: %d%s\n' \
  "$PASS_HL" "$passed" "$O_RESET_COLOR" \
  "$FAIL_HL" "$failed" "$O_RESET_COLOR"

# Final exit code
(( passed > 0 && failed == 0 ))
