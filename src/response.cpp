#include <sstream>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>

#include "../autoconf.h"
#include "response.h"
#include "misc_functions.h"

const std::string response(const std::string &body, client_opts_t &client_opts, uint16_t response) {
	std::string out;
	bool processed = false;
	if (client_opts.html) {
		out = "<html><head><meta name=\"robots\" content=\"noindex, nofollow\" /></head><body>" + body + "</body></html>";
		processed = true;
	}
	if (client_opts.gzip) {
		std::stringstream ss, zss;
		ss << body;
		boost::iostreams::filtering_streambuf<boost::iostreams::input> in;
		in.push(boost::iostreams::gzip_compressor());
		in.push(ss);
		boost::iostreams::copy(in, zss);
		out = zss.str();
		processed = true;
	}

	if (processed) {
		return response_head(out.length(), client_opts, response) + out;
	}
	return response_head(body.length(), client_opts, response) + body;
}


const std::string get_reason(uint16_t response) {
	switch(response) {
		case 100: return "Invalid request type: client request was not a HTTP GET."; break;
		case 101: return "Missing info_hash."; break;
		case 102: return "Missing peer_id."; break;
		case 103: return "Missing port."; break;
		case 150: return "Invalid infohash: infohash is not 20 bytes long."; break;
		case 151: return "Invalid peerid: peerid is not 20 bytes long."; break;
		case 152: return "Invalid numwant. Client requested more peers than allowed by tracker."; break;
		case 200: return "info_hash not found in the database. Sent only by trackers that do not automatically include new hashes into the database."; break;
		case 500: return "Client sent an eventless request before the specified time."; break;
		default:  return "Generic Error";
	}
}
const std::string response_head(size_t content_length, client_opts_t &client_opts, uint16_t response) {
	std::string content_type = "text/plain";
	std::string reason = "OK";
	content_type = client_opts.html ? "text/html" : content_type;
	content_type = client_opts.json ? "application/json" : content_type;
	if (response == 900) reason = get_reason(response);
	std::string head = "HTTP/1.1 " + std::to_string(response) + " " + reason +"\r\nServer: Radiance "+PACKAGE_VERSION;
	head += "\r\nContent-Type: " + content_type;
	if (client_opts.gzip) {
		head += "\r\nContent-Encoding: gzip";
	}
	if (client_opts.http_close) {
		head += "\r\nConnection: Close";
	}
	head += "\r\nContent-Length: " + inttostr(content_length) + "\r\n\r\n";
	return head;
}

const std::string response_error(const std::string &err, client_opts_t &client_opts) {
	return response("d14:failure reason" + inttostr(err.length()) + ':' + err + "12:min intervali5400e8:intervali5400ee", client_opts, 200);
}

const std::string response_warning(const std::string &msg) {
	return "15:warning message" + inttostr(msg.length()) + ':' + msg;
}
