/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mforest- <marvin@d42.fr>                   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/03/22 21:47:25 by mforest-          #+#    #+#             */
/*   Updated: 2026/03/22 21:47:25 by mforest-         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "ai_client.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <future>
#include <algorithm>
#include <cstdlib>

using json = nlohmann::json;

static void	display_help(void)
{
	std::string p = "air";

	std::cout << BOLD << "\nUSAGE:" << RESET << std::endl;
	std::cout << "  " << p << " [OPTIONS] <path>" << std::endl;
	std::cout << "  " << p << " <command>" << std::endl;

	std::cout << "\n" << BOLD << "COMMANDS:" << RESET << std::endl;
	std::cout << "  config     " << RESET << "Open global configuration file" << std::endl;
	std::cout << "  -update    " << RESET << "Update to the latest version from GitHub" << std::endl;
	std::cout << "  -clean     " << RESET << "Execute clean_reports.sh (remove reports dir.)" << std::endl;
	std::cout << "  -delete    " << RESET << "Full uninstall (binary and config)" << std::endl;

	std::cout << "\n" << BOLD << "OPTIONS:" << RESET << std::endl;
	std::cout << "  -s <style>  Set review style (e.g., minimal, security, etc...)" << std::endl;
	std::cout << "  -l <lang>   Set output language (en, fr)" << std::endl;
	std::cout << "  -m          List all available AI models" << std::endl;
	std::cout << "  -d          Enable debug mode (verbose logs)" << std::endl;
	std::cout << "  -h          Show this help message" << std::endl;

	std::cout << "\n" << BOLD << "EXAMPLES:" << RESET << std::endl;
	std::cout << "  air ." << std::endl;
	std::cout << "  air -s security -l fr srcs/" << std::endl;
	std::cout << "  air config" << std::endl;
	std::cout << std::endl;
	exit(0);
}

static std::string	strip_comments(const std::string &code, const std::string &ext)
{
	std::string	result;
	bool		s_comment = false;
	bool		m_comment = false;
	bool		is_script = (ext == ".py" || ext == ".sh" || ext == ".rb" || ext == ".pl");

	for (size_t i = 0; i < code.size(); ++i)
	{
		if (!s_comment && !m_comment)
		{
			if (!is_script && i + 1 < code.size() && code[i] == '/' && code[i + 1] == '/')
				s_comment = true;
			else if (is_script && code[i] == '#')
				s_comment = true;
		}
		
		if (s_comment && code[i] == '\n')
			s_comment = false;

		if (!is_script && !s_comment && !m_comment && i + 1 < code.size() && code[i] == '/' && code[i + 1] == '*')
		{
			m_comment = true;
			i++;
		}
		else if (m_comment && i + 1 < code.size() && code[i] == '*' && code[i + 1] == '/')
		{
			m_comment = false;
			i++;
			continue;
		}

		if (!s_comment && !m_comment)
			result += code[i];
	}
	return (result);
}

static void	load_config(s_config &conf)
{
	char* home = getenv("HOME");
	std::string path = (home) ? std::string(home) + "/.ai_config.json" : "config.json";
	std::ifstream	f(path);
	json			j;

	if (!f.is_open())
	{
		std::cerr << YELLOW << "Warning: " << path << " not found. Using defaults." << RESET << std::endl;
		return ;
	}
	try
	{
		j = json::parse(f);
		conf.api_key = j.value("api_key", "");
		conf.api_url = j.value("api_url", "");
		conf.model_type = j.value("model_type", "gemini");
		conf.model_name = j.value("model_name", "");
		if (conf.lang.empty())
			conf.lang = j.value("default_lang", "en");
		if (conf.style.empty())
			conf.style = j.value("default_style", "minimal");
		if (j.contains("styles") && j["styles"].contains(conf.style))
		{
			if (j["styles"][conf.style].contains(conf.lang))
				conf.prompt = j["styles"][conf.style][conf.lang].get<std::string>();
			else
			{
				std::cerr << RED << "Error: Language '" << conf.lang << "' not found for style '" << conf.style << "'" << RESET << std::endl;
				conf.prompt = "Review this code.";
			}
		}
		else
		{
			std::cerr << RED << "Error: Style '" << conf.style << "' not found in config.json" << RESET << std::endl;
			conf.prompt = "Review this code.";
		}
	}
	catch (const json::parse_error& e)
	{
		std::cerr << RED << "Config Error: Invalid JSON syntax: " << e.what() << RESET << std::endl;
		exit(1);
	}
	catch (const std::exception& e)
	{
		std::cerr << RED << "Config Error: " << e.what() << RESET << std::endl;
		conf.prompt = "Review this code.";
	}
}

static void	display_progress(size_t cur, size_t tot)
{
	int	bar_width;
	int	pos;

	bar_width = 30;
	if (tot > 0)
		pos = bar_width * cur / tot;
	else
		pos = bar_width;
	std::cout << "\r" << YELLOW << "Analyzing: " << RESET << "[";
	for (int i = 0; i < bar_width; ++i)
	{
		if (i < pos)
			std::cout << "=";
		else if (i == pos)
			std::cout << ">";
		else
			std::cout << " ";
	}
	if (tot > 0)
		std::cout << "] " << (cur * 100 / tot) << "%" << std::flush;
	else
		std::cout << "] 100%" << std::flush;
}

static void	scan_path(const std::string &p, std::vector<std::string> &files)
{
	struct stat		s;
	DIR				*dir;
	struct dirent	*e;
	size_t			dot;

	if (stat(p.c_str(), &s) != 0)
		return ;
	if (S_ISDIR(s.st_mode))
	{
		dir = opendir(p.c_str());
		if (!dir)
			return ;
		while ((e = readdir(dir)))
		{
			std::string name = e->d_name;
			if (name != "." && name != ".." && name[0] != '.')
				scan_path(p + "/" + name, files);
		}
		closedir(dir);
	}
	else
	{
		dot = p.find_last_of(".");
		if (dot != std::string::npos)
		{
			std::string ext = p.substr(dot);
			if (ext != ".o" && ext != ".a" && ext != ".out" && ext != ".so")
				files.push_back(p);
		}
	}
}

static void	write_debug(const std::string &msg, bool enabled)
{
	if (!enabled)
		return ;
	std::ofstream	dbg("reports/debug.log", std::ios::app);
	if (dbg.is_open())
		dbg << msg << std::endl;
}

static std::string	process_file(std::string f, s_config conf)
{
	std::ifstream		ifs(f);
	std::stringstream	buf;
	std::string			res;
	std::string			ext;
	std::string			clean_code;
	size_t				last;
	size_t				dot;

	if (!ifs.is_open())
		return (RED "[FAIL] " RESET + f);

	write_debug("[START] Processing: " + f, conf.debug);

	buf << ifs.rdbuf();
	write_debug("[INFO] Read size: " + std::to_string(buf.str().size()) + " bytes", conf.debug);

	dot = f.find_last_of(".");
	ext = (dot != std::string::npos) ? f.substr(dot) : "";
	
	clean_code = strip_comments(buf.str(), ext);
	write_debug("[INFO] Cleaned size: " + std::to_string(clean_code.size()) + " bytes", conf.debug);
	
	write_debug("[API] Calling " + conf.model_type + "...", conf.debug);
	res = call_ai(clean_code, conf);

	if (res.substr(0, 5) == "Error")
	{
		std::ofstream	of("reports/error.md", std::ios::app);
		if (of.is_open())
			of << "### File: " << f << "\nDetails: " << res << "\n---\n";
		write_debug("[ERROR] API failed for: " + f, conf.debug);
		return (RED "[FAIL] " RESET + f + " (reports/error.md)");
	}

	last = f.find_last_of("/");
	std::string out_name = (last == std::string::npos) ? f : f.substr(last + 1);

	std::ofstream	of("reports/" + out_name + ".report.md");
	if (of.is_open())
		of << "# Review: " << f << "\n\n" << res;

	write_debug("[SUCCESS] Finished: " + f, conf.debug);
	return (GREEN "[OK] " RESET + f + " (reports/" + out_name + ".report.md)");
}

int	main(int argc, char **argv)
{
	if (argc > 1 && std::string(argv[1]) == "config")
	{
		char* home = getenv("HOME");
		if (!home) return (1);
		std::string path = std::string(home) + "/.ai_config.json";
		std::string editor = getenv("EDITOR") ? getenv("EDITOR") : "nano";
		std::string cmd = editor + " " + path;
		return (system(cmd.c_str()));
	}

	if (argc > 1 && std::string(argv[1]) == "-clean")
	{
		std::cout << YELLOW << "Cleaning reports..." << RESET << std::endl;
		system("rm -rf reports");
		std::cout << GREEN << "Done." << RESET << std::endl;
		return (0);
	}

	if (argc > 1 && std::string(argv[1]) == "-delete")
	{
		std::cout << RED << "WARNING: This will delete the binary and the configuration." << RESET << std::endl;
		std::cout << "Are you sure? (y/n): ";
		char confirm;
		std::cin >> confirm;
		if (confirm == 'y' || confirm == 'Y')
		{
			system("rm -f ~/bin/ai_reviewer");
			system("rm -f ~/.ai_config.json");
			std::cout << GREEN << "Executable and config deleted. Please remove the alias from your .zshrc/.bashrc manually." << RESET << std::endl;
			return (0);
		}
		return (1);
	}

	if (argc > 1 && std::string(argv[1]) == "-update")
		{
		std::cout << BLUE << "Checking for updates..." << RESET << std::endl;
		int ret = system("curl -sSL https://raw.githubusercontent.com/realgetOff/AI_Reviewer/main/install.sh?t=$(date +%s) | bash");
		if (ret == 0)
			std::cout << GREEN << "Update successful!" << RESET << std::endl;
		else
			std::cout << RED << "Update failed." << RESET << std::endl;
		return (ret);
	}

	s_config								conf = {false, true, false, "", "", "", "", "", "gemini", ""};
	std::vector<std::string>				files;
	std::vector<std::string>				results;
	std::vector<std::future<std::string>>	futures;
	int										opt;

	while ((opt = getopt(argc, argv, "dhms:l:")) != -1)
	{
		if (opt == 'h')
			display_help();
		if (opt == 'm')
		{
			load_config(conf);
			list_provider_models(conf);
			return (0);
		}
		if (opt == 's')
			conf.style = optarg;
		if (opt == 'l')
			conf.lang = optarg;
		if (opt == 'd')
			conf.debug = true;
	}
	load_config(conf);
	if (conf.api_key.empty() || optind >= argc)
		display_help();
	if (conf.model_type != "gemini" && conf.model_name.empty())
	{
		std::cerr << RED << "Error: model_name is required in config.json for " << conf.model_type << RESET << std::endl;
		return (1);
	}
	mkdir("reports", 0777);
	for (int i = optind; i < argc; i++)
		scan_path(argv[i], files);
	if (files.empty())
		return (0);
	for (const auto &f : files)
		futures.push_back(std::async(std::launch::async, process_file, f, conf));
	display_progress(0, files.size());
	for (size_t i = 0; i < futures.size(); i++)
	{
		results.push_back(futures[i].get());
		display_progress(i + 1, files.size());
	}
	std::cout << "\n\n" << BOLD << "Final Results:" << RESET << std::endl;
	for (const auto &r : results)
		std::cout << r << std::endl;
	return (0);
}
