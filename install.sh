#!/bin/bash

cleanup() {
  rm -rf "$TMP" &
}

available() {
  command -v "$1" >/dev/null
}

download() {
  curl --globoff --location --proto-default https -f -o "$2" \
      --remote-time --retry 10 --retry-max-time 10 "$1"
}

check_platform() {
  if [ "$os" = "Darwin" ]; then
    if [ "$EUID" -eq 0 ]; then
      echo "This script is intended to run as non-root on macOS"
      return 1
    fi
  elif [ "$os" = "Linux" ]; then
    if [ "$EUID" -ne 0 ]; then
      if ! available sudo; then
        error "This script is intended to run as root on Linux"
        return 3
      fi

      sudo="sudo"
    fi
  fi

  return 0
}

setup_lm_pull() {
  local binfile="lm-pull"
  local from_file="${binfile}.py"
  local host="https://raw.githubusercontent.com"
  local branch="${BRANCH:-s}"
  local url="${host}/ericcurtin/lm-pull/${branch}/bin/${from_file}"
  local to_file="${2}/${from_file}"
  download "$url" "$to_file"

  local lm_pull_bin="${1}/${binfile}"
  $sudo install -m755 "$to_file" "$lm_pull_bin"
}

main() {
  set -e -o pipefail
  local os
  os="$(uname -s)"
  local sudo=""
  check_platform

  local bindirs=("/opt/homebrew/bin" "/usr/local/bin" "/usr/bin" "/bin")
  local bindir
  for bindir in "${bindirs[@]}"; do
    if echo "$PATH" | grep -q "$bindir"; then
      break
    fi
  done

  if [ -z "$bindir" ]; then
    echo "No suitable bindir found in PATH"
    exit 5
  fi

  TMP="$(mktemp -d)"
  trap cleanup EXIT

  setup_lm_pull "$bindir" "$TMP"
}

main "$@"

