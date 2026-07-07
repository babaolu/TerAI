// src/tools/web_search.cpp
#include "tools/web_search.h"
#include "providers/http_client.h"
#include <curl/curl.h>
#include <stdexcept>
#include <sstream>

namespace terai {

WebSearchTool::WebSearchTool(const json& cfg) {
    _engine     = cfg.value("engine",         "ddg");
    _google_key = cfg.value("google_api_key", "");
    _google_cx  = cfg.value("google_cx",      "");
}

// ── URL encode via libcurl ────────────────────────────────────────────────────
static std::string url_encode(const std::string& raw) {
    CURL* c = curl_easy_init();
    if (!c) return raw;
    char* enc = curl_easy_escape(c, raw.c_str(), (int)raw.size());
    std::string result(enc);
    curl_free(enc);
    curl_easy_cleanup(c);
    return result;
}

// ── GET helper (no body) ──────────────────────────────────────────────────────
static std::string http_get(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl init failed");

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* p, size_t s, size_t n, void* u) -> size_t {
        static_cast<std::string*>(u)->append(p, s*n); return s*n;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,1L);
    struct curl_slist* h = curl_slist_append(nullptr, "User-Agent: TerAI/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);

    CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(h);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK)
        throw std::runtime_error(curl_easy_strerror(rc));
    return body;
}

// ── DuckDuckGo instant answer ─────────────────────────────────────────────────
ToolResult WebSearchTool::ddg_search(const std::string& query, int max_results) {
    std::string url = "https://api.duckduckgo.com/?q=" + url_encode(query)
                    + "&format=json&no_html=1&skip_disambig=1";
    std::string raw = http_get(url);

    json data;
    try {
        data = json::parse(raw);
    } catch (std::exception&) {
        // Network blocked, captive portal, or non-JSON response (e.g. proxy error page)
        std::string snippet = raw.substr(0, std::min((size_t)150, raw.size()));
        return {false,
            "Search unavailable — non-JSON response received. "
            "Check network connectivity. Response started with: \"" + snippet + "\"",
            "web_search"};
    }

    std::vector<std::string> results;

    if (!data["Abstract"].get<std::string>().empty()) {
        results.push_back(
            "**" + data.value("Heading","Answer") + "**\n"
            + data["Abstract"].get<std::string>() + "\n"
            + data.value("AbstractURL","")
        );
    }

    for (auto& topic : data["RelatedTopics"]) {
        if ((int)results.size() >= max_results) break;
        if (topic.is_object() && !topic.value("Text","").empty())
            results.push_back("- " + topic["Text"].get<std::string>()
                              + "\n  " + topic.value("FirstURL",""));
    }

    for (auto& r : data["Results"]) {
        if ((int)results.size() >= max_results) break;
        if (!r.value("Text","").empty())
            results.push_back("- " + r["Text"].get<std::string>()
                              + "\n  " + r.value("FirstURL",""));
    }

    if (results.empty())
        results.push_back("No instant results for '" + query + "'.");

    std::string out = "Search: " + query + "\n\n";
    for (auto& r : results) out += r + "\n\n";
    return {true, out, "web_search"};
}

ToolResult WebSearchTool::google_search(const std::string& query, int max_results) {
    std::string url = "https://www.googleapis.com/customsearch/v1?key=" + _google_key
                    + "&cx=" + _google_cx
                    + "&q=" + url_encode(query)
                    + "&num=" + std::to_string(max_results);
    std::string raw = http_get(url);
    json data = json::parse(raw);

    std::string out = "Google results for: " + query + "\n\n";
    for (auto& item : data["items"])
        out += "**" + item.value("title","") + "**\n"
             + item.value("snippet","") + "\n"
             + item.value("link","") + "\n\n";
    return {true, out, "web_search"};
}

ToolResult WebSearchTool::run(const std::map<std::string,std::string>& args) {
    auto it = args.find("query");
    if (it == args.end())
        return {false, "Missing required argument: query", "web_search"};

    int max = 5;
    auto mit = args.find("max_results");
    if (mit != args.end()) {
        try { max = std::stoi(mit->second); } catch (...) {}
    }

    try {
        if (_engine == "google" && !_google_key.empty())
            return google_search(it->second, max);
        return ddg_search(it->second, max);
    } catch (std::exception& e) {
        return {false, std::string("Search error: ") + e.what(), "web_search"};
    }
}

} // namespace terai
