#pragma once
// include/core/agent.h

#include "core/config.h"
#include "core/optimizer.h"
#include "core/memory.h"
#include "core/display.h"
#include "providers/base_provider.h"
#include "tools/tool_registry.h"
#include <memory>
#include <string>

namespace terai {

struct AgentOptions {
    bool use_tools = true;
    bool verbose   = false;
    bool stream    = true;
};

class Agent {
public:
    using Options = AgentOptions;

    explicit Agent(Config& cfg, AgentOptions opts = AgentOptions{});

    void run_once(const std::string& prompt);
    void run_interactive();
    void switch_provider(const std::string& name);
    void switch_model   (const std::string& model);

private:
    Config&                         _cfg;
    AgentOptions                    _opts;
    std::unique_ptr<BaseProvider>   _provider;
    std::unique_ptr<ToolRegistry>   _tools;
    Memory                          _memory;
    ContextOptimizer                _optimizer;
    int                             _turn_count = 0;

    std::string build_system() const;
    std::string run_loop(const std::string& user_input);
    ToolCall    parse_action(const std::string& text) const;
    std::string extract_final(const std::string& text) const;
    void        reflect();
    bool        handle_command(const std::string& input);
    void        print_help() const;
    void        print_history() const;
};

} // namespace terai
