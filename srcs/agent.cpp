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
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstdio>
#include <unistd.h>

static const std::vector<std::string> BYPASS = {
    "valgrind", "gcc", "g++", "clang", "clang++",
    "make", "ls", "cat", "echo", "wc", "file",
    "nm", "objdump", "strace", "python3", "python",
    "head", "tail", "grep", "find", "pwd", "hexdump",
    "readelf", "ldd", "strings", "size", "ar"
};

static const std::vector<std::string> BLACKLIST = {
    "rm -rf /", "wget", "ssh", "scp",
    "dd", "mkfs", "shutdown", "reboot",
    "> /dev/sd", ":(){ :|:& };:"
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
    for (const auto &b : BYPASS)
        if (first_word == b)
            return true;
    return false;
}

static bool ask_permission(const std::string &cmd)
{
    std::cout << YELLOW << "[AGENT] Execute? " << RESET
              << "`" << cmd << "`"
              << YELLOW << " (y/n): " << RESET;
    char c;
    std::cin >> c;
    std::cin.ignore();
    return (c == 'y' || c == 'Y');
}

static std::string exec_command(const std::string &cmd, bool debug)
{
    agent_log("Running: " + cmd, debug);
    std::string full_cmd = "(" + cmd + ") 2>&1";
    FILE *pipe = popen(full_cmd.c_str(), "r");
    if (!pipe)
    {
        agent_log("ERROR: popen failed", debug);
        return ("(popen failed)");
    }
    std::string output;
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe))
        output += buf;
    pclose(pipe);
    if (output.size() > 2000)
        output = output.substr(0, 2000) + "\n...(truncated)";
    if (output.empty())
        output = "(no output)";
    agent_log("Output: " + output.substr(0, 200), debug);
    return output;
}

static std::string get_ls(bool debug)
{
    agent_log("Running initial ls -la", debug);
    FILE *pipe = popen("ls -la 2>&1", "r");
    if (!pipe) return ("(ls failed)");
    std::string output;
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe))
        output += buf;
    pclose(pipe);
    return output;
}

void run_agent(const std::vector<std::string> &/*files*/, s_config conf)
{
    char cwd_buf[1024];
    std::string cwd = (getcwd(cwd_buf, sizeof(cwd_buf))) ? cwd_buf : ".";

    agent_log("Starting agent in: " + cwd, conf.debug);
    agent_log("Max iterations: " + std::to_string(conf.max_iter), conf.debug);

    std::string ls_output = get_ls(conf.debug);
    agent_log("Directory contents:\n" + ls_output, conf.debug);

    std::string agent_system =
        conf.prompt
        + "\n\nWorking directory: " + cwd
        + "\n\nDirectory contents:\n" + ls_output
        + "\n\nYou are an autonomous agent. Explore freely: use ls, cat, make, compile, run tests."
          " Do NOT assume file contents — read them with cat/head if needed."
          " Respond ONLY in JSON, no text outside JSON:\n"
          "{\n"
          "  \"thoughts\": \"brief plan or observation\",\n"
          "  \"commands\": [\"cmd1\", \"cmd2\"],\n"
          "  \"done\": false,\n"
          "  \"report\": \"\"\n"
          "}\n"
          "Set done=true and fill report (markdown) when finished. "
          "Leave commands empty when done.";

    std::string history;

    std::cout << BLUE << "[AGENT] Starting... (max " << conf.max_iter << " iterations)" << RESET << std::endl;

    for (int iter = 0; iter < conf.max_iter; iter++)
    {
        agent_log("=== Iteration " + std::to_string(iter + 1) + "/" + std::to_string(conf.max_iter) + " ===", conf.debug);
        std::cout << BLUE << "[AGENT] Iteration " << (iter + 1) << "/" << conf.max_iter << "..." << RESET << std::flush;

        s_config call_conf = conf;
        if (history.empty())
            call_conf.prompt = agent_system;
        else
            call_conf.prompt = agent_system
                + "\n\nResults so far:\n" + history
                + "\n\nContinue. Respond ONLY in JSON.";

        std::string raw = call_ai("", call_conf);
        std::cout << "\r" << std::string(60, ' ') << "\r";

        if (raw.substr(0, 5) == "Error")
        {
            std::cerr << RED << "[AGENT] API error: " << raw << RESET << std::endl;
            break;
        }

        agent_log("Raw response preview: " + raw.substr(0, 300), conf.debug);

        std::string clean_raw = raw;
        size_t js = clean_raw.find('{');
        size_t je = clean_raw.rfind('}');
        if (js != std::string::npos && je != std::string::npos)
            clean_raw = clean_raw.substr(js, je - js + 1);

        json j;
        try { j = json::parse(clean_raw); }
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
                std::string cmd = c.get<std::string>();

                if (is_blacklisted(cmd))
                {
                    std::cout << RED << "[AGENT] BLOCKED: " << cmd << RESET << std::endl;
                    agent_log("BLOCKED: " + cmd, conf.debug);
                    iter_results += "$ " + cmd + "\n[BLOCKED - unsafe]\n\n";
                    continue;
                }

                bool execute = false;
                if (is_bypassed(cmd))
                {
                    std::cout << GREEN << "[AGENT] ✓ " << RESET << cmd << std::endl;
                    execute = true;
                }
                else
                    execute = ask_permission(cmd);

                if (!execute)
                {
                    iter_results += "$ " + cmd + "\n[SKIPPED]\n\n";
                    continue;
                }

                std::string output = exec_command(cmd, conf.debug);
                std::cout << CYAN << "  → " << RESET
                          << output.substr(0, 300)
                          << (output.size() > 300 ? "..." : "") << std::endl;
                iter_results += "$ " + cmd + "\n" + output + "\n\n";
            }
        }

        history += "[Iter " + std::to_string(iter + 1) + "]\n" + iter_results + "\n";

        if (done)
        {
            agent_log("Done.", conf.debug);
            std::string report_path = "reports/agent_report.md";
            std::ofstream of(report_path);
            if (of.is_open())
                of << "# Agent Report\n\n" << (report.empty() ? history : report);
            of.close();
            std::cout << "\n" << BOLD << "Final Results:" << RESET << std::endl;
            std::cout << GREEN << "[OK]   " << RESET
                      << "Agent report (reports/agent_report.md)" << std::endl;
            if (conf.format == "pdf")
                save_as_pdf("reports/agent_report.md", conf.debug);
            return;
        }
    }

    // Max iterations atteint
    agent_log("Max iterations reached.", conf.debug);
    std::string report_path = "reports/agent_report.md";
    std::ofstream of(report_path);
    if (of.is_open())
        of << "# Agent Report (partial)\n\n" << history;
    of.close();
    std::cout << "\n" << BOLD << "Final Results:" << RESET << std::endl;
    std::cout << YELLOW << "[PARTIAL] " << RESET
              << "Agent report (reports/agent_report.md)" << std::endl;
    if (conf.format == "pdf")
        save_as_pdf("reports/agent_report.md", conf.debug);
}
