// src/main.cpp
// TerAI — Terminal AI Assistant
// Native C++ build for Termux ARM64

#include "core/config.h"
#include "core/agent.h"
#include "core/display.h"
#include "core/setup.h"
#include "daemon/background.h"
#include "providers/http_client.h"

#include <iostream>
#include <string>
#include <vector>
#include <cstring>

using namespace terai;

static void print_usage(const char* prog) {
    std::printf(R"(
Usage: %s [OPTIONS] [PROMPT]

  PROMPT              One-shot mode: run prompt and exit

Options:
  -p, --provider NAME   LLM provider (anthropic, openai, gemini, openrouter,
                          huggingface, nvidia_nim, ollama)
  -m, --model NAME      Model name override
  --no-tools            Disable tool use (pure chat mode)
  --no-stream           Disable streaming (wait for full response)
  -v, --verbose         Show token counts, tool cache hits, reflections

  --setup               Run interactive setup wizard
  --config              Print current configuration
  --daemon              Start background Ollama improvement daemon
  --daemon-stop         Stop running daemon
  --daemon-status       Show daemon status and recent log
  -h, --help            Show this help

Examples:
  terai                              # Interactive REPL
  terai "list files in ~/projects"   # One-shot
  terai -p anthropic "explain RLHF"  # Use Claude
  terai --daemon                     # Start background daemon
  terai --provider ollama --model mistral
)", prog);
}

int main(int argc, char* argv[]) {
    HttpClient::global_init();

    Config cfg;
    Display::init(cfg.get<bool>("display","color","",true));

    // ── Parse arguments ───────────────────────────────────────────────────────
    std::string one_shot_prompt;
    std::string provider_override;
    std::string model_override;
    bool do_setup        = false;
    bool do_config       = false;
    bool do_daemon       = false;
    bool do_daemon_stop  = false;
    bool do_daemon_status= false;
    bool no_tools        = false;
    bool no_stream       = false;
    bool verbose         = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help")         { print_usage(argv[0]); return 0; }
        if (arg == "--setup")                        { do_setup        = true; continue; }
        if (arg == "--config")                       { do_config       = true; continue; }
        if (arg == "--daemon")                       { do_daemon       = true; continue; }
        if (arg == "--daemon-stop")                  { do_daemon_stop  = true; continue; }
        if (arg == "--daemon-status")                { do_daemon_status= true; continue; }
        if (arg == "--no-tools")                     { no_tools        = true; continue; }
        if (arg == "--no-stream")                    { no_stream       = true; continue; }
        if (arg == "-v" || arg == "--verbose")       { verbose         = true; continue; }

        if ((arg == "-p" || arg == "--provider") && i+1 < argc) {
            provider_override = argv[++i]; continue;
        }
        if ((arg == "-m" || arg == "--model") && i+1 < argc) {
            model_override = argv[++i]; continue;
        }

        // Positional = one-shot prompt
        if (!arg.empty() && arg[0] != '-') {
            one_shot_prompt = arg;
        }
    }

    // ── Apply overrides ───────────────────────────────────────────────────────
    if (!provider_override.empty()) cfg.set("active_provider", provider_override);
    if (!model_override.empty())    cfg.set("model_override",  model_override);

    // ── Dispatch ──────────────────────────────────────────────────────────────
    if (do_setup) {
        run_setup(cfg);
        HttpClient::global_cleanup();
        return 0;
    }

    if (do_config) {
        cfg.display();
        HttpClient::global_cleanup();
        return 0;
    }

    if (do_daemon_stop) {
        BackgroundDaemon::stop();
        HttpClient::global_cleanup();
        return 0;
    }

    if (do_daemon_status) {
        BackgroundDaemon::status();
        HttpClient::global_cleanup();
        return 0;
    }

    if (do_daemon) {
        BackgroundDaemon daemon(cfg);
        daemon.start();
        HttpClient::global_cleanup();
        return 0;
    }

    // ── Print banner ──────────────────────────────────────────────────────────
    Display::banner("");

    // ── Build agent ───────────────────────────────────────────────────────────
    Agent::Options opts;
    opts.use_tools = !no_tools;
    opts.stream    = !no_stream;
    opts.verbose   = verbose;

    try {
        Agent agent(cfg, opts);

        if (!one_shot_prompt.empty()) {
            agent.run_once(one_shot_prompt);
        } else {
            agent.run_interactive();
        }
    } catch (std::exception& e) {
        Display::error(std::string("Fatal: ") + e.what());
        HttpClient::global_cleanup();
        return 1;
    }

    HttpClient::global_cleanup();
    return 0;
}
