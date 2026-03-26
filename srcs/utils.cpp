/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   utils.cpp                                          :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mforest- <marvin@d42.fr>                   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/03/23 12:04:30 by mforest-          #+#    #+#             */
/*   Updated: 2026/03/23 12:04:30 by mforest-         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "ai_client.hpp"

void	display_help(void)
{
	std::string p = "air";

	std::cout << BOLD << "\nUSAGE:" << RESET << std::endl;
	std::cout << "  " << p << " [OPTIONS] <path>" << std::endl;
	std::cout << "  " << p << " <command>" << std::endl;

	std::cout << "\n" << BOLD << "COMMANDS:" << RESET << std::endl;
	std::cout << "  config      " << RESET << "Open global configuration file" << std::endl;
	std::cout << "  -update     " << RESET << "Update to the latest version from GitHub" << std::endl;
	std::cout << "  -clean      " << RESET << "Execute clean_reports.sh (remove reports dir.)" << std::endl;
	std::cout << "  -delete     " << RESET << "Full uninstall (binary and config)" << std::endl;

	std::cout << "\n" << BOLD << "OPTIONS:" << RESET << std::endl;
	std::cout << "  -f <fmt>    Set default format (md, pdf) in config" << std::endl;
	std::cout << "  -s <style>  Set review style (minimal, security)" << std::endl;
	std::cout << "  -l <lang>   Set output language (en, fr)" << std::endl;
	std::cout << "  -c <prompt> Use custom prompt instead of config style" << std::endl;
	std::cout << "  -t <sec>    Set request timeout in seconds (default: 30)" << std::endl;
	std::cout << "  -I <sec>    Set interactive program timeout in seconds (default: 10)" << std::endl;
	std::cout << "  -a          Enable agent mode" << std::endl;
	std::cout << "  -g          Analyze all files as one (global context mode)" << std::endl;
	std::cout << "  -d          Enable debug mode (verbose logs)" << std::endl;
	std::cout << "  -m          List all available AI models" << std::endl;
	std::cout << "  --version	Show current version" << std::endl;
	std::cout << "  -h          Show this help message" << std::endl;

	std::cout << "\n" << BOLD << "EXAMPLES:" << RESET << std::endl;
	std::cout << "  air ." << std::endl;
	std::cout << "  air -f pdf" << std::endl;
	std::cout << "  air -s security -l fr srcs/*" << std::endl;
	std::cout << "  air -c \"try to break my code by doing edges cases\" -a" << std::endl;
	std::cout << std::endl;
	exit(0);
}

void	update_config_file(const std::string &key, const std::string &value)
{
	char* home = getenv("HOME");
	if (!home)
		return;
	std::string path = std::string(home) + "/.ai_config.json";
	
	json j;
	std::ifstream input(path);
	if (input.is_open())
	{
		try
		{
			input >> j;
		}
		catch (...)
		{
			std::cerr << RED << "Error: Could not parse JSON for update." << RESET << std::endl;
			return;
		}
		input.close();
	}

	if (key == "format")
		j["default_format"] = value;
	else
		j[key] = value;

	std::ofstream output(path);
	if (output.is_open())
	{
		output << j.dump(4);
		output.close();
	}
	else
		std::cerr << RED << "Error: Could not write to " << path << RESET << std::endl;
}

std::string	strip_comments(const std::string &code, const std::string &ext)
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

void	load_config(s_config &conf)
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
		if (conf.timeout == 0)
    		conf.timeout = j.value("default_timeout", 30);
		if(conf.interactive_timeout == 0)
			conf.interactive_timeout = j.value("default_interactive_timeout", 10);
		conf.api_key = j.value("api_key", "");
		conf.api_url = j.value("api_url", "");
		conf.model_type = j.value("model_type", "gemini");
		conf.model_name = j.value("model_name", "");
		
		conf.format = j.value("default_format", "md");

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

void	display_progress(size_t cur, size_t tot)
{
	int	bar_width = 30;
	int	pos = (tot > 0) ? bar_width * cur / tot : bar_width;

	std::cout << "\r" << YELLOW << "Analyzing: " << RESET << "[";
	for (int i = 0; i < bar_width; ++i)
	{
		if (i < pos) std::cout << "=";
		else if (i == pos) std::cout << ">";
		else std::cout << " ";
	}
	std::cout << "] " << (tot > 0 ? (cur * 100 / tot) : 100) << "%" << std::flush;
}

int check_commands(std::string command)
{
	char* home = getenv("HOME");
	if (!home) return (1);
	std::string path = std::string(home) + "/.ai_config.json";

	if (command == "config")
		return (system(("vim " + path).c_str()));
	if (command == "-clean")
	{
		system("rm -rf reports");
		std::cout << GREEN << "Reports cleaned." << RESET << std::endl;
		return (0);
	}
	if (command == "-delete")
	{
		std::cout << RED << "Delete air and config? (y/n): " << RESET;
		char c; std::cin >> c;
		if (c == 'y' || c == 'Y')
		{
			system("rm -f ~/bin/ai_reviewer ~/.ai_config.json");
			return (0);
		}
		return (1);
	}
	if (command == "--version" || command == "-v")
	{
   	 	std::cout << GREEN << "air " << CURRENT_VERSION << RESET << std::endl;
   		return (0);
	}
	if (command == "-update")
	{
		return (system("curl -sSL https://raw.githubusercontent.com/realgetOff/AI_Reviewer/main/install.sh | bash"));
	}
	return(2);
}

void	scan_path(const std::string &p, std::vector<std::string> &files)
{
	struct stat s;
	if (stat(p.c_str(), &s) != 0) return;
	if (S_ISDIR(s.st_mode))
	{
		DIR *dir = opendir(p.c_str());
		struct dirent *e;
		if (!dir) return;
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
		size_t dot = p.find_last_of(".");
		if (dot != std::string::npos)
		{
			std::string ext = p.substr(dot);
			if (ext != ".o" && ext != ".a" && ext != ".out" && ext != ".so")
				files.push_back(p);
		}
	}
}

void	write_debug(const std::string &msg, bool enabled)
{
	if (!enabled) return;
	std::ofstream dbg("reports/debug.log", std::ios::app);
	if (dbg.is_open()) dbg << msg << std::endl;
}

std::string process_file(std::string f, s_config conf)
{
    std::ifstream ifs(f);
    std::stringstream buf;
    std::string res, ext, clean_code, out_name, report_path;

    if (!ifs.is_open()) return (RED "[FAIL] " RESET + f);
    buf << ifs.rdbuf();
    size_t dot = f.find_last_of(".");
    ext = (dot != std::string::npos) ? f.substr(dot) : "";
    clean_code = strip_comments(buf.str(), ext);
    res = call_ai(clean_code, conf);

    if (res.substr(0, 5) == "Error")
    {
        std::ofstream of("reports/error.md", std::ios::app);
        if (of.is_open()) of << "### File: " << f << "\n" << res << "\n---\n";
        return (RED "[FAIL] " RESET + f);
    }

    size_t last = f.find_last_of("/");
    out_name = (last == std::string::npos) ? f : f.substr(last + 1);
	report_path = "reports/" + out_name + ".md";

    std::ofstream of(report_path);
    if (of.is_open())
        of << "# Review: " << f << "\n\n" << res;
    of.close();
	
	std::string display_path = "reports/" + out_name + (conf.format == "pdf" ? ".pdf" : ".md");
	return (GREEN "[OK]   " RESET + f + " (" + display_path + ")|" + report_path);
}

bool save_as_pdf(const std::string& md_filename, bool debug)
{
    std::string pdf_filename = md_filename.substr(0, md_filename.size() - 3) + ".pdf";
    
    write_debug("[PDF] Input md: " + md_filename, debug);
    write_debug("[PDF] Output pdf: " + pdf_filename, debug);

    std::ifstream check_md(md_filename);
    if (!check_md.is_open())
    {
		write_debug("[PDF] ERROR: input .md file not found: " + md_filename, debug);
        std::cerr << RED << "Error: PDF conversion failed." << RESET << std::endl;
        return false;
    }
    check_md.close();
    write_debug("[PDF] Input file exists OK", debug);

    const std::vector<std::string> engines = {"wkhtmltopdf", "weasyprint", "xelatex", "pdflatex"};

    bool success = false;
    for (const auto& engine : engines)
    {
        std::string check = "command -v " + engine + " > /dev/null 2>&1";
        int avail = system(check.c_str());
        write_debug("[PDF] Engine '" + engine + "' available: " + (avail == 0 ? "yes" : "no"), debug);
        if (avail != 0)
            continue;

        std::string cmd = "pandoc " + md_filename
                        + " --pdf-engine=" + engine
                        + " -o " + pdf_filename
                        + " 2>/tmp/pandoc_err.log";
        write_debug("[PDF] Running: " + cmd, debug);
        int ret = system(cmd.c_str());
        write_debug("[PDF] pandoc exit code: " + std::to_string(ret), debug);

        if (debug)
        {
            std::ifstream err_log("/tmp/pandoc_err.log");
            if (err_log.is_open())
            {
                std::stringstream ss;
                ss << err_log.rdbuf();
                std::string err_content = ss.str();
                if (!err_content.empty())
                    write_debug("[PDF] pandoc stderr: " + err_content, debug);
            }
        }

        if (ret == 0)
        {
            success = true;
            write_debug("[PDF] Success with engine: " + engine, debug);
            break;
        }
        write_debug("[PDF] Failed with engine: " + engine + ", trying next...", debug);
    }

    if (success)
    {
        if (std::remove(md_filename.c_str()) != 0)
            write_debug("[PDF] WARN: Could not remove .md file: " + md_filename, debug);
        else
            write_debug("[PDF] Removed .md file: " + md_filename, debug);
        return true;
    }

    std::cerr << RED << "Error: PDF conversion failed." << RESET << std::endl;
    std::cerr << YELLOW << "Tip: Install one of: wkhtmltopdf, weasyprint, "
              << "xelatex (texlive), or pdflatex" << RESET << std::endl;
    write_debug("[PDF] All engines failed.", debug);
    return false;
}

std::string process_all(const std::vector<std::string> &files, s_config conf)
{
	std::string all_code = "";

	for (const auto &f : files)
	{
		std::ifstream ifs(f);
		if (!ifs.is_open())
		{
			write_debug("[GLOBAL] Could not open: " + f, conf.debug);
			continue;
		}
		std::stringstream buf;
		buf << ifs.rdbuf();
		size_t dot = f.find_last_of(".");
		std::string ext = (dot != std::string::npos) ? f.substr(dot) : "";
		std::string clean = strip_comments(buf.str(), ext);
		all_code += "// === FILE: " + f + " ===\n" + clean + "\n\n";
		write_debug("[GLOBAL] Added file: " + f + " (" + std::to_string(clean.size()) + " chars)", conf.debug);
	}

	write_debug("[GLOBAL] Total payload size: " + std::to_string(all_code.size()), conf.debug);
	std::string res = call_ai(all_code, conf);

	if (res.substr(0, 5) == "Error")
	{
		std::ofstream of("reports/error.md", std::ios::app);
		if (of.is_open()) of << "### Global analysis\n" << res << "\n---\n";
		return (RED "[FAIL] " RESET "Global analysis failed|");
	}

	std::string report_path = "reports/global.md";
	std::ofstream of(report_path);
	if (of.is_open())
		of << "# Global Review\n\n" << res;
	of.close();

	std::string display_path = (conf.format == "pdf") ? "reports/global.pdf" : "reports/global.md";
	return (GREEN "[OK]   " RESET "Global analysis (" + display_path + ")|" + report_path);
}
