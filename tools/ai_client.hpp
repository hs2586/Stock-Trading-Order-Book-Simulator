// ai_client.hpp - tiny AI client with two providers, both using libcurl
//
//   ollama (default) : free, runs on your own computer, no key needed
//   claude           : Anthropic API, needs ANTHROPIC_API_KEY and paid credit
//
// Choose with the environment variable AI_PROVIDER (ollama or claude).
//
// Usage:
//   std::string error;
//   std::string answer = ai::ask("your prompt", error);
//   if (answer.empty()) { /* look at error */ }
#pragma once
#include <cstdlib>
#include <string>
#include <vector>
#include <curl/curl.h>

namespace ai {

// Turns normal text into text that is safe inside a JSON string ("..." and \n etc.)
inline std::string jsonEscape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 15];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

inline void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

inline unsigned parseHex4(const std::string& s, size_t pos) {
    if (pos + 4 > s.size()) return 0;
    return static_cast<unsigned>(std::strtoul(s.substr(pos, 4).c_str(), nullptr, 16));
}

// Finds  "key":"....."  in a JSON text and returns the unescaped string value.
// (A minimal reader. A real project would use a JSON library such as nlohmann/json.)
inline bool readJsonString(const std::string& body, const std::string& key, std::string& out) {
    const std::string marker = "\"" + key + "\":\"";
    size_t p = body.find(marker);
    if (p == std::string::npos) return false;
    p += marker.size();
    out.clear();
    while (p < body.size() && body[p] != '"') {
        const char c = body[p];
        if (c != '\\') { out += c; ++p; continue; }
        if (++p >= body.size()) break;
        switch (body[p]) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                unsigned cp = parseHex4(body, p + 1);
                p += 4;
                // emoji and some symbols arrive as two \u pieces (a "surrogate pair")
                if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 < body.size() &&
                    body[p + 1] == '\\' && body[p + 2] == 'u') {
                    const unsigned low = parseHex4(body, p + 3);
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        p += 6;
                    }
                }
                appendUtf8(out, cp);
                break;
            }
            default: out += body[p];   // \"  \\  \/
        }
        ++p;
    }
    return true;
}

inline size_t writeCallback(char* data, size_t size, size_t count, void* userdata) {
    static_cast<std::string*>(userdata)->append(data, size * count);
    return size * count;
}

// Sends a JSON body with HTTP POST. Returns true if the server answered at all.
inline bool httpPost(const std::string& url, const std::vector<std::string>& extraHeaders,
                     const std::string& body, long timeoutSeconds,
                     std::string& response, long& status, std::string& error) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "could not start libcurl";
        return false;
    }
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "content-type: application/json");
    for (const std::string& h : extraHeaders) headers = curl_slist_append(headers, h.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);

    const CURLcode rc = curl_easy_perform(curl);
    status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc == CURLE_COULDNT_CONNECT) {
        error = "could not connect (is the server running?)";
        return false;
    }
    if (rc != CURLE_OK) {
        error = std::string("network error: ") + curl_easy_strerror(rc);
        return false;
    }
    return true;
}

// ---------- Provider 1: Ollama (free, local) ----------
inline std::string askOllama(const std::string& prompt, std::string& error, int maxTokens) {
    const char* modelEnv = std::getenv("OLLAMA_MODEL");
    const std::string model = modelEnv ? modelEnv : "qwen2.5-coder:3b";

    const std::string body =
        "{\"model\":\"" + jsonEscape(model) + "\",\"prompt\":\"" + jsonEscape(prompt) +
        "\",\"stream\":false,\"options\":{\"num_ctx\":4096,\"num_predict\":" +
        std::to_string(maxTokens) + "}}";

    std::string response;
    long status = 0;
    if (!httpPost("http://localhost:11434/api/generate", {}, body, 600, response, status, error)) {
        error = "Ollama: " + error + ". Open the Ollama app, then try again.";
        return "";
    }
    if (status != 200) {
        std::string message;
        if (!readJsonString(response, "error", message)) message = response.substr(0, 300);
        error = "Ollama returned HTTP " + std::to_string(status) + ": " + message;
        return "";
    }
    std::string text;
    if (!readJsonString(response, "response", text)) {
        error = "Ollama: unexpected response format";
        return "";
    }
    return text;
}

// ---------- Provider 2: Claude (Anthropic API, paid) ----------
inline std::string askClaude(const std::string& prompt, std::string& error, int maxTokens) {
    const char* key = std::getenv("ANTHROPIC_API_KEY");
    if (!key || !*key) {
        error = "ANTHROPIC_API_KEY is not set";
        return "";
    }
    const char* modelEnv = std::getenv("CLAUDE_MODEL");
    const std::string model = modelEnv ? modelEnv : "claude-sonnet-5-5";

    const std::string body =
        "{\"model\":\"" + jsonEscape(model) + "\",\"max_tokens\":" + std::to_string(maxTokens) +
        ",\"messages\":[{\"role\":\"user\",\"content\":\"" + jsonEscape(prompt) + "\"}]}";

    std::string response;
    long status = 0;
    const std::vector<std::string> headers = {
        std::string("x-api-key: ") + key, "anthropic-version: 2023-06-01"};
    if (!httpPost("https://api.anthropic.com/v1/messages", headers, body, 180,
                  response, status, error))
        return "";
    if (status != 200) {
        std::string message;
        if (!readJsonString(response, "message", message)) message = response.substr(0, 300);
        error = "API returned HTTP " + std::to_string(status) + ": " + message;
        return "";
    }
    std::string text;
    if (!readJsonString(response, "text", text)) {
        error = "unexpected response format";
        return "";
    }
    return text;
}

// Picks the provider from AI_PROVIDER (default: ollama)
inline std::string ask(const std::string& prompt, std::string& error, int maxTokens = 2048) {
    const char* p = std::getenv("AI_PROVIDER");
    const std::string provider = p ? p : "ollama";
    if (provider == "ollama") return askOllama(prompt, error, maxTokens);
    if (provider == "claude") return askClaude(prompt, error, maxTokens);
    error = "unknown AI_PROVIDER '" + provider + "' (use ollama or claude)";
    return "";
}

inline std::string providerName() {
    const char* p = std::getenv("AI_PROVIDER");
    return p ? p : "ollama";
}

}  // namespace ai