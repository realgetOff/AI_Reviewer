# AI Reviewer (air)

A high-performance CLI tool for automated code reviews. It strips comments from your source code to optimize token usage and privacy, then generates detailed Markdown reports using LLMs.

## Installation

Install `ai_reviewer` and the `air` shortcut with a single command :

```bash
curl -sSL https://raw.githubusercontent.com/realgetOff/AI_Reviewer/main/install.sh | bash
```

*Note: Restart your terminal or run `source ~/.zshrc` after installation.*


## Configuration

Set your API key and preferences directly in your terminal :

```bash
air config
```

### Commands

| Command | Description |
| :--- | :--- |
| `air` | Analyze current directory |
| `air <file>` | Analyze a specific file or path |
| `air config` | Edit global configuration (`~/.ai_config.json`) |
| `air -clean` | Run the cleanup script (remove reports dir.) |
| `air -update` | Update to the latest version from GitHub |
| `air -delete` | Fully uninstall the program and its config |

### Options

| Flag | Description |
| :--- | :--- |
| `-s <style>` | Set review style (e.g., `minimal`, `security`, `audit`, `crash`, `fix`) |
| `-l <lang>` | Set output language (`en`, `fr`) |
| `-f <fmt>` | Set default format (`md`, `pdf`) |
| `-t <sec>` | Set custom request timeout (default: 30s) |
| `-i <n>` | Set max agent iterations (default: 5) |
| `-I <sec>` | Set interactive program timeout (default: 10s) |
| `-c <prompt>` | Use custom prompt instead of config style |
| `-m` | List available AI models |
| `-d` | Enable debug mode (verbose logs) |
| `-g` | Analyze all files as one (global context mode) |
| `-a` | Enable agent mode (AI executes commands autonomously) |
| `--version` | Show current version |
| `-h` | Display help menu |

## Output

Output is saved in the `reports/` directory:

```
reports/*.md
reports/*.pdf
reports/error.md
reports/debug.log
```

## AI Providers

Use this guide to configure your `config.json`. Copy the **API URL** and the **Model Type** corresponding to your preferred AI service.

| Provider | Model Type | API URL | Key Format |
| :--- | :--- | :--- | :--- |
| **Google Gemini** | `gemini` | `https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent` | URL Param |
| **OpenAI (GPT-4o)** | `openai` | `https://api.openai.com/v1/chat/completions` | Bearer Token |
| **Anthropic (Claude 3.5)** | `claude` | `https://api.anthropic.com/v1/messages` | x-api-key |
| **Mistral AI** | `mistral` | `https://api.mistral.ai/v1/chat/completions` | Bearer Token |
| **Zhipu AI** | `glm` | `https://open.bigmodel.cn/api/paas/v4/chat/completions` | Bearer Token |

*Note: For Gemini **only**, you must leave `model_name: ""` (empty) because the model is already specified within the API URL (ofc you can change `gemini-2.5-flash` by any of available models (check `air -m`))*

### Configuration Example (Gemini)

If you want to use Gemini-2.5-Flash, your `config.json` should look like this:

```json
{
  "api_key": "copy-your-gemini-api-key-here",
  "api_url": "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent",
  "model_type": "gemini",
  "model_name": "",
  "default_style": "minimal",
  "language": "en",
  "styles":
  {
    "minimal":
    {
      "en": "Identify critical logic bugs.",
      "fr": "Identifie les bugs logiques critiques."
    }
  }
}
```

(use the chinese model, alternative to claude and almost free;)

