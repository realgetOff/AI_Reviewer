#!/bin/bash
GREEN="\033[1;92m"
BLUE="\033[1;34m"
YELLOW="\033[1;33m"
RED="\033[1;31m"
RESET="\033[0m"
PANDOC_URL="https://github.com/jgm/pandoc/releases/download/3.1.11/pandoc-3.1.11-linux-amd64.tar.gz"
REPO_URL="https://github.com/realgetOff/AI_Reviewer.git"
CACHE_DIR="$HOME/.cache/ai_reviewer"
BIN_DIR="$HOME/bin"
CONFIG_FILE="$HOME/.ai_config.json"

echo -e "${BLUE}==> Preparing installation...${RESET}"

if ! command -v curl &> /dev/null; then
    echo -e "${RED}Error: curl is required but not installed.${RESET}"
    echo -e "${YELLOW}==> Please install curl: apt install curl / brew install curl${RESET}"
    exit 1
fi
if ! command -v git &> /dev/null; then
    echo -e "${RED}Error: git is required but not installed.${RESET}"
    exit 1
fi
if ! command -v meson &> /dev/null; then
    echo -e "${BLUE}==> Installing meson via pip...${RESET}"
    pip install meson --user || { echo -e "${RED}Failed to install meson${RESET}"; exit 1; }
fi
if ! command -v ninja &> /dev/null; then
    echo -e "${BLUE}==> Installing ninja via pip...${RESET}"
    pip install ninja --user || { echo -e "${RED}Failed to install ninja${RESET}"; exit 1; }
fi

if [ -d "$CACHE_DIR/.git" ]; then
    echo -e "${BLUE}==> Updating existing cache...${RESET}"
    pushd "$CACHE_DIR" > /dev/null
    git fetch origin || { echo -e "${RED}Fetch failed${RESET}"; exit 1; }
    git reset --hard origin/main || { echo -e "${RED}Reset failed${RESET}"; exit 1; }
else
    echo -e "${BLUE}==> Cloning repository...${RESET}"
    mkdir -p "$CACHE_DIR"
    git clone "$REPO_URL" "$CACHE_DIR" || { echo -e "${RED}Clone failed${RESET}"; exit 1; }
    pushd "$CACHE_DIR" > /dev/null
fi

VERSION=$(grep "#define CURRENT_VERSION" includes/ai_client.hpp | cut -d'"' -f2)
echo -e "${GREEN}==> ai_reviewer ${VERSION}${RESET}"

echo -e "${BLUE}==> Compiling with meson...${RESET}"

if [ ! -d "build" ]; then
    meson setup build --prefix="$HOME/.local" --bindir="$BIN_DIR" &> /dev/null
else
    meson setup --reconfigure build --prefix="$HOME/.local" --bindir="$BIN_DIR" &> /dev/null
fi

if ! meson compile -C build &> /dev/null; then
    echo -e "${YELLOW}==> Compilation failed, retrying with clean build...${RESET}"
    rm -rf build
    meson setup build --prefix="$HOME/.local" --bindir="$BIN_DIR" &> /dev/null || { echo -e "${RED}Meson setup failed${RESET}"; exit 1; }
    meson compile -C build &> /dev/null || { echo -e "${RED}Compilation failed again. Check your dependencies (libcurl, libnghttp2).${RESET}"; exit 1; }
fi

mkdir -p "$BIN_DIR"
if [ -f "$BIN_DIR/ai_reviewer" ]; then
    mv "$BIN_DIR/ai_reviewer" "$BIN_DIR/ai_reviewer.old"
fi

ninja install -C build &> /dev/null || {
    cp build/ai_reviewer "$BIN_DIR/" || { echo -e "${RED}Install failed${RESET}"; exit 1; }
}
chmod +x "$BIN_DIR/ai_reviewer"
rm -f "$BIN_DIR/ai_reviewer.old"

if [ ! -f "$CONFIG_FILE" ]; then
    if [ -f "config.json" ]; then
        cp config.json "$CONFIG_FILE"
        echo -e "${GREEN}==> Default config created at $CONFIG_FILE${RESET}"
    else
        echo -e "${YELLOW}==> Warning: config.json not found in repo.${RESET}"
    fi
else
    echo -e "${YELLOW}==> Global config already exists. Keeping your current settings.${RESET}"
fi

popd > /dev/null

if [[ "$SHELL" == */zsh ]]; then
    SHELL_RC="$HOME/.zshrc"
elif [[ "$SHELL" == */bash ]]; then
    SHELL_RC="$HOME/.bashrc"
else
    SHELL_RC="$HOME/.profile"
fi

echo -e "${BLUE}==> Updating $SHELL_RC...${RESET}"
if ! grep -q "$BIN_DIR" "$SHELL_RC"; then
    echo "export PATH=\"$BIN_DIR:\$PATH\"" >> "$SHELL_RC"
    echo -e "${GREEN}==> Added $BIN_DIR to PATH.${RESET}"
fi
if ! grep -q "alias air=" "$SHELL_RC"; then
    echo "alias air='ai_reviewer'" >> "$SHELL_RC"
    echo -e "${GREEN}==> Alias 'air' added successfully!${RESET}"
fi

if ! command -v pandoc &> /dev/null; then
    echo -e "${BLUE}==> Installing Pandoc (portable version)...${RESET}"
    curl -L "$PANDOC_URL" -o /tmp/pandoc.tar.gz
    tar -xzf /tmp/pandoc.tar.gz --strip-components=2 -C "$BIN_DIR" pandoc-3.1.11/bin/pandoc
    chmod +x "$BIN_DIR/pandoc"
    rm /tmp/pandoc.tar.gz
    echo -e "${GREEN}==> Pandoc installed successfully!${RESET}"
fi
if ! command -v wkhtmltopdf &> /dev/null; then
    echo -e "${BLUE}==> Tip: For better PDF quality, install wkhtmltopdf if possible.${RESET}"
fi

echo -e "\n${GREEN}Installation complete!${RESET}"
echo -e "1. Refresh your terminal: ${BLUE}source $SHELL_RC${RESET}"
echo -e "2. Set your API key: ${BLUE}air config${RESET}"
