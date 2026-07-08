// src/daemon/background.cpp
#include "daemon/background.h"
#include "providers/provider_factory.h"
#include "providers/http_client.h"
#include "core/config.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <random>
#include <chrono>
#include <thread>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace terai {

static const std::map<std::string,std::string> IMPROVEMENT_PROMPTS = {
    {"add_docstrings",
     "Add missing docstrings and inline comments to this code. "
     "Output only the improved code, no markdown fences:\n\n{CODE}"},
    {"improve_error_handling",
     "Add proper error handling (try/catch, null checks, return codes) where missing. "
     "Output only the improved code:\n\n{CODE}"},
    {"suggest_optimizations",
     "Add 3 specific optimisation suggestions as comments at the top of the file. "
     "Keep all other code unchanged:\n\n{CODE}"},
    {"fix_style",
     "Fix coding style issues (spacing, naming, line length) without changing logic. "
     "Output only the corrected code:\n\n{CODE}"}
};

// ── Helpers ───────────────────────────────────────────────────────────────────
std::string BackgroundDaemon::expand(const std::string& p) {
    if (p.empty() || p[0] != '~') return p;
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + p.substr(1) : p;
}

std::string BackgroundDaemon::now_str() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void BackgroundDaemon::log(const std::string& msg) const {
    std::string line = "[" + now_str() + "] " + msg + "\n";
    std::ofstream f(_log_file, std::ios::app);
    f << line;
    std::cout << line << std::flush;
}

// ── Constructor ───────────────────────────────────────────────────────────────
BackgroundDaemon::BackgroundDaemon(Config& cfg) : _cfg(cfg) {
    const json& dcfg = cfg.data().value("daemon", json::object());

    _pid_file    = expand(dcfg.value("pid_file",   "~/.terai/daemon.pid"));
    _log_file    = expand(dcfg.value("log_file",   "~/.terai/daemon.log"));
    _interval_s  = dcfg.value("improvement_interval_minutes", 30) * 60;
    _max_files   = dcfg.value("max_files_per_cycle", 5);
    _provider_name = dcfg.value("provider", "ollama");

    if (dcfg.contains("model") && !dcfg["model"].is_null())
        _model = dcfg["model"].get<std::string>();

    for (auto& p : dcfg.value("scan_paths",    json::array()))
        _scan_paths.push_back(expand(p.get<std::string>()));
    for (auto& e : dcfg.value("scan_extensions", json::array()))
        _extensions.push_back(e.get<std::string>());
    for (auto& t : dcfg.value("tasks", json::array()))
        _tasks.push_back(t.get<std::string>());

    fs::create_directories(fs::path(_pid_file).parent_path());
    fs::create_directories(fs::path(_log_file).parent_path());
}

// ── Collect eligible files ────────────────────────────────────────────────────
std::vector<std::string> BackgroundDaemon::collect_files() const {
    std::vector<std::string> files;
    for (auto& base : _scan_paths) {
        if (!fs::is_directory(base)) continue;
        for (auto& entry : fs::recursive_directory_iterator(
                base, fs::directory_options::skip_permission_denied))
        {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            if (std::find(_extensions.begin(), _extensions.end(), ext) != _extensions.end())
                files.push_back(entry.path().string());
        }
    }
    return files;
}

