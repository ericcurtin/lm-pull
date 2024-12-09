#!/bin/bash

download() {
  local url="$1"
  shift
  local args=()
  while [[ $# -gt 0 ]]; do
    if [[ -n "$1" ]]; then
      args+=("$1")
    fi
    shift
  done

  curl --globoff --location --proto-default https -f "${args[@]}" \
    --remote-time --retry 2 --continue-at - "$url"
}

main() {
  set -ex -o pipefail
  local model="$1"
  local bn
  bn="$(basename "$model")"
  if [[ "$model" = https://* ]]; then
    download "$model" "$bn"
  elif [[ "$model" = ollama://* ]]; then
    model=${model#"ollama://"}
    if [[ "$model" != */* ]]; then
      model="library/$model"
    fi

    local model_tag="latest"
    if [[ "$model" == *:* ]]; then
      model=$(echo "${model}" | cut -f1 -d:)
      model_tag=$(echo "${model}" | cut -f2 -d:)
    fi

    local model_bn
    model_bn="$(basename "$model")"
    echo "Pulling manifest for ${model}:${model_tag}"
    local accept_header="Accept: application/vnd.docker.distribution.manifest.v2+json"
    local manifest
    manifest="$(download "https://registry.ollama.ai/v2/${model}/manifests/${model_tag}" "--header" "${accept_header}")"
    local layer
    layer=$(echo "$manifest" | jq -r ".layers[] | select(.mediaType == \"application/vnd.ollama.image.model\") | .digest")
    echo "Pulling blob ${layer}"
    download "https://registry.ollama.ai/v2/${model}/blobs/${layer}" \
      "--header" "${accept_header}" "-o" "$model_bn"
  fi
}

main "$@"

