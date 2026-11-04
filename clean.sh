#!/usr/bin/env bash
set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

SCRIPT_DIR=$(dirname -- "${BASH_SOURCE[0]}")
cd -- "$SCRIPT_DIR" # Switch to project root

shopt -s nullglob
set -o xtrace
rm -vf -- lispico-confetti
