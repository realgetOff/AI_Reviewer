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

static const std::vector<std::string> BYPASS =
{
    "valgrind", "gcc", "g++", "clang", "clang++",
    "make", "ls", "cat", "echo", "wc", "file",
    "nm", "objdump", "strace", "python3", "python",
    "head", "tail", "grep", "find", "pwd", "hexdump",
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
        if (cmd.find(b) != std::string::npos)
            return true;
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
        if (first_word == b)
            return true;
    return false;
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

static bool ask_permission(const std::string &cmd, bool fr)
{
    std::cout << YELLOW << (fr ? "[AGENT] Exécuter? " : "[AGENT] Execute? ") << RESET
              << "`" << cmd << "`"
              << YELLOW << (fr ? " (o/n): " : " (y/n): ") << RESET;
    char c;
    std::cin >> c;
    std::cin.ignore();
    return (c == 'y' || c == 'Y' || c == 'o' || c == 'O');
}

static std::string exec_command(const std::string &cmd, const s_config &conf)
{
    std::string output;
    char        buf[512];
    FILE        *pipe;
    int         ret;

    std::string t_str = std::to_string(conf.interactive_timeout);
    agent_log("Running shell: " + cmd, conf.debug);
    std::string full_cmd = "timeout " + t_str + "s bash -c '(" + cmd + " </dev/null) 2>&1'";
    
    pipe = popen(full_cmd.c_str(), "r");
    if (!pipe)
        return ("(popen failed)");

    while (fgets(buf, sizeof(buf), pipe))
        output += buf;

    ret = pclose(pipe);

    if (WEXITSTATUS(ret) == 124)
        output += "\n(timeout: killed after " + t_str + "s)";
    else if (WIFSIGNALED(ret))
    {
        int sig = WTERMSIG(ret);
        output += "\n[CRASH] Process terminated by signal " + std::to_string(sig);
        if (sig == SIGSEGV)
            output += " (Segmentation fault)";
        output += "\n";
    }

    if (output.empty())
        output = "(no output)";

    std::string preview = (output.size() > 200) ? output.substr(0, 200) : output;
    agent_log("Output: " + preview, conf.debug);
    
    return (output);
}

static std::string sanitize_output(const std::string& str)
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

// Escape control characters inside JSON string values so the parser
// does not choke on raw newlines / tabs that the LLM sometimes emits.
static std::string fix_json_strings(const std::string &raw)
{
    std::string out;
    out.reserve(raw.size());
    bool in_string = false;
    bool escaped   = false;

    for (size_t i = 0; i < raw.size(); ++i)
    {
        char c = raw[i];

        if (escaped)
        {
            out += c;
            escaped = false;
            continue;
        }

        if (c == '\\' && in_string)
        {
            out += c;
            escaped = true;
            continue;
        }

        if (c == '"')
        {
            in_string = !in_string;
            out += c;
            continue;
        }

        if (in_string)
        {
            // These characters are forbidden unescaped inside JSON strings
            if (c == '\n')      { out += "\\n";  continue; }
            if (c == '\r')      { out += "\\r";  continue; }
            if (c == '\t')      { out += "\\t";  continue; }
            if ((unsigned char)c < 0x20)
            {
                // Other control characters: emit \uXXXX
                char esc[8];
                snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)c);
                out += esc;
                continue;
            }
        }

        out += c;
    }
    return out;
}

