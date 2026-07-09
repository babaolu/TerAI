// src/daemon/background.cpp
#include "daemon/background.h"
#include "providers/provider_factory.h"
#include "providers/ollama.h"
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
#include <array>

namespace fs = std::filesystem;

namespace terai {

// Shorter, more targeted prompts. The original 2048-token budget was the
// main reason generations took 5+ minutes on-device — none of these tasks
// need anywhere near that much output.
static const std::map<std::string,std::string> IMPROVEMENT_PROMPTS = {
    {"add_docstrings",
     "Add missing docstrings and inline comments to this code. Keep changes "
     "minimal and targeted. Output only the improved code, no markdown fences:\n\n{CODE}"},
    {"improve_error_handling",
     "Add proper error handling (try/catch, null checks, return codes) only "
     "where clearly missing. Output only the improved code:\n\n{CODE}"},
    {"suggest_optimizations",
     "Add up to 3 specific optimisation suggestions as comments at the top "
     "of the file. Keep all other code unchanged:\n\n{CODE}"},
    {"fix_style",
     "Fix coding style issues (spacing, naming, line length) without "
     "changing logic. Output only the corrected code:\n\n{CODE}"}
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

std::string BackgroundDaemon::content_fingerprint(const std::string& data) {
    // Not cryptographic — just a cheap change-detection fingerprint so we
    // don't need to pull in a hashing library for this.
    size_t h = std::hash<std::string>{}(data);
    std::ostringstream ss;
    ss << std::hex << h;
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
    _ledger_file = expand(dcfg.value("ledger_file","~/.terai/daemon_ledger.json"));
    _interval_s  = dcfg.value("improvement_interval_minutes", 30) * 60;
    _max_files   = dcfg.value("max_files_per_cycle", 5);
    _provider_name = dcfg.value("provider", "ollama");

    // Realistic defaults for on-device CPU inference. The old 300s timeout
    // with a 2048-token budget was consistently exceeded — most of these
    // tasks need well under 768 tokens of output. request_timeout_seconds
    // and daemon_max_tokens are both configurable via ~/.terai/config.json
    // if a given device/model needs more headroom.
    _request_timeout_s        = dcfg.value("request_timeout_seconds", 120);
    _max_tokens                = dcfg.value("max_tokens", 768);
    _max_consecutive_timeouts  = dcfg.value("max_consecutive_timeouts", 2);
    _git_safety_net             = dcfg.value("git_safety_net", true);

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

    load_ledger();
}

// ── Ledger persistence ────────────────────────────────────────────────────────
// Tracks every (file, task) attempt across daemon runs so we don't blindly
// re-run the same improvement forever, don't hammer a file/task combo that
// keeps timing out, and can tell whether a file changed since our last
// successful pass (skip if unchanged — nothing new to improve).
void BackgroundDaemon::load_ledger() {
    if (!fs::exists(_ledger_file)) return;
    try {
        std::ifstream f(_ledger_file);
        json arr = json::parse(f);
        for (auto& e : arr) {
            DaemonLedgerEntry entry;
            entry.task                 = e.value("task", "");
            entry.outcome               = e.value("outcome", "");
            entry.content_hash          = e.value("content_hash", "");
            entry.ts                    = e.value("ts", "");
            entry.consecutive_failures  = e.value("consecutive_failures", 0);
            _ledger[e.value("key", "")] = entry;
        }
    } catch (...) {
        log("  ⚠ Ledger file corrupt, starting fresh");
    }
}

void BackgroundDaemon::save_ledger() const {
    json arr = json::array();
    for (auto& [key, e] : _ledger) {
        arr.push_back({
            {"key", key}, {"task", e.task}, {"outcome", e.outcome},
            {"content_hash", e.content_hash}, {"ts", e.ts},
            {"consecutive_failures", e.consecutive_failures}
        });
    }
    std::ofstream f(_ledger_file);
    f << arr.dump(2);
}

bool BackgroundDaemon::should_skip(const std::string& path, const std::string& task,
                                   const std::string& current_hash) const {
    std::string key = path + "::" + task;
    auto it = _ledger.find(key);
    if (it == _ledger.end()) return false;

    const auto& e = it->second;

    // Already successfully applied this exact task to this exact content —
    // nothing changed since, so re-running would be pure waste (or worse,
    // repeated mutation of already-improved code).
    if (e.outcome == "success" && e.content_hash == current_hash) return true;

    // Failed (timeout/error) too many times in a row on this exact content —
    // back off instead of retrying every single cycle forever, as the log
    // you shared shows happening for hours straight.
    if (e.consecutive_failures >= 3 && e.content_hash == current_hash) return true;

    return false;
}

void BackgroundDaemon::record_outcome(const std::string& path, const std::string& task,
                                      const std::string& outcome, const std::string& new_hash) {
    std::string key = path + "::" + task;
    auto& e = _ledger[key];
    e.task = task;
    e.ts   = now_str();

    if (outcome == "success") {
        e.outcome = "success";
        e.content_hash = new_hash;
        e.consecutive_failures = 0;
    } else {
        // Keep the PRE-attempt hash so should_skip can recognise "still the
        // same failing content" vs "file changed, worth trying again".
        if (e.content_hash != new_hash) e.consecutive_failures = 0;
        e.content_hash = new_hash;
        e.outcome = outcome;
        e.consecutive_failures += 1;
    }
    save_ledger();
}

// ── Git safety net ────────────────────────────────────────────────────────────
bool BackgroundDaemon::is_git_repo(const std::string& file_path) const {
    if (!_git_safety_net) return false;
    std::string dir = fs::path(file_path).parent_path().string();
    std::string cmd = "cd '" + dir + "' && git rev-parse --is-inside-work-tree "
                       ">/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

std::string BackgroundDaemon::git_diff_stat(const std::string& file_path) const {
    std::string dir  = fs::path(file_path).parent_path().string();
    std::string name = fs::path(file_path).filename().string();
    std::string cmd  = "cd '" + dir + "' && git diff --stat -- '" + name + "' 2>/dev/null";

    std::string out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    std::array<char, 256> buf{};
    while (fgets(buf.data(), buf.size(), pipe)) out += buf.data();
    pclose(pipe);
    return out;
}

void BackgroundDaemon::git_commit_change(const std::string& file_path,
                                         const std::string& task) const {
    std::string dir  = fs::path(file_path).parent_path().string();
    std::string name = fs::path(file_path).filename().string();
    std::string msg  = "chore(terai-daemon): " + task + " for " + name;
    std::string cmd  = "cd '" + dir + "' && git add '" + name +
                       "' && git commit -q -m '" + msg + "' >/dev/null 2>&1";
    int rc = std::system(cmd.c_str());
    if (rc != 0)
        log("  ⚠ git commit failed for " + name + " (exit " + std::to_string(rc) +
            ") — change was written but not committed; check 'git status' manually");
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

std::vector<std::string> BackgroundDaemon::select_files(
    const std::vector<std::string>& files) const
{
    std::vector<std::string> copy = files;
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(copy.begin(), copy.end(), g);
    if ((int)copy.size() > _max_files) copy.resize(_max_files);
    return copy;
}

// ── Improve one file ──────────────────────────────────────────────────────────
bool BackgroundDaemon::improve_file(const std::string& path, const std::string& task) {
    auto pit = IMPROVEMENT_PROMPTS.find(task);
    if (pit == IMPROVEMENT_PROMPTS.end()) return false;

    std::ifstream f(path);
    if (!f) return false;
    std::string code((std::istreambuf_iterator<char>(f)), {});
    if (code.size() < 50) return false;

    std::string pre_hash = content_fingerprint(code);
    if (should_skip(path, task, pre_hash)) {
        log("  ⤳ Skipping " + fs::path(path).filename().string() + " [" + task +
            "] — already tried on this exact content (see ~/.terai/daemon_ledger.json)");
        return false;
    }

    bool truncated = false;
    if (code.size() > 3000) { code = code.substr(0, 3000); truncated = true; }

    std::string prompt = pit->second;
    size_t pos = prompt.find("{CODE}");
    if (pos != std::string::npos) prompt.replace(pos, 6, code);

    json prov_cfg = _cfg.data()["providers"].value(_provider_name, json::object());
    prov_cfg["name"] = _provider_name;
    if (!_model.empty()) prov_cfg["default_model"] = _model;

    std::unique_ptr<BaseProvider> provider;
    try {
        provider = ProviderFactory::create(prov_cfg);
        if (auto* ollama = dynamic_cast<OllamaProvider*>(provider.get()))
            ollama->set_timeout(_request_timeout_s);
    } catch (...) { return false; }

    log("  Improving " + fs::path(path).filename().string() + " [" + task + "]"
        " (timeout=" + std::to_string(_request_timeout_s) + "s, max_tokens="
        + std::to_string(_max_tokens) + ")...");

    auto start_time = std::chrono::steady_clock::now();

    try {
        LLMResponse resp = provider->complete(
            {{"user", prompt, ""}},
            "You are a code improvement assistant. Return only improved code. "
            "No markdown fences, no explanation. Be concise — do not restate "
            "unchanged code sections unnecessarily.",
            _max_tokens, 0.3
        );

        auto elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time).count();

        std::string improved = resp.content;
        if (improved.size() > 3 && improved.substr(0,3) == "```") {
            improved = improved.substr(improved.find('\n') + 1);
            size_t last = improved.rfind("```");
            if (last != std::string::npos) improved = improved.substr(0, last);
        }

        if (improved.empty() || improved == code) {
            log("  No changes for " + fs::path(path).filename().string() +
                " (" + std::to_string(elapsed_s) + "s)");
            record_outcome(path, task, "no_change", pre_hash);
            return false;
        }

        std::ofstream out(path, std::ios::trunc);
        if (!out) return false;
        out << improved;
        if (truncated) out << "\n// Note: file was truncated for AI processing\n";
        out.close();

        std::string post_hash = content_fingerprint(improved);

        // Diff visibility + git safety net — this is what actually answers
        // "what did it change and can I undo it": a real diff stat, and a
        // real git commit you can `git show`/`git revert` if it's a
        // regression, instead of a silent in-place overwrite.
        if (is_git_repo(path)) {
            git_commit_change(path, task);
            std::string stat = git_diff_stat(path);
            log("  ✓ Improved " + fs::path(path).filename().string() +
                " (" + std::to_string(elapsed_s) + "s) — committed to git"
                + (stat.empty() ? "" : (" | " + stat)));
        } else {
            log("  ✓ Improved " + fs::path(path).filename().string() +
                " (" + std::to_string(elapsed_s) + "s) — NOT in a git repo, "
                "no diff/undo available. Original size " +
                std::to_string(code.size()) + "B -> " +
                std::to_string(improved.size()) + "B");
        }

        record_outcome(path, task, "success", post_hash);
        return true;

    } catch (std::exception& e) {
        auto elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time).count();
        std::string what = e.what();
        std::string outcome = (what.find("Timeout") != std::string::npos) ? "timeout" : "error";
        log("  ✗ " + outcome + " after " + std::to_string(elapsed_s) + "s: " + what);
        record_outcome(path, task, outcome, pre_hash);
        return false;
    }
}

// ── One improvement cycle ─────────────────────────────────────────────────────
void BackgroundDaemon::run_cycle() {
    log("=== Improvement cycle started ===");
    auto files = collect_files();
    log("Found " + std::to_string(files.size()) + " eligible files");

    if (files.empty()) { log("Nothing to improve."); return; }

    auto selected = select_files(files);
    std::random_device rd;

    int improved = 0;
    int consecutive_timeouts = 0;

    for (auto& fp : selected) {
        std::string task = _tasks[rd() % _tasks.size()];
        bool ok = improve_file(fp, task);

        if (ok) {
            improved++;
            consecutive_timeouts = 0;
        } else {
            // Circuit breaker: if Ollama is clearly too slow/unreachable
            // right now, stop burning the rest of the cycle on guaranteed
            // failures — try again next cycle instead of spending 25+
            // minutes hitting the same wall repeatedly, as happened here.
            std::string key = fp + "::" + task;
            auto it = _ledger.find(key);
            if (it != _ledger.end() && it->second.outcome == "timeout") {
                consecutive_timeouts++;
                if (consecutive_timeouts >= _max_consecutive_timeouts) {
                    log("  ⚠ " + std::to_string(consecutive_timeouts) +
                        " consecutive timeouts — aborting rest of this cycle early. "
                        "Consider lowering daemon.max_tokens or raising "
                        "daemon.request_timeout_seconds in ~/.terai/config.json.");
                    break;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    log("=== Cycle done: " + std::to_string(improved) + "/" +
        std::to_string(selected.size()) + " files improved ===\n");
}

// ── Daemon loop ───────────────────────────────────────────────────────────────
void BackgroundDaemon::daemon_loop() {
    { std::ofstream f(_pid_file); f << getpid(); }

    log("Daemon started (PID " + std::to_string(getpid()) + ")");
    log("Interval: " + std::to_string(_interval_s/60) + "m | Tasks: "
        + std::to_string(_tasks.size()) + " | timeout=" +
        std::to_string(_request_timeout_s) + "s | max_tokens=" +
        std::to_string(_max_tokens));
    log("Provider: " + _provider_name);

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
        std::cout << "[TerAI daemon started: PID " << pid << "]\n";
        std::cout << "[Log: " << _log_file << "]\n";
        return;
    }

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

    bool proc_alive = false;
    if (fs::exists(pid_file)) {
        std::ifstream f(pid_file);
        pid_t pid; f >> pid;
        if (kill(pid, 0) == 0) {
            std::cout << "[Daemon process: RUNNING — PID " << pid << "]\n";
            proc_alive = true;
        } else {
            std::cout << "[Daemon process: NOT RUNNING — stale PID file (" << pid << ")]\n";
        }
    } else {
        std::cout << "[Daemon process: NOT RUNNING]\n";
    }

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

    // ── Ledger summary — answers "is it actually making progress?" ─────────
    std::string ledger_file = base + "/daemon_ledger.json";
    if (fs::exists(ledger_file)) {
        try {
            std::ifstream lf(ledger_file);
            json arr = json::parse(lf);
            int success = 0, timeout = 0, error = 0, no_change = 0;
            for (auto& e : arr) {
                std::string o = e.value("outcome", "");
                if (o == "success")   success++;
                else if (o == "timeout") timeout++;
                else if (o == "error")   error++;
                else if (o == "no_change") no_change++;
            }
            std::cout << "\n[Ledger: " << success << " succeeded, " << timeout
                      << " timed out, " << error << " errored, " << no_change
                      << " no-change — " << arr.size() << " total file+task combos tracked]\n";
        } catch (...) {}
    }

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
    std::cout << "OK (model: " << effective_model << ", timeout: "
              << _request_timeout_s << "s, max_tokens: " << _max_tokens << ")\n\n";

    run_cycle();

    std::cout << "\n=== Test cycle complete — see output above for what happened ===\n";
    std::cout << "If you saw '0 eligible files', check daemon.scan_paths and\n"
                 "daemon.scan_extensions in ~/.terai/config.json match real files.\n";
}

} // namespace terai
