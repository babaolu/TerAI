// src/core/display.cpp
#include "core/display.h"
#include <iostream>
#include <unistd.h>   // isatty
#include <cstdio>

namespace terai {

bool Display::_color      = true;
int  Display::_spinner_idx = 0;

static const char* SPINNER[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
static const int   SPINNER_N  = 10;

void Display::init(bool use_color) {
    _color = use_color && isatty(STDOUT_FILENO);
}

std::string Display::c(const std::string& code, const std::string& text) {
    if (!_color) return text;
    return code + text + Colors::RESET;
}

static const char* BANNER_TEXT = R"(
╔══════════════════════════════════════════╗
║   ████████╗███████╗██████╗  █████╗ ██╗  ║
║      ██╔══╝██╔════╝██╔══██╗██╔══██╗██║  ║
║      ██║   █████╗  ██████╔╝███████║██║  ║
║      ██║   ██╔══╝  ██╔══██╗██╔══██║██║  ║
║      ██║   ███████╗██║  ██║██║  ██║██║  ║
║      ╚═╝   ╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ║
║   Terminal AI  ·  Native C++ · ARM64     ║
╚══════════════════════════════════════════╝
)";

void Display::banner(const std::string&) {
    std::cout << c(std::string(Colors::CYAN) + Colors::BOLD, BANNER_TEXT) << "\n";
}

void Display::status(const std::string& msg) {
    std::cout << c(Colors::DIM, "  " + msg) << "\n";
}

void Display::separator() {
    std::cout << c(Colors::DIM, std::string(48, '-')) << "\n";
}

void Display::assistant(const std::string& text) {
    std::string label = c(std::string(Colors::BCYAN) + Colors::BOLD, "terai");
    std::string sep   = c(Colors::DIM, " ❯ ");
    std::cout << "\n" << label << sep << text << "\n";
}

void Display::tool_call(const std::string& name, const std::string& args_json) {
    std::string label = c(Colors::YELLOW, "  ⚙ " + name);
    std::string args  = c(Colors::DIM, args_json.substr(0, std::min((int)args_json.size(), 80)));
    std::cout << "\n" << label << " " << args << "\n";
}

void Display::thinking() {
    std::string frame = SPINNER[_spinner_idx++ % SPINNER_N];
    std::string msg   = c(Colors::DIM, "\r  " + frame + " thinking...");
    std::cout << msg << std::flush;
}

void Display::clear_line() {
    std::cout << "\r" << std::string(30, ' ') << "\r" << std::flush;
}

void Display::error(const std::string& msg) {
    std::string label = c(std::string(Colors::RED) + Colors::BOLD, "ERROR");
    std::cout << "\n  [" << label << "] " << msg << "\n";
}

void Display::success(const std::string& msg) {
    std::cout << c(Colors::BGREEN, "  ✓ " + msg) << "\n";
}

void Display::dim(const std::string& msg) {
    std::cout << c(Colors::DIM, msg) << "\n";
}

void Display::stream_token(const std::string& token) {
    std::cout << token << std::flush;
}

std::string Display::prompt_prefix() {
    return "\n" + c(std::string(Colors::BGREEN) + Colors::BOLD, "you")
               + c(Colors::DIM, " ❯ ");
}

} // namespace terai
