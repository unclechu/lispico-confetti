#!/usr/bin/env bash
#
# Measure time and memory usage for each example.
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
  E_FAIL_COLOR=
else
  E_RESET_COLOR=$'\033[0m'
  E_FAIL_COLOR=$'\033[1;37;41m'
fi

cleanup() {
  rm -fv -- "$TMP_FILE"
}

TMP_FILE="$(mktemp)"
trap cleanup EXIT

TIMEOUT=30s

passed=0
failed=0

measure-example() {
  set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

  if (( $# != 1 )); then
    >&2 printf \
      '%s[CRITICAL FAIL]%s measure-example: Unexpected amount of arguments, expects exactly 1 but got: %d\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR" "$#"
    return 1
  fi

  local SOURCE="$1"
  local NAME="${SOURCE#./}"

  local exit_code=0
  local elapsed=0
  local peak_ram=0

  /usr/bin/env time -q -f '%e %M' -o "$TMP_FILE" timeout -- "$TIMEOUT" \
    "$LISPICO_EXECUTABLE" "$SOURCE" &>/dev/null || exit_code=$?

  read -r elapsed peak_ram < "$TMP_FILE"

  printf '%-70s %12s %14s %10d\n' "$NAME" "${elapsed} s" "${peak_ram} KiB" "$exit_code"

  if (( exit_code == 0 )); then
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
  measure-example "$source"
done

# Test all ported Lispico examples
for source in examples/ported-lispico-examples/*.confetti; do
  measure-example "$source"
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
