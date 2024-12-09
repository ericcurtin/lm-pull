# lm-pull

This project provides a command-line utility to download machine learning models from various sources, including HuggingFace and Ollama. It supports resuming downloads, displaying progress, and handling different URL schemes.

## Features

- Download models from HuggingFace and Ollama.
- Resume interrupted downloads.
- Display download progress.
- Handle different URL schemes for model sources.

## Dependencies

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
  - `ollama://`: URL to an Ollama model. (also the default)

## Example

To download a model from HuggingFace:
```sh
lm-pull hf://QuantFactory/SmolLM-135M-GGUF/SmolLM-135M.Q3_K_S.gguf
```

To download a model from Ollama:
```sh
lm-pull granite-code
```

