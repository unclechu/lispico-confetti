#!/usr/bin/env bash
#
# Measure time and memory usage for each example.
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

: "${LISPICO_EXECUTABLE:=./lispico-confetti}"

LISPICO_EXECUTABLE_NAME=${LISPICO_EXECUTABLE##*/}
LISPICO_EXECUTABLE_DIR=$(dirname -- "$LISPICO_EXECUTABLE")

# Guard dependencies
if [[ ! -x "$LISPICO_EXECUTABLE" ]]; then
  >&2 printf "“%s” is not an executable (did you run “./build.sh” first?)\n" "$LISPICO_EXECUTABLE"
  exit 1
fi
>/dev/null /usr/bin/env --version
>/dev/null /usr/bin/env time --version
>/dev/null /usr/bin/env timeout --version
>/dev/null timeout --version
>/dev/null env --version
>/dev/null type size
>/dev/null type mktemp
>/dev/null type rm
>/dev/null type awk

source tests/dev-scripts/colors.sh

if [[ $LISPICO_EXECUTABLE_NAME != lispico-confetti ]]; then
  >&2 printf \
    "%sWARNING!%s Lispico executable name is not “lispico-confetti” but “%s” (shebang test will fail!)\n" \
    "$E_FAIL_COLOR" "$E_RESET_COLOR" \
    "${E_FAIL_COLOR}${LISPICO_EXECUTABLE_NAME}${E_RESET_COLOR}"
fi

TMP_FILE="$(mktemp)"
cleanup() { rm -fv -- "$TMP_FILE"; }
trap cleanup EXIT

TIMEOUT=30s

passed=0
failed=0

measure-example() {
  set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

  if (( $# != 2 )) || ! [[ -f $1 && -d $2 ]]; then
    >&2 printf \
      '%s[CRITICAL FAIL]%s measure-example: Unexpected arguments, need a file and a directory \n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR"
    return 1
  fi

  local SOURCE="$1"
  local NAME="${SOURCE#./}"
  local EXPECTATIONS_DIR=$2

  local exit_code=0
  local elapsed=0
  local peak_ram=0

  if [[ ${NAME##*/} == shebang.confetti ]]; then
    PATH="$LISPICO_EXECUTABLE_DIR:$PATH" LISPICO_SOURCE_HIGHLIGHT=none \
      /usr/bin/env time -q -f '%e %M' -o "$TMP_FILE" timeout -- "$TIMEOUT" \
      "$SOURCE" &>/dev/null || exit_code=$?
  else
    /usr/bin/env time -q -f '%e %M' -o "$TMP_FILE" timeout -- "$TIMEOUT" \
      "$LISPICO_EXECUTABLE" --source-highlight=none -ps "$SOURCE" &>/dev/null || exit_code=$?
  fi

  read -r elapsed peak_ram < "$TMP_FILE"

  printf '%-70s %12s %14s %10d\n' "$NAME" "${elapsed} s" "${peak_ram} KiB" "$exit_code"

  # Read the expected exit code, defaulting to zero.
  local example_name="${SOURCE##*/}"; example_name="${example_name%.confetti}"
  local expected_file="${EXPECTATIONS_DIR}/${example_name}.exitcode"
  local expected_exit_code=0

  if [[ -f "$expected_file" ]]; then
    expected_exit_code=$(< "$expected_file")
  fi

  if (( exit_code == expected_exit_code )); then
    (( passed += 1 ))
  else
    (( failed += 1 ))
  fi
}

cat tests/examples.env
source tests/examples.env
echo

echo 'Lispico benchmark'
printf 'Executable: “%s”\n\n' "$LISPICO_EXECUTABLE"

printf '%-70s %12s %14s %10s\n' 'Example' 'Elapsed' 'Peak RAM' 'Exit code'
echo '—————————————————————————————————————————————————————————————————————————————————————————————————————————————'

# Make sure wildcards are expanded instead of converted into plain text
shopt -s nullglob

# Test all existing example programs
for source in examples/*.confetti; do
  measure-example "$source" tests/expected-examples
done

# Test all ported Lispico examples
for source in examples/ported-lispico-examples/*.confetti; do
  measure-example "$source" tests/expected-ported-lispico-examples
done

if (( failed != 0 )); then
  PASS_HL=$O_SECONDARY_COLOR
  FAIL_HL=$O_FAIL_COLOR
else
  PASS_HL=$O_PASS_COLOR
  FAIL_HL=$O_SECONDARY_COLOR
fi

echo '—————————————————————————————————————————————————————————————————————————————————————————————————————————————'
printf 'Measured: %d, %sSuccessful: %d%s, %sFailed: %d%s\n\n' \
  "$(( passed + failed ))" \
  "$PASS_HL" "$passed" "$O_RESET_COLOR" \
  "$FAIL_HL" "$failed" "$O_RESET_COLOR"

echo 'Executable sections:'
size "$LISPICO_EXECUTABLE" | awk '
  NR == 1 {
    printf "%12s %12s %12s %12s %s\n", "text (KiB)", "data (KiB)", "bss (KiB)", "total (KiB)", "filename"
  }
  NR > 1 {
    printf "%12.2f %12.2f %12.2f %12.2f %s\n", $1 / 1024, $2 / 1024, $3 / 1024, $4 / 1024, $6
  }
'

echo
echo 'Elapsed  — Wall-clock execution time, including process startup.'
echo 'Peak RAM — Maximum resident set size (RSS), including Linux runtime and libraries.'
echo 'text     — Executable code and read-only data.'
echo 'data     — Initialized static/global data.'
echo 'bss      — Uninitialized or zero-initialized static/global data.'
echo 'dec      — Sum of text, data, and bss (not peak RAM).'

# Final exit code
[[ "$passed" -gt 0 && "$failed" -eq 0 ]]
