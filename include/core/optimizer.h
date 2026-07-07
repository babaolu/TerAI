#pragma once
// include/core/optimizer.h

#include "core/types.h"
#include <string>
#include <vector>
#include <map>

namespace terai {

struct TokenReport {
    int    total_tokens;
    int    context_window;
    double utilization_pct;
};

class ContextOptimizer {
public:
    struct Config {
        bool enabled            = true;
        int  context_window     = 16000;
        int  summarize_threshold= 12000;
        bool compress_ws        = true;
        bool cache_results      = true;
    };

    explicit ContextOptimizer(const Config& cfg);

    // Compress messages if over threshold; returns possibly-shorter copy
    std::vector<Message> optimize(const std::vector<Message>& messages,
                                  std::string& system);

    // Approximate token count
    static int count_tokens(const std::string& text);

    TokenReport report(const std::vector<Message>& messages,
                       const std::string& system) const;

    // Tool result cache
    std::string get_cached(const std::string& key) const;
    void        set_cached(const std::string& key, const std::string& val);

private:
    Config _cfg;
    std::map<std::string,std::string> _cache;

    static std::string compress_whitespace(const std::string& text);
    std::vector<Message> compress_history(const std::vector<Message>& msgs) const;
};

} // namespace terai
