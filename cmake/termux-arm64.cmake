# cmake/termux-arm64.cmake
# Termux native ARM64 build — run INSIDE Termux on-device
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/termux-arm64.cmake ..
#
# Key insight: this is NOT a cross-compile — we ARE the ARM64 target.
# Do NOT set CMAKE_SYSROOT; that makes CMake treat it like a cross-compile
# and prevents clang from finding its own C++ standard library headers
# (which live at $PREFIX/include/c++/v1/ in Termux clang 21+).

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Termux prefix — all packages install here
if(NOT DEFINED TERMUX_PREFIX)
  set(TERMUX_PREFIX /data/data/com.termux/files/usr)
endif()

# Use Termux's clang explicitly (in case $PATH isn't set in cmake context)
set(CMAKE_C_COMPILER   ${TERMUX_PREFIX}/bin/clang)
set(CMAKE_CXX_COMPILER ${TERMUX_PREFIX}/bin/clang++)

# Tell CMake where to find Termux-installed libraries and headers.
# Using CMAKE_FIND_ROOT_PATH (not CMAKE_SYSROOT) avoids the cross-compile
# header-search breakage while still finding libcurl, readline, etc.
list(APPEND CMAKE_PREFIX_PATH      ${TERMUX_PREFIX})
list(APPEND CMAKE_LIBRARY_PATH     ${TERMUX_PREFIX}/lib)
list(APPEND CMAKE_INCLUDE_PATH     ${TERMUX_PREFIX}/include)

# Bake the Termux lib dir into the binary's rpath so it finds .so files
# at runtime without needing LD_LIBRARY_PATH
set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,${TERMUX_PREFIX}/lib")

# Install into Termux prefix so `make install` puts `terai` in $PATH
set(CMAKE_INSTALL_PREFIX ${TERMUX_PREFIX})
