#pragma once
// include/third_party/llama_api.h
//
// This declares the subset of llama.cpp's public C API that
// LocalLlamaProvider depends on. It is NOT a full copy of llama.h — it's
// deliberately narrow so the real dependency surface is easy to audit.
//
// On-device, this should be replaced by #include <llama.h> once your
// llama.cpp checkout's headers are confirmed to match (or this file
// edited to match your version — check function names below against
// your actual llama.h; llama.cpp's C API has renamed several of these
// across versions, most notably:
//   llama_load_model_from_file      -> llama_model_load_from_file (newer)
//   llama_new_context_with_model    -> llama_init_from_model      (newer)
//   llama_free_model                -> llama_model_free           (newer)
//   llama_get_vocab (implicit)      -> llama_model_get_vocab      (newer)
//   llama_kv_cache_seq_rm           -> may be under a "memory" API
//                                       in very recent versions
//
// If your version differs, this file is the ONLY place that needs edits —
// local_llama.cpp never calls llama.cpp functions directly except through
// the declarations here.

#include <cstdint>
#include <cstddef>

extern "C" {

// ── Opaque types ──────────────────────────────────────────────────────────────
struct llama_model;
struct llama_context;
struct llama_sampler;
struct llama_vocab;

typedef int32_t llama_token;
typedef int32_t llama_pos;
typedef int32_t llama_seq_id;

// ── Model loading ─────────────────────────────────────────────────────────────
struct llama_model_params {
    int32_t n_gpu_layers;   // number of layers to offload to GPU (Vulkan/Turnip etc.)
    bool    use_mmap;
    bool    use_mlock;
    // (real struct has more fields; llama_model_default_params() fills
    // sane defaults for all of them — we only override what we need)
};

struct llama_context_params {
    uint32_t n_ctx;          // context window size
    uint32_t n_batch;        // logical batch size for prompt processing
    int32_t  n_threads;      // threads for generation
    int32_t  n_threads_batch;// threads for batch/prompt processing
};

llama_model_params   llama_model_default_params();
llama_context_params llama_context_default_params();

llama_model*   llama_model_load_from_file(const char* path, llama_model_params params);
void           llama_model_free(llama_model* model);

llama_context* llama_init_from_model(llama_model* model, llama_context_params params);
void           llama_free(llama_context* ctx);

const llama_vocab* llama_model_get_vocab(const llama_model* model);

// ── Tokenization ───────────────────────────────────────────────────────────────
int32_t llama_tokenize(const llama_vocab* vocab,
                       const char* text, int32_t text_len,
                       llama_token* tokens, int32_t n_tokens_max,
                       bool add_special, bool parse_special);

int32_t llama_token_to_piece(const llama_vocab* vocab, llama_token token,
                             char* buf, int32_t length,
                             int32_t lstrip, bool special);

bool llama_vocab_is_eog(const llama_vocab* vocab, llama_token token);

// ── Batch / decode ─────────────────────────────────────────────────────────────
struct llama_batch {
    int32_t      n_tokens;
    llama_token* token;
    float*       embd;
    llama_pos*   pos;
    int32_t*     n_seq_id;
    llama_seq_id** seq_id;
    int8_t*      logits;
};

llama_batch llama_batch_init(int32_t n_tokens, int32_t embd, int32_t n_seq_max);
void        llama_batch_free(llama_batch batch);

int32_t llama_decode(llama_context* ctx, llama_batch batch);

uint32_t llama_n_ctx(const llama_context* ctx);

// KV-cache trimming — used for the prefix-reuse optimization. Removes
// cached positions [p0, p1) for the given sequence, so we can keep the
// system-prompt prefix decoded and only redo the per-request suffix.
void llama_kv_cache_seq_rm(llama_context* ctx, llama_seq_id seq_id,
                          llama_pos p0, llama_pos p1);

// ── Sampling ──────────────────────────────────────────────────────────────────
struct llama_sampler_chain_params {
    bool no_perf;
};

llama_sampler_chain_params llama_sampler_chain_default_params();
llama_sampler* llama_sampler_chain_init(llama_sampler_chain_params params);
void           llama_sampler_chain_add(llama_sampler* chain, llama_sampler* smpl);

llama_sampler* llama_sampler_init_temp(float t);
llama_sampler* llama_sampler_init_dist(uint32_t seed);

llama_token llama_sampler_sample(llama_sampler* chain, llama_context* ctx, int32_t idx);
void        llama_sampler_accept(llama_sampler* chain, llama_token token);
void        llama_sampler_free(llama_sampler* chain);

// ── Backend lifecycle ──────────────────────────────────────────────────────────
void llama_backend_init();
void llama_backend_free();

} // extern "C"
