#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <sstream>
#include <queue>
#include <mutex>
#include <boost/asio.hpp>
#include <thread>

#include "radiance.h"
#include "site_comm.h"
#include "config.h"
#include "logger.h"

using boost::asio::ip::tcp;

site_comm::site_comm() : t_active(false) {
	load_config();
}

void site_comm::load_config() {
	site_host = conf->get_str("site_host");
	site_path = conf->get_str("site_path");
	site_password = conf->get_str("site_password");
	readonly = conf->get_bool("readonly");
}

void site_comm::reload_config() {
	load_config();
}

bool site_comm::all_clear() {
	return (token_queue.empty());
}

void site_comm::expire_token(int torrent, int user) {
	std::stringstream token_pair;
	token_pair << user << ':' << torrent;
	if (expire_token_buffer != "") {
		expire_token_buffer += ",";
	}
	expire_token_buffer += token_pair.str();
	if (expire_token_buffer.length() > 350) {
		syslog(fatal) << "Flushing overloaded token buffer";
		if (!readonly) {
			std::lock_guard<std::mutex> lock(expire_queue_lock);
			token_queue.push(expire_token_buffer);
		}
		expire_token_buffer.clear();
	}
}

void site_comm::flush_tokens()
{
	if (readonly) {
		expire_token_buffer.clear();
		return;
	}
	std::lock_guard<std::mutex> lock(expire_queue_lock);
	size_t qsize = token_queue.size();
	if (qsize > 0) {
		syslog(trace) << "Token expire queue size: " << qsize;
	}
	if (expire_token_buffer == "") {
		return;
	}
	token_queue.push(expire_token_buffer);
	expire_token_buffer.clear();
	if (t_active == false) {
		std::thread thread(&site_comm::do_flush_tokens, this);
		thread.detach();
	}
}

void site_comm::do_flush_tokens()
{
	t_active = true;
	try {
		while (!token_queue.empty()) {
			boost::asio::io_service io_service;

			tcp::resolver resolver(io_service);
			tcp::resolver::query query(site_host, "http");
			tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
			tcp::resolver::iterator end;

			tcp::socket socket(io_service);
			boost::system::error_code error = boost::asio::error::host_not_found;
			while (error && endpoint_iterator != end) {
				socket.close();
				socket.connect(*endpoint_iterator++, error);
			}
			if (error) {
				throw boost::system::system_error(error);
			}

			boost::asio::streambuf request;
			std::ostream request_stream(&request);
			request_stream << "GET " << site_path << "/tools.php?key=" << site_password
				<< "&type=expiretoken&action=radiance&tokens=" << token_queue.front() << " HTTP/1.0\r\n"
				<< "Host: " << site_host << "\r\n"
				<< "Accept: */*\r\n"
				<< "Connection: close\r\n\r\n";

			boost::asio::write(socket, request);

			boost::asio::streambuf response;
			boost::asio::read_until(socket, response, "\r\n");

			std::istream response_stream(&response);
			std::string http_version;
			response_stream >> http_version;
			unsigned int status_code;
			response_stream >> status_code;
			std::string status_message;
			std::getline(response_stream, status_message);

			if (!response_stream || http_version.substr(0, 5) != "HTTP/") {
				syslog(fatal) << "Invalid response";
				continue;
			}

			if (status_code == 200) {
				std::lock_guard<std::mutex> lock(expire_queue_lock);
				token_queue.pop();
			} else {
				syslog(fatal) << "Response returned with status code " << status_code << " when trying to expire a token!";;
			}
		}
	} catch (std::exception &e) {
		syslog(fatal) << "Exception: " << e.what();
	}
	t_active = false;
}

site_comm::~site_comm()
{
}
