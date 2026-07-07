// src/providers/anthropic.cpp
#include "providers/anthropic.h"
#include "providers/http_client.h"
#include <stdexcept>

namespace terai {

AnthropicProvider::AnthropicProvider(const json& cfg) {
    _name     = "anthropic";
    _api_key  = cfg.value("api_key",       "");
    _base_url = cfg.value("base_url",      "https://api.anthropic.com");
    _model    = cfg.value("default_model", "claude-sonnet-4-6");
}

Headers AnthropicProvider::build_headers() const {
    return {
        {"x-api-key",         _api_key},
        {"anthropic-version", "2023-06-01"},
        {"content-type",      "application/json"}
    };
}

json AnthropicProvider::build_payload(const std::vector<Message>& messages,
                                      const std::string& system,
                                      int max_tokens, double temperature,
                                      bool do_stream) const
{
    json payload = {
        {"model",       _model},
        {"max_tokens",  max_tokens},
        {"temperature", temperature},
        {"messages",    messages_to_json(messages)}
    };
    if (!system.empty()) payload["system"] = system;
    if (do_stream)       payload["stream"] = true;
    return payload;
}

LLMResponse AnthropicProvider::complete(const std::vector<Message>& messages,
                                        const std::string& system,
                                        int max_tokens, double temperature)
{
    std::string url  = _base_url + "/v1/messages";
    json        body = build_payload(messages, system, max_tokens, temperature, false);

    auto resp = HttpClient::post(url, body.dump(), build_headers());
    json data = json::parse(resp.body);

    LLMResponse out;
    out.content      = data["content"][0]["text"].get<std::string>();
    out.model        = data.value("model", _model);
    out.stop_reason  = data.value("stop_reason", "");
    if (data.contains("usage")) {
        out.input_tokens  = data["usage"].value("input_tokens",  0);
        out.output_tokens = data["usage"].value("output_tokens", 0);
    }
    return out;
}

LLMResponse AnthropicProvider::stream(const std::vector<Message>& messages,
                                      const std::string& system,
                                      int max_tokens, double temperature,
                                      StreamCallback cb)
{
    std::string url  = _base_url + "/v1/messages";
    json        body = build_payload(messages, system, max_tokens, temperature, true);

    LLMResponse out;
    out.model = _model;

    HttpClient::post_stream_sse(url, body.dump(), build_headers(),
        [&](const std::string& data_line) {
            try {
                json ev = json::parse(data_line);
                std::string type = ev.value("type", "");

                if (type == "content_block_delta") {
                    std::string token = ev["delta"].value("text", "");
                    out.content += token;
                    if (cb) cb(token);
                }
                else if (type == "message_delta") {
                    out.stop_reason = ev["delta"].value("stop_reason", "");
                }
                else if (type == "message_start" && ev.contains("message")) {
                    auto& usage = ev["message"]["usage"];
                    out.input_tokens = usage.value("input_tokens", 0);
                }
                else if (type == "message_delta" && ev.contains("usage")) {
                    out.output_tokens = ev["usage"].value("output_tokens", 0);
                }
            } catch (...) {}
        });

    return out;
}

// ── Base helper ───────────────────────────────────────────────────────────────
json BaseProvider::messages_to_json(const std::vector<Message>& msgs) {
    json arr = json::array();
    for (auto& m : msgs)
        arr.push_back({{"role", m.role}, {"content", m.content}});
    return arr;
}

} // namespace terai
