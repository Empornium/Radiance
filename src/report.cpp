#include <iostream>
#include <map>
#include <sstream>
#include "misc_functions.h"
#include "report.h"
#include "response.h"
#include "user.h"
#include "domain.h"

std::string report(params_type &params, user_list &users_list, domain_list &domains_list, client_opts_t &client_opts) {
	std::stringstream output;
	std::string action = params["get"];
	if (action == "") {
		output << "Invalid action\n";
	} else if (action == "stats") {
		time_t uptime = time(NULL) - stats.start_time;
		int up_d = uptime / 86400;
		uptime -= up_d * 86400;
		int up_h = uptime / 3600;
		uptime -= up_h * 3600;
		int up_m = uptime / 60;
		int up_s = uptime - up_m * 60;
		std::string up_ht = up_h <= 9 ? '0' + inttostr(up_h) : inttostr(up_h);
		std::string up_mt = up_m <= 9 ? '0' + inttostr(up_m) : inttostr(up_m);
		std::string up_st = up_s <= 9 ? '0' + inttostr(up_s) : inttostr(up_s);

		// JSON: C way, or "eat your greens",
		// R"()" denotes a "raw" string literal
		output << "{" << std::endl
		<< R"(  "uptime": )" << "\"" << up_d << " days, " << up_ht << ':' << up_mt << ':' << up_st << "\"" << ',' << std::endl
		<< R"(  "connections opened": )" << stats.opened_connections << ',' << std::endl
		<< R"(  "open connections": )" << stats.open_connections << ',' << std::endl
		<< R"(  "connections/s": )" << stats.connection_rate << ',' << std::endl
		<< R"(  "requests handled": )" << stats.requests << ',' << std::endl
		<< R"(  "requests/s": )" << stats.request_rate << ',' << std::endl
		<< R"(  "successful announcements": )" << stats.succ_announcements << ',' << std::endl
		<< R"(  "failed announcements": )" << (stats.announcements - stats.succ_announcements) << ',' << std::endl
		<< R"(  "scrapes": )" << stats.scrapes << ',' << std::endl
		<< R"(  "leechers tracked": )" << stats.leechers << ',' << std::endl
		<< R"(  "seeders tracked": )" << stats.seeders << ',' << std::endl
		<< R"(  "bytes read": )" << stats.bytes_read << ',' << std::endl
		<< R"(  "bytes written": )" << stats.bytes_written << ',' << std::endl
		<< R"(  "IPv4 peers": )" << stats.ipv4_peers << ','  << std::endl
		<< R"(  "IPv6 peers": )" << stats.ipv6_peers << std::endl
		<< "}" << std::endl;
	} else if (action == "db") {
		output << "{" << std::endl
		<< R"(  "torrent_queue": )" << stats.torrent_queue << ',' << std::endl
		<< R"(  "user_queue": )" << stats.user_queue << ',' << std::endl
		<< R"(  "peer_queue": )" << stats.peer_queue << ',' << std::endl
		<< R"(  "peer_hist_queue": )" << stats.peer_hist_queue << ',' << std::endl
		<< R"(  "snatch_queue": )" << stats.snatch_queue << ',' << std::endl
		<< R"(  "token_queue": )" << stats.token_queue << std::endl
		<< "}" << std::endl;
	} else if (action == "domain") {
		output << "{" << std::endl;
		auto domain = domains_list.begin();
		while (domain != domains_list.end()) {
			output << R"(  ")" << domain->first << R"(": )" << domain->second.use_count();
			if (++domain != domains_list.end()) output << ',';
			output << std::endl;
		}
		output << "}" << std::endl;
	} else if (action == "user") {
		std::string key = params["key"];
		if (key == "") {
			output << "Invalid action\n";
		} else {
			user_list::const_iterator u = users_list.find(key);
			if (u != users_list.end()) {
				output << "{" << std::endl
				<< R"(  "forbidden": )" << !u->second->can_leech() << ',' << std::endl
				<< R"(  "protected": )" << u->second->is_protected() << ',' << std::endl
				<< R"(  "track ipv6": )" << u->second->track_ipv6() << ',' << std::endl
				<< R"(  "personal freeleech": )" << u->second->pfl() << ',' << std::endl
				<< R"(  "personal doubleseed": )" << u->second->pds() << ',' << std::endl
				<< R"(  "leeching": )" << u->second->get_leeching() << ',' << std::endl
				<< R"(  "seeding": )" << u->second->get_seeding() << std::endl
				<< "}" << std::endl;
			}
		}
	} else {
		output << "Invalid action" << std::endl;
	}
	client_opts.json = true;
	return response(output.str(), client_opts, 200);
}
