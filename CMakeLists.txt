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
