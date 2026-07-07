#!/data/data/com.termux/files/usr/bin/bash
# TerAI C++ Termux Installer
# Builds natively inside Termux — no proot, no emulation
# Tested on Samsung S23 Ultra (ARM64 / aarch64)
#
# Usage: bash install.sh

set -e

GREEN='\033[0;32m'; CYAN='\033[0;36m'
YELLOW='\033[0;33m'; RED='\033[0;31m'; NC='\033[0m'

echo -e "${CYAN}"
cat << 'BANNER'
╔══════════════════════════════════════════╗
║      TerAI C++ — Termux Installer        ║
║   Native ARM64 · Zero emulation layer    ║
╚══════════════════════════════════════════╝
BANNER
echo -e "${NC}"

TERAI_SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="$HOME/terai-cpp"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"

# ── Step 1: Termux packages ──────────────────────────────────────────────────
echo -e "${YELLOW}[1/4] Installing Termux build dependencies...${NC}"

# Pin a known-good mirror BEFORE updating. Termux's default repo config runs
# an interactive "pick a mirror" prompt on pkg update, which hangs forever in
# a non-interactive script and gets SIGKILLed by Android when the session
# backgrounds/locks. Writing sources.list directly skips that prompt entirely.
TERMUX_SOURCES="$PREFIX/etc/apt/sources.list"
if ! grep -q "packages-cf.termux.dev" "$TERMUX_SOURCES" 2>/dev/null; then
  echo -e "${YELLOW}  Pinning a stable mirror (skips interactive mirror picker)...${NC}"
  echo "deb https://packages-cf.termux.dev/apt/termux-main stable main" > "$TERMUX_SOURCES"
fi

# Keep the session alive during install — Android can kill backgrounded/locked
# Termux sessions mid-build otherwise.
command -v termux-wake-lock >/dev/null 2>&1 && termux-wake-lock

pkg update -y 2>/dev/null || true
pkg install -y clang cmake make libcurl readline 2>/dev/null || {
  echo -e "${RED}  pkg install failed — try: pkg update && pkg upgrade${NC}"
  exit 1
}
echo -e "${GREEN}  ✓ clang, cmake, libcurl, readline installed${NC}"

# ── Step 2: Copy source ──────────────────────────────────────────────────────
echo -e "${YELLOW}[2/4] Copying source to $INSTALL_DIR...${NC}"
mkdir -p "$INSTALL_DIR"
cp -r "$TERAI_SRC"/src           "$INSTALL_DIR/"
cp -r "$TERAI_SRC"/include       "$INSTALL_DIR/"
cp -r "$TERAI_SRC"/cmake         "$INSTALL_DIR/"
cp    "$TERAI_SRC"/CMakeLists.txt "$INSTALL_DIR/"
echo -e "${GREEN}  ✓ Source copied${NC}"

# ── Step 3: Build ─────────────────────────────────────────────────────────────
echo -e "${YELLOW}[3/4] Building TerAI (this may take 1-2 minutes on ARM64)...${NC}"
BUILD_DIR="$INSTALL_DIR/build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Wipe stale CMakeCache from any previous failed run — cached bad compiler
# detection causes phantom errors even after the root cause is fixed.
rm -f CMakeCache.txt
rm -rf CMakeFiles

cmake .. \
  -DCMAKE_TOOLCHAIN_FILE="$INSTALL_DIR/cmake/termux-arm64.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  2>&1 | grep -E "(error|warning|Found|C\+\+|Build)" || true

# Use -j2 instead of -j$(nproc) to cap memory pressure during parallel compile
make -j2 2>&1
if [ ! -f terai ]; then
  echo -e "${RED}  Build failed — check output above${NC}"
  exit 1
fi
echo -e "${GREEN}  ✓ Build complete${NC}"

# ── Step 4: Install ───────────────────────────────────────────────────────────
echo -e "${YELLOW}[4/4] Installing...${NC}"
cp terai "$PREFIX/bin/terai"
chmod +x "$PREFIX/bin/terai"
echo -e "${GREEN}  ✓ 'terai' installed to $PREFIX/bin/${NC}"

echo ""
echo -e "${CYAN}Installation complete!${NC}"
echo ""
echo -e "Binary size: $(du -sh $PREFIX/bin/terai | cut -f1)"
echo ""
echo -e "Quick start:"
echo -e "  ${GREEN}terai${NC}                        # Interactive REPL"
echo -e "  ${GREEN}terai --setup${NC}                # Configure API keys"
echo -e "  ${GREEN}terai 'what is my IP?'${NC}       # One-shot mode"
echo -e "  ${GREEN}terai -p anthropic${NC}           # Use Claude"
echo -e "  ${GREEN}terai --daemon${NC}               # Start background Ollama daemon"
echo -e "  ${GREEN}terai -h${NC}                     # Full help"
echo ""
echo -e "${YELLOW}Tip — Install Ollama for local LLM:${NC}"
echo -e "  curl -fsSL https://ollama.com/install.sh | sh"
echo -e "  ollama pull qwen2.5:1.5b   # 1GB — fast on S23 Ultra"
echo -e "  ollama pull llama3.2       # 2GB — better quality"
echo ""
read -p "Run setup wizard now? [Y/n] " ans
if [[ "$ans" != "n" && "$ans" != "N" ]]; then
  terai --setup
fi
