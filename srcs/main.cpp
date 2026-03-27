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

int	main(int argc, char **argv)
{
	signal(SIGPIPE, SIG_IGN);

	s_config								conf = {false, false, false, false, "", "", "", "", "", "", "gemini", "", "md", 0, 5, 10};
	std::vector<std::string>				files;
	std::vector<std::string>				results;
	std::vector<std::future<std::string>>	futures;
	int										opt;

	check_update();
	if (argc > 1)
		if (check_commands(argc, argv) != 2)
			return (0);
	while ((opt = getopt(argc, argv, "t:i:I:agdhms:l:f:c:")) != -1)
	{
		if (opt == 'I')
		{
    		if (!optarg)
    		{
    		    std::cerr << RED << "Error: -I requires a value" << RESET << std::endl;
    		    return (1);
    		}
    		conf.interactive_timeout = std::atoi(optarg);
    		if (conf.interactive_timeout <= 0)
    		{
        		std::cerr << RED << "Error: -I must be > 0" << RESET << std::endl;
        		return (1);
    		}
		}
		if (opt == 'h')
			display_help();
		if (opt == 'g')
			conf.global = true;
		if (opt == 'a')
			conf.agent = true;
		if (opt == 'c')
			conf.custom_prompt = optarg;
		if (opt == 'i')
		{
			if (!optarg)
			{
				std::cerr << RED << "Error: -i requires a value" << RESET << std::endl;
				return (1);
			}
			conf.max_iter = std::atoi(optarg);
			if (conf.max_iter <= 0)
			{
				std::cerr << RED << "Error: -i must be > 0" << RESET << std::endl;
				return (1);
			}
		}
		if (opt == 'm')
		{
			load_config(conf);
			list_provider_models(conf);
			return (0);
		}
		if (opt == 't')
		{
			if (!optarg)
			{
				std::cerr << RED << "Error: -t requires a value" << RESET << std::endl;
				return (1);
			}
			conf.timeout = std::atoi(optarg);
			if (conf.timeout <= 0)
			{
				std::cerr << RED << "Error: timeout must be > 0" << RESET << std::endl;
				return (1);
			}
		}
		if (opt == 's')
			conf.style = optarg;
		if (opt == 'l')
			conf.lang = optarg;
		if (opt == 'd')
			conf.debug = true;
		if (opt == 'f')
		{
			std::string new_fmt = optarg;
			if (new_fmt == "pdf" || new_fmt == "md")
			{
				update_config_file("format", new_fmt);
				std::cout << GREEN << "Default format updated to: " << new_fmt << RESET << std::endl;
				return (0);
			}
			else
			{
				std::cerr << RED << "Error: format must be 'pdf' or 'md'" << RESET << std::endl;
				return (1);
			}
		}
	}
	load_config(conf);
	if (!conf.custom_prompt.empty())
		conf.prompt = conf.custom_prompt;
	if (conf.agent)
	{
		mkdir("reports", 0777);
		run_agent(files, conf);
		return (0);
	}
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
	if (conf.global)
	{
		std::cout << YELLOW << "Analyzing: " << RESET << "[global mode]" << std::flush;
		std::string result = process_all(files, conf);
		std::cout << "\r" << std::string(50, ' ') << "\r";
		std::cout << "\n" << BOLD << "Final Results:" << RESET << std::endl;
		std::string display = result;
		std::string md_path = "";
		size_t sep = result.rfind('|');
		if (sep != std::string::npos)
		{
			display = result.substr(0, sep);
			md_path  = result.substr(sep + 1);
		}
		std::cout << display << std::endl;
		if (conf.format == "pdf" && !md_path.empty())
			save_as_pdf(md_path, conf.debug);
		return (0);
	}
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
	{
		std::string display = r;
		std::string md_path = "";
		size_t sep = r.rfind('|');
		if (sep != std::string::npos)
		{
			display = r.substr(0, sep);
			md_path  = r.substr(sep + 1);
		}
		std::cout << display << std::endl;
		if (conf.format == "pdf" && !md_path.empty())
			save_as_pdf(md_path, conf.debug);
	}
	return (0);
}
