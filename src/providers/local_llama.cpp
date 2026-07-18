// src/providers/local_llama.cpp
#include "providers/local_llama.h"
#include "third_party/llama_api.h"
#include <stdexcept>
#include <atomic>
#include <cstring>
#include <vector>

namespace terai {

// llama_backend_init/free are process-global — refcount them so multiple
// LocalLlamaProvider instances in the same process don't double-init or
// free the backend out from under each other.
static std::atomic<int> g_backend_refcount{0};

struct LocalLlamaProvider::Impl {
    llama_model*        model = nullptr;
    llama_context*      ctx   = nullptr;
    const llama_vocab*  vocab = nullptr;

    std::string model_path;
    int         n_gpu_layers   = 0;
    int         n_ctx          = 4096;
    int         n_threads      = 0;   // 0 = let llama.cpp decide
    int         n_threads_batch= 0;

    // ── KV-cache prefix reuse ────────────────────────────────────────────────
    // If consecutive calls share the exact same system prompt (true for
    // every daemon call — same "You are a code improvement assistant..."
    // message every time), we keep the system prompt's decoded state in
    // the KV cache and only decode the new user turn, instead of
    // re-processing the whole system prompt from scratch every request.
    std::string last_system_prompt;
    llama_pos   n_past_after_system = 0;
    bool        has_cached_prefix   = false;

    ~Impl() {
        if (ctx)   llama_free(ctx);
        if (model) llama_model_free(model);
        if (g_backend_refcount.fetch_sub(1) == 1) llama_backend_free();
    }
};

LocalLlamaProvider::LocalLlamaProvider(const json& cfg) {
    _name  = "local_llama";
    _model = cfg.value("default_model", "");  // display name only, informational

    _impl = std::make_unique<Impl>();
    _impl->model_path       = cfg.value("model_path", "");
    _impl->n_gpu_layers     = cfg.value("n_gpu_layers", 0);
    _impl->n_ctx            = cfg.value("n_ctx", 4096);
    _impl->n_threads        = cfg.value("n_threads", 0);
    _impl->n_threads_batch  = cfg.value("n_threads_batch", 0);

    if (_impl->model_path.empty())
        throw std::runtime_error(
            "local_llama provider requires providers.local_llama.model_path "
            "in ~/.terai/config.json (path to a .gguf model file)");

    if (g_backend_refcount.fetch_add(1) == 0) llama_backend_init();

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = _impl->n_gpu_layers;
    mparams.use_mmap     = true;
    mparams.use_mlock    = false;

    _impl->model = llama_model_load_from_file(_impl->model_path.c_str(), mparams);
    if (!_impl->model) {
        if (g_backend_refcount.fetch_sub(1) == 1) llama_backend_free();
        throw std::runtime_error(
            "Failed to load model: " + _impl->model_path +
            " — check the path and that it's a valid .gguf file");
    }

    _impl->vocab = llama_model_get_vocab(_impl->model);

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx           = _impl->n_ctx;
    cparams.n_batch         = 512;
    cparams.n_threads       = _impl->n_threads;
    cparams.n_threads_batch = _impl->n_threads_batch > 0
                              ? _impl->n_threads_batch : _impl->n_threads;

    _impl->ctx = llama_init_from_model(_impl->model, cparams);
    if (!_impl->ctx) {
        llama_model_free(_impl->model);
        if (g_backend_refcount.fetch_sub(1) == 1) llama_backend_free();
        throw std::runtime_error("Failed to create llama context for " + _impl->model_path);
    }
}

LocalLlamaProvider::~LocalLlamaProvider() = default;

bool LocalLlamaProvider::is_loaded() const {
    return _impl && _impl->ctx != nullptr;
}

// ── Tokenization helper ───────────────────────────────────────────────────────
static std::vector<llama_token> tokenize(const llama_vocab* vocab,
                                         const std::string& text,
                                         bool add_special) {
    std::vector<llama_token> tokens(text.size() + 16);
    int32_t n = llama_tokenize(vocab, text.c_str(), (int32_t)text.size(),
                               tokens.data(), (int32_t)tokens.size(),
                               add_special, /*parse_special=*/true);
    if (n < 0) {
        // Buffer was too small — n is the negated required size.
        tokens.resize(-n);
        n = llama_tokenize(vocab, text.c_str(), (int32_t)text.size(),
                          tokens.data(), (int32_t)tokens.size(),
                          add_special, true);
    }
    tokens.resize(n);
    return tokens;
}

static std::string token_to_piece(const llama_vocab* vocab, llama_token tok) {
    char buf[256];
    int32_t n = llama_token_to_piece(vocab, tok, buf, sizeof(buf), 0, true);
    if (n < 0) return "";
    return std::string(buf, n);
}

// Decode a contiguous run of tokens starting at `pos_start`, on sequence 0.
// Only the LAST token gets logits=true (we only need logits to sample the
// next token after the whole run, not after every intermediate token).
static void decode_tokens(llama_context* ctx, const std::vector<llama_token>& toks,
                          llama_pos pos_start) {
    if (toks.empty()) return;

    llama_batch batch = llama_batch_init((int32_t)toks.size(), 0, 1);
    for (size_t i = 0; i < toks.size(); ++i) {
        batch.token[i]      = toks[i];
        batch.pos[i]        = pos_start + (llama_pos)i;
        batch.n_seq_id[i]   = 1;
        batch.seq_id[i][0]  = 0;
        batch.logits[i]     = (i == toks.size() - 1) ? 1 : 0;
    }
    batch.n_tokens = (int32_t)toks.size();

    int32_t rc = llama_decode(ctx, batch);
    llama_batch_free(batch);

    if (rc != 0)
        throw std::runtime_error("llama_decode failed (rc=" + std::to_string(rc) + ")");
}

// ── Core inference ─────────────────────────────────────────────────────────────
LLMResponse LocalLlamaProvider::run_inference(const std::vector<Message>& messages,
                                              const std::string& system,
                                              int max_tokens, double temperature,
                                              StreamCallback cb) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (!is_loaded())
        throw std::runtime_error("local_llama model not loaded");

