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
>/dev/null type rm
>/dev/null type mktemp
>/dev/null type mkfifo

source tests/dev-scripts/colors.sh

# By default the builds and tests are running in parallel.
# Set `RUN_IN_PARALLEL=0` to run everything sequentially
# (it’ll still create threads but will wait for each to finish first).
: "${RUN_IN_PARALLEL:=1}"

if [[ $RUN_IN_PARALLEL != 0 && $RUN_IN_PARALLEL != 1 ]]; then
  >&2 printf \
    '%s[CRITICAL FAIL]%s RUN_IN_PARALLEL can be either 0 or 1!\n' \
    "$E_FAIL_COLOR" "$E_RESET_COLOR"
  exit 1
fi

TIME_FORMAT='Real: %e s | User: %U s | System: %S s | CPU: %P | RAM: %M KiB'
TIMEOUT=30s

passed=0
failed=0

print-line-separator() {
  set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail
  echo '————————————————————————————————————————————————————————————————————————'
}

set-c-test-variables() {
  set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

  if (( $# != 1 )) || [[ ! -s $1 ]]; then
    >&2 printf \
      '%s[CRITICAL FAIL]%s set-c-test-variables expects a single argument with a C test source file!\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR"
    return 1
  fi

  NAME=${1##*/}; NAME=${NAME%.c}
  OUTPUT_EXECUTABLE_NAME="./lispico-confetti-$NAME-tests"
  ENV_FILE=${1%.c}.env
}

TMP_DIR="$(mktemp -d)"
cleanup() { rm -rfv -- "$TMP_DIR"; }
trap cleanup EXIT

mkfifo -- "$TMP_DIR/done"
exec {notify_fd}<>"$TMP_DIR/done" # Open FIFO descriptor for read-write
rm -- "$TMP_DIR/done" # Open descriptor survives

to-log-file-path() {
  set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

  if (( $# != 1 )) || [[ -z $1 ]]; then
    >&2 printf \
      '%s[CRITICAL FAIL]%s to-log-file-path expects a single non-empty string argument!\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR"
    return 1
  fi

  local x=$1; x=${x//\//_}; x=${x//\./_}; x=${x//-/_}
  printf "%s/%s.log" "$TMP_DIR" "$x"
}

# No-op for `(( RUN_IN_PARALLEL == 0 ))`.
#
# Relies on presence of `pids` array in the call-site scope.
#
# shellcheck disable=SC2120
render-captured-logs-sequentially() {
  set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

  if (( $# != 0 )); then
    >&2 printf \
      '%s[CRITICAL FAIL]%s render-captured-logs-sequentially does not take any arguments!\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR"
    return 1
  fi

  if (( RUN_IN_PARALLEL == 0 )); then return 0; fi

  local log_file
  local log
  local i

  # Render the captured logs sequentially as soon as any arrive
  for (( i = 0; i < ${#pids[@]}; ++i )); do
    IFS= read -r log_file <&"$notify_fd"
    log=$(<"$log_file")
    printf '%s\n' "$log"
    rm -v -- "$log_file"
  done
}

# No-op for `(( RUN_IN_PARALLEL == 1 ))`.
handle-sequential-pid() {
  set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

  if (( $# != 1 )); then
    >&2 printf \
      '%s[CRITICAL FAIL]%s handle-sequential-pid expects a single argument with a pid!\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR"
    return 1
  fi

  if (( RUN_IN_PARALLEL == 1 )); then return 0; fi

  wait -- "$pid"
  log=$(<"$log_file")
  printf '%s\n' "$log"
  rm -v -- "$log_file"
}

# shellcheck disable=SC2120
build-c-sources() {
  set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

  if (( $# != 0 )); then
    >&2 printf \
      '%s[CRITICAL FAIL]%s build-c-sources does not take any arguments!\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR"
    return 1
  fi

  echo 'Building C sources into objects…'

  if [[ ! -v CC ]]; then local CC=cc; fi
  if [[ ! -v CFLAGS ]]; then local CFLAGS=" "; fi
  if [[ ! -v LIBS ]]; then local LIBS=" "; fi
  local CC="$CC" CFLAGS="$CFLAGS" LIBS="$LIBS"

  local pids=()
  local pid
  local object_file
  local log
  local log_file

  CFLAGS="$CFLAGS -r"

  SOURCES=(
    src/*.c
    lispico/src/*.c
    confetti/confetti.c
    generated/confetti_unidata.c

    # Not used for Lispico Confetti yet
    # tests/c/utils/*.c
  )
  SOURCE_FILES=()
  for source in "${SOURCES[@]}"; do
    if [[ ${source##*/} != parse.c ]]; then
      SOURCE_FILES+=("$source")
    fi
  done

  for c_source_file in "${SOURCE_FILES[@]}"; do
    object_file=${c_source_file##*/}
    object_file=${object_file%.c}.o

    log_file=$(to-log-file-path "$c_source_file")

    (
      trap 'printf "%s\n" "$log_file" >&"$notify_fd"' EXIT

      (
        set -o xtrace
        /usr/bin/env time -f "$TIME_FORMAT" env \
          CC="$CC" \
          CFLAGS="$CFLAGS" \
          LIBS="$LIBS" \
          DO_NOT_ADD_SOURCES=1 \
          MAIN_SOURCE_FILE="$c_source_file" \
          OUTPUT_EXECUTABLE_NAME="$TMP_DIR/$object_file" \
          ./build.sh
      ) >"$log_file" 2>&1 {notify_fd}>&-

    ) & pid="$!"

    pids+=("$pid")
    handle-sequential-pid "$pid"
  done

  render-captured-logs-sequentially
  wait -- "${pids[@]}"

  printf '%s[OK]%s C objects built successfully\n' "$O_SECONDARY_COLOR" "$O_RESET_COLOR"
}

build-c-test() {
  set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

  if (( $# != 1 )) || [[ ! -s $1 ]]; then
    >&2 printf \
      '%s[CRITICAL FAIL]%s build-c-test expects a single argument with a C test source file!\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR"
    return 1
  fi

  local MAIN_SOURCE_FILE=$1
  printf 'Building “%s%s%s”…\n' "$O_SECONDARY_COLOR" "$MAIN_SOURCE_FILE" "$O_RESET_COLOR"

  local NAME OUTPUT_EXECUTABLE_NAME ENV_FILE exit_code=0
  set-c-test-variables "$MAIN_SOURCE_FILE"

  (
    shopt -s nullglob

    if [[ -s $ENV_FILE ]]; then
      printf 'Applying “%s%s%s” file…\n' "$O_SECONDARY_COLOR" "$ENV_FILE" "$O_RESET_COLOR"
      ENV_CONTENTS=$(<"$ENV_FILE")
      printf '%s%s%s\n' "$O_SECONDARY_COLOR" "$ENV_CONTENTS" "$O_RESET_COLOR"
      eval -- "$ENV_CONTENTS"
    fi

    if [[ ! -v CC ]]; then local CC=cc; fi
    if [[ ! -v CFLAGS ]]; then local CFLAGS=" "; fi
    if [[ ! -v LIBS ]]; then local LIBS=-lm; fi
    local CC="$CC" CFLAGS="$CFLAGS" LIBS="$LIBS"

    # Intentional wildcard expanding:
    # shellcheck disable=SC2125
    CFLAGS="$CFLAGS $TMP_DIR/*.o"

    set -o xtrace
    /usr/bin/env time -f "$TIME_FORMAT" env \
      CC="$CC" \
      CFLAGS="$CFLAGS" \
      LIBS="$LIBS" \
      DO_NOT_ADD_SOURCES=1 \
      MAIN_SOURCE_FILE="$MAIN_SOURCE_FILE" \
      OUTPUT_EXECUTABLE_NAME="$OUTPUT_EXECUTABLE_NAME" \
      ./build.sh
  ) || exit_code=$?
  if (( exit_code != 0 )); then
    >&2 printf '%s[FAIL]%s C test “%s%s%s” failed to build\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR" "$E_SECONDARY_COLOR" "$MAIN_SOURCE_FILE" "$E_RESET_COLOR"
    return 1
  fi

  printf '%s[OK]%s C test “%s%s%s” built successfully\n' \
    "$O_SECONDARY_COLOR" "$O_RESET_COLOR" "$O_SECONDARY_COLOR" "$MAIN_SOURCE_FILE" "$O_RESET_COLOR"
}

run-c-test() {
  set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

  if (( $# != 1 )) || [[ ! -s $1 ]]; then
    >&2 printf \
      '%s[CRITICAL FAIL]%s run-c-test expects a single argument with a C test source file!\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR"
    return 1
  fi

  local MAIN_SOURCE_FILE=$1
  printf 'Running “%s%s%s” C test…\n' "$O_SECONDARY_COLOR" "$MAIN_SOURCE_FILE" "$O_RESET_COLOR"

  local NAME OUTPUT_EXECUTABLE_NAME ENV_FILE exit_code=0
  set-c-test-variables "$MAIN_SOURCE_FILE"

  (
    printf 'Running “%s%s%s”…\n' "$O_SECONDARY_COLOR" "$OUTPUT_EXECUTABLE_NAME" "$O_RESET_COLOR"
    set -o xtrace
    /usr/bin/env \
      FORCE_COLOR=1 time -f "$TIME_FORMAT" timeout -- "$TIMEOUT" "$OUTPUT_EXECUTABLE_NAME"
  ) || exit_code=$?
  if (( exit_code != 0 )); then
    >&2 printf '%s[FAIL]%s C test “%s%s%s” did not pass\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR"  "$E_SECONDARY_COLOR" "$MAIN_SOURCE_FILE" "$E_RESET_COLOR"
    return 1
  fi

  printf '%s[PASS]%s C test “%s%s%s” passed\n' \
    "$O_PASS_COLOR" "$O_RESET_COLOR"  "$O_SECONDARY_COLOR" "$MAIN_SOURCE_FILE" "$O_RESET_COLOR"
}

if (( $# == 0 )); then
  # Make sure wildcards are expanded instead of converted into plain text
  shopt -s nullglob

  tests=()
  pids=()
  built_tests=()

  build-c-sources

  # Build all C tests in parallel
  for c_test_src in tests/c/*.c; do
    tests+=("$c_test_src")
    log_file=$(to-log-file-path "$c_test_src")

    (
      trap 'printf "%s\n" "$log_file" >&"$notify_fd"' EXIT

      (
        print-line-separator
        build-c-test "$c_test_src"
      ) >"$log_file" 2>&1 {notify_fd}>&-

    ) & pid="$!"

    pids+=("$pid")
    handle-sequential-pid "$pid"
  done

  render-captured-logs-sequentially

  # Wait for the build background jobs to finish and register outcome for each
  for (( i = 0 ; i < "${#tests[@]}" ; ++i )); do
    if wait -- "${pids[i]}"; then
      built_tests+=("${tests[i]}") # Successful build
    else
      (( failed += 1 ))
    fi
  done

  pids=()

  # Run all successfully built C tests in parallel
  for (( i = 0 ; i < "${#built_tests[@]}" ; ++i )); do
    log_file=$(to-log-file-path "${built_tests[i]}")

    (
      trap 'printf "%s\n" "$log_file" >&"$notify_fd"' EXIT

      (
        print-line-separator
        run-c-test "${built_tests[i]}"
      ) >"$log_file" 2>&1 {notify_fd}>&-

    ) & pid="$!"

    pids+=("$pid")
    handle-sequential-pid "$pid"
  done

  render-captured-logs-sequentially

  # Wait for the C test running background jobs to finish and register outcome for each
  for (( i = 0 ; i < "${#pids[@]}" ; ++i )); do
    if wait -- "${pids[i]}"; then
      (( passed += 1 )) # Passed
    else
      (( failed += 1 ))
    fi
  done

elif (( $# == 1 )) && [[ -s $1 ]]; then

  build-c-sources

  print-line-separator

  if ! build-c-test "$1"; then
    (( failed += 1 ))
  else
    if ! run-c-test "$1"; then
      (( failed += 1 ))
    else
      (( passed += 1 ))
    fi
  fi

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

print-line-separator

printf 'C tests summary: %sPassed: %d%s, %sFailed: %d%s\n' \
  "$PASS_HL" "$passed" "$O_RESET_COLOR" \
  "$FAIL_HL" "$failed" "$O_RESET_COLOR"

# Final exit code
(( passed > 0 && failed == 0 ))
