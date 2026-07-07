// src/core/optimizer.cpp
#include "core/optimizer.h"
#include <regex>
#include <sstream>
#include <algorithm>
#include <numeric>

namespace terai {

ContextOptimizer::ContextOptimizer(const Config& cfg) : _cfg(cfg) {}

int ContextOptimizer::count_tokens(const std::string& text) {
    // ~4 chars per token — good enough for budgeting without a tokenizer
    return static_cast<int>(text.size()) / 4;
}

// Collapse 3+ newlines → 2, multiple spaces → 1 (outside of code blocks)
std::string ContextOptimizer::compress_whitespace(const std::string& text) {
    // Protect code blocks
    std::string result;
    result.reserve(text.size());

    bool in_code = false;
    size_t i = 0;
    while (i < text.size()) {
        // Detect ``` fences
        if (i + 2 < text.size() && text[i]=='`' && text[i+1]=='`' && text[i+2]=='`') {
            in_code = !in_code;
            result += "```";
            i += 3;
            continue;
        }
        if (in_code) { result += text[i++]; continue; }

        // Outside code: compress runs of newlines
        if (text[i] == '\n') {
            result += '\n';
            while (i < text.size() && text[i] == '\n') ++i;
            if (i < text.size() && text[i] == '\n') ++i; // allow at most 2
            continue;
        }
        // Compress multiple spaces
        if (text[i] == ' ') {
            result += ' ';
            while (i < text.size() && text[i] == ' ') ++i;
            continue;
        }
        result += text[i++];
    }
    return result;
}

std::vector<Message> ContextOptimizer::compress_history(
    const std::vector<Message>& msgs) const
{
    if (msgs.size() <= 6) return msgs;

    // Summarise everything except the last 6 messages
    size_t keep = 6;
    auto older_end = msgs.begin() + (msgs.size() - keep);

    std::string summary = "=== COMPRESSED CONVERSATION HISTORY ===\n";
    for (auto it = msgs.begin(); it != older_end; ++it) {
        std::string snip = it->content.substr(
            0, std::min((size_t)150, it->content.size()));
        if (it->content.size() > 150) snip += "... [truncated]";
        summary += "[" + it->role + "]: " + snip + "\n";
    }
    summary += "=== END COMPRESSED HISTORY ===\n";

    std::vector<Message> result;
    result.push_back({"user",      summary,    ""});
    result.push_back({"assistant", "I have noted the prior conversation context.", ""});
    for (auto it = older_end; it != msgs.end(); ++it)
        result.push_back(*it);

    return result;
}

std::vector<Message> ContextOptimizer::optimize(const std::vector<Message>& messages,
                                                std::string& system)
{
    if (!_cfg.enabled) return messages;

    std::vector<Message> msgs = messages;

    // Compress whitespace in every message
    if (_cfg.compress_ws) {
        system = compress_whitespace(system);
        for (auto& m : msgs)
            m.content = compress_whitespace(m.content);
    }

    // Count total tokens
    int total = count_tokens(system);
    for (auto& m : msgs) total += count_tokens(m.content);

    if (total > _cfg.summarize_threshold)
        msgs = compress_history(msgs);

    return msgs;
}

TokenReport ContextOptimizer::report(const std::vector<Message>& messages,
                                     const std::string& system) const
{
    int total = count_tokens(system);
    for (auto& m : messages) total += count_tokens(m.content);
    return {
        total,
        _cfg.context_window,
        static_cast<double>(total) / _cfg.context_window * 100.0
    };
}

std::string ContextOptimizer::get_cached(const std::string& key) const {
    auto it = _cache.find(key);
    return (it != _cache.end()) ? it->second : "";
}

void ContextOptimizer::set_cached(const std::string& key, const std::string& val) {
    if (_cache.size() > 50) {
        _cache.erase(_cache.begin());  // evict oldest
    }
    _cache[key] = val;
}

} // namespace terai
