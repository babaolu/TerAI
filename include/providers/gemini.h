#pragma once
// include/providers/gemini.h

#include "providers/base_provider.h"
#include "providers/http_client.h"

namespace terai {

class GeminiProvider : public BaseProvider {
public:
    explicit GeminiProvider(const json& cfg);

    LLMResponse complete(const std::vector<Message>& messages,
                         const std::string& system,
                         int max_tokens, double temperature) override;

    LLMResponse stream(const std::vector<Message>& messages,
                       const std::string& system,
                       int max_tokens, double temperature,
                       StreamCallback cb) override;

private:
    json    build_payload(const std::vector<Message>& messages,
                          const std::string& system,
                          int max_tokens, double temperature) const;
    Headers build_headers() const;
    std::string endpoint(bool stream_mode) const;
};

} // namespace terai
