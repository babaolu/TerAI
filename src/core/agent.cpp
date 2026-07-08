// src/core/agent.cpp
#include "core/agent.h"
#include "providers/provider_factory.h"
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <regex>
#include <sstream>
#include <algorithm>

#ifdef HAVE_READLINE
#  include <readline/readline.h>
#  include <readline/history.h>
#endif

namespace terai {

// ── System prompt template ─────────────────────────────────────────────────────
static const char* SYSTEM_TPL = R"(You are TerAI, a fast, intelligent terminal AI assistant running natively in Termux on Android (ARM64). Built in C++ for zero-latency response.

## Your capabilities:
- Execute shell commands, read/write files, search the web
- Reason step-by-step before acting (ReAct loop)
- Learn from successful patterns to improve over time

## ReAct reasoning format:
When you need tools or multi-step reasoning, structure your output as:

THOUGHT: [Your reasoning about what to do]
ACTION: [tool_name]
ARGS: {"key": "value", "key2": "value2"}

Supported tools: {TOOL_LIST}

After seeing the OBSERVATION, continue with another THOUGHT/ACTION or:
FINAL: [Your complete answer]

For simple questions that don't need tools, just respond directly — no need for the ReAct structure.

## Rules:
- Always verify shell commands are safe before running
- Read files before modifying them
- Be concise — the user is on a mobile terminal
- Ask for clarification if a task is ambiguous

{PATTERN_CTX}
{EXTRA_SYS})";

// ── Constructor ───────────────────────────────────────────────────────────────
Agent::Agent(Config& cfg, Options opts)
    : _cfg(cfg)
    , _opts(opts)
    , _memory(json::object())  // replaced below
    , _optimizer({
        cfg.get<bool>  ("token_optimization","enabled",           "",  true),
        cfg.get<int>   ("agent","context_window",              "", 16000),
        cfg.get<int>   ("agent","summarize_threshold",         "", 12000),
        cfg.get<bool>  ("token_optimization","compress_whitespace","", true),
        cfg.get<bool>  ("token_optimization","cache_tool_results", "", true)
      })
{
    // Rebuild memory with proper json config
    const json& mem_cfg = cfg.data().value("memory", json::object());
    _memory = Memory(mem_cfg);

    _provider = ProviderFactory::create(cfg.active_provider_config());
    if (_opts.use_tools)
        _tools = std::make_unique<ToolRegistry>(cfg.data());
}

// ── System prompt ─────────────────────────────────────────────────────────────
std::string Agent::build_system() const {
    std::string tool_list = _tools ? _tools->list_str() : "none";
    std::string pattern_ctx = _memory.pattern_context();
    std::string extra = _cfg.get<std::string>("agent","system_prompt_extra","","");

    std::string sys = SYSTEM_TPL;
    auto replace = [&](const std::string& from, const std::string& to) {
        size_t pos = sys.find(from);
        while (pos != std::string::npos) {
            sys.replace(pos, from.size(), to);
            pos = sys.find(from, pos + to.size());
        }
    };
    replace("{TOOL_LIST}",   tool_list);
    replace("{PATTERN_CTX}", pattern_ctx);
    replace("{EXTRA_SYS}",   extra);
    return sys;
}

