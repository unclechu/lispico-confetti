#!/usr/bin/env bash
#
# Tests runner script for Lispico Confetti examples.
#
# Allow unicode quotes:
# shellcheck disable=SC1111
# Do not assume environment variables presence:
# shellcheck enable=check-unassigned-uppercase
#
# Set directory to project root:
# shellcheck source-path=SCRIPTDIR/..

set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

SCRIPT_DIR=$(dirname -- "${BASH_SOURCE[0]}")
cd -- "$SCRIPT_DIR/.." # Switch to project root

: "${LISPICO_EXECUTABLE:=./lispico-confetti}"

LISPICO_EXECUTABLE_NAME=${LISPICO_EXECUTABLE##*/}
LISPICO_EXECUTABLE_DIR=$(dirname -- "$LISPICO_EXECUTABLE")

# Guard dependencies
if [[ ! -x "$LISPICO_EXECUTABLE" ]]; then
  >&2 printf "“%s” is not an executable (did you run “./build.sh” first?)\n" "$LISPICO_EXECUTABLE"
  exit 1
fi
>/dev/null type cmp
>/dev/null type diff
>/dev/null type mktemp
>/dev/null type rm
>/dev/null type cat
>/dev/null type timeout

# Terminal colors
# Disable stdout terminal coloring for redirects
if [[ ! -t 1 && ${FORCE_COLOR:-0} != 1 && ${GITHUB_ACTIONS:-} != true ]]; then
  O_RESET_COLOR=
  O_SECONDARY_COLOR=
  O_FAIL_COLOR=
  O_PASS_COLOR=
else
  O_RESET_COLOR=$'\033[0m'
  O_SECONDARY_COLOR=$'\033[1m'
  O_FAIL_COLOR=$'\033[1;37;41m'
  O_PASS_COLOR=$'\033[32m'
fi
# Disable stderr terminal coloring for redirects
if [[ ! -t 2 && ${FORCE_COLOR:-0} != 1 && ${GITHUB_ACTIONS:-} != true ]]; then
  E_RESET_COLOR=
  # E_SECONDARY_COLOR=
  E_FAIL_COLOR=
  # E_PASS_COLOR=
else
  E_RESET_COLOR=$'\033[0m'
  # E_SECONDARY_COLOR=$'\033[1m'
  E_FAIL_COLOR=$'\033[1;37;41m'
  # E_PASS_COLOR=$'\033[32m'
fi

if [[ $LISPICO_EXECUTABLE_NAME != lispico-confetti ]]; then
  >&2 printf \
    "%sWARNING!%s Lispico executable name is not “lispico-confetti” but “%s” (shebang test will fail!)\n" \
    "$E_FAIL_COLOR" "$E_RESET_COLOR" \
    "${E_FAIL_COLOR}${LISPICO_EXECUTABLE_NAME}${E_RESET_COLOR}"
fi

cleanup() {
  rm -rfv -- "$TMP_DIR"
}

TMP_DIR="$(mktemp -d)"
trap cleanup EXIT

TIMEOUT=30s

passed=0
failed=0

run_test() {
  set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

  if (( $# != 2 )); then
    >&2 printf \
      '%s[CRITICAL FAIL]%s run_test: Unexpected amount of arguments, expects exactly 2 but got: %d\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR" "$#"
    return 1
  fi

  local SOURCE="$1"
  local EXPECTED="$2"
  local NAME="${SOURCE#./}"

  local EXPECTED_OUT_FILE=${EXPECTED}.stdout
  local EXPECTED_ERR_FILE=${EXPECTED}.stderr
  local EXPECTED_EXITCODE_FILE=${EXPECTED}.exitcode

  local expected_exit_code=0
  local actual_exit_code=0
  local ok=true

  if [[ ! -f "$EXPECTED_OUT_FILE" ]]; then
    >&2 printf '%s[FAIL]%s %s: missing expected stdout\n' "$E_FAIL_COLOR" "$E_RESET_COLOR" "$NAME"
    (( failed += 1 ))
    return
  fi

  if [[ -f "$EXPECTED_EXITCODE_FILE" ]]; then
    expected_exit_code=$(< "$EXPECTED_EXITCODE_FILE")
  fi

  if [[ ${NAME##*/} == shebang.confetti ]]; then
    PATH="$LISPICO_EXECUTABLE_DIR:$PATH" \
      timeout -- "$TIMEOUT" "$PWD/$SOURCE" >"$TMP_DIR/stdout" 2>"$TMP_DIR/stderr" \
      || actual_exit_code=$?
  else
    timeout -- "$TIMEOUT" \
      "$LISPICO_EXECUTABLE" "$SOURCE" >"$TMP_DIR/stdout" 2>"$TMP_DIR/stderr" || actual_exit_code=$?
  fi

  if (( actual_exit_code != expected_exit_code )); then
    >&2 printf '  Exit status: expected “%s”, got “%s”\n' "$expected_exit_code" "$actual_exit_code"
    ok=false
  fi

  if ! cmp -s -- "$EXPECTED_OUT_FILE" "$TMP_DIR/stdout"; then
    >&2 printf '  Stdout differs:\n'
    >&2 diff -u -- "$EXPECTED_OUT_FILE" "$TMP_DIR/stdout" || true
    ok=false
  fi

  if [[ -f "$EXPECTED_ERR_FILE" ]]; then
    if ! cmp -s -- "$EXPECTED_ERR_FILE" "$TMP_DIR/stderr"; then
      >&2 printf '  Stderr differs:\n'
      >&2 diff -u -- "$EXPECTED_ERR_FILE" "$TMP_DIR/stderr" || true
      ok=false
    fi
  elif [[ -s "$TMP_DIR/stderr" ]]; then
    >&2 printf '  Unexpected stderr:\n'
    >&2 cat -- "$TMP_DIR/stderr"
    ok=false
  fi

  if [[ "$ok" == true ]]; then
    printf '%s[PASS]%s %s\n' "$O_PASS_COLOR" "$O_RESET_COLOR" "$NAME"
    (( passed += 1 ))
  else
    >&2 printf '%s[FAIL]%s %s\n' "$E_FAIL_COLOR" "$E_RESET_COLOR" "$NAME"
    (( failed += 1 ))
  fi
}

cat tests/examples.env
source tests/examples.env
echo

# Make sure wildcards are expanded instead of converted into plain text
shopt -s nullglob

# Test all existing example programs
for source in examples/*.confetti; do
  # Get the name with parent directory path and *.confetti extension removed
  name="${source##*/}"
  name="${name%.confetti}"

  run_test "$source" "tests/expected-examples/$name"
done

# Test all ported Lispico examples
for source in examples/ported-lispico-examples/*.confetti; do
  # Get the name with parent directory path and *.confetti extension removed
  name="${source##*/}"
  name="${name%.confetti}"

  run_test "$source" "tests/expected-ported-lispico-examples/$name"
done

# Test all expected failures
for source in examples/fail/*.confetti; do
  # Get the name with parent directory path and *.confetti extension removed
  name="${source##*/}"
  name="${name%.confetti}"

  run_test "$source" "tests/expected-examples/fail/$name"
done

if (( failed != 0 )); then
  PASS_HL=$O_SECONDARY_COLOR
  FAIL_HL=$O_FAIL_COLOR
else
  PASS_HL=$O_PASS_COLOR
  FAIL_HL=$O_SECONDARY_COLOR
fi

echo '————————————————————————————————————————————————————————————————————————'

printf '%sPassed: %d%s, %sFailed: %d%s\n' \
  "$PASS_HL" "$passed" "$O_RESET_COLOR" \
  "$FAIL_HL" "$failed" "$O_RESET_COLOR"

# Final exit code
[[ "$passed" -gt 0 && "$failed" -eq 0 ]]
