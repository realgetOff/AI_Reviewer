/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ai_client.cpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mforest- <marvin@d42.fr>                   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/03/22 21:47:31 by mforest-          #+#    #+#             */
/*   Updated: 2026/03/22 21:47:31 by mforest-         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "ai_client.hpp"

size_t	write_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
	size_t	realsize;

	realsize = size * nmemb;
	((std::string *)data)->append((char *)ptr, realsize);
	return (realsize);
}

void	list_provider_models(const s_config &config)
{
	CURL				*curl;
	struct curl_slist	*headers;
	std::string			resp;
	std::string			url;

	curl = curl_easy_init();
	headers = NULL;
	if (config.model_type == "gemini")
	{
		url = "https://generativelanguage.googleapis.com/v1beta/models?key=" + config.api_key;
	}
	else if (config.model_type == "openai")
		url = "https://api.openai.com/v1/models";
	else if (config.model_type == "claude")
		url = "https://api.anthropic.com/v1/models";
	else
		return ;
	if (config.model_type == "openai" || config.model_type == "mistral")
	{
		headers = curl_slist_append(headers, ("Authorization: Bearer " + config.api_key).c_str());
	}
	else if (config.model_type == "claude")
	{
		headers = curl_slist_append(headers, ("x-api-key: " + config.api_key).c_str());
		headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
	}
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
	if (curl_easy_perform(curl) == CURLE_OK)
	{
		try
		{
			auto j = json::parse(resp);
			if (config.model_type == "gemini")
			{
				for (auto& m : j["models"])
					std::cout << " - " << m["name"].get<std::string>() << std::endl;
			}
			else
			{
				for (auto& m : j["data"])
					std::cout << " - " << m["id"].get<std::string>() << std::endl;
			}
		}
		catch (...)
		{
			std::cout << RED << "API Error: " << resp << RESET << std::endl;
		}
	}
	curl_easy_cleanup(curl);
	curl_slist_free_all(headers);
}

static std::string	build_payload(const s_config &conf, const std::string &code)
{
	std::string	p;

	p = conf.prompt + "\n\nCode:\n" + code;
	if (conf.model_type == "gemini")
		return (json{{"contents", {{{"parts", {{{"text", p}}}}}}}}.dump());
	if (conf.model_type == "openai" || conf.model_type == "mistral")
		return (json{{"model", conf.model_name}, {"messages", {{{"role", "user"}, {"content", p}}}}}.dump());
	if (conf.model_type == "claude")
		return (json{{"model", conf.model_name}, {"max_tokens", 1024}, {"messages", {{{"role", "user"}, {"content", p}}}}}.dump());
	return ("");
}

std::string call_ai(const std::string &code, const s_config &config)
{
    CURL                *curl;
    struct curl_slist   *headers;
    std::string         resp;
    std::string         url;
    long                code_http;

    curl = curl_easy_init();
    if (!curl)
    {
        write_debug("[AI] ERROR: curl_easy_init() failed", config.debug);
        return ("Error: curl init failed");
    }
    headers = NULL;
    url = config.api_url;
    code_http = 0;

    write_debug("[AI] model_type: " + config.model_type, config.debug);
    write_debug("[AI] api_url: " + config.api_url, config.debug);
    write_debug("[AI] model_name: " + config.model_name, config.debug);
    write_debug("[AI] api_key empty: " + std::string(config.api_key.empty() ? "YES" : "NO"), config.debug);
    write_debug("[AI] prompt: " + config.prompt.substr(0, 80) + "...", config.debug);

    if (config.model_type == "gemini")
        url += "?key=" + config.api_key;

    std::string payload = build_payload(config, code);
    write_debug("[AI] payload size: " + std::to_string(payload.size()), config.debug);
    write_debug("[AI] payload preview: " + payload.substr(0, 120) + "...", config.debug);

    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (config.model_type == "openai" || config.model_type == "mistral")
        headers = curl_slist_append(headers, ("Authorization: Bearer " + config.api_key).c_str());
    else if (config.model_type == "claude")
    {
        headers = curl_slist_append(headers, ("x-api-key: " + config.api_key).c_str());
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
	if (config.timeout > 0)
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)config.timeout);
	else
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
	{
		std::string tinfo;
		if (config.timeout > 0)
			tinfo = std::to_string(config.timeout) + "s";
		else
			tinfo = "none (agent)";
		write_debug("[AI] timeout: " + tinfo, config.debug);
	}

    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code_http);

    write_debug("[AI] CURLcode: " + std::to_string(res), config.debug);
    write_debug("[AI] HTTP code: " + std::to_string(code_http), config.debug);
    write_debug("[AI] response size: " + std::to_string(resp.size()), config.debug);
    write_debug("[AI] response preview: " + (resp.size() > 200 ? resp.substr(0, 200) : resp), config.debug);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK)
    {
        write_debug("[AI] CURL error: " + std::string(curl_easy_strerror(res)), config.debug);
        return ("Error: CURL " + std::string(curl_easy_strerror(res)));
    }
    if (code_http != 200)
    {
        write_debug("[AI] HTTP error body: " + resp, config.debug);
        return ("Error: " + std::to_string(code_http) + " | Raw: " + resp);
    }

    try
    {
        auto j = json::parse(resp);
        if (config.model_type == "gemini")
            return (j["candidates"][0]["content"]["parts"][0]["text"]);
        if (config.model_type == "claude")
            return (j["content"][0]["text"]);
        return (j["choices"][0]["message"]["content"]);
    }
    catch (const std::exception& e)
    {
        write_debug("[AI] JSON parse error: " + std::string(e.what()), config.debug);
        return ("Error: Parse failed | Raw: " + resp);
    }
}
