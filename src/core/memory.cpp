// src/core/memory.cpp
#include "core/memory.h"
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

namespace terai {

std::string Memory::expand(const std::string& p) {
    if (p.empty() || p[0] != '~') return p;
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + p.substr(1) : p;
}

std::string Memory::now_iso() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

Memory::Memory(const json& cfg) {
    _enabled      = cfg.value("enabled",            true);
    _history_file = expand(cfg.value("history_file","~/.terai/history.json"));
    _max_entries  = cfg.value("max_history_entries", 500);
    _patterns_file= fs::path(_history_file).parent_path() / "patterns.json";

    fs::create_directories(fs::path(_history_file).parent_path());
    load_patterns();
}

// ── Session ───────────────────────────────────────────────────────────────────
void Memory::add(const std::string& role, const std::string& content) {
    _session.push_back({role, content, now_iso()});
}

std::vector<Message> Memory::session() const { return _session; }

void Memory::clear_session() { _session.clear(); }

void Memory::save_session(const std::string& title) {
    if (!_enabled || _session.empty()) return;

    auto history = load_history();

    // Auto-title from first user message
    std::string t = title;
    if (t.empty()) {
        for (auto& m : _session) {
            if (m.role == "user") {
                t = m.content.substr(0, std::min((size_t)60, m.content.size()));
                if (m.content.size() > 60) t += "...";
                break;
            }
        }
    }

    json msgs = json::array();
    for (auto& m : _session)
        msgs.push_back({{"role",m.role},{"content",m.content},{"ts",m.timestamp}});

    json entry = {
        {"id",       now_iso()},
        {"title",    t},
        {"ts",       now_iso()},
        {"messages", msgs}
    };
    history.push_back(entry);

    if ((int)history.size() > _max_entries)
        history.erase(history.begin(), history.begin() + (history.size() - _max_entries));

    std::ofstream f(_history_file);
    f << json(history).dump(2);
}

std::vector<json> Memory::load_history() const {
    if (!fs::exists(_history_file)) return {};
    try {
        std::ifstream f(_history_file);
        return json::parse(f).get<std::vector<json>>();
    } catch (...) { return {}; }
}

std::vector<json> Memory::history(int limit) const {
    auto h = load_history();
    if ((int)h.size() > limit)
        h.erase(h.begin(), h.begin() + (h.size() - limit));
    return h;
}

bool Memory::load_session_by_id(const std::string& id) {
    auto h = load_history();
    for (auto& entry : h) {
        if (entry.value("id", "") != id) continue;

        _session.clear();
        for (auto& m : entry.value("messages", json::array())) {
            _session.push_back({
                m.value("role", ""),
                m.value("content", ""),
                m.value("ts", "")
            });
        }
        return true;
    }
    return false;
}

// ── Patterns ──────────────────────────────────────────────────────────────────
void Memory::load_patterns() {
    if (!fs::exists(_patterns_file)) return;
    try {
        std::ifstream f(_patterns_file);
        auto arr = json::parse(f);
        for (auto& p : arr) {
            _patterns.push_back({
                p.value("type",""),
                p.value("description",""),
                p.value("example",""),
                p.value("ts",""),
                p.value("uses",1)
            });
        }
    } catch (...) {}
}

void Memory::save_patterns() const {
    json arr = json::array();
    for (auto& p : _patterns)
        arr.push_back({{"type",p.type},{"description",p.description},
                       {"example",p.example},{"ts",p.ts},{"uses",p.uses}});
    std::ofstream f(_patterns_file);
    f << arr.dump(2);
}

void Memory::save_pattern(const std::string& type,
                          const std::string& desc,
                          const std::string& example)
{
    std::string ex = example.substr(0, std::min((size_t)500, example.size()));
    _patterns.push_back({type, desc, ex, now_iso(), 1});
    if (_patterns.size() > 100)
        _patterns.erase(_patterns.begin());
    save_patterns();
}

std::vector<Pattern> Memory::recent_patterns(int n) const {
    if (_patterns.empty()) return {};
    int start = std::max(0, (int)_patterns.size() - n);
    return std::vector<Pattern>(_patterns.begin() + start, _patterns.end());
}

std::string Memory::pattern_context() const {
    auto recent = recent_patterns(3);
    if (recent.empty()) return "";
    std::string out = "## Learned patterns from prior sessions:\n";
    for (auto& p : recent)
        out += "- [" + p.type + "] " + p.description + "\n";
    return out;
}

} // namespace terai
