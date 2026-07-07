// src/providers/gemini.cpp
#include "providers/gemini.h"
#include "providers/http_client.h"

namespace terai {

GeminiProvider::GeminiProvider(const json& cfg) {
    _name     = "gemini";
    _api_key  = cfg.value("api_key",       "");
    _base_url = cfg.value("base_url",      "https://generativelanguage.googleapis.com/v1beta");
    _model    = cfg.value("default_model", "gemini-2.0-flash");
}

Headers GeminiProvider::build_headers() const {
    return {{"Content-Type", "application/json"}};
}

std::string GeminiProvider::endpoint(bool stream_mode) const {
    std::string action = stream_mode ? "streamGenerateContent" : "generateContent";
    return _base_url + "/models/" + _model + ":" + action + "?key=" + _api_key;
}

json GeminiProvider::build_payload(const std::vector<Message>& messages,
                                   const std::string& system,
                                   int max_tokens, double temperature) const
{
    json contents = json::array();
    for (auto& m : messages) {
        std::string role = (m.role == "assistant") ? "model" : "user";
        contents.push_back({
            {"role",  role},
            {"parts", json::array({{{"text", m.content}}})}
        });
    }

    json payload = {
        {"contents", contents},
        {"generationConfig", {
            {"maxOutputTokens", max_tokens},
            {"temperature",     temperature}
        }}
    };

    if (!system.empty()) {
        payload["systemInstruction"] = {
            {"parts", json::array({{{"text", system}}})}
        };
    }
    return payload;
}

LLMResponse GeminiProvider::complete(const std::vector<Message>& messages,
                                     const std::string& system,
                                     int max_tokens, double temperature)
{
    json body = build_payload(messages, system, max_tokens, temperature);
    auto resp = HttpClient::post(endpoint(false), body.dump(), build_headers());
    json data = json::parse(resp.body);

    LLMResponse out;
    out.model   = _model;
    out.content = data["candidates"][0]["content"]["parts"][0]["text"].get<std::string>();
    if (data.contains("usageMetadata")) {
        out.input_tokens  = data["usageMetadata"].value("promptTokenCount",     0);
        out.output_tokens = data["usageMetadata"].value("candidatesTokenCount", 0);
    }
    return out;
}

LLMResponse GeminiProvider::stream(const std::vector<Message>& messages,
                                   const std::string& system,
                                   int max_tokens, double temperature,
                                   StreamCallback cb)
{
    // Gemini stream returns NDJSON lines (each is a full JSON response)
    json body = build_payload(messages, system, max_tokens, temperature);
    LLMResponse out;
    out.model = _model;

    HttpClient::post_stream_ndjson(endpoint(true), body.dump(), build_headers(),
        [&](const std::string& line) {
            try {
                json ev = json::parse(line);
                std::string token =
                    ev["candidates"][0]["content"]["parts"][0]["text"].get<std::string>();
                out.content += token;
                if (cb) cb(token);
            } catch (...) {}
        });

    return out;
}

} // namespace terai
