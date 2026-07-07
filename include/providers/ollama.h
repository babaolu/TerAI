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

private:
    json build_payload(const std::vector<Message>& messages,
                       const std::string& system,
                       int max_tokens, double temperature,
                       bool do_stream) const;
};

} // namespace terai
