#pragma once
// include/daemon/background.h

#include "core/config.h"
#include "third_party/json.hpp"
#include <string>
#include <map>

namespace terai {

using json = nlohmann::json;

// One entry in the daemon's persistent "what have I already tried" ledger.
// This is separate from the interactive Agent's Memory/pattern system —
// the daemon runs as its own process and needs its own durable state to
// avoid re-attempting the same file+task forever, and to avoid clobbering
// a file that's already been improved with a stale generation.
struct DaemonLedgerEntry {
    std::string task;
    std::string outcome;        // "success" | "no_change" | "timeout" | "error"
    std::string content_hash;   // hash of file content AFTER this attempt
    std::string ts;
    int         consecutive_failures = 0;
};

class BackgroundDaemon {
public:
    explicit BackgroundDaemon(Config& cfg);

    void start();           // Fork and run
    static void stop();     // Send SIGTERM to running daemon
    static void status();   // Print PID + last log lines + Ollama reachability

    // Run exactly one improvement cycle in the foreground (no fork), with
    // verbose stdout output. Use this to verify the daemon actually does
    // something useful without waiting for the timer or backgrounding —
    // e.g. `terai --daemon-test`.
    void run_test_cycle();

private:
    Config&     _cfg;
    std::string _pid_file;
    std::string _log_file;
    std::string _ledger_file;
    int         _interval_s;
    int         _max_files;
    int         _request_timeout_s;
    int         _max_tokens;
    int         _max_consecutive_timeouts;   // circuit breaker threshold
    bool        _git_safety_net;
    std::vector<std::string> _scan_paths;
    std::vector<std::string> _extensions;
    std::vector<std::string> _exclude_dir_names;
    bool                      _respect_gitignore;
    std::vector<std::string> _tasks;
    std::string _provider_name;
    std::string _model;

    std::map<std::string, DaemonLedgerEntry> _ledger;  // key: "path::task"

    void daemon_loop();
    void run_cycle();
    bool improve_file(const std::string& path, const std::string& task);
    std::vector<std::string> collect_files() const;
    std::vector<std::string> select_files(const std::vector<std::string>& files) const;
    bool is_excluded_path(const std::string& path) const;
    bool is_git_ignored(const std::string& path) const;

    void log(const std::string& msg) const;
    static std::string expand(const std::string& p);
    static std::string now_str();
    static std::string content_fingerprint(const std::string& data);

    // Ledger persistence
    void load_ledger();
    void save_ledger() const;
    bool should_skip(const std::string& path, const std::string& task,
                     const std::string& current_hash) const;
    void record_outcome(const std::string& path, const std::string& task,
                        const std::string& outcome, const std::string& new_hash);

    // Git safety net — if the scanned path is inside a git repo, commit
    // each successful change individually so it's diffable/revertable via
    // normal git tooling instead of being an opaque overwrite.
    bool is_git_repo(const std::string& file_path) const;
    void git_commit_change(const std::string& file_path, const std::string& task) const;
    std::string git_diff_stat(const std::string& file_path) const;

    // Pings the configured Ollama base_url — returns true if it responds.
    static bool check_ollama_reachable(const std::string& base_url);
};

} // namespace terai