// ── Parse ACTION block ────────────────────────────────────────────────────────
ToolCall Agent::parse_action(const std::string& text) const {
    ToolCall tc;
    std::regex action_re(R"(ACTION:\s*(\w+))", std::regex::icase);
    std::regex args_re(R"(ARGS:\s*(\{[^}]*\}))", std::regex::icase | std::regex::multiline);
    std::smatch m;

    if (!std::regex_search(text, m, action_re)) return tc;
    tc.name  = m[1].str();
    // lowercase
    std::transform(tc.name.begin(), tc.name.end(), tc.name.begin(), ::tolower);
    tc.valid = true;

    if (std::regex_search(text, m, args_re)) {
        try {
            json args_json = json::parse(m[1].str());
            for (auto& [k,v] : args_json.items()) {
                if (v.is_string())    tc.args[k] = v.get<std::string>();
                else if (v.is_number_integer()) tc.args[k] = std::to_string(v.get<int>());
                else if (v.is_boolean())        tc.args[k] = v.get<bool>() ? "true" : "false";
                else                            tc.args[k] = v.dump();
            }
        } catch (...) {
            // Try key="value" pairs as fallback
            std::regex kv_re(R"(\"(\w+)\"\s*:\s*\"([^\"]*)\")");
            auto begin = std::sregex_iterator(m[1].first, m[1].second, kv_re);
            for (auto it = begin; it != std::sregex_iterator(); ++it)
                tc.args[(*it)[1].str()] = (*it)[2].str();
        }
    }
    return tc;
}

// ── Extract FINAL answer ──────────────────────────────────────────────────────
std::string Agent::extract_final(const std::string& text) const {
    std::regex final_re(R"(FINAL:\s*([\s\S]*))", std::regex::icase);
    std::smatch m;
    if (std::regex_search(text, m, final_re))
        return m[1].str();

    // No ReAct structure — return as-is (direct answer)
    if (text.find("THOUGHT:") == std::string::npos &&
        text.find("ACTION:")  == std::string::npos)
        return text;

    return text;
}

// ── ReAct agent loop ──────────────────────────────────────────────────────────
std::string Agent::run_loop(const std::string& user_input) {
    auto msgs = _memory.session();
    msgs.push_back({"user", user_input, ""});

    std::string system = build_system();
    msgs = _optimizer.optimize(msgs, system);

    int max_iter = _cfg.get<int>("agent","max_iterations","",10);
    int max_tok  = _cfg.get<int>("agent","max_tokens","",   4096);
    double temp  = _cfg.get<double>("agent","temperature","", 0.7);

    std::string last_response;
    std::vector<std::pair<std::string,std::string>> used_tools;

    for (int iter = 0; iter < max_iter; ++iter) {
        LLMResponse resp;

        if (_opts.stream) {
            // Stream tokens to terminal as they arrive
            std::string label = Display::prompt_prefix();
            // Print the terai prefix once
            std::cout << "\n" << "\033[96m\033[1mterai\033[0m \033[2m❯\033[0m ";
            resp = _provider->stream(msgs, system, max_tok, temp,
                [](const std::string& token) {
                    Display::stream_token(token);
                });
            std::cout << "\n";
        } else {
            resp = _provider->complete(msgs, system, max_tok, temp);
        }

        last_response = resp.content;

        if (_opts.verbose)
            Display::dim("[tokens in=" + std::to_string(resp.input_tokens)
                       + " out=" + std::to_string(resp.output_tokens) + "]");

        // Check for tool call
        ToolCall tc = parse_action(last_response);
        if (!tc.valid || !_opts.use_tools || !_tools) break;

        // Cache lookup
        std::string cache_key = tc.name + "::" + json(tc.args).dump();
        std::string cached    = _optimizer.get_cached(cache_key);
        std::string observation;

        if (!cached.empty()) {
            observation = "[cached] " + cached;
            if (_opts.verbose) Display::dim("[tool result from cache]");
        } else {
            Display::tool_call(tc.name, json(tc.args).dump());
            ToolResult result = _tools->run(tc.name, tc.args);
            observation       = result.format();
            _optimizer.set_cached(cache_key, observation);
            used_tools.push_back({tc.name, observation});
        }

        // Inject observation and continue loop
        msgs.push_back({"assistant", last_response,  ""});
        msgs.push_back({"user",
            "OBSERVATION from " + tc.name + ":\n" + observation + "\n\nContinue.", ""});

        // Re-optimise after adding tool exchange
        msgs = _optimizer.optimize(msgs, system);
    }

    std::string final_ans = extract_final(last_response);

    // Save tool pattern on success
    bool si_enabled = _cfg.get<bool>("self_improvement","enabled","",true);
    bool save_pat   = _cfg.get<bool>("self_improvement","save_successful_patterns","",true);
    if (si_enabled && save_pat && !used_tools.empty()) {
        std::vector<std::string> names;
        for (auto& [n,_] : used_tools) names.push_back(n);
        std::string desc = "Used [";
        for (size_t i=0;i<names.size();++i) { if(i) desc+=","; desc+=names[i]; }
        desc += "] to answer: " + user_input.substr(0,80);
        _memory.save_pattern("tool_sequence", desc, final_ans.substr(0,300));
    }

    return final_ans;
}

