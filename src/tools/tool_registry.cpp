// src/tools/tool_registry.cpp
#include "tools/tool_registry.h"
#include "tools/web_search.h"
#include "tools/filesystem.h"
#include <sstream>

namespace terai {

ToolRegistry::ToolRegistry(const json& config) {
    const json& tools = config.value("tools", json::object());

    // Web search
    if (tools.value("web_search", json::object()).value("enabled", true))
        register_tool(std::make_unique<WebSearchTool>(
            tools.value("web_search", json::object())));

    // Shell
    if (tools.value("shell", json::object()).value("enabled", true))
        register_tool(std::make_unique<ShellTool>(
            tools.value("shell", json::object())));

    // Filesystem
    if (tools.value("filesystem", json::object()).value("enabled", true)) {
        const json& fs_cfg = tools.value("filesystem", json::object());
        register_tool(std::make_unique<ReadFileTool>(fs_cfg));
        register_tool(std::make_unique<WriteFileTool>());
        register_tool(std::make_unique<ListDirTool>());
    }
}

void ToolRegistry::register_tool(std::unique_ptr<BaseTool> t) {
    _tools[t->name()] = std::move(t);
}

bool ToolRegistry::has(const std::string& name) const {
    return _tools.count(name) > 0;
}

ToolResult ToolRegistry::run(const std::string& name,
                             const std::map<std::string,std::string>& args)
{
    auto it = _tools.find(name);
    if (it == _tools.end())
        return {false, "Unknown tool: " + name + ". Available: " + list_str(), name};
    return it->second->run(args);
}

std::vector<std::string> ToolRegistry::list() const {
    std::vector<std::string> names;
    for (auto& [k,_] : _tools) names.push_back(k);
    return names;
}

std::string ToolRegistry::list_str() const {
    std::string s;
    for (auto& n : list()) {
        if (!s.empty()) s += ", ";
        s += n;
    }
    return s;
}

std::string ToolRegistry::schema_str() const {
    std::string s;
    for (auto& [name, tool] : _tools) {
        s += "- " + name + "(" + tool->params_hint() + ")\n";
    }
    return s;
}

} // namespace terai
