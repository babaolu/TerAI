#pragma once
// include/providers/local_llama.h
//
// Direct in-process llama.cpp integration. Links against libllama.so and
// calls the C API directly — no HTTP, no JSON-per-token, no subprocess,
// no separate server process to keep warm.
//
// This is the provider to use for the daemon specifically: because the
// model and context live for the provider's entire lifetime (not
// per-request like Ollama's HTTP server), there is no cold-load between
// daemon cycles and no keep_alive to manage. It also enables KV-cache
// prefix reuse (see below) for the daemon's own workload pattern, which
// an HTTP-based server can't do transparently across separate requests.
//
// IMPORTANT — llama.cpp's C API is not guaranteed stable across versions.
// The function signatures used here match a recent-generation llama.cpp
// (the llama_model_load_from_file / llama_init_from_model naming, unified
// vocab accessor, sampler-chain API). If your checkout predates this,
// check include/third_party/llama_api.h against your own llama.h and
// adjust — the real C API calls are isolated entirely inside
// local_llama.cpp so a version mismatch only requires editing one file.

#include "providers/base_provider.h"
#include <memory>
#include <mutex>

namespace terai {

class LocalLlamaProvider : public BaseProvider {
public:
    explicit LocalLlamaProvider(const json& cfg);
    ~LocalLlamaProvider() override;

    // Non-copyable — owns raw GPU/CPU resources (model, context, sampler).
    LocalLlamaProvider(const LocalLlamaProvider&) = delete;
    LocalLlamaProvider& operator=(const LocalLlamaProvider&) = delete;

    LLMResponse complete(const std::vector<Message>& messages,
                         const std::string& system,
                         int max_tokens, double temperature) override;

    LLMResponse stream(const std::vector<Message>& messages,
                       const std::string& system,
                       int max_tokens, double temperature,
                       StreamCallback cb) override;

    bool is_loaded() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;

    // Serializes access to the model/context — TerAI's Agent and the
    // background daemon could in principle both hold a LocalLlamaProvider
    // in the same process; llama_decode is not safe to call concurrently
    // on the same context.
    std::mutex _mutex;

    LLMResponse run_inference(const std::vector<Message>& messages,
                              const std::string& system,
                              int max_tokens, double temperature,
                              StreamCallback cb);  // cb may be null
};

} // namespace terai
