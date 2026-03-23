#!/bin/bash

GREEN="\033[1;92m"
BLUE="\033[1;34m"
YELLOW="\033[1;33m"
RESET="\033[0m"

REPO_URL="https://github.com/realgetOff/AI_Reviewer.git"
TEMP_DIR="/tmp/ai_reviewer_build"
BIN_DIR="$HOME/bin"

echo -e "${BLUE}==> Preparing installation...${RESET}"
rm -rf "$TEMP_DIR"
git clone "$REPO_URL" "$TEMP_DIR" && cd "$TEMP_DIR"

echo -e "${BLUE}==> Compiling source code...${RESET}"
make re

mkdir -p "$BIN_DIR"
cp ai_reviewer "$BIN_DIR/"
chmod +x "$BIN_DIR/ai_reviewer"

if [ ! -f "$HOME/.ai_config.json" ]; then
    cp config.json "$HOME/.ai_config.json"
    echo -e "${GREEN}==> Default config created at ~/.ai_config.json${RESET}"
else
    echo -e "${YELLOW}==> Global config already exists. Skipping...${RESET}"
fi

if [[ "$SHELL" == */zsh ]]; then
    SHELL_RC="$HOME/.zshrc"
elif [[ "$SHELL" == */bash ]]; then
    SHELL_RC="$HOME/.bashrc"
else
    SHELL_RC="$HOME/.profile"
fi

echo -e "${BLUE}==> Updating $SHELL_RC...${RESET}"

if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
    echo "export PATH=\"$BIN_DIR:\$PATH\"" >> "$SHELL_RC"
    echo -e "${GREEN}==> Added $BIN_DIR to PATH.${RESET}"
fi

if ! grep -q "alias air=" "$SHELL_RC"; then
    echo "alias air='ai_reviewer'" >> "$SHELL_RC"
    echo -e "${GREEN}==> Alias 'air' added successfully!${RESET}"
fi

rm -rf "$TEMP_DIR"

echo -e "\n${GREEN}Installation complete!${RESET}"
echo -e "1. Refresh your terminal or run: ${BLUE}source $SHELL_RC${RESET}"
echo -e "2. Set your API key with: ${BLUE}air config${RESET}"
echo -e "3. Run an analysis with: ${BLUE}air .${RESET}"
