// src/providers/openai_compat.cpp
#include "providers/openai_compat.h"
#include "providers/http_client.h"
#include <stdexcept>

namespace terai {

OpenAICompatProvider::OpenAICompatProvider(const json& cfg) {
    _name     = cfg.value("name",          "openai");
    _api_key  = cfg.value("api_key",       "");
    _base_url = cfg.value("base_url",      "https://api.openai.com/v1");
    _model    = cfg.value("default_model", "gpt-4o-mini");
}

Headers OpenAICompatProvider::build_headers() const {
    Headers h = {
        {"Authorization", "Bearer " + _api_key},
        {"Content-Type",  "application/json"}
    };
    // OpenRouter extra headers
    if (_name == "openrouter") {
        h["HTTP-Referer"] = "https://github.com/babaolu/terai";
        h["X-Title"]      = "TerAI";
    }
    return h;
}

json OpenAICompatProvider::build_payload(const std::vector<Message>& messages,
                                         const std::string& system,
                                         int max_tokens, double temperature,
                                         bool do_stream) const
{
    json chat_msgs = json::array();
    if (!system.empty())
        chat_msgs.push_back({{"role","system"}, {"content", system}});
    for (auto& m : messages)
        chat_msgs.push_back({{"role", m.role}, {"content", m.content}});

    json payload = {
        {"model",       _model},
        {"messages",    chat_msgs},
        {"max_tokens",  max_tokens},
        {"temperature", temperature}
    };
    if (do_stream) payload["stream"] = true;
    return payload;
}

LLMResponse OpenAICompatProvider::complete(const std::vector<Message>& messages,
                                           const std::string& system,
                                           int max_tokens, double temperature)
{
    std::string url  = _base_url + "/chat/completions";
    json        body = build_payload(messages, system, max_tokens, temperature, false);

    auto resp = HttpClient::post(url, body.dump(), build_headers());
    json data = json::parse(resp.body);

    LLMResponse out;
    out.content     = data["choices"][0]["message"]["content"].get<std::string>();
    out.model       = data.value("model", _model);
    out.stop_reason = data["choices"][0].value("finish_reason", "");
    if (data.contains("usage")) {
        out.input_tokens  = data["usage"].value("prompt_tokens",     0);
        out.output_tokens = data["usage"].value("completion_tokens", 0);
    }
    return out;
}

LLMResponse OpenAICompatProvider::stream(const std::vector<Message>& messages,
                                         const std::string& system,
                                         int max_tokens, double temperature,
                                         StreamCallback cb)
{
    std::string url  = _base_url + "/chat/completions";
    json        body = build_payload(messages, system, max_tokens, temperature, true);

    LLMResponse out;
    out.model = _model;

    HttpClient::post_stream_sse(url, body.dump(), build_headers(),
        [&](const std::string& data_line) {
            try {
                json ev    = json::parse(data_line);
                auto& c0   = ev["choices"][0];
                std::string token = c0["delta"].value("content", "");
                if (!token.empty()) {
                    out.content += token;
                    if (cb) cb(token);
                }
                out.stop_reason = c0.value("finish_reason", out.stop_reason);
            } catch (...) {}
        });

    return out;
}

} // namespace terai
