#pragma once
// include/tools/web_search.h

#include "tools/tool_registry.h"

namespace terai {

class WebSearchTool : public BaseTool {
public:
    explicit WebSearchTool(const json& cfg);

    std::string name()        const override { return "web_search"; }
    std::string description() const override {
        return "Search the internet for current information";
    }
    ToolResult  run(const std::map<std::string,std::string>& args) override;

private:
    std::string _engine;
    std::string _google_key;
    std::string _google_cx;

    ToolResult ddg_search(const std::string& query, int max_results);
    ToolResult google_search(const std::string& query, int max_results);
};

} // namespace terai