// ── Improve one file ──────────────────────────────────────────────────────────
bool BackgroundDaemon::improve_file(const std::string& path, const std::string& task) {
    auto pit = IMPROVEMENT_PROMPTS.find(task);
    if (pit == IMPROVEMENT_PROMPTS.end()) return false;

    // Read file
    std::ifstream f(path);
    if (!f) return false;
    std::string code((std::istreambuf_iterator<char>(f)), {});
    if (code.size() < 50) return false;

    // Truncate very large files for the local model
    bool truncated = false;
    if (code.size() > 3000) { code = code.substr(0, 3000); truncated = true; }

    std::string prompt = pit->second;
    size_t pos = prompt.find("{CODE}");
    if (pos != std::string::npos) prompt.replace(pos, 6, code);

    // Build provider
    json prov_cfg = _cfg.data()["providers"].value(_provider_name, json::object());
    prov_cfg["name"] = _provider_name;
    if (!_model.empty()) prov_cfg["default_model"] = _model;

    std::unique_ptr<BaseProvider> provider;
    try {
        provider = ProviderFactory::create(prov_cfg);
    } catch (...) { return false; }

    log("  Improving " + fs::path(path).filename().string() + " [" + task + "]...");

    try {
        LLMResponse resp = provider->complete(
            {{"user", prompt, ""}},
            "You are a code improvement assistant. Return only improved code. "
            "No markdown fences, no explanation.",
            2048, 0.3
        );

        std::string improved = resp.content;
        // Strip markdown fences if model added them
        if (improved.size() > 3 && improved.substr(0,3) == "```") {
            improved = improved.substr(improved.find('\n') + 1);
            size_t last = improved.rfind("```");
            if (last != std::string::npos) improved = improved.substr(0, last);
        }

        if (improved.empty() || improved == code) {
            log("  No changes for " + fs::path(path).filename().string());
            return false;
        }

        std::ofstream out(path, std::ios::trunc);
        if (!out) return false;
        out << improved;
        if (truncated) out << "\n// Note: file was truncated for AI processing\n";
        log("  ✓ Improved " + fs::path(path).filename().string());
        return true;

    } catch (std::exception& e) {
        log("  ✗ Error: " + std::string(e.what()));
        return false;
    }
}

// ── One improvement cycle ─────────────────────────────────────────────────────
void BackgroundDaemon::run_cycle() {
    log("=== Improvement cycle started ===");
    auto files = collect_files();
    log("Found " + std::to_string(files.size()) + " eligible files");

    if (files.empty()) { log("Nothing to improve."); return; }

    // Randomly select up to _max_files
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(files.begin(), files.end(), g);
    files.resize(std::min((int)files.size(), _max_files));

    int improved = 0;
    for (auto& fp : files) {
        std::string task = _tasks[rd() % _tasks.size()];
        if (improve_file(fp, task)) ++improved;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    log("=== Cycle done: " + std::to_string(improved) + "/" +
        std::to_string(files.size()) + " files improved ===\n");
}

// ── Daemon loop ───────────────────────────────────────────────────────────────
void BackgroundDaemon::daemon_loop() {
    // Write PID file
    { std::ofstream f(_pid_file); f << getpid(); }

    log("Daemon started (PID " + std::to_string(getpid()) + ")");
    log("Interval: " + std::to_string(_interval_s/60) + "m | Tasks: "
        + std::to_string(_tasks.size()));
    log("Provider: " + _provider_name);

    // Handle SIGTERM
    std::signal(SIGTERM, [](int) {
        std::ofstream("/tmp/.terai_daemon_stop") << "1";
        std::exit(0);
    });

    while (true) {
        run_cycle();
        log("Sleeping " + std::to_string(_interval_s/60) + " minutes...");
        std::this_thread::sleep_for(std::chrono::seconds(_interval_s));
    }
}

// ── Start ─────────────────────────────────────────────────────────────────────
void BackgroundDaemon::start() {
    // Check if already running
    if (fs::exists(_pid_file)) {
        std::ifstream f(_pid_file);
        pid_t existing; f >> existing;
        if (kill(existing, 0) == 0) {
            std::cout << "[Daemon already running: PID " << existing << "]\n";
            return;
        }
        fs::remove(_pid_file);
    }

    pid_t pid = fork();
    if (pid < 0) { std::perror("fork"); return; }

    if (pid > 0) {
        // Parent
        std::cout << "[TerAI daemon started: PID " << pid << "]\n";
        std::cout << "[Log: " << _log_file << "]\n";
        return;
    }

    // Child — become session leader
    setsid();
    daemon_loop();
    std::exit(0);
}

// ── Stop ──────────────────────────────────────────────────────────────────────
void BackgroundDaemon::stop() {
    const char* home = std::getenv("HOME");
    std::string pid_file = std::string(home ? home : "/tmp") + "/.terai/daemon.pid";

    if (!fs::exists(pid_file)) { std::cout << "[No daemon running]\n"; return; }

    std::ifstream f(pid_file);
    pid_t pid; f >> pid;

    if (kill(pid, SIGTERM) == 0) {
        fs::remove(pid_file);
        std::cout << "[Daemon stopped: PID " << pid << "]\n";
    } else {
        fs::remove(pid_file);
        std::cout << "[Daemon was not running]\n";
    }
}

// ── Status ────────────────────────────────────────────────────────────────────
bool BackgroundDaemon::check_ollama_reachable(const std::string& base_url) {
    try {
        Headers h = {{"Content-Type", "application/json"}};
        HttpClient::get(base_url + "/api/tags", h, 3);
        return true;
    } catch (...) {
        return false;
    }
}

void BackgroundDaemon::status() {
    const char* home = std::getenv("HOME");
    std::string base = home ? std::string(home) + "/.terai" : "/tmp";
    std::string pid_file = base + "/daemon.pid";
    std::string log_file = base + "/daemon.log";

    // ── Process alive? ────────────────────────────────────────────────────
    bool proc_alive = false;
    if (fs::exists(pid_file)) {
        std::ifstream f(pid_file);
        pid_t pid; f >> pid;
        // kill(pid, 0) checks existence without sending a real signal
        if (kill(pid, 0) == 0) {
            std::cout << "[Daemon process: RUNNING — PID " << pid << "]\n";
            proc_alive = true;
        } else {
            std::cout << "[Daemon process: NOT RUNNING — stale PID file (" << pid << ")]\n";
        }
    } else {
        std::cout << "[Daemon process: NOT RUNNING]\n";
    }

    // ── Ollama actually reachable? ────────────────────────────────────────
    // A running daemon process tells you nothing about whether it's doing
    // useful work — if Ollama itself is down, every cycle silently fails.
    Config cfg;
    const json& dcfg = cfg.data().value("daemon", json::object());
    std::string provider_name = dcfg.value("provider", "ollama");
    std::string base_url = cfg.data()["providers"].value(provider_name, json::object())
                                .value("base_url", "http://localhost:11434");

    bool ollama_ok = check_ollama_reachable(base_url);
    std::cout << "[Ollama server (" << base_url << "): "
              << (ollama_ok ? "REACHABLE" : "NOT REACHABLE") << "]\n";

    if (proc_alive && !ollama_ok) {
        std::cout << "  ⚠ Daemon process is alive but cannot reach Ollama.\n"
                     "    It will keep failing silently every cycle until\n"
                     "    Ollama is started: run 'ollama serve' or check\n"
                     "    that '" << base_url << "' is correct.\n";
    }

    // ── Recent log ─────────────────────────────────────────────────────────
    if (fs::exists(log_file)) {
        std::ifstream f(log_file);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(f, line)) lines.push_back(line);
        std::cout << "\nLast 10 log lines (" << log_file << "):\n";
        int start = std::max(0, (int)lines.size() - 10);
        for (int i = start; i < (int)lines.size(); ++i)
            std::cout << "  " << lines[i] << "\n";
        if (lines.empty())
            std::cout << "  (log file exists but is empty — no cycle has completed yet)\n";
    } else {
        std::cout << "\n(no log file yet — daemon hasn't started a cycle)\n";
    }
}

