/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ai_client.hpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mforest- <marvin@d42.fr>                   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/03/22 19:19:45 by mforest-          #+#    #+#             */
/*   Updated: 2026/03/22 19:19:45 by mforest-         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef AI_CLIENT_HPP
#define AI_CLIENT_HPP

#define CURRENT_VERSION "v1.0.6"

#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[93m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <iostream>
#include <future>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <algorithm>
#include <cstdlib>
#include <curl/curl.h>

using json = nlohmann::json;

struct s_config
{
	bool verbose;
	bool save_md;
	bool debug;

	std::string	style;
	std::string	lang;
	std::string	prompt;
	std::string	api_key;
	std::string	api_url;
	std::string model_type;
	std::string model_name;
};

void		check_update(void);
void	    display_help(void);
std::string	strip_comments(const std::string &code, const std::string &ext);
void		load_config(s_config &conf);
void		display_progress(size_t cur, size_t tot);
void		scan_path(const std::string &p, std::vector<std::string> &files);
void		write_debug(const std::string &msg, bool enabled);
int     	check_commands(std::string command);
std::string	process_file(std::string f, s_config conf);

void 		list_provider_models(const s_config &config);
std::string	call_ai(const std::string &code, const s_config &config);

#endif
