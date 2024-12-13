#include <curl/curl.h>
#include <sys/stat.h>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"

#define printe(...)               \
  do {                            \
    fprintf(stderr, __VA_ARGS__); \
  } while (0)

struct progress_data {
  size_t file_size = 0;
  std::chrono::steady_clock::time_point start_time =
      std::chrono::steady_clock::now();
  bool printed = false;
};

struct FileDeleter {
  void operator()(FILE* file) const {
    if (file) {
      fclose(file);
    }
  }
};

typedef std::unique_ptr<FILE, FileDeleter> FILE_ptr;

// Function to get the basename of a path
static std::string basename(const std::string& path) {
  const size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) {
    return path;
  }

  return path.substr(pos + 1);
}

static bool starts_with(const std::string& str, const std::string& prefix) {
  return str.rfind(prefix, 0) == 0;
}

static int remove_proto(std::string& model_) {
  const std::string::size_type pos = model_.find("://");
  if (pos == std::string::npos) {
    return 1;
  }

  model_ = model_.substr(pos + 3);  // Skip past "://"
  return 0;
}

class CurlWrapper {
 public:
  int init(const std::string& url,
           const std::vector<std::string>& headers,
           const std::string& output_file,
           const bool progress,
           std::string* response_str = nullptr) {
    std::string output_file_partial;
    curl = curl_easy_init();
    if (!curl) {
      return 1;
    }

    progress_data data;
    FILE_ptr out;
    if (!output_file.empty()) {
      output_file_partial = output_file + ".partial";
      out.reset(fopen(output_file_partial.c_str(), "ab"));
    }

    set_write_options(response_str, out);
    data.file_size = set_resume_point(output_file_partial);
    set_progress_options(progress, data);
    set_headers(headers);
    perform(url);
    if (!output_file.empty()) {
      std::filesystem::rename(output_file_partial, output_file);
    }

    if (data.printed) {
      printe("\n");
    }

    return 0;
  }

  ~CurlWrapper() {
    if (chunk) {
      curl_slist_free_all(chunk);
    }

    if (curl) {
      curl_easy_cleanup(curl);
    }
  }

 private:
  CURL* curl = nullptr;
  struct curl_slist* chunk = nullptr;

