#pragma once
// include/core/memory.h

#include "core/types.h"
#include "third_party/json.hpp"
#include <string>
#include <vector>

namespace terai {

using json = nlohmann::json;

struct Pattern {
    std::string type;
    std::string description;
    std::string example;
    std::string ts;
    int         uses = 1;
};

class Memory {
public:
    explicit Memory(const json& cfg);

    // Session messages (current conversation)
    void add(const std::string& role, const std::string& content);
    std::vector<Message> session() const;
    void clear_session();

    // Persist session to history file
    void save_session(const std::string& title = "");

    // Pattern learning (self-improvement)
    void save_pattern(const std::string& type,
                      const std::string& desc,
                      const std::string& example);
    std::vector<Pattern> recent_patterns(int n = 3) const;
    std::string          pattern_context()           const;

    // History listing
    std::vector<json> history(int limit = 20) const;

private:
    bool        _enabled;
    std::string _history_file;
    int         _max_entries;
    std::string _patterns_file;

    std::vector<Message>  _session;
    std::vector<Pattern>  _patterns;

    void load_patterns();
    void save_patterns() const;
    std::vector<json> load_history() const;

    static std::string now_iso();
    static std::string expand(const std::string& p);
};

} // namespace terai
