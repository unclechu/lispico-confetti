#!/usr/bin/env bash
set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

: "${MAIN_SOURCE_FILE:=interpreter.c}"
: "${OUTPUT_EXECUTABLE_NAME:=lispico-confetti}"

: "${CC:=cc}"
: "${CFLAGS:=}"
: "${LIBS:=-lm}"

shopt -s nullglob

SOURCES=(
  src/*.c
  lispico/src/*.c
  confetti/confetti.c
  generated/confetti_unidata.c
  "${MAIN_SOURCE_FILE}"
)
SOURCE_FILES=()
for source in "${SOURCES[@]}"; do
  if [[ ${source##*/} != parse.c ]]; then
    SOURCE_FILES+=("$source")
  fi
done

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
