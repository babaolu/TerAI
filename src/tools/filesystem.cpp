// src/tools/filesystem.cpp
#include "tools/filesystem.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <array>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace terai {

// ── Path helpers ──────────────────────────────────────────────────────────────
static std::string expand_home(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    const char* home = std::getenv("HOME");
    if (!home) return path;
    return std::string(home) + path.substr(1);
}

// ── Shell tool ────────────────────────────────────────────────────────────────
ShellTool::ShellTool(const json& cfg) {
    _require_confirm = cfg.value("require_confirm", true);
    if (cfg.contains("blocked_commands"))
        for (auto& b : cfg["blocked_commands"])
            _blocked.push_back(b.get<std::string>());
}

bool ShellTool::is_blocked(const std::string& cmd) const {
    for (auto& b : _blocked)
        if (cmd.find(b) != std::string::npos) return true;
    return false;
}

ToolResult ShellTool::run(const std::map<std::string,std::string>& args) {
    auto it = args.find("command");
    if (it == args.end())
        return {false, "Missing required argument: command", "shell"};

    const std::string& cmd = it->second;

    if (is_blocked(cmd))
        return {false, "Command blocked for safety: " + cmd, "shell"};

    int timeout = 30;
    auto tit = args.find("timeout");
    if (tit != args.end()) try { timeout = std::stoi(tit->second); } catch(...) {}

    std::string cwd_arg;
    auto cwit = args.find("cwd");
    if (cwit != args.end()) cwd_arg = expand_home(cwit->second);

    // Use popen for output capture; respect timeout via shell's timeout command
    std::string full_cmd = "timeout " + std::to_string(timeout) + " sh -c "
                         + "'" + cmd + "' 2>&1";
    if (!cwd_arg.empty())
        full_cmd = "cd '" + cwd_arg + "' && " + full_cmd;

    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe)
        return {false, "Failed to open shell pipe", "shell"};

    std::string output;
    std::array<char, 512> buf{};
    while (fgets(buf.data(), buf.size(), pipe))
        output += buf.data();

    int status = pclose(pipe);
    bool ok = (WEXITSTATUS(status) == 0);

    if (output.empty()) output = "(no output)";
    if (!ok) output += "\n[exit code: " + std::to_string(WEXITSTATUS(status)) + "]";

    return {ok, output, "shell"};
}

// ── Read file ─────────────────────────────────────────────────────────────────
ReadFileTool::ReadFileTool(const json& cfg) {
    _max_size_kb = cfg.value("max_file_size_kb", 1024);
}

ToolResult ReadFileTool::run(const std::map<std::string,std::string>& args) {
    auto it = args.find("path");
    if (it == args.end())
        return {false, "Missing required argument: path", "read_file"};

    std::string path = expand_home(it->second);

    if (!fs::exists(path))
        return {false, "File not found: " + path, "read_file"};

    auto size_kb = fs::file_size(path) / 1024;
    if ((int)size_kb > _max_size_kb)
        return {false, "File too large (" + std::to_string(size_kb) + "KB > "
                + std::to_string(_max_size_kb) + "KB)", "read_file"};

    std::ifstream f(path);
    if (!f) return {false, "Cannot open: " + path, "read_file"};

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) lines.push_back(line);

    int start = 0, end = (int)lines.size();
    auto sit = args.find("start_line");
    auto eit = args.find("end_line");
    if (sit != args.end()) try { start = std::stoi(sit->second) - 1; } catch(...) {}
    if (eit != args.end()) try { end   = std::stoi(eit->second);     } catch(...) {}

    start = std::max(0, start);
    end   = std::min(end, (int)lines.size());

    std::string content;
    for (int i = start; i < end; ++i)
        content += std::to_string(i+1) + "\t" + lines[i] + "\n";

    return {true, "File: " + path + "\n\n" + content, "read_file"};
}

// ── Write file ────────────────────────────────────────────────────────────────
ToolResult WriteFileTool::run(const std::map<std::string,std::string>& args) {
    auto pit = args.find("path");
    auto cit = args.find("content");
    if (pit == args.end() || cit == args.end())
        return {false, "Missing path or content", "write_file"};

    std::string path    = expand_home(pit->second);
    std::string content = cit->second;
    std::string mode    = "write";
    auto mit = args.find("mode");
    if (mit != args.end()) mode = mit->second;

    fs::create_directories(fs::path(path).parent_path());

    auto flags = std::ios::out | (mode == "append" ? std::ios::app : std::ios::trunc);
    std::ofstream f(path, flags);
    if (!f) return {false, "Cannot open for writing: " + path, "write_file"};

    f << content;
    std::string action = (mode == "append") ? "Appended to" : "Wrote";
    return {true, action + " " + path + " (" + std::to_string(content.size()) + " chars)", "write_file"};
}

// ── List directory ────────────────────────────────────────────────────────────
ToolResult ListDirTool::run(const std::map<std::string,std::string>& args) {
    std::string path = ".";
    auto it = args.find("path");
    if (it != args.end()) path = expand_home(it->second);

    bool show_hidden = false;
    auto hit = args.find("show_hidden");
    if (hit != args.end()) show_hidden = (hit->second == "true" || hit->second == "1");

    if (!fs::exists(path))
        return {false, "Directory not found: " + path, "list_dir"};

    std::vector<std::string> dirs, files;
    for (auto& entry : fs::directory_iterator(path)) {
        std::string fname = entry.path().filename().string();
        if (!show_hidden && fname[0] == '.') continue;
        if (entry.is_directory()) dirs.push_back("📁 " + fname + "/");
        else                      files.push_back("📄 " + fname);
    }
    std::sort(dirs.begin(),  dirs.end());
    std::sort(files.begin(), files.end());

    std::string out = "Contents of " + path + ":\n";
    for (auto& d : dirs)  out += d + "\n";
    for (auto& f : files) out += f + "\n";
    return {true, out, "list_dir"};
}

} // namespace terai
