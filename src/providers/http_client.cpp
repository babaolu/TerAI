// src/providers/http_client.cpp
#include "providers/http_client.h"
#include <curl/curl.h>
#include <stdexcept>
#include <sstream>
#include <iostream>

namespace terai {

// ── Write callback helpers ────────────────────────────────────────────────────
static size_t write_string(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    static_cast<std::string*>(userdata)->append(ptr, total);
    return total;
}

struct StreamCtx {
    StreamHandler handler;
    std::string   buffer;   // accumulate partial lines
    bool          is_ndjson;// vs SSE
};

static size_t write_stream(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    auto*  ctx   = static_cast<StreamCtx*>(userdata);
    ctx->buffer.append(ptr, total);

    // Process complete lines
    size_t pos;
    while ((pos = ctx->buffer.find('\n')) != std::string::npos) {
        std::string line = ctx->buffer.substr(0, pos);
        ctx->buffer.erase(0, pos + 1);

        // Trim \r
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty()) continue;

        if (ctx->is_ndjson) {
            // Ollama: each line is a JSON object
            ctx->handler(line);
        } else {
            // SSE: lines look like "data: {...}" or "data: [DONE]"
            if (line.rfind("data: ", 0) == 0) {
                std::string payload = line.substr(6);
                if (payload == "[DONE]") return total;
                ctx->handler(payload);
            }
        }
    }
    return total;
}

// ── Build curl slist from headers ─────────────────────────────────────────────
static curl_slist* make_slist(const Headers& headers) {
    curl_slist* list = nullptr;
    for (auto& [k, v] : headers)
        list = curl_slist_append(list, (k + ": " + v).c_str());
    return list;
}

// ── Global init / cleanup ─────────────────────────────────────────────────────
void HttpClient::global_init()    { curl_global_init(CURL_GLOBAL_DEFAULT); }
void HttpClient::global_cleanup() { curl_global_cleanup(); }

// ── POST (blocking) ───────────────────────────────────────────────────────────
HttpResponse HttpClient::post(const std::string& url,
                              const std::string& body,
                              const Headers&     headers,
                              int                timeout_s)
{
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    HttpResponse resp;
    curl_slist*  hlist = make_slist(headers);

    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,  (long)body.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     hlist);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &resp.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        (long)timeout_s);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        curl_slist_free_all(hlist);
        curl_easy_cleanup(curl);
        throw std::runtime_error(std::string("curl error: ") + curl_easy_strerror(rc));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status);
    curl_slist_free_all(hlist);
    curl_easy_cleanup(curl);

    if (!resp.ok()) {
        throw std::runtime_error("HTTP " + std::to_string(resp.status) + ": " + resp.body);
    }
    return resp;
}

// ── POST with NDJSON streaming (Ollama) ──────────────────────────────────────
void HttpClient::post_stream_ndjson(const std::string& url,
                                    const std::string& body,
                                    const Headers&     headers,
                                    StreamHandler      handler,
                                    int                timeout_s)
{
    CURL*      curl  = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");
    curl_slist* hlist = make_slist(headers);

    StreamCtx ctx{ handler, "", true };

    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,  (long)body.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     hlist);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_stream);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        (long)timeout_s);

    CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(hlist);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK)
        throw std::runtime_error(std::string("stream curl error: ") + curl_easy_strerror(rc));
}

// ── POST with SSE streaming (OpenAI / Anthropic) ─────────────────────────────
void HttpClient::post_stream_sse(const std::string& url,
                                 const std::string& body,
                                 const Headers&     headers,
                                 StreamHandler      handler,
                                 int                timeout_s)
{
    CURL*       curl  = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");
    curl_slist* hlist = make_slist(headers);

    StreamCtx ctx{ handler, "", false };

    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,  (long)body.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     hlist);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_stream);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        (long)timeout_s);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(hlist);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK)
        throw std::runtime_error(std::string("stream curl error: ") + curl_easy_strerror(rc));
}

} // namespace terai
