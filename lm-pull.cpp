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

// Function to download a file using libcurl
void download(const std::string& url, const std::vector<std::string>& headers, const std::string& output_file) {
    CURL* curl;
    CURLcode res;
    curl = curl_easy_init();
    if(curl) {
        std::ofstream out(output_file, std::ios::binary);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
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
        download(model, {}, bn);
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
        std::cout << "Pulling manifest for " << model_trimmed << ":" << model_tag << std::endl;
        std::string accept_header = "Accept: application/vnd.docker.distribution.manifest.v2+json";
        std::vector<std::string> headers = {"--header", accept_header};
        std::string manifest_url = "https://registry.ollama.ai/v2/" + model_trimmed + "/manifests/" + model_tag;

        download(manifest_url, headers, "manifest.json");

        std::ifstream manifest_file("manifest.json");
        nlohmann::json manifest;
        manifest_file >> manifest;
        manifest_file.close();

        std::string layer;
        for (const auto& l : manifest["layers"]) {
            if (l["mediaType"] == "application/vnd.ollama.image.model") {
                layer = l["digest"];
                break;
            }
        }

        std::cout << "Pulling blob " << layer << std::endl;
        std::string blob_url = "https://registry.ollama.ai/v2/" + model_trimmed + "/blobs/" + layer;
        download(blob_url, headers, model_bn);
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
