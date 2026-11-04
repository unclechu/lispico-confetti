#!/usr/bin/env bash
#
# Re-generate `tests/expected-examples/*.{stdout,stderr,exitcode}` files.
#
# WARNING! This does not mean those results are correct.
# Inspect the resulting files manually first.
#
# Also note that removed examples won’t result into automatically removing
# expectation files.
#
# Allow unicode quotes:
# shellcheck disable=SC1111
#
# Set directory to project root:
# shellcheck source-path=SCRIPTDIR/../..

set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

SCRIPT_DIR=$(dirname -- "${BASH_SOURCE[0]}")
cd -- "$SCRIPT_DIR/../.." # Switch to project root

: "${LISPICO_EXECUTABLE:=./lispico-confetti}"

# Guard dependencies
if [[ ! -x "$LISPICO_EXECUTABLE" ]]; then
  >&2 printf "“%s” is not an executable (did you run “./build.sh” first?)\n" "$LISPICO_EXECUTABLE"
  exit 1
fi

# Terminal colors
# Disable stdout terminal coloring for redirects
if [[ ! -t 1 && ${FORCE_COLOR:-0} != 1 && ${GITHUB_ACTIONS:-} != true ]]; then
  O_RESET_COLOR=
  O_SECONDARY_COLOR=
  # O_FAIL_COLOR=
  O_PASS_COLOR=
  O_CLEAR_LAST_LINE=
else
  O_RESET_COLOR=$'\033[0m'
  O_SECONDARY_COLOR=$'\033[1m'
  # O_FAIL_COLOR=$'\033[1;37;41m'
  O_PASS_COLOR=$'\033[32m'
  O_CLEAR_LAST_LINE=$'\e[1A\e[K'
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

only_missing=false

handle-example() {
  set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

  if (( $# != 1 )); then
    >&2 printf \
      '%s[FAIL]%s handle-example(): Unexpected amount of arguments: %s%d%s\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR" \
      "$E_FAIL_COLOR" "$#" "$E_RESET_COLOR"
    return 1
  elif [[ -z $1 || ! -s $1 ]]; then
    >&2 printf \
      '%s[FAIL]%s handle-example(): Example file “%s%s%s” does not exist\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR" \
      "$E_FAIL_COLOR" "$1" "$E_RESET_COLOR"
    return 1
  fi

  local source=$1; source=${source#./}

  # Example directory
  local dir=${source%/*}

  # Get the name with parent directory path and *.confetti extension removed
  local name=${source##*/}; name=${name%.confetti}

  local exit_code=0

  if [[ $dir == examples/fail ]]; then
    local subdir=fail/
  elif [[ $dir == examples ]]; then
    local subdir=
  else
    >&2 printf \
      '%s[FAIL]%s Unexpected example directory: “%s%s%s”\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR" \
      "$E_FAIL_COLOR" "$dir" "$E_RESET_COLOR"
    exit 1
  fi

  local expectation_out=tests/expected-examples/${subdir}${name}.stdout
  local expectation_err=tests/expected-examples/${subdir}${name}.stderr
  local expectation_ec=tests/expected-examples/${subdir}${name}.exitcode

  if [[ $only_missing == true ]] && [[
    -e $expectation_out && -e $expectation_err && -e $expectation_ec
  ]]; then
    printf \
      '%s[SKIP]%s Example file “%s%s%s” already have expectations generated\n' \
      "$O_SECONDARY_COLOR" "$O_RESET_COLOR" \
      "$O_SECONDARY_COLOR" "$source" "$O_RESET_COLOR"
  else
    printf \
      '%s[    ]%s Processing example file “%s%s%s”\n' \
      "$O_SECONDARY_COLOR" "$O_RESET_COLOR" \
      "$O_SECONDARY_COLOR" "$source" "$O_RESET_COLOR"

    "$LISPICO_EXECUTABLE" "$source" >"$expectation_out" 2>"$expectation_err" || exit_code=$?
    printf '%d\n' "$exit_code" >"$expectation_ec"

    printf \
      '%s%s[ OK ]%s Generated expectations for example file “%s%s%s”\n' \
      "$O_CLEAR_LAST_LINE" \
      "$O_PASS_COLOR" "$O_RESET_COLOR" \
      "$O_PASS_COLOR" "$source" "$O_RESET_COLOR"
  fi
}

cat tests/examples.env
source tests/examples.env
echo

# By default do it for all examples.
# Use `missing` argument to kill all examples that already have expectations
# generated for them.
if (( $# == 0 )) || ( (( $# == 1 )) && [[ $1 == missing ]] ); then

  if (( $# == 1 )) && [[ $1 == missing ]]; then
    only_missing=true
  fi

  # Make sure wildcards are expanded instead of converted into plain text
  shopt -s nullglob

  # All existing example programs
  for source in examples/*.confetti; do
    handle-example "$source"
  done

  # TODO: All expected failures
  # for source in examples/fail/*.confetti; do
  #   handle-example "$source"
  # done

# Re-generate only for a specific example file.
elif (( $# == 1 )); then

  if [[ -z $1 || ! -s $1 ]]; then
    >&2 printf \
      '%s[FAIL]%s Example file “%s%s%s” does not exist\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR" \
      "$E_FAIL_COLOR" "$1" "$E_RESET_COLOR"
    exit 1
  fi

  source=$1; source=${source#./}
  handle-example "$source"

else
  >&2 printf \
    '%s[FAIL]%s Unexpected amount of arguments: %s%d%s\n' \
    "$E_FAIL_COLOR" "$E_RESET_COLOR" \
    "$E_FAIL_COLOR" "$#" "$E_RESET_COLOR"
  exit 1
fi

printf '%s[DONE]%s\n' "$O_PASS_COLOR" "$O_RESET_COLOR"
