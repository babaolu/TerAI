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

private:
    int  _timeout_s = 300;

    json build_payload(const std::vector<Message>& messages,
                       const std::string& system,
                       int max_tokens, double temperature,
                       bool do_stream) const;
};

} // namespace terai