  void set_write_options(std::string* response_str, const FILE_ptr& out) {
    if (response_str) {
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, capture_data);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, response_str);
    } else {
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, out.get());
    }
  }

  size_t set_resume_point(const std::string& output_file) {
    size_t file_size = 0;
    if (std::filesystem::exists(output_file)) {
      file_size = std::filesystem::file_size(output_file);
      curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE,
                       static_cast<curl_off_t>(file_size));
    }

    return file_size;
  }

  void set_progress_options(bool progress, progress_data& data) {
    if (progress) {
      curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
      curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &data);
      curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    }
  }

  void set_headers(const std::vector<std::string>& headers) {
    if (!headers.empty()) {
      if (chunk) {
        curl_slist_free_all(chunk);
        chunk = 0;
      }

      for (const auto& header : headers) {
        chunk = curl_slist_append(chunk, header.c_str());
      }

      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    }
  }

  void perform(const std::string& url) {
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      printe("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    }
  }

  static std::string human_readable_time(double seconds) {
    int hrs = static_cast<int>(seconds) / 3600;
    int mins = (static_cast<int>(seconds) % 3600) / 60;
    int secs = static_cast<int>(seconds) % 60;

    std::ostringstream out;
    if (hrs > 0) {
      out << hrs << "h " << std::setw(2) << std::setfill('0') << mins << "m "
          << std::setw(2) << std::setfill('0') << secs << "s";
    } else if (mins > 0) {
      out << mins << "m " << std::setw(2) << std::setfill('0') << secs << "s";
    } else {
      out << secs << "s";
    }

    return out.str();
  }

  static std::string human_readable_size(curl_off_t size) {
    static const char* suffix[] = {"B", "KB", "MB", "GB", "TB"};
    char length = sizeof(suffix) / sizeof(suffix[0]);
    int i = 0;
    double dbl_size = size;
    if (size > 1024) {
      for (i = 0; (size / 1024) > 0 && i < length - 1; i++, size /= 1024) {
        dbl_size = size / 1024.0;
      }
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << dbl_size << " " << suffix[i];
    return out.str();
  }

  static int progress_callback(void* ptr,
                               curl_off_t total_to_download,
                               curl_off_t now_downloaded,
                               curl_off_t,
                               curl_off_t) {
    progress_data* data = static_cast<progress_data*>(ptr);
    if (total_to_download <= 0) {
      return 0;
    }

    total_to_download += data->file_size;
    const curl_off_t now_downloaded_plus_file_size =
        now_downloaded + data->file_size;
    const curl_off_t percentage =
        (now_downloaded_plus_file_size * 100) / total_to_download;
    const curl_off_t pos = (percentage / 5);
    std::string progress_bar;
    for (int i = 0; i < 20; ++i) {
      progress_bar.append((i < pos) ? "█" : " ");
    }

    // Calculate download speed and estimated time to completion
    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed_seconds =
        now - data->start_time;
    const double speed = now_downloaded / elapsed_seconds.count();
    const double estimated_time = (total_to_download - now_downloaded) / speed;
    printe("\r%ld%% |%s| %s/%s  %.2f MB/s  %s      ", percentage,
           progress_bar.c_str(), human_readable_size(now_downloaded).c_str(),
           human_readable_size(total_to_download).c_str(),
           speed / (1024 * 1024), human_readable_time(estimated_time).c_str());
    fflush(stderr);
    data->printed = true;

    return 0;
  }

  // Function to write data to a file
  static size_t write_data(void* ptr, size_t size, size_t nmemb, void* stream) {
    FILE* out = static_cast<FILE*>(stream);
    return fwrite(ptr, size, nmemb, out);
  }

  // Function to capture data into a string
  static size_t capture_data(void* ptr,
                             size_t size,
                             size_t nmemb,
                             void* stream) {
    std::string* str = static_cast<std::string*>(stream);
    str->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
  }
};

int download(const std::string& url,
             const std::vector<std::string>& headers,
             const std::string& output_file,
             const bool progress,
             std::string* response_str = nullptr) {
  CurlWrapper curl;
  if (curl.init(url, headers, output_file, progress, response_str)) {
    return 1;
  }

  return 0;
}

int huggingface_dl(const std::string& model,
                   const std::vector<std::string> headers,
                   const std::string& bn) {
  // Find the second occurrence of '/' after protocol string
  size_t pos = model.find('/');
  pos = model.find('/', pos + 1);
  if (pos == std::string::npos) {
    return 1;
  }

  const std::string hfr = model.substr(0, pos);
  const std::string hff = model.substr(pos + 1);
  const std::string url =
      "https://huggingface.co/" + hfr + "/resolve/main/" + hff;
  return download(url, headers, bn, true);
}

int ollama_dl(std::string& model,
              const std::vector<std::string> headers,
              const std::string& bn) {
  if (model.find('/') == std::string::npos) {
    model = "library/" + model;
  }

  std::string model_tag = "latest";
  size_t colon_pos = model.find(':');
  if (colon_pos != std::string::npos) {
    model_tag = model.substr(colon_pos + 1);
    model = model.substr(0, colon_pos);
  }

  std::string manifest_url =
      "https://registry.ollama.ai/v2/" + model + "/manifests/" + model_tag;
  std::string manifest_str;
  const int ret = download(manifest_url, headers, "", false, &manifest_str);
  if (ret) {
    return ret;
  }

  nlohmann::json manifest = nlohmann::json::parse(manifest_str);
  std::string layer;
  for (const auto& l : manifest["layers"]) {
    if (l["mediaType"] == "application/vnd.ollama.image.model") {
      layer = l["digest"];
      break;
    }
  }

  std::string blob_url =
      "https://registry.ollama.ai/v2/" + model + "/blobs/" + layer;
  return download(blob_url, headers, bn, true);
}

static void print_usage() {
  printf(
      "Usage:\n"
      "  lm-pull <model>\n"
      "\n"
      "Examples:\n"
      "  lm-pull llama3\n"
      "  lm-pull ollama://granite-code\n"
      "  lm-pull ollama://smollm:135m\n"
      "  lm-pull hf://QuantFactory/SmolLM-135M-GGUF/SmolLM-135M.Q2_K.gguf\n"
      "  lm-pull "
      "huggingface://bartowski/SmolLM-1.7B-Instruct-v0.2-GGUF/"
      "SmolLM-1.7B-Instruct-v0.2-IQ3_M.gguf\n"
      "  lm-pull https://example.com/some-file1.gguf\n");
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    print_usage();
    return 1;
  }

  std::string model = argv[1];
  if (model == "-h" || model == "--help") {
    print_usage();
    return 0;
  }

  const std::string bn = basename(model);
  const std::vector<std::string> headers = {
      "--header",
      "Accept: application/vnd.docker.distribution.manifest.v2+json"};

  if (starts_with(model, "https://")) {
    download(model, {}, bn, true);
  } else if (starts_with(model, "hf://") ||
             starts_with(model, "huggingface://")) {
    remove_proto(model);
    huggingface_dl(model, headers, bn);
  } else if (starts_with(model, "ollama://")) {
    remove_proto(model);
    ollama_dl(model, headers, bn);
  } else {
    ollama_dl(model, headers, bn);
  }

  return 0;
}
