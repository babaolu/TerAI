// src/core/config.cpp
#include "core/config.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace terai {

// ── Defaults ──────────────────────────────────────────────────────────────────
json Config::defaults() {
    return {
        {"active_provider", "ollama"},
        {"model_override",  nullptr},
        {"providers", {
            {"anthropic",   {{"api_key",""},{"default_model","claude-sonnet-4-6"},
                             {"base_url","https://api.anthropic.com"}}},
            {"openai",      {{"api_key",""},{"default_model","gpt-4o-mini"},
                             {"base_url","https://api.openai.com/v1"}}},
            {"gemini",      {{"api_key",""},{"default_model","gemini-2.0-flash"},
                             {"base_url","https://generativelanguage.googleapis.com/v1beta"}}},
            {"openrouter",  {{"api_key",""},{"default_model","meta-llama/llama-3.1-8b-instruct:free"},
                             {"base_url","https://openrouter.ai/api/v1"}}},
            {"huggingface", {{"api_key",""},{"default_model","mistralai/Mixtral-8x7B-Instruct-v0.1"},
                             {"base_url","https://api-inference.huggingface.co"}}},
            {"nvidia_nim",  {{"api_key",""},{"default_model","meta/llama-3.1-8b-instruct"},
                             {"base_url","https://integrate.api.nvidia.com/v1"}}},
            {"ollama",      {{"api_key",""},{"default_model","llama3.2"},
                             {"base_url","http://localhost:11434"}}}
        }},
        {"agent", {
            {"max_iterations",       10},
            {"max_tokens",         4096},
            {"temperature",         0.7},
            {"context_window",    16000},
            {"summarize_threshold",12000},
            {"system_prompt_extra",  ""}
        }},
        {"memory", {
            {"enabled",            true},
            {"history_file",       "~/.terai/history.json"},
            {"max_history_entries", 500}
        }},
        {"tools", {
            {"web_search",  {{"enabled",true},{"engine","ddg"},
                             {"google_api_key",""},{"google_cx",""}}},
            {"shell",       {{"enabled",true},{"require_confirm",true},
                             {"blocked_commands",{"rm -rf /","mkfs","dd if="}}}},
            {"filesystem",  {{"enabled",true},{"max_file_size_kb",1024}}},
            {"gmail",       {{"enabled",false},
                             {"credentials_file","~/.terai/gmail_credentials.json"},
                             {"token_file","~/.terai/gmail_token.json"}}}
        }},
        {"daemon", {
            {"enabled",                     false},
            {"provider",                 "ollama"},
            {"model",                      nullptr},
            {"pid_file",       "~/.terai/daemon.pid"},
            {"log_file",       "~/.terai/daemon.log"},
            {"scan_paths",     {"~/projects","~/code"}},
            {"scan_extensions", {".py",".js",".ts",".c",".cpp",".sh"}},
            {"improvement_interval_minutes", 30},
            {"max_files_per_cycle",           5},
            {"tasks", {"add_docstrings","improve_error_handling","suggest_optimizations"}}
        }},
        {"token_optimization", {
            {"enabled",             true},
            {"compress_whitespace", true},
            {"cache_tool_results",  true}
        }},
        {"self_improvement", {
            {"enabled",                      true},
            {"reflection_after_n_turns",        5},
            {"save_successful_patterns",      true}
        }},
        {"display", {
            {"color",            true},
            {"show_token_count", true},
            {"show_provider",    true},
            {"stream",           true}
        }}
    };
}

// ── Filesystem helpers ────────────────────────────────────────────────────────
std::string Config::config_dir() {
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp";
    return std::string(home) + "/.terai";
}

std::string Config::config_file() {
    return config_dir() + "/config.json";
}

// ── Deep merge ────────────────────────────────────────────────────────────────
json Config::deep_merge(const json& base, const json& override) {
    json result = base;
    for (auto& [key, val] : override.items()) {
        if (result.contains(key) && result[key].is_object() && val.is_object())
            result[key] = deep_merge(result[key], val);
        else
            result[key] = val;
    }
    return result;
}

