#pragma once
// include/core/types.h — Shared types across TerAI

#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <map>

namespace terai {

struct LLMResponse {
    std::string content;
    int         input_tokens  = 0;
    int         output_tokens = 0;
    std::string model;
    std::string stop_reason;

    int total_tokens() const { return input_tokens + output_tokens; }
    bool empty()       const { return content.empty(); }
};

struct Message {
    std::string role;     // "user" | "assistant" | "system"
    std::string content;
    std::string timestamp;
};

struct ToolResult {
    bool        success   = false;
    std::string output;
    std::string tool_name;

    std::string format() const {
        return "[" + std::string(success ? "✓" : "✗") + " " + tool_name + "]\n" + output;
    }
};

struct ToolCall {
    std::string                      name;
    std::map<std::string,std::string> args;
    bool valid = false;
};

using StreamCallback = std::function<void(const std::string& token)>;

enum class ProviderKind {
    Anthropic, OpenAI, Gemini, OpenRouter, HuggingFace, NvidiaNim, Ollama,
    LocalLlama, Unknown
};

} // namespace terai
