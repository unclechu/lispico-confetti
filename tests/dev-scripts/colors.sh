# This file intended to be sourced into the scripts.
#
# Do not assume environment variables presence:
# shellcheck enable=check-unassigned-uppercase
# Disable unused variables warning:
# shellcheck disable=SC2034

# Terminal colors
# Disable stdout terminal coloring for redirects
if [[ ! -t 1 && ${FORCE_COLOR:-0} != 1 && ${GITHUB_ACTIONS:-} != true ]]; then
  O_RESET_COLOR=
  O_SECONDARY_COLOR=
  O_FAIL_COLOR=
  O_PASS_COLOR=
  O_CLEAR_LAST_LINE=
else
  O_RESET_COLOR=$'\033[0m'
  O_SECONDARY_COLOR=$'\033[1m'
  O_FAIL_COLOR=$'\033[1;37;41m'
  O_PASS_COLOR=$'\033[32m'
  O_CLEAR_LAST_LINE=$'\e[1A\e[K'
fi
# Disable stderr terminal coloring for redirects
if [[ ! -t 2 && ${FORCE_COLOR:-0} != 1 && ${GITHUB_ACTIONS:-} != true ]]; then
  E_RESET_COLOR=
  E_SECONDARY_COLOR=
  E_FAIL_COLOR=
  E_PASS_COLOR=
else
  E_RESET_COLOR=$'\033[0m'
  E_SECONDARY_COLOR=$'\033[1m'
  E_FAIL_COLOR=$'\033[1;37;41m'
  E_PASS_COLOR=$'\033[32m'
fi
