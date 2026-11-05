#!/usr/bin/env bash
set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

: "${MAIN_SOURCE_FILE:=interpreter.c}"
: "${OUTPUT_EXECUTABLE_NAME:=lispico-confetti}"

: "${CC:=cc}"
: "${CFLAGS:=}"
: "${LIBS:=-lm}"

shopt -s nullglob

SOURCE_FILES=(
  src/*.c
  lispico/src/*.c
  confetti/confetti.c
  generated/confetti_unidata.c
  "${MAIN_SOURCE_FILE}"
)

# The variables substitution is intentionally preserving arguments splitting.
# shellcheck disable=SC2206
BUILD_CMD=(
  $CC -std=c23 -O2 -Wall -Wextra -Wpedantic $CFLAGS
  "${SOURCE_FILES[@]}"
  -o "$OUTPUT_EXECUTABLE_NAME"
  $LIBS
)

set -o xtrace
"${BUILD_CMD[@]}" "$@"
