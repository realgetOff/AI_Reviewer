/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   agent.cpp                                          :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mforest- <marvin@d42.fr>                   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/03/26 22:08:06 by mforest-          #+#    #+#             */
/*   Updated: 2026/03/26 22:08:06 by mforest-         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "ai_client.hpp"
#include <atomic>
#include <cctype>
#include <csignal>
#include <cstring>
#include <mutex>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <thread>

static struct termios g_saved_termios;
static bool           g_termios_saved = false;
static std::mutex     g_instr_mutex;
static std::string    g_pending_instruction;
static std::atomic<bool> g_esc_reader_running{false};
static std::atomic<bool> g_esc_reader_paused{false};
static std::atomic<bool> g_redirect_ui_requested{false};
static struct sigaction g_old_sigint;

static int term_cols(void)
{
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
        return 80;
    if (ws.ws_col <= 0)
        return 80;
    return ws.ws_col;
}

static void tty_restore(void)
{
    if (g_termios_saved && isatty(STDIN_FILENO))
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
}

static void agent_sigint_handler(int)
{
    g_esc_reader_running = false;
    tty_restore();
    _exit(130);
}

static void tty_enable_raw(void)
{
    if (!isatty(STDIN_FILENO))
        return;
    if (!g_termios_saved)
    {
        tcgetattr(STDIN_FILENO, &g_saved_termios);
        g_termios_saved = true;
    }
    struct termios raw = g_saved_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

static void esc_reader_thread_fn(void)
{
    while (g_esc_reader_running)
    {
        if (g_esc_reader_paused)
        {
            usleep(10000);
            continue;
        }
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 100000;
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0)
            continue;
        if (g_esc_reader_paused)
            continue;
        unsigned char c;
        if (read(STDIN_FILENO, &c, 1) != 1)
            continue;
        if (c != 27)
            continue;

        fd_set fd2;
        FD_ZERO(&fd2);
        FD_SET(STDIN_FILENO, &fd2);
        struct timeval tv2;
        tv2.tv_sec  = 0;
        tv2.tv_usec = 50000;
        if (select(STDIN_FILENO + 1, &fd2, NULL, NULL, &tv2) <= 0)
        {
            g_redirect_ui_requested = true;
            continue;
        }

        unsigned char c2;
        if (read(STDIN_FILENO, &c2, 1) != 1)
        {
            g_redirect_ui_requested = true;
            continue;
        }

        if (c2 == '[')
        {
            while (read(STDIN_FILENO, &c, 1) == 1)
            {
                if (c >= 0x40 && c <= 0x7E)
                    break;
            }
            continue;
        }
    }
}

static const char *U_HOR = "\xe2\x94\x80";
static const char *U_VER = "\xe2\x94\x82";
static const char *U_TL = "\xe2\x95\xad";
static const char *U_TR = "\xe2\x95\xae";
static const char *U_BL = "\xe2\x95\xb0";
static const char *U_BR = "\xe2\x95\xaf";

static std::string pad_line(const std::string &content, int inner_w)
{
    std::string s = content;
    if (static_cast<int>(s.size()) > inner_w)
        s = s.substr(0, static_cast<size_t>(inner_w - 3)) + "...";
    while (static_cast<int>(s.size()) < inner_w)
        s += " ";
    return s;
}

static std::string footer_row_inside_box(const std::string &cwd, int inner)
{
    std::string r = "ESC * Ctrl+C";
    std::string d = cwd;
    if ((int)(d.size() + r.size() + 2) > inner)
    {
        int keep = inner - static_cast<int>(r.size()) - 4;
        if (keep < 4)
            keep = 4;
        d = "..." + d.substr(d.size() - static_cast<size_t>(keep));
    }
    std::string row = d;
    while ((int)(row.size() + r.size()) < inner)
        row += " ";
    row += r;
    if ((int)row.size() > inner)
        row = pad_line(d + " " + r, inner);
    return row;
}

static bool agent_prompt_is_blank(const std::string &s)
{
    for (unsigned char c : s)
    {
        if (!std::isspace(c))
            return false;
    }
    return true;
}

static std::string read_prompt_rounded_box(bool fr, const std::string &cwd, const s_config &conf)
{
    tty_restore();

    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
    {
        ws.ws_col = 80;
        ws.ws_row = 24;
    }
    int cols = ws.ws_col;
    int rows = ws.ws_row;
    if (cols < 40)
        cols = 80;
    if (rows < 10)
        rows = 24;

    std::string model_tag = conf.model_name;
    if (model_tag.empty())
        model_tag = conf.model_type;

    std::cout << "  " << ORANGE << "air" << RESET << " " << DIM << CURRENT_VERSION << RESET
              << " * " << TEAL << model_tag << RESET
              << " * " << "[API] agent" << "\n";
    if (fr)
        std::cout << DIM << "  1 agent * 0 MCP * ESC pour rediriger" << RESET << "\n\n";
    else
        std::cout << DIM << "  1 agent * 0 MCP * ESC to redirect" << RESET << "\n\n";

    int mid = rows / 2;
    if (mid < 5)
        mid = 5;
    for (int r = 3; r < mid - 4; r++)
        std::cout << "\n";

    int box_w = cols - 4;
    if (box_w < 44)
        box_w = 44;
    int top_inner = box_w - 2;
    std::string title = " prompt ";
    int fill = top_inner - static_cast<int>(title.size());
    if (fill < 0)
        fill = 0;
    int left = fill / 2;
    int right = fill - left;

    std::cout << "  " << U_TL;
    for (int i = 0; i < left; i++)
        std::cout << U_HOR;
    std::cout << title;
    for (int i = 0; i < right; i++)
        std::cout << U_HOR;
    std::cout << U_TR << "\n";

    int inner = box_w - 4;
    std::string hint;
    if (fr)
        hint = " Objectif / consigne (Entree = valider)";
    else
        hint = " Task / instructions (Enter = submit)";
    std::cout << "  " << U_VER << " " << DIM << pad_line(hint, inner) << RESET << " " << U_VER << "\n";

    std::cout << "  " << U_VER << " " << pad_line("", inner) << " " << U_VER << "\n";

    std::cout << "  " << U_VER << " " << ORANGE << ">" << RESET << " ";
    std::cout.flush();

    std::string user_prompt;
    std::getline(std::cin, user_prompt);

    // std::cout << "  " << U_VER << " " << pad_line(user_prompt, inner) << " " << U_VER << "\n";
    std::cout << "  " << U_VER << " " << DIM << footer_row_inside_box(cwd, inner) << RESET << " " << U_VER << "\n";

    std::cout << "  " << U_BL;
    for (int i = 0; i < box_w - 2; i++)
        std::cout << U_HOR;
    std::cout << U_BR << "\n\n";
    return user_prompt;
}

static std::string read_redirect_rounded_box(bool fr, const std::string &cwd, const s_config &conf,
                                             bool after_done)
{
    (void)conf;
    int cols = term_cols();
    int box_w = cols - 8;
    if (box_w < 52)
        box_w = 52;
    if (box_w > cols - 4)
        box_w = cols - 4;
    int inner = box_w - 4;
    int top_inner = box_w - 2;
    std::string title = fr ? " redirection " : " redirect ";
    int fill = top_inner - static_cast<int>(title.size());
    if (fill < 0)
        fill = 0;
    int left = fill / 2;
    int right = fill - left;

    std::cout << "\n";
    std::cout << "  " << U_TL;
    for (int i = 0; i < left; i++)
        std::cout << U_HOR;
    std::cout << ORANGE << title << RESET;
    for (int i = 0; i < right; i++)
        std::cout << U_HOR;
    std::cout << U_TR << "\n";

    std::string h;
    if (after_done)
    {
        if (fr)
            h = " Suite (meme flux que ESC, historique conserve). Entree = envoyer. Vide = quitter.";
        else
            h = " Follow-up (same as ESC, history kept). Enter to send. Empty line = exit.";
    }
    else if (fr)
        h = " Nouvelle consigne (historique conserve). Entree = envoyer.";
    else
        h = " New instruction (history kept). Enter to send.";
    std::cout << "  " << U_VER << " " << DIM << pad_line(h, inner) << RESET << " " << U_VER << "\n";
    std::cout << "  " << U_VER << " " << ORANGE << ">" << RESET << " ";
    std::cout.flush();
    std::string line;
    std::getline(std::cin, line);
    // std::cout << "  " << U_VER << " " << pad_line(line, inner) << " " << U_VER << "\n";
    std::cout << "  " << U_BL;
    for (int i = 0; i < box_w - 2; i++)
        std::cout << U_HOR;
    std::cout << U_BR << "\n";
    return line;
}

static void print_iteration_banner(int iter, int max_iter, bool fr)
{
    (void)fr;
    int cols = term_cols();
    std::string cap_r;
    if (max_iter > 0)
        cap_r = std::to_string(max_iter);
    else
        cap_r = "inf";

    std::string mid = " iter " + std::to_string(iter + 1) + " / " + cap_r + " ";
    int box_w = 48;
    if (box_w > cols - 6)
        box_w = cols - 6;
    if (box_w < static_cast<int>(mid.size()) + 6)
        box_w = static_cast<int>(mid.size()) + 6;

    int top_inner = box_w - 2;
    int fill = top_inner - static_cast<int>(mid.size());
    if (fill < 0)
        fill = 0;
    int left = fill / 2;
    int right = fill - left;

    std::cout << "\n";
    std::cout << "  " << DIM << U_TL;
    for (int i = 0; i < left; i++)
        std::cout << U_HOR;
    std::cout << TEAL << mid << RESET << DIM;
    for (int i = 0; i < right; i++)
        std::cout << U_HOR;
    std::cout << U_TR << RESET << "\n";

    std::cout << "  " << DIM << U_BL;
    for (int i = 0; i < top_inner; i++)
        std::cout << U_HOR;
    std::cout << U_BR << RESET << "\n" << std::flush;
}

static const std::vector<std::string> BYPASS =
{
    "valgrind", "gcc", "g++", "clang", "clang++",
    "make", "ls", "cat", "grep", "rg", "echo", "wc", "file",
    "nm", "objdump", "strace", "python3", "python",
    "head", "tail", "find", "pwd", "hexdump",
    "readelf", "ldd", "strings", "size", "ar", "diff",
    "chmod", "mkdir", "touch", "cp", "mv"
};

static const std::vector<std::string> BLACKLIST =
{
    "rm -rf /", "ssh", "scp", "dd", "mkfs",
    "shutdown", "reboot", "> /dev/sd", ":(){ :|:& };:"
};

static void agent_log(const std::string &msg, bool debug)
{
    if (debug)
        std::cout << CYAN << "[AGENT] " << RESET << msg << std::endl;
    write_debug("[AGENT] " + msg, debug);
}

static bool is_blacklisted(const std::string &cmd)
{
    for (const auto &b : BLACKLIST)
    {
        if (cmd.find(b) != std::string::npos)
            return true;
    }
    return false;
}

static bool is_bypassed(const std::string &cmd)
{
    std::string first_word = cmd.substr(0, cmd.find(' '));
    size_t slash = first_word.rfind('/');
    if (slash != std::string::npos)
        first_word = first_word.substr(slash + 1);
    if (first_word.size() >= 2 && first_word.substr(0, 2) == "./")
        first_word = first_word.substr(2);
    for (const auto &b : BYPASS)
    {
        if (first_word == b)
            return true;
    }
    return false;
}

static bool read_shell_confirm_raw(void)
{
    struct termios saved;
    if (tcgetattr(STDIN_FILENO, &saved) == -1)
        return false;
    struct termios raw = saved;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1)
        return false;

    for (;;)
    {
        unsigned char c;
        if (read(STDIN_FILENO, &c, 1) != 1)
            continue;

        if (c == '\n' || c == '\r')
        {
            tcsetattr(STDIN_FILENO, TCSANOW, &saved);
            return true;
        }
        if (c == 127 || c == 8)
        {
            tcsetattr(STDIN_FILENO, TCSANOW, &saved);
            return false;
        }
        if (c == 'y' || c == 'Y' || c == 'o' || c == 'O')
        {
            tcsetattr(STDIN_FILENO, TCSANOW, &saved);
            return true;
        }
        if (c == 'n' || c == 'N')
        {
            tcsetattr(STDIN_FILENO, TCSANOW, &saved);
            return false;
        }
        if (c != 27)
            continue;

        fd_set fd2;
        FD_ZERO(&fd2);
        FD_SET(STDIN_FILENO, &fd2);
        struct timeval tv2;
        tv2.tv_sec  = 0;
        tv2.tv_usec = 60000;
        if (select(STDIN_FILENO + 1, &fd2, NULL, NULL, &tv2) <= 0)
        {
            tcsetattr(STDIN_FILENO, TCSANOW, &saved);
            return false;
        }

        unsigned char c2;
        if (read(STDIN_FILENO, &c2, 1) != 1)
        {
            tcsetattr(STDIN_FILENO, TCSANOW, &saved);
            return false;
        }
        if (c2 == '[')
        {
            std::string seq;
            unsigned char ch;
            while (read(STDIN_FILENO, &ch, 1) == 1)
            {
                seq += static_cast<char>(ch);
                if (ch >= 0x40 && ch <= 0x7E)
                    break;
            }
            if (seq.find('3') != std::string::npos && seq.find('~') != std::string::npos)
            {
                tcsetattr(STDIN_FILENO, TCSANOW, &saved);
                return false;
            }
            continue;
        }
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
        return false;
    }
}

void check_child_status(int status, std::string &output)
{
    if (WIFSIGNALED(status))
    {
        int sig = WTERMSIG(status);
        output += "\n[CRASH] Process terminated by signal: " + std::to_string(sig) + "\n";
        if (sig == 11)
            output += "Reason: Segmentation fault\n";
        else if (sig == 6)
            output += "Reason: Aborted\n";
    }
    else if (WIFEXITED(status))
    {
        int exit_code = WEXITSTATUS(status);
        if (exit_code != 0)
            output += "\n[EXIT] Process exited with code: " + std::to_string(exit_code) + "\n";
    }
}

static bool ask_permission(const std::string &cmd, bool fr, const std::string &cwd)
{
    g_esc_reader_paused = true;
    tty_restore();

    int cols = term_cols();
    int box_w = cols - 2;
    if (box_w < 44)
        box_w = 44;
    int inner = box_w - 4;

    int top_inner = box_w - 2;
    std::string title;
    if (fr)
        title = " commande shell ";
    else
        title = " shell command ";
    int fill = top_inner - static_cast<int>(title.size());
    if (fill < 0)
        fill = 0;
    int left = fill / 2;
    int right = fill - left;

    std::cout << "\n" << YELLOW << U_TL;
    for (int i = 0; i < left; i++)
        std::cout << U_HOR;
    std::cout << title;
    for (int i = 0; i < right; i++)
        std::cout << U_HOR;
    std::cout << U_TR << RESET << "\n";

    std::string cmd_line = "`" + cmd + "`";
    if (static_cast<int>(cmd_line.size()) > inner)
        cmd_line = cmd_line.substr(0, static_cast<size_t>(inner - 3)) + "...";
    while (static_cast<int>(cmd_line.size()) < inner)
        cmd_line += " ";
    std::cout << YELLOW << U_VER << RESET << " " << cmd_line << " " << YELLOW << U_VER << RESET << "\n";

    std::string l2;
    if (fr)
        l2 = " Entree = oui  *  Retour arriere / Suppr = non  *  ESC = non";
    else
        l2 = " Enter = yes  *  Backspace / Delete = no  *  ESC = no";
    std::cout << YELLOW << U_VER << RESET << " " << pad_line(l2, inner) << " " << YELLOW << U_VER << RESET << "\n";

    std::cout << YELLOW << U_BL;
    for (int i = 0; i < box_w - 2; i++)
        std::cout << U_HOR;
    std::cout << U_BR << RESET << "\n";

    bool ok = read_shell_confirm_raw();

    tty_enable_raw();
    g_esc_reader_paused = false;

    return ok;
}

static std::string bash_double_escape(const std::string &s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char uc : s)
    {
        char c = static_cast<char>(uc);
        if (c == '\\' || c == '"' || c == '$' || c == '`')
            o += '\\';
        o += c;
    }
    return o;
}

static std::string exec_command(const std::string &cmd, const s_config &conf)
{
    std::string output;
    char        buf[512];
    FILE        *pipe;
    int         ret;

    agent_log("Running shell: " + cmd, conf.debug);
    std::string inner    = cmd + " </dev/null";
    std::string full_cmd = "bash -c \"" + bash_double_escape(inner) + " 2>&1\"";

    pipe = popen(full_cmd.c_str(), "r");
    if (!pipe)
        return ("(popen failed)");

    while (fgets(buf, sizeof(buf), pipe))
        output += buf;

    ret = pclose(pipe);

    if (WIFSIGNALED(ret))
    {
        int sig = WTERMSIG(ret);
        output += "\n[CRASH] Process terminated by signal " + std::to_string(sig);
        if (sig == SIGSEGV)
            output += " (Segmentation fault)";
        output += "\n";
    }

    if (output.empty())
        output = "(no output)";

    std::string preview;
    if (output.size() > 200)
        preview = output.substr(0, 200);
    else
        preview = output;
    agent_log("Output: " + preview, conf.debug);

    return (output);
}

static std::string sanitize_output(const std::string &str)
{
    std::string clean;
    clean.reserve(str.length());
    for (unsigned char c : str)
    {
        if ((c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t')
            clean += c;
        else
            clean += '?';
    }
    return clean;
}

static std::string strip_markdown_fence(const std::string &raw)
{
    std::string s = raw;

    const char *tags[] = { "```json", "```", nullptr };
    for (int t = 0; tags[t]; ++t)
    {
        size_t pos = s.find(tags[t]);
        if (pos == std::string::npos)
            continue;
        size_t brace = s.find('{');
        if (brace != std::string::npos && pos >= brace)
            continue;
        s.erase(pos, strlen(tags[t]));
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\r'))
            s.erase(pos, 1);
        break;
    }

    size_t last   = s.rfind("```");
    size_t rbrace = s.rfind('}');
    if (last  != std::string::npos &&
        rbrace != std::string::npos &&
        last > rbrace)
        s.erase(last);

    return s;
}

// NOTE: temp fix by claude

static std::string fix_json_strings(const std::string &raw)
{
    std::string out;
    out.reserve(raw.size() + 64);
    bool in_string = false;

    for (size_t i = 0; i < raw.size(); ++i)
    {
        unsigned char c = (unsigned char)raw[i];

        if (!in_string)
        {
            if (c == '"')
                in_string = true;
            out += (char)c;
            continue;
        }

        if (c == '\\')
        {
            unsigned char next = 0;
            if (i + 1 < raw.size())
                next = (unsigned char)raw[i + 1];

            if (next == '"'  || next == '\\' || next == '/' ||
                next == 'b'  || next == 'f'  || next == 'n' ||
                next == 'r'  || next == 't'  || next == 'u')
            {
                out += (char)c;
                out += (char)next;
                ++i;
                continue;
            }

            out += "\\\\";
            continue;
        }

        if (c == '"')
        {
            in_string = false;
            out += '"';
            continue;
        }

        if (c == '\n') { out += "\\n";  continue; }
        if (c == '\r') { out += "\\r";  continue; }
        if (c == '\t') { out += "\\t";  continue; }
        if (c < 0x20)
        {
            char esc[8];
            snprintf(esc, sizeof(esc), "\\u%04x", (unsigned int)c);
            out += esc;
            continue;
        }

        out += (char)c;
    }
    return out;
}

static std::string extract_field(const std::string &src, const std::string &key)
{
    std::string needle = "\"" + key + "\"";
    size_t pos = src.find(needle);
    if (pos == std::string::npos)
        return "";
    pos += needle.size();

    while (pos < src.size() &&
           (src[pos] == ' ' || src[pos] == ':' || src[pos] == '\t'))
        ++pos;
    if (pos >= src.size())
        return "";

    if (src[pos] == '"')
    {
        ++pos;
        std::string val;
        while (pos < src.size())
        {
            char c = src[pos];

            if (c == '\\' && pos + 1 < src.size())
            {
                val += c;
                val += src[pos + 1];
                pos += 2;
                continue;
            }

            if (c == '"')
            {
                size_t peek = pos + 1;
                while (peek < src.size() &&
                       (src[peek] == ' ' || src[peek] == '\t'))
                    ++peek;
                if (peek >= src.size()  ||
                    src[peek] == ','    ||
                    src[peek] == '}'    ||
                    src[peek] == '\n'   ||
                    src[peek] == '\r')
                    break;
                val += "\\\"";
                ++pos;
                continue;
            }

            val += c;
            ++pos;
        }
        return val;
    }

    if (src[pos] == 't') return "true";
    if (src[pos] == 'f') return "false";
    return "";
}

static std::string exec_interactive(const std::string &cmd,
                                    const std::vector<std::string> &inputs,
                                    bool debug)
{
    int in_pipe[2];
    int out_pipe[2];
    pid_t pid;
    std::string output;
    char buf[512];

    agent_log("Running interactive: " + cmd, debug);
    for (const auto &inp : inputs)
        agent_log("  input: " + inp, debug);

    signal(SIGPIPE, SIG_IGN);

    if (pipe(in_pipe) < 0 || pipe(out_pipe) < 0)
        return ("(pipe failed)");

    pid = fork();
    if (pid < 0)
        return ("(fork failed)");

    if (pid == 0)
    {
        close(in_pipe[1]);
        close(out_pipe[0]);
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(in_pipe[0]);
        close(out_pipe[1]);

        std::vector<std::string> parts;
        std::istringstream iss(cmd);
        std::string part;
        while (iss >> part)
            parts.push_back(part);

        std::vector<char *> argv_vec;
        for (auto &p : parts)
            argv_vec.push_back(&p[0]);
        argv_vec.push_back(nullptr);

        execvp(argv_vec[0], argv_vec.data());
        _exit(1);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);

    fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);

    auto read_available = [&]() {
        ssize_t n;
        while ((n = read(out_pipe[0], buf, sizeof(buf) - 1)) > 0)
        {
            buf[n] = '\0';
            output += buf;
        }
    };

    for (const auto &inp : inputs)
    {
        int st;
        if (waitpid(pid, &st, WNOHANG) != 0)
            break;
        usleep(200000);
        read_available();
        std::string line = inp + "\n";
        if (write(in_pipe[1], line.c_str(), line.size()) == -1)
            break;
        agent_log("Sent: " + inp, debug);
    }

    close(in_pipe[1]);

    int status  = 0;
    bool exited = false;

    while (!exited)
    {
        usleep(100000);
        read_available();
        pid_t res = waitpid(pid, &status, WNOHANG);
        if (res == pid)
        {
            exited = true;
            break;
        }
    }

    if (exited)
    {
        if (WIFSIGNALED(status))
        {
            int sig = WTERMSIG(status);
            output += "\n[CRASH] Process terminated by signal " + std::to_string(sig);
            if (sig == SIGSEGV)
                output += " (Segmentation fault)";
            else if (sig == SIGABRT)
                output += " (Abort/Assertion failed)";
            output += "\n";
        }
        else if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
        {
            output += "\n[EXIT] Process finished with error code "
                      + std::to_string(WEXITSTATUS(status)) + "\n";
        }
    }

    read_available();
    close(out_pipe[0]);

    if (output.empty())
        output = "(no output)";

    output = sanitize_output(output);

    std::string preview;
    if (output.size() > 200)
        preview = output.substr(0, 200);
    else
        preview = output;
    agent_log("Interactive output: " + preview, debug);
    return output;
}

static std::string get_ls(bool debug)
{
    agent_log("Running initial ls -la", debug);
    FILE *pipe = popen("ls -la 2>&1", "r");
    if (!pipe)
        return ("(ls failed)");
    std::string output;
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe))
        output += buf;
    pclose(pipe);
    return output;
}

void run_agent(const std::vector<std::string> &, s_config conf)
{
    bool fr = (conf.lang == "fr");

    struct SessionEndMarker
    {
        bool fr;
        explicit SessionEndMarker(bool f) : fr(f)
        {
        }
        ~SessionEndMarker()
        {
            if (!isatty(STDOUT_FILENO))
                return;
            if (fr)
                std::cout << "\n" << DIM << "--- fin session air ---" << RESET << "\n\n";
            else
                std::cout << "\n" << DIM << "--- end air agent session ---" << RESET << "\n\n";
            std::cout << std::flush;
        }
    } session_end(fr);

    if (isatty(STDOUT_FILENO))
    {
        std::cout << "\033[2J\033[H";
        if (fr)
            std::cout << DIM << "--- session air ---" << RESET << "\n";
        else
            std::cout << DIM << "--- air agent ---" << RESET << "\n";
    }

    char cwd_buf[1024];
    std::string cwd;
    if (getcwd(cwd_buf, sizeof(cwd_buf)))
        cwd = cwd_buf;
    else
        cwd = ".";

    agent_log("Starting agent in: " + cwd, conf.debug);
    if (conf.max_iter > 0)
        agent_log("Max iterations: " + std::to_string(conf.max_iter), conf.debug);
    else
        agent_log("Max iterations: unlimited (until done)", conf.debug);

    auto make_agent_system_base = [&](const std::string &up, const std::string &ls) -> std::string {
        std::string agent_system_base;
        if (!up.empty())
            agent_system_base = up + "\n\n";
        /* conf.prompt (config.json "styles") is for review mode only — not injected in agent. */
        agent_system_base +=
            "Working directory: " + cwd +
            "\n\nDirectory contents:\n" + ls + "\n\n";

        if (fr)
        {
            agent_system_base +=
                "Tu travailles dans le repertoire indique : tu peux explorer (ls, chemins), lire des "
                "fichiers de facon ciblee (grep, head, etc.), lancer make ou des commandes shell utiles.\n"
                "Pour un gros fichier, evite de tout envoyer dans le contexte : grep ou extraits.\n\n"
                "Ta mission est uniquement ce que l'utilisateur a demande dans la boite (ci-dessus). "
                "Quand c'est termine, mets done=true et arrete — pas d'etapes supplementaires non demandees.\n"
                "Exemple : seulement compiler et le build reussit (exit 0) -> done=true et commands=[].\n"
                "Sans demande explicite : ne force pas une recompilation (ex. touch), ni des commandes "
                "apres que l'objectif soit deja atteint.\n\n"
                "IMPORTANT : binaire interactif (shell, REPL, jeu) -> type 'interactive' avec inputs ; "
                "sinon type 'shell'.\n\n"
                "Reponds UNIQUEMENT en JSON valide :\n";
        }
        else
        {
            agent_system_base +=
                "You work in the given directory: explore (ls, paths), read files selectively "
                "(grep, head, etc.), run make or shell commands as needed.\n"
                "For large files, avoid dumping everything into context — use grep or excerpts.\n\n"
                "Your mission is only what the user asked in the prompt box above. When that is done, "
                "set done=true and stop — no extra steps the user did not request.\n"
                "Example: user only wanted to compile and the build succeeded (exit 0) -> done=true and "
                "commands=[].\n"
                "Unless explicitly asked: do not force rebuilds (e.g. touch) or run more commands after "
                "the goal is already met.\n\n"
                "IMPORTANT: interactive program (shell, REPL, game) -> type 'interactive' with inputs; "
                "otherwise type 'shell'.\n\n"
                "Respond ONLY in valid JSON:\n";
        }

        agent_system_base +=
            "{\n"
            "  \"thoughts\": \"...\",\n"
            "  \"commands\": [\n"
            "    {\"type\": \"shell\", \"cmd\": \"make\"},\n"
            "    {\"type\": \"interactive\", \"cmd\": \"./minishell\","
            " \"inputs\": [\"echo hi\", \"exit\"]}\n"
            "  ],\n"
            "  \"done\": false,\n"
            "  \"report\": \"\"\n"
            "}\n";

        if (fr)
            agent_system_base +=
                "Regle : si done=true, commands doit etre [] (aucune commande a lancer). "
                "Si tu as encore des commandes a lancer, done=false jusqu'a la derniere etape.\n\n"
                "Quand done=true, report peut rester vide ; un bref resume dans thoughts suffit si utile.";
        else
            agent_system_base +=
                "Rule: if done=true, commands must be [] (nothing left to run). "
                "If you still need shell commands, keep done=false until the final step.\n\n"
                "When done=true, report may stay empty; a short summary in thoughts is enough if useful.";
        return agent_system_base;
    };

    /* esc_thread must be declared BEFORE AgentUiCleanup so that on return, ~AgentUiCleanup
       runs first (join + tty_restore). If ~std::thread ran first on a joinable thread,
       std::terminate() is called ("terminate called without an active exception"). */
    std::thread esc_thread;

    struct AgentUiCleanup
    {
        std::thread *th;
        bool         sigint_hooked;
        AgentUiCleanup() : th(nullptr), sigint_hooked(false)
        {
        }
        void set(std::thread &t)
        {
            th = &t;
        }
        void hook_sigint(void)
        {
            struct sigaction sa;
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = agent_sigint_handler;
            sigemptyset(&sa.sa_mask);
            sigaction(SIGINT, &sa, &g_old_sigint);
            sigint_hooked = true;
        }
        ~AgentUiCleanup()
        {
            g_esc_reader_running = false;
            if (th && th->joinable())
                th->join();
            tty_restore();
            if (sigint_hooked)
                sigaction(SIGINT, &g_old_sigint, nullptr);
        }
    } ui_cleanup;

    std::string user_prompt = read_prompt_rounded_box(fr, cwd, conf);

    std::string ls_output = get_ls(conf.debug);
    agent_log("Directory:\n" + ls_output, conf.debug);

    std::string agent_system_base = make_agent_system_base(user_prompt, ls_output);

    std::string history;
    int         iter = 0;

    if (isatty(STDIN_FILENO))
    {
        tty_enable_raw();
        g_esc_reader_running = true;
        g_esc_reader_paused = false;
        esc_thread = std::thread(esc_reader_thread_fn);
        ui_cleanup.set(esc_thread);
        ui_cleanup.hook_sigint();
    }

    bool inner_fatal = false;

    for (;;)
    {
        if (g_redirect_ui_requested.exchange(false))
        {
            g_esc_reader_paused = true;
            tty_restore();
            std::string extra = read_redirect_rounded_box(fr, cwd, conf, false);
            if (!extra.empty())
            {
                std::lock_guard<std::mutex> lock(g_instr_mutex);
                if (!g_pending_instruction.empty())
                    g_pending_instruction += "\n";
                g_pending_instruction += extra;
            }
            if (isatty(STDIN_FILENO))
            {
                tty_enable_raw();
            }
            g_esc_reader_paused = false;
        }

        int remaining = -1;
        if (conf.max_iter > 0)
            remaining = conf.max_iter - iter - 1;

        std::string iter_log;
        if (conf.max_iter > 0)
            iter_log = std::to_string(iter + 1) + "/" + std::to_string(conf.max_iter);
        else
            iter_log = std::to_string(iter + 1) + "/∞";
        agent_log("=== Iteration " + iter_log + " ===", conf.debug);

        print_iteration_banner(iter, conf.max_iter, fr);

        s_config call_conf = conf;
        std::string iteration_info;

        if (fr)
        {
            if (conf.max_iter > 0)
            {
                iteration_info =
                    "\n\n[INFO] Iteration: " + std::to_string(iter + 1) + "/" +
                    std::to_string(conf.max_iter) +
                    ". Restantes apres celle-ci: " + std::to_string(remaining) + ".";
                if (remaining == 0)
                    iteration_info += " Derniere iteration prevue : termine si c'est fait.";
            }
            else
            {
                iteration_info =
                    "\n\n[INFO] Iteration: " + std::to_string(iter + 1) + " (no cap).";
            }
        }
        else
        {
            if (conf.max_iter > 0)
            {
                iteration_info =
                    "\n\n[INFO] Iteration: " + std::to_string(iter + 1) + "/" +
                    std::to_string(conf.max_iter) +
                    ". Remaining after this one: " + std::to_string(remaining) + ".";
                if (remaining == 0)
                    iteration_info += " Last planned iteration: finish if the task is done.";
            }
            else
            {
                iteration_info =
                    "\n\n[INFO] Iteration: " + std::to_string(iter + 1) + " (no cap).";
            }
        }

        std::string user_note;
        {
            std::lock_guard<std::mutex> lock(g_instr_mutex);
            user_note = g_pending_instruction;
            g_pending_instruction.clear();
        }
        if (!user_note.empty())
        {
            if (fr)
                iteration_info += "\n\n[INSTRUCTION UTILISATEUR] " + user_note;
            else
                iteration_info += "\n\n[USER INSTRUCTION] " + user_note;
        }

        if (history.empty())
            call_conf.prompt = agent_system_base + iteration_info;
        else
        {
            std::string cont_msg;
            if (fr)
                cont_msg =
                    "\n\nContinue seulement si la tache n'est pas terminee. Reponds UNIQUEMENT en JSON. "
                    "Si c'est fini : done=true, commands=[], sans travail supplementaire non demande.";
            else
                cont_msg =
                    "\n\nContinue only if the task is not finished yet. Respond ONLY in JSON. "
                    "If finished: done=true, commands=[] — no extra work unless the user asked for it.";
            call_conf.prompt = agent_system_base + iteration_info +
                               "\n\nResults so far:\n" + history + cont_msg;
        }

        agent_log(
            "Sending prompt size: " + std::to_string(call_conf.prompt.size()) +
            " bytes (~" + std::to_string(call_conf.prompt.size() / 4) + " tokens est.)",
            conf.debug);

        std::string raw = call_ai("", call_conf);

        if (raw.size() >= 5 && raw.substr(0, 5) == "Error")
        {
            std::cerr << RED << "[AGENT] API error: " << raw << RESET << std::endl;
            inner_fatal = true;
            break;
        }

        agent_log("Raw response preview: " + raw.substr(0, 300), conf.debug);

        std::string clean_raw = strip_markdown_fence(raw);

        size_t js = clean_raw.find('{');
        size_t je = clean_raw.rfind('}');
        if (js != std::string::npos && je != std::string::npos)
            clean_raw = clean_raw.substr(js, je - js + 1);

        clean_raw = fix_json_strings(clean_raw);

        json j;
        bool parse_ok = false;

        try
        {
            j = json::parse(clean_raw);
            parse_ok = true;
        }
        catch (const std::exception &e)
        {
            agent_log("JSON parse error: " + std::string(e.what()), conf.debug);
            agent_log("Attempting field extraction fallback...", conf.debug);
        }

        std::string thoughts, report;
        bool done = false;

        if (parse_ok)
        {
            thoughts = j.value("thoughts", "");
            done     = j.value("done", false);
            report   = j.value("report", "");
        }
        else
        {
            thoughts             = extract_field(clean_raw, "thoughts");
            report               = extract_field(clean_raw, "report");
            std::string done_str = extract_field(clean_raw, "done");
            done                 = (done_str == "true");

            if (thoughts.empty() && report.empty())
            {
                std::cerr << RED << "[AGENT] Could not parse response as JSON."
                          << RESET << std::endl;
                inner_fatal = true;
                break;
            }
            agent_log("Fallback extraction succeeded.", conf.debug);
        }

        if (!thoughts.empty())
        {
            std::cout << MAGENTA << "[AGENT] " << RESET << thoughts << std::endl;
            agent_log("Thoughts: " + thoughts, conf.debug);
        }

        std::string iter_results;

        if (!done && parse_ok && j.contains("commands") && j["commands"].is_array())
        {
            for (const auto &c : j["commands"])
            {
                std::string type = "shell";
                std::string cmd;

                if (c.is_string())
                    cmd = c.get<std::string>();
                else if (c.is_object())
                {
                    type = c.value("type", "shell");
                    cmd  = c.value("cmd", "");
                }

                if (cmd.empty())
                    continue;

                if (is_blacklisted(cmd))
                {
                    std::cout << RED << "[AGENT] BLOCKED: " << cmd << RESET << std::endl;
                    agent_log("BLOCKED: " + cmd, conf.debug);
                    iter_results += "$ " + cmd + "\n[BLOCKED - unsafe]\n\n";
                    continue;
                }

                bool execute = false;

                if (is_bypassed(cmd) || type == "interactive")
                {
                    std::cout << GREEN << "[AGENT] Auto: " << RESET << cmd << std::endl;
                    execute = true;
                }
                else
                    execute = ask_permission(cmd, fr, cwd);

                if (!execute)
                {
                    iter_results += "$ " + cmd + "\n[SKIPPED]\n\n";
                    continue;
                }

                std::string output;

                if (type == "interactive")
                {
                    std::vector<std::string> inputs;

                    if (c.contains("inputs") && c["inputs"].is_array())
                    {
                        for (const auto &inp : c["inputs"])
                            inputs.push_back(inp.get<std::string>());
                    }

                    if (fr)
                        std::cout << CYAN << "[AGENT] Mode interactif: " << RESET << cmd << std::endl;
                    else
                        std::cout << CYAN << "[AGENT] Interactive mode: " << RESET << cmd << std::endl;

                    for (const auto &inp : inputs)
                        std::cout << CYAN << "  -> input: " << RESET << inp << std::endl;

                    output = exec_interactive(cmd, inputs, conf.debug);
                }
                else
                    output = exec_command(cmd, conf);

                std::string suffix;
                if (output.size() > 300)
                    suffix = "...";
                else
                    suffix = "";
                std::cout << CYAN << "  -> " << RESET
                          << output.substr(0, 300) << suffix << std::endl;
                iter_results += "$ " + cmd + "\n" + output + "\n\n";
            }
        }
        else if (done && parse_ok && j.contains("commands") && j["commands"].is_array()
                 && !j["commands"].empty())
            agent_log("Ignoring commands because done=true (model should use commands: []).",
                      conf.debug);

        std::string iter_summary = "[Iter " + std::to_string(iter + 1) + "]\n";
        iter_summary += "THOUGHTS: " + thoughts + "\n";
        iter_summary += "ACTIONS & RESULTS:\n" + iter_results;
        history += iter_summary + "\n---\n";

        bool cap_reached = false;
        if (conf.max_iter > 0 && iter >= conf.max_iter - 1)
            cap_reached = true;

        if (done || cap_reached)
        {
            agent_log("Finalizing.", conf.debug);

            std::cout << "\n";
            std::cout << std::flush;

            g_esc_reader_paused = true;
            tty_restore();
            std::string extra = read_redirect_rounded_box(fr, cwd, conf, true);
            if (agent_prompt_is_blank(extra))
            {
                if (fr)
                    std::cout << DIM << "Session agent terminee." << RESET << "\n";
                else
                    std::cout << DIM << "Agent session ended." << RESET << "\n";
                std::cout << std::flush;
                return;
            }
            {
                std::lock_guard<std::mutex> lock(g_instr_mutex);
                if (!g_pending_instruction.empty())
                    g_pending_instruction += "\n";
                g_pending_instruction += extra;
            }
            if (isatty(STDIN_FILENO))
                tty_enable_raw();
            g_esc_reader_paused = false;

            iter = 0;
            continue;
        }
        ++iter;
    }

    if (inner_fatal)
        return;
}
