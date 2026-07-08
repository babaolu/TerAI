#pragma once
// include/providers/http_client.h

#include <string>
#include <map>
#include <functional>

namespace terai {

using Headers       = std::map<std::string, std::string>;
using StreamHandler = std::function<void(const std::string& chunk)>;

struct HttpResponse {
    int         status = 0;
    std::string body;
    bool        ok() const { return status >= 200 && status < 300; }
};

class HttpClient {
public:
    // GET request — no body
    static HttpResponse get(const std::string& url,
                            const Headers&     headers,
                            int                timeout_s = 10);

    // POST with full body returned
    static HttpResponse post(const std::string& url,
                             const std::string& body,
                             const Headers&     headers,
                             int                timeout_s = 120);

    // POST with NDJSON streaming (Ollama) — calls handler per line
    static void post_stream_ndjson(const std::string& url,
                                   const std::string& body,
                                   const Headers&     headers,
                                   StreamHandler      handler,
                                   int                timeout_s = 300);

    // POST with SSE streaming (Anthropic/OpenAI style) — calls handler per data: line
    static void post_stream_sse(const std::string& url,
                                const std::string& body,
                                const Headers&     headers,
                                StreamHandler      handler,
                                int                timeout_s = 300);

    // Initialise / cleanup libcurl global state
    static void global_init();
    static void global_cleanup();
};

} // namespace terai
