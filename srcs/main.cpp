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

	s_config								conf = {false, true, false, "", "", "", "", "", "gemini", ""};
	std::vector<std::string>				files;
	std::vector<std::string>				results;
	std::vector<std::future<std::string>>	futures;
	int										opt;

	check_update();

	if (argc > 1)
		if(check_commands(argv[1]) != 2)
			return(0);

	std::cout << "Hello nice update" << std::endl;
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
