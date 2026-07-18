# Integrating LocalLlamaProvider into TerAI

This adds a fourth way to run local models: direct in-process linkage
against `libllama.so`, alongside your existing Anthropic/OpenAI/Gemini/
Ollama providers. For the daemon specifically, this is a structural fix,
not just a speed optimization — the model stays loaded in-process for the
provider's entire lifetime, so there's no `keep_alive` to manage and no
cold-load between cycles at all.

## New files (drop into your existing tree)

```
include/providers/local_llama.h
include/third_party/llama_api.h
src/providers/local_llama.cpp
```

`tests/llama_stub_for_testing.cpp` and `tests/test_local_llama.cpp` are
optional — they let you re-verify the control-flow logic (prefix-cache
reuse, RAII, refcounting) against a fake backend before wiring in the real
`libllama.so`, the same way I validated it here. Not part of the production
build; compile/run manually if useful:

```bash
g++ -std=c++20 -I include -I include/third_party \
  tests/test_local_llama.cpp src/providers/local_llama.cpp \
  tests/llama_stub_for_testing.cpp -o test_local_llama
./test_local_llama
```

## ⚠️ Version-sensitive step — check `llama_api.h` against your `llama.h`

llama.cpp's C API isn't guaranteed stable across versions. I wrote
`llama_api.h` against a recent-generation naming (the API your MobileCAD
notes suggest you're already building against, based on the Vulkan/Turnip
work). Before building, diff the function signatures in
`include/third_party/llama_api.h` against your actual
`external/llama.cpp/include/llama.h` (or wherever your checkout lives).
The three most likely to have drifted:

| `llama_api.h` uses | Older llama.cpp used |
|---|---|
| `llama_model_load_from_file` | `llama_load_model_from_file` |
| `llama_init_from_model` | `llama_new_context_with_model` |
| `llama_model_free` | `llama_free_model` |

If your version differs, **only edit `llama_api.h`** — `local_llama.cpp`
never calls llama.cpp functions directly except through that header, so a
naming mismatch is a one-file fix.

Once confirmed, you can replace the `#include "third_party/llama_api.h"`
line in `local_llama.cpp` with `#include <llama.h>` directly if you'd
rather depend on the real header than my narrowed subset — just make sure
the struct field names (`n_gpu_layers`, `use_mmap`, `n_ctx`, `n_threads`,
etc.) still match, since I only declared the fields I actually use.

## Wiring into `provider_factory.cpp`

Add the include and the branch:

```cpp
#ifdef TERAI_WITH_LLAMA_CPP
#include "providers/local_llama.h"
#endif

// ...inside ProviderFactory::create():
#ifdef TERAI_WITH_LLAMA_CPP
    if (name == "local_llama")
        return std::make_unique<LocalLlamaProvider>(cfg);
#endif
```

Gating behind `TERAI_WITH_LLAMA_CPP` means the rest of TerAI still builds
fine on a machine without llama.cpp available — see CMake section below.

## Wiring into `config.cpp` defaults

Add a `local_llama` block under `providers`:

```cpp
{"local_llama", {
    {"model_path",      ""},         // path to a .gguf file — required
    {"n_gpu_layers",      0},        // match your proven Vulkan/Turnip setup
    {"n_ctx",          4096},
    {"n_threads",         0},        // 0 = let llama.cpp decide
    {"n_threads_batch",   0}
}},
```

To actually use it for the daemon, set `daemon.provider` to `"local_llama"`
instead of `"ollama"` in `~/.terai/config.json`. Note: `daemon.request_
timeout_seconds`, `keep_alive`, and `num_threads` (the Ollama-specific
knobs we spent the last several rounds tuning) don't apply to this
provider — there's no HTTP request to time out, and `n_threads`/
`n_gpu_layers` are the equivalent local knobs instead.

## Wiring into `CMakeLists.txt`

```cmake
option(TERAI_ENABLE_LLAMA_CPP "Build with direct libllama.so linkage" OFF)

if(TERAI_ENABLE_LLAMA_CPP)
    set(LLAMA_CPP_DIR "" CACHE PATH "Path to your llama.cpp build (containing libllama.so and headers)")
    if(NOT LLAMA_CPP_DIR)
        message(FATAL_ERROR "Set -DLLAMA_CPP_DIR=/path/to/llama.cpp/build when TERAI_ENABLE_LLAMA_CPP=ON")
    endif()

    find_library(LLAMA_LIB llama PATHS "${LLAMA_CPP_DIR}" "${LLAMA_CPP_DIR}/lib" REQUIRED)
    find_library(GGML_LIB  ggml  PATHS "${LLAMA_CPP_DIR}" "${LLAMA_CPP_DIR}/lib")

    target_sources(terai PRIVATE src/providers/local_llama.cpp)
    target_compile_definitions(terai PRIVATE TERAI_WITH_LLAMA_CPP)
    target_include_directories(terai PRIVATE "${LLAMA_CPP_DIR}/include")
    target_link_libraries(terai PRIVATE ${LLAMA_LIB})
    if(GGML_LIB)
        target_link_libraries(terai PRIVATE ${GGML_LIB})
    endif()
endif()
```

Build with:
```bash
cmake .. -DCMAKE_TOOLCHAIN_FILE=cmake/termux-arm64.cmake \
         -DTERAI_ENABLE_LLAMA_CPP=ON \
         -DLLAMA_CPP_DIR=/path/to/your/llama.cpp/build
make -j2
```

Without `-DTERAI_ENABLE_LLAMA_CPP=ON`, `local_llama.cpp` is never compiled
and `local_llama` simply won't appear in `--config`'s available providers —
everything else builds exactly as before.

## What this actually buys you over Ollama for the daemon

- **No cold-load, ever.** The model loads once when the daemon process
  starts and stays in memory for its entire life — not per-request like an
  HTTP server, not subject to any `keep_alive` timeout.
- **KV-cache prefix reuse.** Every daemon call uses the *exact same* system
  prompt ("You are a code improvement assistant..."). This implementation
  detects that and skips re-decoding it on every call — verified above:
  9-16 tokens decoded per call after the first, instead of the full prompt
  every time.
- **Direct GPU layer offload** via `n_gpu_layers`, matching the Vulkan/
  Turnip backend your TerAI notes say is already getting ~37 t/s on
  TinyLlama — set it once here instead of routing through Ollama's own
  (separate) GPU configuration.
- **No JSON/HTTP per token.** `StreamCallback` fires directly per decoded
  token in-process.

## What I could NOT verify from here

I don't have your actual `llama.h`, a real `.gguf` model, or a device with
a GPU to test against — everything above validates the *C++ logic*
(control flow, memory safety, the prefix-cache decision logic) against a
deliberately fake backend. The real integration test is on your device:
build with `TERAI_ENABLE_LLAMA_CPP=ON`, point `model_path` at a real model,
and run `terai --daemon-test` (or `-p local_llama` for interactive use) to
confirm actual inference against real hardware.