    // Build the flat prompt text. A real deployment should use the model's
    // proper chat template (llama.cpp exposes llama_chat_apply_template for
    // this) — kept as plain concatenation here to match TerAI's other
    // providers' simple message handling; swap in the chat-template call
    // if your model expects a specific format (most instruct models do).
    std::string user_turn;
    for (auto& m : messages)
        user_turn += "[" + m.role + "]: " + m.content + "\n";

    llama_pos pos = 0;
    std::vector<llama_token> new_tokens;

    bool reuse_prefix = _impl->has_cached_prefix &&
                        _impl->last_system_prompt == system;

    if (reuse_prefix) {
        // Trim the KV cache back to exactly the end of the cached system
        // prompt, discarding anything decoded for a PREVIOUS request's
        // user turn — then only the new user turn needs decoding. This is
        // the optimization an HTTP-based server can't do transparently:
        // each Ollama request re-sends and re-processes the full system
        // prompt every single time.
        llama_kv_cache_seq_rm(_impl->ctx, 0, _impl->n_past_after_system, -1);
        pos = _impl->n_past_after_system;
        new_tokens = tokenize(_impl->vocab, user_turn, /*add_special=*/false);
    } else {
        // Different (or first) system prompt — clear everything and
        // decode system + user turn fresh.
        llama_kv_cache_seq_rm(_impl->ctx, 0, 0, -1);
        auto sys_tokens = tokenize(_impl->vocab, system, /*add_special=*/true);
        decode_tokens(_impl->ctx, sys_tokens, 0);

        _impl->n_past_after_system = (llama_pos)sys_tokens.size();
        _impl->last_system_prompt  = system;
        _impl->has_cached_prefix   = true;

        pos = _impl->n_past_after_system;
        new_tokens = tokenize(_impl->vocab, user_turn, /*add_special=*/false);
    }

    int input_tokens = (int)new_tokens.size() +
                        (reuse_prefix ? 0 : _impl->n_past_after_system);

    decode_tokens(_impl->ctx, new_tokens, pos);
    pos += (llama_pos)new_tokens.size();

    // ── Sampler chain: temperature + distribution sampling ─────────────────
    llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    llama_sampler* sampler = llama_sampler_chain_init(sparams);
    llama_sampler_chain_add(sampler, llama_sampler_init_temp((float)temperature));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(0));  // 0 = random seed

    std::string output;
    int generated = 0;

    for (; generated < max_tokens; ++generated) {
        llama_token next = llama_sampler_sample(sampler, _impl->ctx, -1);
        llama_sampler_accept(sampler, next);

        if (llama_vocab_is_eog(_impl->vocab, next)) break;

        std::string piece = token_to_piece(_impl->vocab, next);
        output += piece;
        if (cb) cb(piece);

        std::vector<llama_token> one = {next};
        decode_tokens(_impl->ctx, one, pos);
        pos += 1;
    }

    llama_sampler_free(sampler);

    LLMResponse resp;
    resp.content       = output;
    resp.model         = _model.empty() ? _impl->model_path : _model;
    resp.input_tokens  = input_tokens;
    resp.output_tokens = generated;
    resp.stop_reason   = (generated >= max_tokens) ? "length" : "stop";
    return resp;
}

LLMResponse LocalLlamaProvider::complete(const std::vector<Message>& messages,
                                         const std::string& system,
                                         int max_tokens, double temperature) {
    return run_inference(messages, system, max_tokens, temperature, nullptr);
}

LLMResponse LocalLlamaProvider::stream(const std::vector<Message>& messages,
                                       const std::string& system,
                                       int max_tokens, double temperature,
                                       StreamCallback cb) {
    return run_inference(messages, system, max_tokens, temperature, cb);
}

} // namespace terai