// ── Constructor ───────────────────────────────────────────────────────────────
Config::Config() {
    load();
}

void Config::load() {
    json def = defaults();
    std::string path = config_file();

    if (fs::exists(path)) {
        try {
            std::ifstream f(path);
            json stored = json::parse(f);
            _data = deep_merge(def, stored);
        } catch (...) {
            _data = def;
        }
    } else {
        _data = def;
        // Create dir and write defaults
        fs::create_directories(config_dir());
        save();
    }
}

void Config::save() const {
    fs::create_directories(config_dir());
    std::ofstream f(config_file());
    f << _data.dump(2);
}

// ── Getters ───────────────────────────────────────────────────────────────────
template<>
std::string Config::get<std::string>(const std::string& k1,
                                     const std::string& k2,
                                     const std::string& k3,
                                     std::string def) const {
    try {
        if (!k2.empty() && !k3.empty())
            return _data.at(k1).at(k2).at(k3).get<std::string>();
        if (!k2.empty())
            return _data.at(k1).at(k2).get<std::string>();
        return _data.at(k1).get<std::string>();
    } catch (...) { return def; }
}

template<>
int Config::get<int>(const std::string& k1,
                     const std::string& k2,
                     const std::string& k3,
                     int def) const {
    try {
        if (!k2.empty() && !k3.empty())
            return _data.at(k1).at(k2).at(k3).get<int>();
        if (!k2.empty())
            return _data.at(k1).at(k2).get<int>();
        return _data.at(k1).get<int>();
    } catch (...) { return def; }
}

template<>
bool Config::get<bool>(const std::string& k1,
                       const std::string& k2,
                       const std::string& k3,
                       bool def) const {
    try {
        if (!k2.empty() && !k3.empty())
            return _data.at(k1).at(k2).at(k3).get<bool>();
        if (!k2.empty())
            return _data.at(k1).at(k2).get<bool>();
        return _data.at(k1).get<bool>();
    } catch (...) { return def; }
}

template<>
double Config::get<double>(const std::string& k1,
                           const std::string& k2,
                           const std::string& k3,
                           double def) const {
    try {
        if (!k2.empty() && !k3.empty())
            return _data.at(k1).at(k2).at(k3).get<double>();
        if (!k2.empty())
            return _data.at(k1).at(k2).get<double>();
        return _data.at(k1).get<double>();
    } catch (...) { return def; }
}

// ── Setters ───────────────────────────────────────────────────────────────────
void Config::set(const std::string& k1, const json& value) {
    _data[k1] = value;
    save();
}
void Config::set(const std::string& k1, const std::string& k2, const json& value) {
    _data[k1][k2] = value;
    save();
}
void Config::set(const std::string& k1, const std::string& k2,
                 const std::string& k3, const json& value) {
    _data[k1][k2][k3] = value;
    save();
}

// ── Active provider config ────────────────────────────────────────────────────
json Config::active_provider_config() const {
    std::string name = _data.value("active_provider", "ollama");
    json cfg = _data["providers"].value(name, json::object());
    cfg["name"] = name;

    // Apply model override
    if (!_data["model_override"].is_null()) {
        cfg["default_model"] = _data["model_override"].get<std::string>();
    }
    return cfg;
}

// ── Display ───────────────────────────────────────────────────────────────────
void Config::display() const {
    json masked = _data;
    for (auto& [pname, pcfg] : masked["providers"].items()) {
        if (pcfg.contains("api_key")) {
            std::string key = pcfg["api_key"].get<std::string>();
            if (key.size() > 10)
                pcfg["api_key"] = key.substr(0,6) + "..." + key.substr(key.size()-4);
        }
    }
    std::cout << masked.dump(2) << "\n";
}

} // namespace terai
