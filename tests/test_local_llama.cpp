// Test harness for LocalLlamaProvider — verifies:
//   1. Construction/destruction (model load, context create, RAII cleanup)
//   2. complete() and stream() both produce output without crashing
//   3. Same-system-prompt calls trigger prefix-cache REUSE (not full reset)
//   4. Different-system-prompt call triggers a full reset (correct fallback)
//   5. No double-free / backend refcount issues across two provider instances

#include "providers/local_llama.h"
#include "third_party/json.hpp"
#include <iostream>
#include <cassert>

using namespace terai;

int main() {
    json cfg = {
        {"model_path", "/fake/model.gguf"},
        {"n_gpu_layers", 20},
        {"n_ctx", 2048},
        {"n_threads", 4}
    };

    std::cout << "=== Test 1: construction ===\n";
    LocalLlamaProvider provider(cfg);
    assert(provider.is_loaded());
    std::cout << "OK — model loaded\n\n";

    std::string daemon_system =
        "You are a code improvement assistant. Return only improved code.";

    std::cout << "=== Test 2: first call (no cache yet — expect full decode) ===\n";
    auto r1 = provider.complete(
        {{"user", "improve this: void foo() {}", ""}},
        daemon_system, 50, 0.3);
    std::cout << "Response: \"" << r1.content << "\"\n";
    std::cout << "input_tokens=" << r1.input_tokens
              << " output_tokens=" << r1.output_tokens << "\n\n";
    assert(!r1.content.empty());

    std::cout << "=== Test 3: second call, SAME system prompt (expect prefix reuse) ===\n";
    std::cout << "(watch for 'kv_cache_seq_rm' trimming back to n_past_after_system,\n"
                 " NOT clearing everything — that's the reuse path firing)\n";
    auto r2 = provider.complete(
        {{"user", "improve this: int bar(int x) { return x; }", ""}},
        daemon_system, 50, 0.3);
    std::cout << "Response: \"" << r2.content << "\"\n";
    std::cout << "input_tokens=" << r2.input_tokens
              << " (should be much smaller than call 1 — system prompt not re-decoded)\n\n";
    assert(r2.input_tokens < r1.input_tokens);

    std::cout << "=== Test 4: third call, DIFFERENT system prompt (expect full reset) ===\n";
    std::string other_system = "You are a totally different assistant with a much "
        "longer system prompt so token-count comparisons are meaningful "
        "regardless of which specific words happen to be used.";
    auto r3 = provider.complete(
        {{"user", "hello", ""}}, other_system, 50, 0.3);
    std::cout << "Response: \"" << r3.content << "\"\n";
    std::cout << "input_tokens=" << r3.input_tokens << "\n\n";

    std::cout << "=== Test 4b: follow-up call reusing r3's NEW system prompt ===\n";
    std::cout << "(this is the real proof the cache correctly re-keyed to the new\n"
                 " prompt — should be much smaller than r3, same way r2 < r1)\n";
    auto r3b = provider.complete(
        {{"user", "goodbye", ""}}, other_system, 50, 0.3);
    std::cout << "input_tokens=" << r3b.input_tokens
              << " (should be small — just 'goodbye', system prompt reused)\n\n";
    assert(r3b.input_tokens < r3.input_tokens);

    std::cout << "=== Test 5: streaming mode ===\n";
    std::string streamed;
    auto r4 = provider.stream(
        {{"user", "improve this: void baz() {}", ""}},
        daemon_system, 50, 0.3,
        [&](const std::string& tok) { streamed += tok; std::cout << "[token]" << tok; });
    std::cout << "\nStreamed matches final content: "
              << (streamed == r4.content ? "YES" : "NO") << "\n\n";
    assert(streamed == r4.content);

    std::cout << "=== Test 6: second provider instance (backend refcount safety) ===\n";
    {
        LocalLlamaProvider provider2(cfg);
        assert(provider2.is_loaded());
        auto r5 = provider2.complete({{"user","test","" }}, "sys", 20, 0.5);
        std::cout << "Second instance works independently: \"" << r5.content << "\"\n";
    }
    std::cout << "Second instance destroyed cleanly\n\n";

    std::cout << "=== ALL TESTS PASSED ===\n";
    return 0;
}
