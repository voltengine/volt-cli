#include "common.hpp"

#include "util/file.hpp"
#include "util/http.hpp"
#include "util/system.hpp"

namespace fs = std::filesystem;
namespace tc = termcolor;
using namespace util;

std::vector<std::string> anim_frames {
	"_",".","o","O","^","*","-","."
};

namespace common {

std::string prepend_default_scope(std::string_view id) {
	json config = json::parse(util::read_file(
			std::getenv("VOLT_PATH") / fs::path("config.json")));

	if (id.find('/') == std::string::npos)
		return config["defaultScope"].as<json::string>() + '/' + id.data();
	else
		return std::string(id);
}

json find_manifest_in_archives(std::string id) {
	static const std::regex id_validator(
			"(?=^.{1,39}\\/.{1,64}$)^([a-z\\d]+(-[a-z\\d]+)*)\\/"
			"([a-z][a-z\\d]*(-[a-z\\d]+)*)$");

	if (!std::regex_match(id, id_validator))
		throw std::runtime_error("Invalid package ID.");

	json manifest;

	json::object archives = json::parse(util::read_file(
			std::getenv("VOLT_PATH") / fs::path("config.json"))
			)["archives"];

	for (auto &archive : archives) {
		std::string url = archive.first;
		if (url.back() != '/')
			url += '/';

		std::cout << "Checking at \"" << url << "\"..." << std::endl;

		url += "package/" + id + '/';
		try {
			manifest = json::parse(util::download(url));
			break;
		} catch (std::exception &e) {
			std::cout << tc::bright_yellow
					  << "Not found: " << e.what() << '\n'
					  << tc::reset;
		}
	}

	if (manifest.is<json::null>())
		throw std::runtime_error("Package not found in archives.");

	return manifest;
}

std::string select_archive() {
	fs::path volt_path = std::getenv("VOLT_PATH");
	fs::path config_path = volt_path / "config.json";

	json config = json::parse(util::read_file(config_path));
	json::object &archives = config["archives"];

	if (archives.size() == 0)
		throw std::runtime_error("Archive list is empty.");

	size_t archive_index = 0;
	if (archives.size() != 1) {
		std::cout << tc::bright_green
				  << "Multiple archives are available:\n"
				  << tc::reset;

		for (auto it = archives.begin(); it != archives.end(); it++) {
			std::cout << tc::bright_green << '['
					  << tc::reset << std::distance(archives.begin(), it)
					  << tc::bright_green << "]: "
					  << tc::reset << it->first
					  << '\n';
		}

		while (true) {
			std::cout << tc::bright_green
					  << "Selection: "
					  << tc::reset;

			std::string line;
			std::getline(std::cin, line);

			try {
				archive_index = std::stoull(line);
				if (archive_index >= archives.size())
					throw std::out_of_range("");
				break;
			} catch (std::logic_error) {
				std::cout << tc::bright_red
						  << "Invalid input.\n"
						  << tc::reset;
			}
		}

		std::cout << '\n';
	}

	auto it = archives.begin();
	std::advance(it, archive_index);
	std::string url = it->first;
	if (url.back() != '/')
		url += '/';

	return url;
}

std::string get_cached_token(const std::string &archive_url) {
	fs::path volt_path = std::getenv("VOLT_PATH");
	fs::path config_path = volt_path / "config.json";

	json config = json::parse(util::read_file(config_path));
	return config["archives"][archive_url];
}

json get_user_info(const std::string &token) {
	fs::path volt_path = std::getenv("VOLT_PATH");
	fs::path cert_path = volt_path / "cacert.pem";

	if (token.empty())
		return json();

	std::string buffer;
	json response;
	http request;

	request.set_certificate(cert_path);
	request.set_method(http::method::get);
	request.set_url("https://api.github.com/user");
	request.set_header("Accept", "application/vnd.github.v3+json");
	request.set_header("Authorization", "Bearer " + token);
	request.on_response([](const auto &response) {
		if (response.status != 200 && response.status != 401) {
			throw http::error("Remote returned " +
					std::to_string(response.status) + ".");
		}
	});
	request.on_data([&buffer](const auto &data) {
		buffer.insert(buffer.end(), data.begin(), data.end());
	});

	request.send();
	response = json::parse(buffer);
	buffer.clear();

	if (response.contains("login"))
		return response;

	return json();
}
 
authorization_result authorize(const std::string &archive_url) {
	fs::path volt_path = std::getenv("VOLT_PATH");
	fs::path config_path = volt_path / "config.json";
	fs::path cert_path = volt_path / "cacert.pem";

	std::cout << "Fetching client ID...\n";

	std::string client_id = util::download(archive_url + "auth/id/");

	std::string buffer;
	json response;
	http request;

	request.set_certificate(cert_path);
	request.set_method(http::method::post);
	request.set_header("Content-Type", "application/x-www-form-urlencoded");
	request.set_header("Accept", "application/vnd.github.v3+json");
	request.on_response([](const auto &response) {
		if (response.status != 200) {
			throw http::error("Remote returned " +
					std::to_string(response.status) + ".");
		}
	});
	request.on_data([&buffer](const auto &data) {
		buffer.insert(buffer.end(), data.begin(), data.end());
	});

	request.set_url("https://github.com/login/device/code");
	request.set_body("scope=read:org&client_id=" + client_id);

	request.send();
	response = json::parse(buffer);
	buffer.clear();

	std::string device_code = response["device_code"];
	uint32_t expires_in = response["expires_in"];
	uint32_t interval = response["interval"] + 1;
	std::string user_code = response["user_code"];
	std::string verification_uri = response["verification_uri"];

	std::cout << "\nPlease enter your code at verification URL:"
			  << tc::bright_green
			  << "\nCode: " << tc::reset << user_code
			  << tc::bright_green
			  << "\nURL: " << tc::reset << verification_uri
			  << tc::bright_green
			  << "\n\nCode will remain valid for the next "
			  << std::round(expires_in / 60.0) << " minutes."
			  << tc::reset << "\n\n";
	
	request.set_url("https://github.com/login/oauth/access_token");
	request.set_body("client_id=" + client_id
			+ "&device_code=" + device_code
			+ "&grant_type=urn:ietf:params:oauth:grant-type:device_code");

	authorization_result result;

	util::show_terminal_cursor(false);
	try {
		uint32_t seconds = 0, frame = -1;
		auto start_time = std::chrono::system_clock::now();
		auto last_checked = start_time;
		bool opened_browser = false;
		while (true) {
			std::cout << tc::bright_green
					  << anim_frames[frame = (frame + 1) % anim_frames.size()]
					  << tc::reset << " Waiting for authorization...\r";

			std::this_thread::sleep_for(std::chrono::milliseconds(100));

			auto now = std::chrono::system_clock::now();
			uint32_t seconds_from_start = std::chrono::duration_cast<std::
					chrono::seconds>(now - start_time).count();
			
			if (!opened_browser && seconds_from_start > 1) {
				util::open_browser(verification_uri);
				opened_browser = true;
			}

			if (seconds_from_start > expires_in)
				throw std::runtime_error("Code has expired.");

			if (std::chrono::duration_cast<std::chrono::seconds>(
					now - last_checked).count() < interval)
				continue;
			last_checked = now;

			request.send();
			response = json::parse(buffer);
			buffer.clear();

			if (response.contains("access_token"))
				break;
			
			if (response.contains("interval"))
				interval = response["interval"] + 1;
		}

		result.token = response["access_token"].as<json::string>();
		result.user = get_user_info(result.token);

		if (result.user.is<json::null>())
			throw std::runtime_error("Received invalid token.");

		std::cout << "Authorized as "
			      << tc::bright_green
				  << result.user["login"].as<json::string>()
			      << tc::reset << ".               \n";
	} catch (std::exception &e) {
		std::cout << "Authorization failed.         \n";
		util::show_terminal_cursor(true);
		throw e;
	}
	util::show_terminal_cursor(true);

	json config = json::parse(util::read_file(config_path));
	config["archives"][archive_url] = result.token;
	util::write_file(config_path, util::to_string(config));

	std::cout << tc::bright_green << "\nFile has been written:\n"
			  << tc::reset << config_path.string() << '\n';

	return result;
}

}