// ── Self-reflection ────────────────────────────────────────────────────────────
void Agent::reflect() {
    auto msgs = _memory.session();
    if (msgs.size() < 4) return;

    size_t start = msgs.size() >= 6 ? msgs.size() - 6 : 0;
    std::string convo;
    for (size_t i = start; i < msgs.size(); ++i) {
        convo += "[" + msgs[i].role + "]: "
              + msgs[i].content.substr(0, std::min((size_t)200, msgs[i].content.size()))
              + "\n";
    }

    std::vector<Message> ref_msgs = {{"user",
        "Review this conversation and identify:\n"
        "1. What worked well?\n2. Any inefficiencies?\n"
        "3. One improvement for next time.\nBe brief (3 bullets max).\n\n"
        "Conversation:\n" + convo, ""}};

    try {
        LLMResponse r = _provider->complete(ref_msgs,
            "You are a self-improving AI. Analyse conversations concisely.",
            300, 0.5);
        _memory.save_pattern("reflection", "Self-reflection", r.content);
        if (_opts.verbose)
            Display::dim("[Reflection: " + r.content.substr(0,100) + "...]");
    } catch (...) {}
}

// ── run_once ──────────────────────────────────────────────────────────────────
void Agent::run_once(const std::string& prompt) {
    _memory.add("user", prompt);
    Display::thinking();
    std::string resp = run_loop(prompt);
    Display::clear_line();
    if (!_opts.stream)
        Display::assistant(resp);
    _memory.add("assistant", resp);
}

// ── REPL ──────────────────────────────────────────────────────────────────────
void Agent::run_interactive() {
    Display::status("Provider : " + _provider->name() + " | Model: " + _provider->model());
    if (_tools) Display::status("Tools    : " + _tools->list_str());
    Display::separator();
    std::cout << "Type 'exit' or Ctrl-C to quit. '/help' for commands.\n";
    std::cout << "Tip: for large pastes, use '/file <path>' to load a whole file as one prompt.\n\n";

#ifdef HAVE_READLINE
    // Without this, terminals that send bracketed-paste escape sequences
    // (ESC[200~ ... ESC[201~) have each embedded newline in a paste treated
    // as a real Enter press — a multi-line paste gets split into N separate
    // submissions instead of arriving as one block. This tells readline to
    // honor bracketed paste: embedded newlines become literal text in the
    // buffer, and only the user's actual Enter key submits.
    rl_variable_bind("enable-bracketed-paste", "on");
#endif

    int reflection_every = _cfg.get<int>("self_improvement","reflection_after_n_turns","",5);

    while (true) {
        // ── Readline or fallback ──────────────────────────────────────────────
        std::string input;
#ifdef HAVE_READLINE
        char* line = readline(Display::prompt_prefix().c_str());
        if (!line) { std::cout << "\n"; break; }  // Ctrl-D
        input = line;
        if (!input.empty()) add_history(line);
        free(line);
#else
        std::cout << Display::prompt_prefix();
        if (!std::getline(std::cin, input)) break;
#endif
        // Trim
        while (!input.empty() && std::isspace((unsigned char)input.back()))  input.pop_back();
        while (!input.empty() && std::isspace((unsigned char)input.front())) input.erase(input.begin());

        if (input.empty()) continue;
        if (handle_command(input)) continue;

        if (input == "exit" || input == "quit" || input == "bye") {
            _memory.save_session();
            std::cout << "Session saved. Goodbye!\n";
            break;
        }

        _memory.add("user", input);
        ++_turn_count;

        try {
            if (!_opts.stream) Display::thinking();
            std::string resp = run_loop(input);
            if (!_opts.stream) {
                Display::clear_line();
                Display::assistant(resp);
            }
            _memory.add("assistant", resp);

            // Self-reflection
            bool si = _cfg.get<bool>("self_improvement","enabled","",true);
            if (si && _turn_count % reflection_every == 0)
                reflect();

        } catch (std::exception& e) {
            Display::error(e.what());
        }
    }
}

