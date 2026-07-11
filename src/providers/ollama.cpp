// src/providers/ollama.cpp
#include "providers/ollama.h"
#include "providers/http_client.h"
#include <iostream>

namespace terai {

OllamaProvider::OllamaProvider(const json& cfg) {
    _name     = "ollama";
    _api_key  = "";
    _base_url = cfg.value("base_url",      "http://localhost:11434");
    _model    = cfg.value("default_model", "llama3.2");
}

json OllamaProvider::build_payload(const std::vector<Message>& messages,
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
        {"model",    _model},
        {"messages", chat_msgs},
        {"stream",   do_stream},
        {"options",  {
            {"temperature", temperature},
            {"num_predict", max_tokens}
        }}
    };
    if (!_keep_alive.empty()) payload["keep_alive"] = _keep_alive;
    return payload;
}

bool OllamaProvider::is_available() const {
    try {
        HttpClient::get(_base_url + "/api/tags", {{"Content-Type","application/json"}}, 3);
        return true;
    } catch (...) {
        return false;
    }
}

LLMResponse OllamaProvider::complete(const std::vector<Message>& messages,
                                     const std::string& system,
                                     int max_tokens, double temperature)
{
    json body = build_payload(messages, system, max_tokens, temperature, false);
    Headers h = {{"Content-Type", "application/json"}};
    auto resp = HttpClient::post(_base_url + "/api/chat", body.dump(), h, _timeout_s);
    json data = json::parse(resp.body);

    LLMResponse out;
    out.model   = _model;
    out.content = data["message"]["content"].get<std::string>();
    // Ollama token counts live in root of response
    out.input_tokens  = data.value("prompt_eval_count", 0);
    out.output_tokens = data.value("eval_count",        0);
    return out;
}

LLMResponse OllamaProvider::stream(const std::vector<Message>& messages,
                                   const std::string& system,
                                   int max_tokens, double temperature,
                                   StreamCallback cb)
{
    json    body = build_payload(messages, system, max_tokens, temperature, true);
    Headers h    = {{"Content-Type", "application/json"}};

    LLMResponse out;
    out.model = _model;

    HttpClient::post_stream_ndjson(_base_url + "/api/chat", body.dump(), h,
        [&](const std::string& line) {
            try {
                json ev    = json::parse(line);
                std::string token = ev["message"].value("content", "");
                if (!token.empty()) {
                    out.content += token;
                    if (cb) cb(token);
                }
                if (ev.value("done", false)) {
                    out.input_tokens  = ev.value("prompt_eval_count", 0);
                    out.output_tokens = ev.value("eval_count",        0);
                }
            } catch (...) {}
        }, 300);

    return out;
}

} // namespace terai
