#pragma once
// include/tools/filesystem.h

#include "tools/tool_registry.h"

namespace terai {

// ── Shell execution ───────────────────────────────────────────────────────────
class ShellTool : public BaseTool {
public:
    explicit ShellTool(const json& cfg);
    std::string name()        const override { return "shell"; }
    std::string description() const override { return "Execute shell commands"; }
    std::string params_hint() const override {
        return "command (string, required), timeout (int, optional, default 30), "
               "cwd (string, optional)";
    }
    ToolResult  run(const std::map<std::string,std::string>& args) override;

private:
    bool                     _require_confirm;
    std::vector<std::string> _blocked;
    bool is_blocked(const std::string& cmd) const;
};

// ── Read file ─────────────────────────────────────────────────────────────────
class ReadFileTool : public BaseTool {
public:
    explicit ReadFileTool(const json& cfg);
    std::string name()        const override { return "read_file"; }
    std::string description() const override { return "Read a file from disk"; }
    std::string params_hint() const override {
        return "path (string, required), start_line (int, optional), "
               "end_line (int, optional)";
    }
    ToolResult  run(const std::map<std::string,std::string>& args) override;
private:
    int _max_size_kb;
};

// ── Write file ────────────────────────────────────────────────────────────────
class WriteFileTool : public BaseTool {
public:
    std::string name()        const override { return "write_file"; }
    std::string description() const override { return "Write content to a file"; }
    std::string params_hint() const override {
        return "path (string, required), content (string, required), "
               "mode (string, optional: \"write\" or \"append\")";
    }
    ToolResult  run(const std::map<std::string,std::string>& args) override;
};

// ── List directory ────────────────────────────────────────────────────────────
class ListDirTool : public BaseTool {
public:
    std::string name()        const override { return "list_dir"; }
    std::string description() const override { return "List directory contents"; }
    std::string params_hint() const override {
        return "path (string, optional, default \".\"), "
               "show_hidden (bool, optional)";
    }
    ToolResult  run(const std::map<std::string,std::string>& args) override;
};

} // namespace terai
