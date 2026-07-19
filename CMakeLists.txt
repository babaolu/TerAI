cmake_minimum_required(VERSION 3.16)
project(terai VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build type defaults ──────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ── Compiler flags ───────────────────────────────────────────────────────────
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_Debug   "-g -O0 -fsanitize=address")

add_compile_options(
  -Wall -Wextra -Wpedantic
  -Wno-unused-parameter
)

# ── Find required packages ───────────────────────────────────────────────────
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# readline (for REPL line editing — optional but strongly preferred)
find_library(READLINE_LIB readline)
find_path(READLINE_INCLUDE readline/readline.h)

# ── Include paths ────────────────────────────────────────────────────────────
include_directories(
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/include/third_party
  ${CURL_INCLUDE_DIRS}
)
if(READLINE_INCLUDE)
  include_directories(${READLINE_INCLUDE})
endif()

# ── Source files ─────────────────────────────────────────────────────────────
set(SOURCES
  src/main.cpp
  src/core/config.cpp
  src/core/display.cpp
  src/core/optimizer.cpp
  src/core/memory.cpp
  src/core/agent.cpp
  src/core/setup.cpp
  src/providers/http_client.cpp
  src/providers/provider_factory.cpp
  src/providers/anthropic.cpp
  src/providers/openai_compat.cpp
  src/providers/gemini.cpp
  src/providers/ollama.cpp
  src/tools/tool_registry.cpp
  src/tools/web_search.cpp
  src/tools/filesystem.cpp
  src/daemon/background.cpp
)

# ── Executable ───────────────────────────────────────────────────────────────
add_executable(terai ${SOURCES})

target_link_libraries(terai
  PRIVATE
    CURL::libcurl
    Threads::Threads
)

if(READLINE_LIB)
  target_link_libraries(terai PRIVATE ${READLINE_LIB})
  target_compile_definitions(terai PRIVATE HAVE_READLINE)
endif()

# ── Optional: direct libllama.so linkage ─────────────────────────────────────
# Must come AFTER add_executable(terai ...) above — target_sources() and
# target_link_libraries() operate on an existing target, and "terai" isn't
# defined until add_executable() runs.
option(TERAI_ENABLE_LLAMA_CPP "Build with direct libllama.so linkage" OFF)

if(TERAI_ENABLE_LLAMA_CPP)
  set(LLAMA_CPP_DIR "" CACHE PATH "Path to your llama.cpp checkout or build dir")
  if(NOT LLAMA_CPP_DIR)
    message(FATAL_ERROR "Set -DLLAMA_CPP_DIR=/path/to/llama.cpp when TERAI_ENABLE_LLAMA_CPP=ON")
  endif()

  # CMake does not expand a leading ~ itself — if it slipped through
  # un-expanded from the shell, fix it here rather than silently searching
  # a literal "~/..." path that will never exist.
  if(LLAMA_CPP_DIR MATCHES "^~")
    string(REPLACE "~" "$ENV{HOME}" LLAMA_CPP_DIR "${LLAMA_CPP_DIR}")
  endif()

  message(STATUS "llama.cpp dir (resolved): ${LLAMA_CPP_DIR}")

  # llama.cpp's own CMake build places libllama.so in different
  # subdirectories depending on platform/generator/version — search all the
  # common ones instead of assuming one. bin/ is where it lands on most
  # recent Linux/Android CMake+ninja builds.
  find_library(LLAMA_LIB llama
    PATHS
      "${LLAMA_CPP_DIR}"
      "${LLAMA_CPP_DIR}/lib"
      "${LLAMA_CPP_DIR}/bin"
      "${LLAMA_CPP_DIR}/build"
      "${LLAMA_CPP_DIR}/build/lib"
      "${LLAMA_CPP_DIR}/build/bin"
      "${LLAMA_CPP_DIR}/src"
      "${LLAMA_CPP_DIR}/build/src"
    NO_CACHE
  )
  find_library(GGML_LIB ggml
    PATHS
      "${LLAMA_CPP_DIR}"
      "${LLAMA_CPP_DIR}/lib"
      "${LLAMA_CPP_DIR}/bin"
      "${LLAMA_CPP_DIR}/build"
      "${LLAMA_CPP_DIR}/build/lib"
      "${LLAMA_CPP_DIR}/build/bin"
      "${LLAMA_CPP_DIR}/ggml/src"
      "${LLAMA_CPP_DIR}/build/ggml/src"
    NO_CACHE
  )

  if(NOT LLAMA_LIB)
    message(FATAL_ERROR
      "Could not find libllama.so under ${LLAMA_CPP_DIR}. Run this to "
      "find where your build actually put it, then pass THAT directory "
      "(not necessarily the build root) via -DLLAMA_CPP_DIR=...:\n"
      "  find ${LLAMA_CPP_DIR} -name 'libllama*' 2>/dev/null")
  endif()
  message(STATUS "Found libllama: ${LLAMA_LIB}")
  if(GGML_LIB)
    message(STATUS "Found libggml:  ${GGML_LIB}")
  else()
    message(STATUS "libggml not found separately — likely statically linked into libllama, continuing without it")
  endif()

  # Headers: try both "<dir>/include" (build dir layout) and the dir
  # itself (in case LLAMA_CPP_DIR was already pointed at an include root).
  find_path(LLAMA_INCLUDE_DIR llama.h
    PATHS
      "${LLAMA_CPP_DIR}/include"
      "${LLAMA_CPP_DIR}"
  )
  if(NOT LLAMA_INCLUDE_DIR)
    message(WARNING
      "Could not find llama.h under ${LLAMA_CPP_DIR} — falling back to "
      "TerAI's bundled include/third_party/llama_api.h subset. If you "
      "want the real header instead, pass the correct include path or "
      "edit local_llama.cpp's #include directly.")
  else()
    message(STATUS "Found llama.h: ${LLAMA_INCLUDE_DIR}")
    target_include_directories(terai PRIVATE "${LLAMA_INCLUDE_DIR}")
  endif()

  target_sources(terai PRIVATE src/providers/local_llama.cpp)
  target_compile_definitions(terai PRIVATE TERAI_WITH_LLAMA_CPP)
  target_link_libraries(terai PRIVATE ${LLAMA_LIB})
  if(GGML_LIB)
    target_link_libraries(terai PRIVATE ${GGML_LIB})
  endif()
endif()

# ── Install ──────────────────────────────────────────────────────────────────
install(TARGETS terai DESTINATION bin)

# ── Termux cross-compile helper ──────────────────────────────────────────────
# cmake -DCMAKE_TOOLCHAIN_FILE=cmake/termux-arm64.cmake ..
if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/termux-arm64.cmake")
  message(STATUS "Termux ARM64 toolchain available")
endif()

message(STATUS "Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ std    : ${CMAKE_CXX_STANDARD}")
message(STATUS "libcurl    : ${CURL_LIBRARIES}")
message(STATUS "readline   : ${READLINE_LIB}")
