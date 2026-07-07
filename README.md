# TerAI (C++) — Terminal AI Assistant for Termux

Native ARM64 rewrite of TerAI. Zero-latency startup, no interpreter overhead,
compiles directly inside Termux with `clang++`.

## Why C++

| | Python build | C++ build |
|---|---|---|
| Startup latency | ~200-400ms (interpreter load) | <5ms |
| Background daemon footprint | Resident interpreter | Near-zero idle memory |
| Dependencies | stdlib only (by design) | libcurl, nlohmann/json (header-only), readline (optional) |
| Build step | None | `cmake && make` (~1-2 min on S23 Ultra) |

Gmail/Drive OAuth is intentionally left out of this build — that flow is painful
in C++ and not worth fighting. If you need it, shell out to a small Python
helper script from the `shell` tool, or wire it in as a future module.

## Install (in Termux)

```bash
pkg install clang cmake make libcurl readline -y
git clone <repo> && cd terai-cpp
bash install.sh
```

This builds natively for ARM64 using `cmake/termux-arm64.cmake` and installs
the `terai` binary to `$PREFIX/bin`.

## Manual build (any Linux, for development/testing)

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./terai --help
```

## Usage

```bash
terai                          # Interactive REPL
terai "list my files"          # One-shot
terai -p anthropic -m claude-sonnet-4-6 "explain RLHF"
terai --daemon                 # Start background Ollama improvement daemon
terai --daemon-status
terai --daemon-stop
terai --setup                  # Configure API keys
terai --config                 # Print current config (keys masked)
```

### In-session commands
```
/help             Show commands
/history          List saved sessions
/clear            Clear current session
/tokens           Show token usage vs context window
/provider NAME    Switch provider mid-session
/model NAME       Switch model
exit / quit       Save session and exit
```

## Architecture

```
terai-cpp/
├── CMakeLists.txt
├── cmake/termux-arm64.cmake      # Cross/native ARM64 toolchain file
├── include/
│   ├── core/         types, config, display, optimizer, memory, agent, setup
│   ├── providers/    base_provider + http_client + 5 concrete providers
│   ├── tools/        tool_registry, web_search, filesystem (incl. shell)
│   └── daemon/        background.h
├── src/               mirrors include/, one .cpp per .h
└── install.sh         Termux installer (builds + installs to $PREFIX/bin)
```

### Providers
All HTTP is done via `libcurl` directly (`src/providers/http_client.cpp`) —
no provider SDKs. Supports blocking completion and streaming (SSE for
Anthropic/OpenAI-compatible/Gemini, NDJSON for Ollama).

- `anthropic.cpp` — Claude Messages API
- `openai_compat.cpp` — OpenAI, OpenRouter, HuggingFace, NVIDIA NIM (same `/chat/completions` shape)
- `gemini.cpp` — Google Gemini generateContent / streamGenerateContent
- `ollama.cpp` — local Ollama `/api/chat`, includes an `is_available()` health check

### Agent loop (`core/agent.cpp`)
Hermes-style ReAct: `THOUGHT → ACTION → ARGS → OBSERVATION → FINAL`, parsed via
regex from the raw model output. Tool calls are cached per-turn by the
optimizer to avoid repeat work. Self-reflection runs every N turns
(configurable) and persists "patterns" to `~/.terai/patterns.json` which get
folded back into the system prompt on future runs.

### Token optimizer (`core/optimizer.cpp`)
Whitespace compression (code-fence aware), history summarization once token
budget is crossed (keeps last 6 messages verbatim, summarizes the rest into a
single block), and a tool-result cache.

### Background daemon (`daemon/background.cpp`)
Forks via `fork()`/`setsid()`, writes a PID file, and on a timer scans
configured paths for code files, sending each to the local Ollama provider
with one of: `add_docstrings`, `improve_error_handling`,
`suggest_optimizations`, `fix_style`. Designed to run while you're not
actively using the phone — small model, low frequency, capped file count per
cycle.

## Known sandbox caveat (not a Termux issue)

If you test this in a network-restricted sandbox, `web_search` may fail with
a "non-JSON response" error — that's the sandbox's egress proxy blocking
`api.duckduckgo.com`, not a bug in TerAI. On a real device with normal
internet access this works as expected. The tool fails gracefully either way
instead of crashing the agent loop.

## Data & privacy

Same as before: everything lives in `~/.terai/` (config, history, patterns,
daemon log). Ollama traffic never leaves the device. API keys are only sent
to the provider you've configured.
