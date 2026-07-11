#pragma once
// include/providers/ollama.h

#include "providers/base_provider.h"
#include "providers/http_client.h"

namespace terai {

class OllamaProvider : public BaseProvider {
public:
    explicit OllamaProvider(const json& cfg);

    LLMResponse complete(const std::vector<Message>& messages,
                         const std::string& system,
                         int max_tokens, double temperature) override;

    LLMResponse stream(const std::vector<Message>& messages,
                       const std::string& system,
                       int max_tokens, double temperature,
                       StreamCallback cb) override;

    // Check if Ollama server is reachable
    bool is_available() const;

    // On-device CPU inference can be far slower than a datacenter GPU —
    // callers doing background/batch work should set a realistic timeout
    // instead of hitting the 300s default on every large prompt.
    void set_timeout(int seconds) { _timeout_s = seconds; }

    // How long Ollama should keep the model loaded in memory after this
    // request, e.g. "30m" or "-1" (indefinite). Ollama's own default is
    // 5 minutes — if the caller waits longer than that between requests
    // (e.g. the daemon's sleep interval), every subsequent request pays a
    // full cold model-load on top of generation time, both inside the
    // same request timeout. Setting this to match/exceed the sleep
    // interval keeps the model warm across cycles.
    void set_keep_alive(const std::string& duration) { _keep_alive = duration; }

private:
    int  _timeout_s = 300;
    std::string _keep_alive;  // empty = use Ollama's own default (5m)

    json build_payload(const std::vector<Message>& messages,
                       const std::string& system,
                       int max_tokens, double temperature,
                       bool do_stream) const;
};

} // namespace terai
