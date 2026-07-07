// src/core/setup.cpp
#include "core/setup.h"
#include "core/display.h"
#include "providers/provider_factory.h"
#include <iostream>
#include <string>
#include <vector>

namespace terai {

struct ProviderEntry { std::string name; std::string desc; };
static const std::vector<ProviderEntry> PROVIDERS = {
    {"anthropic",   "Claude (Sonnet/Opus) — best reasoning"},
    {"openai",      "GPT-4o / GPT-4o-mini"},
    {"gemini",      "Google Gemini"},
    {"openrouter",  "OpenRouter — many models, has free tier"},
    {"huggingface", "HuggingFace Inference API — free tier"},
    {"nvidia_nim",  "NVIDIA NIM — optimised inference"},
    {"ollama",      "Ollama — 100% local, no internet needed (default)"}
};

static std::string read_line(const std::string& prompt) {
    std::cout << prompt;
    std::string s;
    std::getline(std::cin, s);
    return s;
}

static std::string mask_key(const std::string& k) {
    if (k.size() > 10) return k.substr(0,6) + "***" + k.substr(k.size()-4);
    return k.empty() ? "not set" : "set";
}

void run_setup(Config& cfg) {
    std::cout << "\n╔══════════════════════════════════╗\n";
    std::cout << "║   TerAI Setup Wizard (C++ build) ║\n";
    std::cout << "╚══════════════════════════════════╝\n\n";

    // ── Active provider ─────────────────────────────────────────────────────
    std::cout << "Available LLM providers:\n";
    std::string current = cfg.get<std::string>("active_provider","","","ollama");
    for (size_t i = 0; i < PROVIDERS.size(); ++i) {
        char mark = (PROVIDERS[i].name == current) ? '*' : ' ';
        std::printf("  %zu. [%c] %-14s — %s\n",
            i+1, mark, PROVIDERS[i].name.c_str(), PROVIDERS[i].desc.c_str());
    }

    std::string choice = read_line("\nDefault provider number [Enter to keep current]: ");
    if (!choice.empty()) {
        try {
            int n = std::stoi(choice);
            if (n >= 1 && n <= (int)PROVIDERS.size()) {
                cfg.set("active_provider", PROVIDERS[n-1].name);
                std::cout << "  → Active provider: " << PROVIDERS[n-1].name << "\n";
            }
        } catch (...) {}
    }

    // ── API keys ─────────────────────────────────────────────────────────────
    std::cout << "\n── API Keys ──────────────────────────────────────\n";
    std::cout << "(Leave blank to skip. Stored in ~/.terai/config.json)\n\n";

    for (auto& p : PROVIDERS) {
        if (p.name == "ollama") continue;
        std::string existing = cfg.get<std::string>("providers", p.name, "api_key", "");
        std::string key = read_line("  " + p.name + " API key [" + mask_key(existing) + "]: ");
        if (!key.empty()) cfg.set("providers", p.name, "api_key", key);
    }

    // ── Ollama ───────────────────────────────────────────────────────────────
    std::cout << "\n── Ollama (Local LLM) ────────────────────────────\n";
    std::string ollama_url = read_line("  Ollama URL [http://localhost:11434]: ");
    if (!ollama_url.empty()) cfg.set("providers", "ollama", "base_url", ollama_url);

    std::string ollama_model = read_line("  Default Ollama model [llama3.2]: ");
    if (!ollama_model.empty()) cfg.set("providers", "ollama", "default_model", ollama_model);

    // ── Daemon ───────────────────────────────────────────────────────────────
    std::cout << "\n── Background Daemon ─────────────────────────────\n";
    std::cout << "  Uses local Ollama to improve code files while TerAI is idle.\n";
    std::string en = read_line("  Enable background daemon? [y/N]: ");
    cfg.set("daemon", "enabled", (en == "y" || en == "Y") ? true : false);

    if (en == "y" || en == "Y") {
        std::string paths = read_line("  Scan paths comma-separated [~/projects]: ");
        if (!paths.empty()) {
            json arr = json::array();
            std::istringstream ss(paths);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
                arr.push_back(tok);
            }
            cfg.set("daemon", "scan_paths", arr);
        }

        std::string interval = read_line("  Improvement interval in minutes [30]: ");
        if (!interval.empty()) {
            try { cfg.set("daemon","improvement_interval_minutes", std::stoi(interval)); }
            catch (...) {}
        }
    }

    // ── Shell safety ─────────────────────────────────────────────────────────
    std::cout << "\n── Shell Tool ────────────────────────────────────\n";
    std::string confirm = read_line("  Require confirmation before shell commands? [Y/n]: ");
    cfg.set("tools","shell","require_confirm", (confirm != "n" && confirm != "N"));

    std::cout << "\n✓ Setup complete! Config saved to ~/.terai/config.json\n\n";
    std::cout << "Quick start:\n";
    std::cout << "  terai                       # Interactive mode\n";
    std::cout << "  terai 'what is my IP?'      # One-shot\n";
    std::cout << "  terai --daemon              # Start background daemon\n";
    std::cout << "  terai --provider anthropic  # Use Claude\n\n";
}

} // namespace terai
