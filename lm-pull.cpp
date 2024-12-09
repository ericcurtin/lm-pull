#include <curl/curl.h>
#include <sys/stat.h>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct progress_data {
  size_t file_size = 0;
  bool printed = false;
};

// Function to get the basename of a path
static std::string basename(const std::string& path) {
  return path.substr(path.find_last_of("/\\") + 1);
}

// Function to check if a file exists
static bool file_exists(const std::string& name) {
  struct stat buffer;
  return (stat(name.c_str(), &buffer) == 0);
}

// Function to get the size of a file
static size_t get_file_size(const std::string& filename) {
  FILE* file = fopen(filename.c_str(), "rb");
  if (!file)
    return 0;
  fseek(file, 0, SEEK_END);
  size_t size = ftell(file);
  fclose(file);
  return size;
}

// Function to write data to a file
static size_t write_data(void* ptr, size_t size, size_t nmemb, void* stream) {
  FILE* out = static_cast<FILE*>(stream);
  return fwrite(ptr, size, nmemb, out);
}

// Function to capture data into a string
static size_t capture_data(void* ptr, size_t size, size_t nmemb, void* stream) {
  std::string* str = static_cast<std::string*>(stream);
  str->append(static_cast<char*>(ptr), size * nmemb);
  return size * nmemb;
}

// Function to display progress
static int progress_callback(void* ptr,
                             curl_off_t total_to_download,
                             curl_off_t now_downloaded,
                             curl_off_t,
                             curl_off_t) {
  progress_data* data = static_cast<progress_data*>(ptr);
  if (total_to_download <= 0)
    return 0;

  total_to_download += data->file_size;
  now_downloaded += data->file_size;
  if (now_downloaded >= total_to_download)
    return 0;

  int percentage = static_cast<int>((now_downloaded * 100) / total_to_download);
  fprintf(stderr, "\rProgress: %d%% |", percentage);
  int pos = (percentage / 5);
  for (int i = 0; i < 20; ++i) {
    if (i < pos)
      fprintf(stderr, "█");
    else
      fprintf(stderr, " ");
  }

  fprintf(stderr, "| %llu/%llu bytes", now_downloaded, total_to_download);
  fflush(stderr);
  data->printed = true;

  return 0;
}

static CURL* init_curl() {
  return curl_easy_init();
}

static void set_write_options(CURL* curl,
                              const std::string& output_file,
                              std::string* response_str) {
  if (response_str) {
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, capture_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response_str);
  } else {
    FILE* out = fopen(output_file.c_str(), "ab");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
  }
}

static void set_resume_point(CURL* curl, const std::string& output_file) {
  size_t file_size = 0;
  if (file_exists(output_file)) {
    file_size = get_file_size(output_file);
    curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE,
                     static_cast<curl_off_t>(file_size));
  }
}

static void set_progress_options(CURL* curl,
                                 const bool progress,
                                 progress_data& data) {
  if (progress) {
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &data);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
  }
}

static void set_headers(CURL* curl, const std::vector<std::string>& headers) {
  if (!headers.empty()) {
    struct curl_slist* chunk = NULL;
    for (const auto& header : headers)
      chunk = curl_slist_append(chunk, header.c_str());

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
  }
}

static void perform_curl(CURL* curl, const std::string& url) {
  CURLcode res;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

  res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
  }
}

static void download(const std::string& url,
                     const std::vector<std::string>& headers,
                     const std::string& output_file,
                     const bool progress,
                     std::string* response_str = nullptr) {
  CURL* curl = init_curl();
  if (curl) {
    set_write_options(curl, output_file, response_str);
    set_resume_point(curl, output_file);
    progress_data data;
    data.file_size = get_file_size(output_file);
    set_progress_options(curl, progress, data);
    set_headers(curl, headers);
    perform_curl(curl, url);

    if (data.printed)
      fprintf(stderr, "\n");

    curl_easy_cleanup(curl);
  }
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

static int huggingface_dl(const std::string& model,
                          const std::vector<std::string> headers,
                          const std::string& bn) {
  // Find the second occurrence of '/' after protocol string
  size_t pos = model.find('/');
  pos = model.find('/', pos + 1);
  if (pos == std::string::npos)
    return 1;

  const std::string hfr = model.substr(0, pos);
  const std::string hff = model.substr(pos + 1);
  const std::string url =
      "https://huggingface.co/" + hfr + "/resolve/main/" + hff;
  download(url, headers, bn, true);

  return 0;
}

static int ollama_dl(std::string& model,
                     const std::vector<std::string> headers,
                     const std::string& bn) {
  if (model.find('/') == std::string::npos)
    model = "library/" + model;

  std::string model_tag = "latest";
  size_t colon_pos = model.find(':');
  if (colon_pos != std::string::npos) {
    model_tag = model.substr(colon_pos + 1);
    model = model.substr(0, colon_pos);
  }

  std::string manifest_url =
      "https://registry.ollama.ai/v2/" + model + "/manifests/" + model_tag;
  std::string manifest_str;
  download(manifest_url, headers, "", false, &manifest_str);
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
  download(blob_url, headers, bn, true);

  return 0;
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <model>\n", argv[0]);
    return 1;
  }

  std::string model = argv[1];
  std::string bn = basename(model);
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
