/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   update.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mforest- <marvin@d42.fr>                   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/03/23 12:31:26 by mforest-          #+#    #+#             */
/*   Updated: 2026/03/23 12:31:26 by mforest-         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "ai_client.hpp"

void check_update()
{
	std::string cmd = "curl -s https://api.github.com/repos/realgetOff/AI_Reviewer/releases/latest | grep 'tag_name' | cut -d'\"' -f4 > /tmp/air_version 2>/dev/null";
    system(cmd.c_str());

	std::ifstream file("/tmp/air_version");
	std::string latest_ver;
	if (std::getline(file, latest_ver) && !latest_ver.empty())
	{
        if (latest_ver != CURRENT_VERSION)
		{
            std::cout << YELLOW << "\nNew version is available: " << BOLD << latest_ver << RESET;
            std::cout << YELLOW << " (Current: " << CURRENT_VERSION << ")" << RESET << std::endl;
            std::cout << "Run " << BOLD << "air -update" << RESET << " to upgrade.\n" << std::endl;
        }
    }
}
