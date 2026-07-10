#pragma once
// include/core/display.h

#include <string>

namespace terai {

struct Colors {
    static constexpr const char* RESET   = "\033[0m";
    static constexpr const char* BOLD    = "\033[1m";
    static constexpr const char* DIM     = "\033[2m";
    static constexpr const char* GREEN   = "\033[32m";
    static constexpr const char* CYAN    = "\033[36m";
    static constexpr const char* YELLOW  = "\033[33m";
    static constexpr const char* BLUE    = "\033[34m";
    static constexpr const char* RED     = "\033[31m";
    static constexpr const char* BGREEN  = "\033[92m";
    static constexpr const char* BCYAN   = "\033[96m";
    static constexpr const char* BYELLOW = "\033[93m";
};

class Display {
public:
    static void init(bool use_color);

    static void banner(const std::string& text);
    static void status(const std::string& msg);
    static void separator();
    static void assistant(const std::string& text);
    static void tool_call(const std::string& name, const std::string& args_json);
    static void thinking();
    static void clear_line();
    static void error(const std::string& msg);
    static void success(const std::string& msg);
    static void dim(const std::string& msg);
    static void stream_token(const std::string& token);  // No newline

    static std::string prompt_prefix();

    // Readline needs invisible bytes (ANSI color codes) wrapped in
    // \001...\002 (RL_PROMPT_START_IGNORE/END_IGNORE) so it can correctly
    // calculate the prompt's on-screen width. Without this, readline
    // miscounts the prompt length and every subsequent keystroke's cursor
    // math is wrong — symptom: new input overwrites old text on the same
    // line instead of advancing normally. Use this variant specifically
    // for the string passed to readline(); use prompt_prefix() for the
    // plain std::cout fallback path.
    static std::string prompt_prefix_readline();

private:
    static bool _color;
    static int  _spinner_idx;
    static std::string c(const std::string& code, const std::string& text);
};

} // namespace terai
