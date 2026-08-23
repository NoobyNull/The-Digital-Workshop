#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dw::lmstudio {

// POST JSON to a URL, return response body. Empty string on failure.
std::string curlPost(const std::string& url, const std::string& body);

// GET a URL, return response body. Empty string on failure.
// timeoutSeconds bounds the whole transfer (connect + response).
std::string curlGet(const std::string& url, long timeoutSeconds = 5);

// Base64 decode (standard alphabet, RFC 4648)
std::vector<uint8_t> base64Decode(const std::string& encoded);

// Base64 encode (standard alphabet, RFC 4648)
std::string base64Encode(const uint8_t* data, size_t len);
std::string base64Encode(const std::vector<uint8_t>& data);

namespace detail {
// libcurl write callback
size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata);
} // namespace detail

} // namespace dw::lmstudio
