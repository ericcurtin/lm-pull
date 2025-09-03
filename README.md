# lm-pull

This project provides a command-line utility to download machine learning models from various sources, including HuggingFace, Ollama, and Docker OCI registries. It supports resuming downloads, displaying progress, and handling different URL schemes.

## Install

```
curl -fsSL https://raw.githubusercontent.com/ericcurtin/lm-pull/s/install.sh | bash
```

## Features

- Download models from HuggingFace, Ollama, and Docker OCI registries.
- Resume interrupted downloads.
- Display download progress.
- Handle different URL schemes for model sources.

## Dependencies

python3 version:

- no dependancies

C++ version:

- [libcurl](https://curl.se/libcurl/)
- [nlohmann/json](https://github.com/nlohmann/json)

## Usage

To download a model, run the following command:
```sh
lm-pull <model-url>
```
- `<model-url>`: The URL of the model to download. Supported URL schemes:
  - `https://`: Direct URL to the model file.
  - `hf://` or `huggingface://`: URL to a HuggingFace model.
  - `docker://`: URL to a Docker OCI registry model.
  - `ollama://`: URL to an Ollama model. (also the default)

```
$ build/lm-pull -h
Usage:
  lm-pull <model>

Examples:
  lm-pull llama3
  lm-pull ollama://granite-code
  lm-pull ollama://smollm:135m
  lm-pull docker://ai/smollm2
  lm-pull docker://ai/smollm2:latest
  lm-pull hf://QuantFactory/SmolLM-135M-GGUF/SmolLM-135M.Q2_K.gguf
  lm-pull huggingface://bartowski/SmolLM-1.7B-Instruct-v0.2-GGUF/SmolLM-1.7B-Instruct-v0.2-IQ3_M.gguf
  lm-pull https://example.com/some-file1.gguf
```

## Example

To download a model from HuggingFace:
```sh
lm-pull hf://QuantFactory/SmolLM-135M-GGUF/SmolLM-135M.Q3_K_S.gguf
```

To download a model from Ollama:
```sh
lm-pull granite-code
```

To download a model from Docker Hub:
```sh
lm-pull docker://ai/smollm2
```