// ── Commands ──────────────────────────────────────────────────────────────────
bool Agent::handle_command(std::string& input) {
    if (input == "/help")    { print_help();    return true; }
    if (input == "/history") { print_history(); return true; }

    if (input == "/clear") {
        _memory.clear_session();
        std::cout << "[Session cleared]\n";
        return true;
    }

    if (input == "/tokens") {
        auto msgs   = _memory.session();
        std::string sys = build_system();
        auto report = _optimizer.report(msgs, sys);
        std::printf("Tokens: %d / %d  (%.1f%% of context window)\n",
            report.total_tokens, report.context_window, report.utilization_pct);
        return true;
    }

    if (input.rfind("/provider ", 0) == 0) {
        switch_provider(input.substr(10));
        return true;
    }

    if (input.rfind("/model ", 0) == 0) {
        switch_model(input.substr(7));
        return true;
    }

    if (input.rfind("/file ", 0) == 0) {
        std::string path = input.substr(6);
        while (!path.empty() && path.front() == ' ') path.erase(path.begin());
        while (!path.empty() && path.back()  == ' ') path.pop_back();

        // Expand leading ~
        if (!path.empty() && path[0] == '~') {
            const char* home = std::getenv("HOME");
            if (home) path = std::string(home) + path.substr(1);
        }

        std::ifstream f(path);
        if (!f) {
            Display::error("Cannot open file: " + path);
            return true;  // handled — nothing to submit
        }
        std::string content((std::istreambuf_iterator<char>(f)), {});
        if (content.empty()) {
            Display::error("File is empty: " + path);
            return true;
        }

        std::cout << "[Loaded " << content.size() << " chars from " << path
                   << " — submitting as one prompt]\n";
        input = content;  // rewrite input; fall through to normal agent turn
        return false;
    }

    return false;  // Not a command
}

void Agent::switch_provider(const std::string& name) {
    try {
        _cfg.set("active_provider", name);
        _provider = ProviderFactory::create(_cfg.active_provider_config());
        std::cout << "[Switched to: " << name << " / " << _provider->model() << "]\n";
    } catch (std::exception& e) {
        Display::error(e.what());
    }
}

void Agent::switch_model(const std::string& model) {
    _provider->set_model(model);
    std::cout << "[Model set to: " << model << "]\n";
}

void Agent::print_help() const {
    std::cout << R"(
TerAI Commands:
  /help             This help
  /history          List saved sessions
  /clear            Clear current session
  /tokens           Show token usage and context utilisation
  /provider NAME    Switch provider  (anthropic, openai, gemini, openrouter, ollama, ...)
  /model NAME       Switch model
  /file PATH        Load a whole file as one prompt (safer than pasting
                     large multi-line text — avoids paste/terminal quirks)
  exit / quit       Save session and exit
)";
}

void Agent::print_history() const {
    auto h = _memory.history(10);
    if (h.empty()) { std::cout << "[No saved history]\n"; return; }
    for (auto& entry : h) {
        std::string id    = entry.value("id","");
        std::string title = entry.value("title","");
        if (title.size() > 50) title = title.substr(0,50) + "...";
        std::cout << "  " << id << " — " << title << "\n";
    }
}

} // namespace terai
