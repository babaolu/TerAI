// src/providers/provider_factory.cpp
#include "providers/provider_factory.h"
#include "providers/anthropic.h"
#include "providers/openai_compat.h"
#include "providers/gemini.h"
#include "providers/ollama.h"
#include <stdexcept>

#ifdef TERAI_WITH_LLAMA_CPP
#include "providers/local_llama.h"
#endif

namespace terai {

std::unique_ptr<BaseProvider> ProviderFactory::create(const json& cfg) {
    std::string name = cfg.value("name", "ollama");

    if (name == "anthropic")
        return std::make_unique<AnthropicProvider>(cfg);

    if (name == "openai" || name == "openrouter" ||
        name == "huggingface" || name == "nvidia_nim")
        return std::make_unique<OpenAICompatProvider>(cfg);

    if (name == "gemini")
        return std::make_unique<GeminiProvider>(cfg);

    if (name == "ollama")
        return std::make_unique<OllamaProvider>(cfg);
#ifdef TERAI_WITH_LLAMA_CPP
    if (name == "local_llama")
	return std::make_unique<LocalLlamaProvider>(cfg);
#endif

    throw std::runtime_error("Unknown provider: '" + name +
        "'. Available: anthropic, openai, gemini, openrouter, "
        "huggingface, nvidia_nim, ollama");
}

std::vector<std::string> ProviderFactory::available_providers() {
    return {"anthropic","openai","gemini","openrouter",
            "huggingface","nvidia_nim","ollama"};
}

} // namespace terai