void BackgroundDaemon::run_test_cycle() {
    std::cout << "=== Running ONE improvement cycle synchronously (foreground) ===\n";

    json prov_cfg = _cfg.data()["providers"].value(_provider_name, json::object());
    std::string base_url = prov_cfg.value("base_url", "http://localhost:11434");
    std::string effective_model = _model.empty()
        ? prov_cfg.value("default_model", "(none configured)")
        : _model;

    std::cout << "Checking Ollama at " << base_url << " ... ";
    if (!check_ollama_reachable(base_url)) {
        std::cout << "NOT REACHABLE\n";
        std::cout << "Cannot run test cycle — Ollama server did not respond to "
                     "GET " << base_url << "/api/tags.\n"
                     "Start it with 'ollama serve', or confirm the base_url in "
                     "~/.terai/config.json is correct.\n"
                     "Daemon would use model: " << effective_model << "\n";
        return;
    }
    std::cout << "OK (model: " << effective_model << ")\n\n";

    run_cycle();  // reuses the exact same logic the background loop uses

    std::cout << "\n=== Test cycle complete — see output above for what happened ===\n";
    std::cout << "If you saw '0 eligible files', check daemon.scan_paths and\n"
                 "daemon.scan_extensions in ~/.terai/config.json match real files.\n";
}

} // namespace terai
