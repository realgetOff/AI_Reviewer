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
	s_config								conf = {false, true, "", "", "", "", "", "", "gemini", "md"};
	std::vector<std::string>				files;
	std::vector<std::string>				results;
	std::vector<std::future<std::string>>	futures;
	int										opt;

	check_update();

	if (argc > 1)
		if(check_commands(argv[1]) != 2)
			return(0);

	while ((opt = getopt(argc, argv, "dhms:l:f:")) != -1)
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
	{
	    std::string display = r;
	    std::string md_path = "";

	    size_t sep = r.rfind('|');
	    if (sep != std::string::npos)
	    {
	        display = r.substr(0, sep);
	        md_path = r.substr(sep + 1);
	    }

	    std::cout << display << std::endl;

	    if (conf.format == "pdf" && !md_path.empty())
	        save_as_pdf(md_path, conf.debug);
	}

    return (0);
}