static std::string exec_interactive(const std::string &cmd,
                                    const std::vector<std::string> &inputs,
                                    int timeout_sec, bool debug)
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

        std::vector<char*> argv_vec;
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
        int status;
        if (waitpid(pid, &status, WNOHANG) != 0)
            break;

        usleep(200000);
        read_available();

        std::string line = inp + "\n";
        if (write(in_pipe[1], line.c_str(), line.size()) == -1)
            break;
        agent_log("Sent: " + inp, debug);
    }

    close(in_pipe[1]);

    int elapsed = 0;
    int status = 0;
    bool exited = false;

    while (elapsed < timeout_sec * 10)
    {
        usleep(100000);
        read_available();
        pid_t res = waitpid(pid, &status, WNOHANG);
        if (res == pid)
        {
            exited = true;
            break;
        }
        elapsed++;
    }

    if (!exited)
    {
        agent_log("Timeout reached, killing process", debug);
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        output += "\n[ERROR] Command timed out after " + std::to_string(timeout_sec) + "s\n";
    }
    else
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
            output += "\n[EXIT] Process finished with error code " + std::to_string(WEXITSTATUS(status)) + "\n";
        }
    }

    read_available();
    close(out_pipe[0]);

    if (output.empty())
    {
        output = "(no output)";
    }

    output = sanitize_output(output);

    std::string preview;
    if (output.size() > 200)
        preview = output.substr(0, 200);
    else
        preview = output;

    agent_log("Interactive output: " + preview, debug);
    return (output);
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

    char cwd_buf[1024];
    std::string cwd;

    if (getcwd(cwd_buf, sizeof(cwd_buf)))
        cwd = cwd_buf;
    else
        cwd = ".";

    agent_log("Starting agent in: " + cwd, conf.debug);
    agent_log("Max iterations: " + std::to_string(conf.max_iter), conf.debug);

    std::string ls_output = get_ls(conf.debug);
    agent_log("Directory:\n" + ls_output, conf.debug);

    std::string agent_system_base = conf.prompt + "\n\nWorking directory: " + cwd + "\n\nDirectory contents:\n" + ls_output + "\n\n";

    if (fr)
    {
        agent_system_base += "Tu es un agent autonome. Explore librement : ls, cat, make, compile, teste.\n"
                             "Ne suppose pas le contenu des fichiers — lis-les avec cat/head si nécessaire.\n\n"
                             "IMPORTANT : avant d'exécuter un binaire, réfléchis :\n"
                             "- Est-ce qu'il attend une entrée utilisateur en boucle ? (shell, REPL, jeu...)\n"
                             "- Si OUI → utilise le type 'interactive' et prépare tes inputs à l'avance\n"
                             "- Si NON → utilise le type 'shell'\n\n"
                             "Réponds UNIQUEMENT en JSON valide :\n";
    }
    else
    {
        agent_system_base += "You are an autonomous agent. Explore freely: ls, cat, make, compile, test.\n"
                             "Do NOT assume file contents — read them with cat/head if needed.\n\n"
                             "IMPORTANT: before running a binary, think:\n"
                             "- Does it wait for user input in a loop? (shell, REPL, game...)\n"
                             "- If YES → use type 'interactive' and prepare your inputs in advance\n"
                             "- If NO  → use type 'shell'\n\n"
                             "Respond ONLY in valid JSON:\n";
    }

    agent_system_base += "{\n"
                         "  \"thoughts\": \"...\",\n"
                         "  \"commands\": [\n"
                         "    {\"type\": \"shell\", \"cmd\": \"make\"},\n"
                         "    {\"type\": \"interactive\", \"cmd\": \"./minishell\", \"inputs\": [\"echo hi\", \"exit\"], \"timeout\": 10}\n"
                         "  ],\n"
                         "  \"done\": false,\n"
                         "  \"report\": \"\"\n"
                         "}\n";

    if (fr)
        agent_system_base += "Mets done=true et remplis report (markdown) quand tu as terminé.";
    else
        agent_system_base += "Set done=true and fill report (markdown) when finished.";

    std::string history;

    std::cout << BLUE << "[AGENT] Starting... (max " << conf.max_iter << " iterations)" << RESET << std::endl;

    for (int iter = 0; iter < conf.max_iter; iter++)
    {
        int remaining = conf.max_iter - iter - 1;

        agent_log("=== Iteration " + std::to_string(iter + 1) + "/" + std::to_string(conf.max_iter) + " ===", conf.debug);
        std::cout << BLUE << "[AGENT] Iteration " << (iter + 1) << "/" << conf.max_iter << "...\n" << RESET << std::flush;

        s_config call_conf = conf;
        std::string iteration_info;

        if (fr)
        {
            iteration_info = "\n\n[INFO] Itération: " + std::to_string(iter + 1) + "/" + std::to_string(conf.max_iter) +
                             ". Restantes après celle-ci: " + std::to_string(remaining) + ".";
            if (remaining == 0)
                iteration_info += " CECI EST TA DERNIÈRE CHANCE. Termine et fais le report.";
        }
        else
        {
            iteration_info = "\n\n[INFO] Iteration: " + std::to_string(iter + 1) + "/" + std::to_string(conf.max_iter) +
                             ". Remaining after this one: " + std::to_string(remaining) + ".";
            if (remaining == 0)
                iteration_info += " THIS IS YOUR LAST CHANCE. Finish and write the report.";
        }

        if (history.empty())
            call_conf.prompt = agent_system_base + iteration_info;
        else
        {
            std::string cont_msg;
            if (fr)
                cont_msg = "\n\nContinue. Réponds UNIQUEMENT en JSON.";
            else
                cont_msg = "\n\nContinue. Respond ONLY in JSON.";
            call_conf.prompt = agent_system_base + iteration_info + "\n\nResults so far:\n" + history + cont_msg;
        }

        agent_log(
            "Sending prompt size: " + std::to_string(call_conf.prompt.size()) +
            " bytes (~" + std::to_string(call_conf.prompt.size() / 4) + " tokens est.)",
            conf.debug
        );

        std::string raw = call_ai("", call_conf);
        std::cout << "\r" << std::string(60, ' ') << "\r";

        if (raw.substr(0, 5) == "Error")
        {
            std::cerr << RED << "[AGENT] API error: " << raw << RESET << std::endl;
            break;
        }

        agent_log("Raw response preview: " + raw.substr(0, 300), conf.debug);

        // Extract the JSON object from the raw response
        std::string clean_raw = raw;
        size_t js = clean_raw.find('{');
        size_t je = clean_raw.rfind('}');
        
        if (js != std::string::npos && je != std::string::npos)
            clean_raw = clean_raw.substr(js, je - js + 1);

        // Fix raw newlines / control chars inside JSON string values
        // before handing to the parser — the LLM sometimes emits them.
        clean_raw = fix_json_strings(clean_raw);

        json j;
        
        try 
        {
            j = json::parse(clean_raw);
        }
        catch (const std::exception &e)
        {
            agent_log("JSON parse error: " + std::string(e.what()), conf.debug);
            std::cerr << RED << "[AGENT] Could not parse response as JSON." << RESET << std::endl;
            break;
        }

        std::string thoughts = j.value("thoughts", "");
        bool done            = j.value("done", false);
        std::string report   = j.value("report", "");

        if (!thoughts.empty())
        {
            std::cout << MAGENTA << "[AGENT] " << RESET << thoughts << std::endl;
            agent_log("Thoughts: " + thoughts, conf.debug);
        }

        std::string iter_results;
        
        if (j.contains("commands") && j["commands"].is_array())
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
                    std::cout << GREEN << "[AGENT] ✓ Auto: " << RESET << cmd << std::endl;
                    execute = true;
                }
                else
                    execute = ask_permission(cmd, fr);

                if (!execute)
                {
                    iter_results += "$ " + cmd + "\n[SKIPPED]\n\n";
                    continue;
                }

                std::string output;
                
                if (type == "interactive")
                {
                    std::vector<std::string> inputs;
                    int timeout_sec;
                    
                    if (c.contains("timeout"))
                        timeout_sec = c.value("timeout", conf.interactive_timeout);
                    else
                        timeout_sec = conf.interactive_timeout;
                    
                    if (c.contains("inputs") && c["inputs"].is_array())
                        for (const auto &inp : c["inputs"])
                            inputs.push_back(inp.get<std::string>());

                    if (fr)
                        std::cout << CYAN << "[AGENT] Mode interactif: " << RESET << cmd << std::endl;
                    else
                        std::cout << CYAN << "[AGENT] Interactive mode: " << RESET << cmd << std::endl;

                    for (const auto &inp : inputs)
                        std::cout << CYAN << "  ↳ input: " << RESET << inp << std::endl;

                    output = exec_interactive(cmd, inputs, conf.interactive_timeout, conf.debug);
                }
                else
                    output = exec_command(cmd, conf);

                std::string suffix;
                
                if (output.size() > 300)
                    suffix = "...";
                else
                    suffix = "";

                std::cout << CYAN << "  → " << RESET << output.substr(0, 300) << suffix << std::endl;
                iter_results += "$ " + cmd + "\n" + output + "\n\n";
            }
        }

        std::string iter_summary = "[Iter " + std::to_string(iter + 1) + "]\n";
        iter_summary += "THOUGHTS: " + thoughts + "\n";
        iter_summary += "ACTIONS & RESULTS:\n" + iter_results;
        history += iter_summary + "\n---\n";

        if (done || iter == conf.max_iter - 1)
        {
            agent_log("Finalizing.", conf.debug);
            std::string report_path = "reports/agent_report.md";
            std::ofstream of(report_path);
            
            if (of.is_open())
            {
                if (!report.empty())
                    of << report;
                else
                    of << "# Agent Report\n\n" << history;
            }
            of.close();
            
            std::cout << "\n" << BOLD << "Final Results:" << RESET << std::endl;
            
            if (done)
                std::cout << GREEN << "[OK]   " << RESET << "Agent report (reports/agent_report.md)" << std::endl;
            else
                std::cout << YELLOW << "[TIMEOUT] " << RESET << "Max iterations reached. Saved report." << std::endl;
            
            if (conf.format == "pdf")
                save_as_pdf("reports/agent_report.md", conf.debug);
            
            return;
        }
    }
}
