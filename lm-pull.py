#!/usr/bin/python3

import os
import sys
import time
import urllib.request
import urllib.error
import shutil
from datetime import datetime
import json
import fcntl

class File:
    def __init__(self):
        self.file = None
        self.fd = -1

    def open(self, filename, mode):
        self.file = open(filename, mode)
        return self.file

    def lock(self):
        if self.file:
            self.fd = self.file.fileno()
            try:
                fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                self.fd = -1
                return 1

        return 0

    def __del__(self):
        if self.fd >= 0:
            fcntl.flock(self.fd, fcntl.LOCK_UN)

        if self.file:
            self.file.close()

class HttpClient:
    def __init__(self):
        pass

    def init(self, url, headers, output_file, progress, response_str=None):
        output_file_partial = None
        if output_file:
            output_file_partial = output_file + ".partial"

        self.file_size = self.set_resume_point(output_file_partial)
        self.printed = False
        if self.urlopen(url, headers):
            return 1

        self.total_to_download = int(self.response.getheader('content-length', 0))
        if response_str is not None:
            response_str.append(self.response.read().decode('utf-8'))
        else:
            out = File()
            if not out.open(output_file_partial, "ab"):
                print("Failed to open file")

                return 1

            if out.lock():
                print("Failed to exclusively lock file")

                return 1

            self.now_downloaded = 0
            self.start_time = time.time()
            self.perform_download(out.file, progress)

        if output_file:
            os.rename(output_file_partial, output_file)

        if self.printed:
            print("\n")

        return 0

    def urlopen(self, url, headers):
        headers["Range"] = f"bytes={self.file_size}-"
        request = urllib.request.Request(url, headers=headers)
        try:
            self.response = urllib.request.urlopen(request)
        except urllib.error.HTTPError as e:
            print(f"Request failed: {e.code}", file=sys.stderr)

            return 1

        if self.response.status not in (200, 206):
            print(f"Request failed: {self.response.status}", file=sys.stderr)

            return 1

        return 0

    def perform_download(self, file, progress):
        self.total_to_download += self.file_size
        self.now_downloaded = 0
        self.start_time = time.time()
        while True:
            data = self.response.read(1024)
            if not data:
                break

            size = file.write(data)
            if progress:
                self.update_progress(size)

    def human_readable_time(self, seconds):
        hrs = int(seconds) // 3600
        mins = (int(seconds) % 3600) // 60
        secs = int(seconds) % 60
        width = 10
        if hrs > 0:
            return f"{hrs}h {mins:02}m {secs:02}s".rjust(width)
        elif mins > 0:
            return f"{mins}m {secs:02}s".rjust(width)
        else:
            return f"{secs}s".rjust(width)

    def human_readable_size(self, size):
        width = 10
        for unit in ["B", "KB", "MB", "GB", "TB"]:
            if size < 1024:
                return f"{size:.2f} {unit}".rjust(width)

            size /= 1024

        return f"{size:.2f} PB".rjust(width)

    def get_terminal_width(self):
        return shutil.get_terminal_size().columns

    def generate_progress_prefix(self, percentage):
        return f"{percentage}% |".rjust(6)

    def generate_progress_suffix(self, now_downloaded_plus_file_size, speed, estimated_time):
        return f"{self.human_readable_size(now_downloaded_plus_file_size)}/{self.human_readable_size(self.total_to_download)}{self.human_readable_size(speed)}/s{self.human_readable_time(estimated_time)}"

    def calculate_progress_bar_width(self, progress_prefix, progress_suffix):
        progress_bar_width = self.get_terminal_width() - len(progress_prefix) - len(progress_suffix) - 3
        if progress_bar_width < 1:
            progress_bar_width = 1

        return progress_bar_width

    def generate_progress_bar(self, progress_bar_width, percentage):
        pos = (percentage * progress_bar_width) // 100
        progress_bar = ""
        for i in range(progress_bar_width):
            progress_bar += "█" if i < pos else " "

        return progress_bar

    def set_resume_point(self, output_file):
        if output_file and os.path.exists(output_file):
            return os.path.getsize(output_file)

        return 0

    def print_progress(self, progress_prefix, progress_bar, progress_suffix):
        print(f"\r{progress_prefix}{progress_bar}| {progress_suffix}", end="")

    def update_progress(self, chunk_size):
        self.now_downloaded += chunk_size
        now_downloaded_plus_file_size = self.now_downloaded + self.file_size
        percentage = (now_downloaded_plus_file_size * 100) // self.total_to_download
        progress_prefix = self.generate_progress_prefix(percentage)
        speed = self.calculate_speed(self.now_downloaded, self.start_time)
        tim = (self.total_to_download - self.now_downloaded) // speed
        progress_suffix = self.generate_progress_suffix(now_downloaded_plus_file_size, speed, tim)
        progress_bar_width = self.calculate_progress_bar_width(progress_prefix, progress_suffix)
        progress_bar = self.generate_progress_bar(progress_bar_width, percentage)
        self.print_progress(progress_prefix, progress_bar, progress_suffix)
        self.printed = True

    def calculate_speed(self, now_downloaded, start_time):
        now = time.time()
        elapsed_seconds = now - start_time
        return now_downloaded / elapsed_seconds

def download(url, headers, output_file, progress, response_str=None):
    http = HttpClient()
    return http.init(url, headers, output_file, progress, response_str)

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
