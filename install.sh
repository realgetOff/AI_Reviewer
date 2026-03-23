#!/bin/bash

GREEN="\033[1;92m"
BLUE="\033[1;34m"
YELLOW="\033[1;33m"
RED="\033[1;31m"
RESET="\033[0m"

PANDOC_URL="https://github.com/jgm/pandoc/releases/download/3.1.11/pandoc-3.1.11-linux-amd64.tar.gz"
REPO_URL="https://github.com/realgetOff/AI_Reviewer.git"
TEMP_DIR="/tmp/ai_reviewer_build"
BIN_DIR="$HOME/bin"
CONFIG_FILE="$HOME/.ai_config.json"

echo -e "${BLUE}==> Preparing installation...${RESET}"
rm -rf "$TEMP_DIR"
git clone "$REPO_URL" "$TEMP_DIR" || { echo -e "${RED}Clone failed${RESET}"; exit 1; }
cd "$TEMP_DIR"

VERSION=$(grep "#define CURRENT_VERSION" includes/ai_client.hpp | cut -d'"' -f2)
echo -e "${GREEN}==> air ${VERSION}${RESET}"

echo -e "${BLUE}==> Compiling source code...${RESET}"
make re || { echo -e "${RED}Compilation failed${RESET}"; exit 1; }

mkdir -p "$BIN_DIR"

if [ -f "$BIN_DIR/ai_reviewer" ]; then
    mv "$BIN_DIR/ai_reviewer" "$BIN_DIR/ai_reviewer.old"
fi
cp ai_reviewer "$BIN_DIR/"
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

rm -rf "$TEMP_DIR"

echo -e "\n${GREEN}Installation complete!${RESET}"
echo -e "1. Refresh your terminal: ${BLUE}source $SHELL_RC${RESET}"
echo -e "2. Set your API key: ${BLUE}air config${RESET}"
