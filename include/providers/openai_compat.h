#pragma once
// include/providers/openai_compat.h

#include "providers/base_provider.h"
#include "providers/http_client.h"

namespace terai {

// Covers: openai, openrouter, huggingface, nvidia_nim
// All expose the same /chat/completions endpoint shape.
class OpenAICompatProvider : public BaseProvider {
public:
    explicit OpenAICompatProvider(const json& cfg);

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
                          int max_tokens, double temperature,
                          bool do_stream) const;
    Headers build_headers() const;
};

} // namespace terai
