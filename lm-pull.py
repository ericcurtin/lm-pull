#!/usr/bin/python3

import os
import sys
import time
import requests
from datetime import datetime
import json


class CurlWrapper:
    def __init__(self):
        self.session = requests.Session()

    def init(self, url, headers, output_file, progress, response_str=None):
        if output_file:
            output_file_partial = output_file + ".partial"
        else:
            output_file_partial = None

        file_exists = False
        partial_file_size = 0
        if output_file_partial and os.path.exists(output_file_partial):
            file_exists = True
            partial_file_size = os.path.getsize(output_file_partial)
            headers["Range"] = f"bytes={partial_file_size}-"

        response = self.session.get(url, headers=headers, stream=True)
        if response.status_code not in (200, 206):
            print(f"Request failed: {response.status_code}", file=sys.stderr)
            return 1

        file_size = int(response.headers.get('content-length', 0))
        if response_str is not None:
            response_str.append(response.text)
        else:
            with open(output_file_partial, "ab") as file:
                self.total_size = file_size + partial_file_size
                self.downloaded_size = partial_file_size
                self.start_time = time.time()
                for data in response.iter_content(chunk_size=1024):
                    size = file.write(data)
                    if progress:
                        self.update_progress(size)

        if output_file:
            os.rename(output_file_partial, output_file)

        print("\n")

        return 0

    def update_progress(self, chunk_size):
        self.downloaded_size += chunk_size
        percentage = (self.downloaded_size * 100) // self.total_size
        speed = self.downloaded_size / (time.time() - self.start_time)
        bar_length = 50
        filled_length = int(bar_length * percentage // 100)
        bar = '█' * filled_length + ' ' * (bar_length - filled_length)
        print(f"\r{percentage}% |{bar}| {speed / (1024 * 1024):.2f} MB/s", end='')

def download(url, headers, output_file, progress, response_str=None):
    curl = CurlWrapper()
    return curl.init(url, headers, output_file, progress, response_str)

def huggingface_dl(model, headers, bn):
    pos = model.find('/')
    pos = model.find('/', pos + 1)
    if pos == -1:
        return 1

    hfr = model[:pos]
    hff = model[pos + 1:]
    url = f"https://huggingface.co/{hfr}/resolve/main/{hff}"
    return download(url, headers, bn, True)

def ollama_dl(model, headers, bn):
    if '/' not in model:
        model = "library/" + model

    model_tag = "latest"
    colon_pos = model.find(':')
    if colon_pos != -1:
        model_tag = model[colon_pos + 1:]
        model = model[:colon_pos]

    manifest_url = f"https://registry.ollama.ai/v2/{model}/manifests/{model_tag}"
    manifest_str = []
    ret = download(manifest_url, headers, "", False, manifest_str)
    if ret:
        return ret

    if not manifest_str:
        print("Error: Manifest string is empty.", file=sys.stderr)
        return 1

    try:
        manifest = json.loads("".join(manifest_str))
    except json.JSONDecodeError as e:
        print(f"Error decoding JSON: {e}", file=sys.stderr)
        print(f"Manifest string: {''.join(manifest_str)}", file=sys.stderr)
        return 1

    layer = ""
    for l in manifest["layers"]:
        if l["mediaType"] == "application/vnd.ollama.image.model":
            layer = l["digest"]
            break

    blob_url = f"https://registry.ollama.ai/v2/{model}/blobs/{layer}"
    return download(blob_url, headers, bn, True)

def print_usage():
    print(
        "Usage:\n"
        "  lm-pull <model>\n"
        "\n"
        "Examples:\n"
        "  lm-pull llama3\n"
        "  lm-pull ollama://granite-code\n"
        "  lm-pull ollama://smollm:135m\n"
        "  lm-pull hf://QuantFactory/SmolLM-135M-GGUF/SmolLM-135M.Q2_K.gguf\n"
        "  lm-pull huggingface://bartowski/SmolLM-1.7B-Instruct-v0.2-GGUF/"
        "SmolLM-1.7B-Instruct-v0.2-IQ3_M.gguf\n"
        "  lm-pull https://example.com/some-file1.gguf\n"
    )

def main():
    if len(sys.argv) != 2:
        print_usage()
        return 1

    model = sys.argv[1]
    if model == "-h" or model == "--help":
        print_usage()
        return 0

    bn = os.path.basename(model)
    headers = {
        "Accept": "application/vnd.docker.distribution.manifest.v2+json"
    }

    if model.startswith("https://"):
        download(model, {}, bn, True)
    elif model.startswith("hf://") or model.startswith("huggingface://"):
        model = model.split("://", 1)[1]
        huggingface_dl(model, headers, bn)
    elif model.startswith("ollama://"):
        model = model.split("://", 1)[1]
        ollama_dl(model, headers, bn)
    else:
        ollama_dl(model, headers, bn)

    return 0

if __name__ == "__main__":
    main()
