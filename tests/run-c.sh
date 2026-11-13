#!/usr/bin/env bash
#
# Tests runner for a C test files.
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

# Guard dependencies
>/dev/null /usr/bin/env --version
>/dev/null /usr/bin/env time --version
>/dev/null /usr/bin/env timeout --version
>/dev/null timeout --version
>/dev/null env --version

source tests/dev-scripts/colors.sh

TIME_FORMAT='Real: %e s | User: %U s | System: %S s | CPU: %P | RAM: %M KiB'
TIMEOUT=30s

passed=0
failed=0

run-c-test() {
  if (( $# != 1 )) || [[ ! -s $1 ]]; then
    >&2 printf \
      '%s[CRITICAL FAIL]%s run-c-test expects a single argument with a C test source file!\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR"
    return 1
  fi

  local MAIN_SOURCE_FILE=$1
  echo '————————————————————————————————————————————————————————————————————————'
  printf 'Running “%s%s%s” C test…\n' "$O_SECONDARY_COLOR" "$MAIN_SOURCE_FILE" "$O_RESET_COLOR"

  local NAME=${MAIN_SOURCE_FILE##*/}; NAME=${NAME%.c}
  local OUTPUT_EXECUTABLE_NAME="./lispico-confetti-$NAME-tests"
  local ENV_FILE=${MAIN_SOURCE_FILE%.c}.env
  local exit_code=0

  (
    shopt -s nullglob

    if [[ -s $ENV_FILE ]]; then
      printf 'Applying “%s%s%s” file…\n' "$O_SECONDARY_COLOR" "$ENV_FILE" "$O_RESET_COLOR"
      ENV_CONTENTS=$(<"$ENV_FILE")
      printf '%s%s%s\n' "$O_SECONDARY_COLOR" "$ENV_CONTENTS" "$O_RESET_COLOR"
      eval -- "$ENV_CONTENTS"
    fi

    if [[ ! -v CC ]]; then CC=cc; fi
    if [[ ! -v CFLAGS ]]; then CFLAGS=; fi
    if [[ ! -v LIBS ]]; then LIBS=-lm; fi

    # Intentional wildcard expanding:
    # shellcheck disable=SC2125
    # Not used for Lispico Confetti yet
    # CFLAGS="$CFLAGS tests/c/utils/*.c"

    printf 'Building “%s%s%s”…\n' "$O_SECONDARY_COLOR" "$MAIN_SOURCE_FILE" "$O_RESET_COLOR"
    set -o xtrace
    /usr/bin/env time -f "$TIME_FORMAT" env \
      CC="$CC" \
      CFLAGS="$CFLAGS" \
      LIBS="$LIBS" \
      MAIN_SOURCE_FILE="$MAIN_SOURCE_FILE" \
      OUTPUT_EXECUTABLE_NAME="$OUTPUT_EXECUTABLE_NAME" \
      ./build.sh
  ) || exit_code=$?
  if (( exit_code != 0 )); then
    >&2 printf '%s[FAIL]%s C test “%s” failed to build\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR" "$MAIN_SOURCE_FILE"
    (( failed += 1 ))
    return 0
  fi

  (
    printf 'Running “%s%s%s”…\n' "$O_SECONDARY_COLOR" "$OUTPUT_EXECUTABLE_NAME" "$O_RESET_COLOR"
    set -o xtrace
    /usr/bin/env time -f "$TIME_FORMAT" timeout -- "$TIMEOUT" "$OUTPUT_EXECUTABLE_NAME"
  ) || exit_code=$?
  if (( exit_code != 0 )); then
    >&2 printf '%s[FAIL]%s C test “%s” did not pass\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR" "$MAIN_SOURCE_FILE"
    (( failed += 1 ))
    return 0
  fi

  printf '%s[PASS]%s C test “%s” passed\n' "$O_PASS_COLOR" "$O_RESET_COLOR" "$MAIN_SOURCE_FILE"
  (( passed += 1 ))
}

if (( $# == 0 )); then
  # Make sure wildcards are expanded instead of converted into plain text
  shopt -s nullglob

  # Run all C tests
  for c_test_src in tests/c/*.c; do
    # Forwarding `CFLAGS` to `./build.sh`
    run-c-test "$c_test_src"
  done
elif (( $# == 1 )) && [[ -s $1 ]]; then
  run-c-test "$1"
else
  >&2 printf '%s[CRITICAL FAIL]%s Unexpected arguments!\n' "$E_FAIL_COLOR" "$E_RESET_COLOR"
  exit 1
fi

if (( failed != 0 )); then
  PASS_HL=$O_SECONDARY_COLOR
  FAIL_HL=$O_FAIL_COLOR
else
  PASS_HL=$O_PASS_COLOR
  FAIL_HL=$O_SECONDARY_COLOR
fi

echo '————————————————————————————————————————————————————————————————————————'

printf 'C tests summary: %sPassed: %d%s, %sFailed: %d%s\n' \
  "$PASS_HL" "$passed" "$O_RESET_COLOR" \
  "$FAIL_HL" "$failed" "$O_RESET_COLOR"

# Final exit code
(( passed > 0 && failed == 0 ))
