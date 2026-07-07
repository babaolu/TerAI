#pragma once
// include/providers/anthropic.h

#include "providers/base_provider.h"
#include "providers/http_client.h"

namespace terai {

class AnthropicProvider : public BaseProvider {
public:
    explicit AnthropicProvider(const json& cfg);

    LLMResponse complete(const std::vector<Message>& messages,
                         const std::string& system,
                         int max_tokens, double temperature) override;

    LLMResponse stream(const std::vector<Message>& messages,
                       const std::string& system,
                       int max_tokens, double temperature,
                       StreamCallback cb) override;

private:
    json  build_payload(const std::vector<Message>& messages,
                        const std::string& system,
                        int max_tokens, double temperature,
                        bool do_stream) const;
    Headers build_headers() const;
};

} // namespace terai
