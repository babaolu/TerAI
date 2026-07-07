#pragma once
// include/daemon/background.h

#include "core/config.h"
#include <string>

namespace terai {

class BackgroundDaemon {
public:
    explicit BackgroundDaemon(Config& cfg);

    void start();           // Fork and run
    static void stop();     // Send SIGTERM to running daemon
    static void status();   // Print PID + last log lines

private:
    Config&     _cfg;
    std::string _pid_file;
    std::string _log_file;
    int         _interval_s;
    int         _max_files;
    std::vector<std::string> _scan_paths;
    std::vector<std::string> _extensions;
    std::vector<std::string> _tasks;
    std::string _provider_name;
    std::string _model;

    void daemon_loop();
    void run_cycle();
    bool improve_file(const std::string& path, const std::string& task);
    std::vector<std::string> collect_files() const;

    void log(const std::string& msg) const;
    static std::string expand(const std::string& p);
    static std::string now_str();
};

} // namespace terai
