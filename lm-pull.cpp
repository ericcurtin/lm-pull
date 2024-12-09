#include <iostream>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <nlohmann/json.hpp>

// Function to get the basename of a path
std::string basename(const std::string& path) {
    return path.substr(path.find_last_of("/\\") + 1);
}

// Function to check if a file exists
bool file_exists(const std::string& name) {
    struct stat buffer;
    return (stat(name.c_str(), &buffer) == 0);
}

// Function to write data to a file
static size_t write_data(void* ptr, size_t size, size_t nmemb, void* stream) {
    std::ofstream* out = static_cast<std::ofstream*>(stream);
    out->write(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

// Function to capture data into a string
static size_t capture_data(void* ptr, size_t size, size_t nmemb, void* stream) {
    std::string* str = static_cast<std::string*>(stream);
    str->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

// Function to display progress
int progress_callback(void* ptr, curl_off_t total_to_download, curl_off_t now_downloaded, curl_off_t, curl_off_t) {
    if (total_to_download <= 0) return 0;
    int percentage = static_cast<int>((now_downloaded * 100) / total_to_download);
    std::cerr << "\r" << "Progress: " << percentage << "% |";
    int pos = (percentage / 2);
    for (int i = 0; i < 50; ++i) {
        if (i < pos) std::cerr << "█";
        else std::cerr << " ";
    }
    std::cerr << "| " << now_downloaded << "/" << total_to_download << " bytes";
    std::cerr.flush();
    return 0;
}

// Function to download a file using libcurl
void download(const std::string& url, const std::vector<std::string>& headers, const std::string& output_file, const bool progress, std::string* response_str = nullptr) {
    CURL* curl;
    CURLcode res;
    curl = curl_easy_init();
    if(curl) {
        if (response_str) {
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, capture_data);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, response_str);
        } else {
            std::ofstream out(output_file, std::ios::binary);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        if (progress) {
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
        }

        if (!headers.empty()) {
            struct curl_slist* chunk = NULL;
            for (const auto& header : headers) {
                chunk = curl_slist_append(chunk, header.c_str());
            }
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
        }
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }
        curl_easy_cleanup(curl);
    }
}

void main_function(const std::string& model) {
    std::string bn = basename(model);
    if (model.rfind("https://", 0) == 0) {
        download(model, {}, bn, true);
    } else if (model.rfind("ollama://", 0) == 0) {
        std::string model_trimmed = model.substr(9);
        if (model_trimmed.find('/') == std::string::npos) {
            model_trimmed = "library/" + model_trimmed;
        }

        std::string model_tag = "latest";
        size_t colon_pos = model_trimmed.find(':');
        if (colon_pos != std::string::npos) {
            model_tag = model_trimmed.substr(colon_pos + 1);
            model_trimmed = model_trimmed.substr(0, colon_pos);
        }

        std::string model_bn = basename(model_trimmed);
        std::string accept_header = "Accept: application/vnd.docker.distribution.manifest.v2+json";
        std::vector<std::string> headers = {"--header", accept_header};
        std::string manifest_url = "https://registry.ollama.ai/v2/" + model_trimmed + "/manifests/" + model_tag;

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

        std::string blob_url = "https://registry.ollama.ai/v2/" + model_trimmed + "/blobs/" + layer;
        download(blob_url, headers, model_bn, true);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <model>" << std::endl;
        return 1;
    }

    main_function(argv[1]);
    return 0;
}

