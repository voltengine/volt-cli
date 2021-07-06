#pragma once

#include "pch.hpp"

namespace util {

class http {
public:
	using headers = std::unordered_map<std::string, std::string>;
	using buffer = std::vector<uint8_t>;
	using error = std::runtime_error;

	enum class version : long {
		http10 = CURL_HTTP_VERSION_1_0,
		http11 = CURL_HTTP_VERSION_1_1,
		http20 = CURL_HTTP_VERSION_2_0,
		http30 = CURL_HTTP_VERSION_3
	};

	struct method {
		static constexpr char
				    get[] = "GET",
				   head[] = "HEAD",
				options[] = "OPTIONS",
				   post[] = "POST",
					put[] = "PUT";
	};

	struct response {
		uint32_t code = 0;
		headers headers;
	};

	static constexpr uint32_t buffer_size = 16 * 1024;

	http() noexcept;

	~http() noexcept;

	void send();

	void set_version(version version) noexcept;

	void set_method(std::string_view method) noexcept;

	void set_url(std::string_view url) noexcept;

	void set_header(const std::string &name, std::string_view value);

	void set_certificate(const std::filesystem::path &path);

	void set_timeout(const std::chrono::seconds &duration) noexcept;

	void on_response(std::function<void(const response &)> &&callback) noexcept;

	void on_data(std::function<void(const buffer &)> &&callback) noexcept;

private:
	CURL *handle;
	headers request_headers;

	std::optional<std::function<void(const response &)>> response_callback;
	std::optional<std::function<void(const buffer &)>> data_callback;

	response tmp_response;
	buffer tmp_buffer;

	static size_t curl_header_function(char *buffer, size_t size, size_t nitems, void *userdata);

	static size_t curl_write_function(char *ptr, size_t size, size_t nmemb, void *userdata);
};

}