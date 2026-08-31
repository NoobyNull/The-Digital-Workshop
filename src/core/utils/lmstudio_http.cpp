#include "lmstudio_http.h"

#include <curl/curl.h>

#include "log.h"

namespace dw::lmstudio {

namespace detail {

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buffer = static_cast<std::string*>(userdata);
    size_t bytes = size * nmemb;
    buffer->append(ptr, bytes);
    return bytes;
}

} // namespace detail

namespace {

// Shared hardening for every local-service transfer: no SIGALRM-based DNS
// timeouts in a multithreaded process, and never route loopback through a
// user's http_proxy environment.
void setCommonOpts(CURL* curl) {
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "127.0.0.1,localhost");
}

} // namespace

std::string curlPost(const std::string& url, const std::string& body, long timeoutSeconds) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return {};
    }

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, detail::writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
    setCommonOpts(curl);

    CURLcode res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        log::errorf("LMStudioHTTP", "curl error: %s", curl_easy_strerror(res));
        return {};
    }
    if (httpCode != 200) {
        log::errorf("LMStudioHTTP", "HTTP %ld: %.500s", httpCode, response.c_str());
    }
    return response;
}

std::string curlGet(const std::string& url, long timeoutSeconds, bool quiet) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return {};
    }

    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, detail::writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
    setCommonOpts(curl);

    CURLcode res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (!quiet)
            log::errorf("LMStudioHTTP", "curl error: %s", curl_easy_strerror(res));
        return {};
    }
    if (httpCode != 200 && !quiet) {
        log::errorf("LMStudioHTTP", "HTTP %ld: %.500s", httpCode, response.c_str());
    }
    return response;
}

std::vector<uint8_t> base64Decode(const std::string& encoded) {
    static constexpr unsigned char kTable[128] = {
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62,
        64, 64, 64, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64, 64, 0,
        1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
        23, 24, 25, 64, 64, 64, 64, 64, 64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
        39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
    };

    std::vector<uint8_t> out;
    out.reserve(encoded.size() * 3 / 4);

    uint32_t val = 0;
    int bits = -8;
    for (char raw : encoded) {
        auto c = static_cast<unsigned char>(raw);
        if (c == '=' || c == '\n' || c == '\r') {
            continue;
        }
        if (c >= 128 || kTable[c] == 64) {
            continue;
        }
        val = (val << 6) | kTable[c];
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

std::string base64Encode(const uint8_t* data, size_t len) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len)
            n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len)
            n |= static_cast<uint32_t>(data[i + 2]);

        out.push_back(kAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kAlphabet[(n >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? kAlphabet[(n >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? kAlphabet[n & 0x3F] : '=');
    }
    return out;
}

std::string base64Encode(const std::vector<uint8_t>& data) {
    return base64Encode(data.data(), data.size());
}

} // namespace dw::lmstudio
