#pragma once
// include/tools/tool_registry.h

#include "core/types.h"
#include "third_party/json.hpp"
#include <string>
#include <memory>
#include <map>
#include <vector>
#include <functional>

namespace terai {

using json = nlohmann::json;

// ── Base tool ─────────────────────────────────────────────────────────────────
class BaseTool {
public:
    virtual ~BaseTool() = default;
    virtual std::string name()        const = 0;
    virtual std::string description() const = 0;
    virtual ToolResult  run(const std::map<std::string,std::string>& args) = 0;
};

// ── Registry ──────────────────────────────────────────────────────────────────
class ToolRegistry {
public:
    explicit ToolRegistry(const json& config);

    ToolResult run(const std::string& name,
                   const std::map<std::string,std::string>& args);

    bool has(const std::string& name) const;

    std::vector<std::string> list() const;

    // Formatted list for display
    std::string list_str() const;

private:
    std::map<std::string, std::unique_ptr<BaseTool>> _tools;
    void register_tool(std::unique_ptr<BaseTool> t);
};

} // namespace terai
