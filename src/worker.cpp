#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <iostream>
#include <string>
#include <map>
#include <sstream>
#include <list>
#include <vector>
#include <set>
#include <algorithm>
#include <mutex>
#include <thread>

#include "../autoconf.h"
#include "config.h"
#include "worker.h"
#include "database.h"
#include "site_comm.h"
#include "misc_functions.h"
#include "response.h"
#include "report.h"
#include "user.h"
#include "domain.h"
#include "logger.h"

//---------- Worker - does stuff with input
worker::worker(torrent_list &torrents, user_list &users, domain_list &domains, std::vector<std::string> &_blacklist, database * db_obj, site_comm * sc) :
	db(db_obj), s_comm(sc), torrents_list(torrents), users_list(users), domains_list(domains), blacklist(_blacklist), status(OPEN), reaper_active(false)
{
	load_config();
}

void worker::load_config() {
	announce_interval = conf->get_uint("announce_interval");
	del_reason_lifetime = conf->get_uint("del_reason_lifetime");
	peers_timeout = conf->get_uint("peers_timeout");
	numwant_limit = conf->get_uint("numwant_limit");
	keepalive_enabled = conf->get_uint("keepalive_timeout") != 0;
	site_password = conf->get_str("site_password");
	report_password = conf->get_str("report_password");
}

void worker::reload_config() {
	load_config();
}

void worker::reload_lists() {
	status = PAUSED;
	db->load_site_options();
	db->load_users(users_list);
	db->load_torrents(torrents_list);
	for (auto const &user: users_list) {
		// Reset user stats
		user.second->reset_stats();
	}
	stats.seeders  = 0;
	stats.leechers = 0;
	db->load_peers(torrents_list, users_list);
	db->load_blacklist(blacklist);
	status = OPEN;
}

bool worker::shutdown() {
	if (status == OPEN) {
		status = CLOSING;
		while(reaper_active) {
			sleep(1);
		}
		torrents_list.clear();
		users_list.clear();
		domains_list.clear();

		delete &torrents_list;
		delete &users_list;
		delete &domains_list;
		syslog(info) << "flushing DB buffers... press Ctrl-C again to terminate immediately";
		return false;
	} else if (status == CLOSING) {
		syslog(info) << "shutting down uncleanly";
		return true;
	} else {
		return false;
	}
}

std::string worker::work(const std::string &input, std::string &ip, uint16_t &ip_ver, client_opts_t &client_opts) {
	unsigned int input_length = input.length();

	//---------- Parse request - ugly but fast. Using substr exploded.
	if (input_length < 60) { // Way too short to be anything useful
		return response_error("GET string too short", client_opts);
	}

	size_t pos = 5; // skip 'GET /'

	// Get the passkey
	std::string passkey;
	passkey.reserve(32);
	if (input[37] != '/') {
		// just handle robots.txt if annouce is malformed.
		// robots.txt requested?
		if(input[11] == '.')
			 return "User-agent: *\nDisallow: /";
		return response_error("Malformed announce", client_opts);
	}

	for (; pos < 37; pos++) {
		passkey.push_back(input[pos]);
	}

	pos = 38;

	// Get the action
	enum action_t {
		INVALID = 0, ANNOUNCE, SCRAPE, UPDATE, REPORT
	};
	action_t action = INVALID;

	switch (input[pos]) {
		case 'a':
			stats.announcements++;
			action = ANNOUNCE;
			pos += 8;
			break;
		case 's':
			stats.scrapes++;
			action = SCRAPE;
			pos += 6;
			break;
		case 'u':
			action = UPDATE;
			pos += 6;
			break;
		case 'r':
			action = REPORT;
			pos += 6;
			break;
	}

	if (input[pos] != '?') {
		// No parameters given. Probably means we're not talking to a torrent client
		client_opts.html = true;
		return response("Tracker is running", client_opts, 200);
	}

	// Parse URL params
	std::list<std::string> infohashes; // For scrape only

	params_type params;
	std::string key;
	std::string value;
	bool parsing_key = true; // true = key, false = value

	++pos; // Skip the '?'
	for (; pos < input_length; ++pos) {
		if (input[pos] == '=') {
			parsing_key = false;
		} else if (input[pos] == '&' || input[pos] == ' ') {
			parsing_key = true;
			if (action == SCRAPE && key == "info_hash") {
				infohashes.push_back(value);
			} else {
				params[key] = value;
			}
			key.clear();
			value.clear();
			if (input[pos] == ' ') {
				break;
			}
		} else {
			if (parsing_key) {
				key.push_back(input[pos]);
			} else {
				value.push_back(input[pos]);
			}
		}
	}
	++pos;

	if (input.compare(pos, 5, "HTTP/") != 0) {
		return response_error("Malformed HTTP request", client_opts);
	}

	std::string http_version;
	pos += 5;
	while (input[pos] != '\r' && input[pos] != '\n') {
		http_version.push_back(input[pos]);
		++pos;
	}
	++pos; // skip line break - should probably be += 2, but just in case a client doesn't send \r

	// Parse headers
	params_type headers;
	parsing_key = true;
	bool found_data = false;

	for (; pos < input_length; ++pos) {
		if (unlikely(std::strncmp(&input[pos], ": ",  2) == 0)) { // Look for `: "` explicitly
			parsing_key = false;
			++pos; // skip space after :
		} else if (input[pos] == '\n' || input[pos] == '\r') {
			parsing_key = true;

			if (found_data) {
				found_data = false; // dodge for getting around \r\n or just \n
				std::transform(key.begin(), key.end(), key.begin(), ::tolower);
				headers[key] = value;
				key.clear();
				value.clear();
			}
		} else {
			found_data = true;
			if (parsing_key) {
				key.push_back(input[pos]);
			} else {
				value.push_back(input[pos]);
			}
		}
	}

	if (keepalive_enabled) {
		auto hdr_http_close = headers.find("connection");
		if (hdr_http_close == headers.end()) {
			client_opts.http_close = (http_version == "1.0");
		} else {
			client_opts.http_close = (hdr_http_close->second != "Keep-Alive");
		}
	} else {
		client_opts.http_close = true;
	}

	if (status != OPEN) {
		return response_error("The tracker is temporarily unavailable.", client_opts);
	}

	if (action == INVALID) {
		return response_error("Invalid action", client_opts);
	}

	if (action == UPDATE) {
		if (passkey == site_password) {
			return update(params, client_opts);
		} else {
			return response_error("Authentication failure", client_opts);
		}
	}

	if (action == REPORT) {
		if (passkey == report_password) {
			std::lock_guard<std::mutex> ul_lock(db->user_list_mutex);
			return report(params, users_list, domains_list, client_opts);
		} else {
			return response_error("Authentication failure", client_opts);
		}
	}

	// Either a scrape or an announce

	std::unique_lock<std::mutex> ul_lock(db->user_list_mutex);
	auto user_it = users_list.find(passkey);
	if (user_it == users_list.end()) {
		syslog(trace) << "Passkey not found " << passkey;
		return response_error("Passkey not found", client_opts);
	}
	user_ptr u = user_it->second;
	ul_lock.unlock();

	if (action == ANNOUNCE) {
		std::string host = get_host(headers);
		std::unique_lock<std::mutex> dl_lock(db->domain_list_mutex);
		auto domain_it = domains_list.find(host);
		if (domain_it == domains_list.end()) {
			domain_ptr tmp_domain = std::make_shared<domain>(host);
			domains_list.insert(std::pair<std::string, domain_ptr>(host, tmp_domain));
			domain_it = domains_list.find(host);
		}
		domain_ptr d = domain_it->second;
		dl_lock.unlock();

		// Let's translate the infohash into something nice
		// info_hash is a url encoded (hex) base 20 number
		std::string info_hash_decoded = hex_decode(params["info_hash"]);
		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		auto tor = torrents_list.find(info_hash_decoded);
		if (tor == torrents_list.end()) {
			std::lock_guard<std::mutex> dr_lock(del_reasons_lock);
			auto msg = del_reasons.find(info_hash_decoded);
			if (msg != del_reasons.end()) {
				if (msg->second.reason != -1) {
					return response_error("Unregistered torrent: " + get_del_reason(msg->second.reason), client_opts);
				} else {
					return response_error("Unregistered torrent", client_opts);
				}
			} else {
				return response_error("Unregistered torrent", client_opts);
			}
		}
		return announce(input, tor->second, u, d, params, headers, ip, ip_ver, client_opts);
	} else {
		return scrape(infohashes, headers, client_opts);
	}
}

std::string worker::announce(const std::string &input, torrent &tor, user_ptr &u, domain_ptr &d, params_type &params, params_type &headers, std::string &ip, uint16_t &ip_ver, client_opts_t &client_opts) {
	cur_time = time(NULL);

	if (params["compact"] != "1") {
		return response_error("Your client does not support compact announces", client_opts);
	}

	int64_t left = std::max((int64_t)0, strtoint64(params["left"]));
	int64_t uploaded = std::max((int64_t)0, strtoint64(params["uploaded"]));
	int64_t downloaded = std::max((int64_t)0, strtoint64(params["downloaded"]));
	int64_t corrupt = std::max((int64_t)0, strtoint64(params["corrupt"]));

	int snatched = 0; // This is the value that gets sent to the database on a snatch
	int active = 1; // This is the value that marks a peer as active/inactive in the database
	bool inserted = false; // If we insert the peer as opposed to update
	bool update_torrent = false; // Whether or not we should update the torrent in the DB
	bool completed_torrent = false; // Whether or not the current announcement is a snatch
	bool stopped_torrent = false; // Was the torrent just stopped?
	bool paused_torrent = false; // Has the peer stopped downloading?
	bool expire_token = false; // Whether or not to expire a token after torrent completion
	bool peer_changed = false; // Whether or not the peer is new or has changed since the last announcement
	bool inc_l = false, inc_s = false, dec_l = false, dec_s = false;
	bool sitewide_freeleech = false;
	bool sitewide_doubleseed = false;
	userid_t userid = u->get_id();
	std::string ipv4 = "", ipv6 = "", public_ipv4 = "", public_ipv6 = "";

	if (ip_ver == 4){
		ipv4=ip;
		public_ipv4=ip;
	} else {
		ipv6=ip;
		public_ipv6=ip;
	}

	time_t now;
	time(&now);

	if (((opts->get_time("SitewideFreeleechStartTime") <= now) &&
	     (opts->get_time("SitewideFreeleechEndTime")   >= now) &&
		 (opts->get_str("SitewideFreeleechMode")   == "timed")) ||
		 (opts->get_str("SitewideFreeleechMode")   == "perma")) {
		sitewide_freeleech = true;
	}

	if (((opts->get_time("SitewideDoubleseedStartTime") <= now) &&
	     (opts->get_time("SitewideDoubleseedEndTime")   >= now) &&
		 (opts->get_str("SitewideDoubleseedMode")   == "timed")) ||
		 (opts->get_str("SitewideDoubleseedMode")   == "perma")) {
		sitewide_doubleseed = true;
	}

	params_type::const_iterator peer_id_iterator = params.find("peer_id");
	if (peer_id_iterator == params.end()) {
		return response_error("No peer ID", client_opts);
	}
	const std::string peer_id = hex_decode(peer_id_iterator->second);
	if (peer_id.length() != 20) {
		return response_error("Invalid peer ID", client_opts);
	}

	std::unique_lock<std::mutex> wl_lock(db->blacklist_mutex);
	if (blacklist.size() > 0) {
		bool found = false; // Found client in blacklist?
		for (unsigned int i = 0; i < blacklist.size(); i++) {
			if (peer_id.compare(0, blacklist[i].length(), blacklist[i]) == 0) {
				found = true;
				break;
			}
		}
		if (found) {
			return response_error("Your client is blacklisted!", client_opts);
		}
	}
	wl_lock.unlock();

	std::stringstream peer_key_stream;
	peer_key_stream << peer_id[12 + (tor.id & 7)] // "Randomize" the element order in the peer map by prefixing with a peer id byte
		<< userid // Include user id in the key to lower chance of peer id collisions
		<< peer_id;
	const std::string peer_key(peer_key_stream.str());

	if (params["event"] == "completed") {
		// Don't update <snatched> here as we may decide to use other conditions later on
		completed_torrent = (left == 0); // Sanity check just to be extra safe
	} else if (params["event"] == "stopped") {
		stopped_torrent = true;
		peer_changed = true;
		update_torrent = true;
		active = 0;
	}
	peer * p;
	peer_list::iterator peer_it;
	// Insert/find the peer in the torrent list
	if (left > 0) {
		peer_it = tor.leechers.find(peer_key);
		if (peer_it == tor.leechers.end()) {
			// We could search the seed list as well, but the peer reaper will sort things out eventually
			peer_it = add_peer(tor.leechers, peer_key);
			inserted = true;
			inc_l = true;
		}
	} else if (completed_torrent) {
		peer_it = tor.leechers.find(peer_key);
		if (peer_it == tor.leechers.end()) {
			peer_it = tor.seeders.find(peer_key);
			if (peer_it == tor.seeders.end()) {
				peer_it = add_peer(tor.seeders, peer_key);
				inserted = true;
				inc_s = true;
			} else {
				completed_torrent = false;
			}
		} else if (tor.seeders.find(peer_key) != tor.seeders.end()) {
			// If the peer exists in both peer lists, just decrement the seed count.
			// Should be cheaper than searching the seed list in the left > 0 case
			dec_s = true;
		}
	} else {
		peer_it = tor.seeders.find(peer_key);
		if (peer_it == tor.seeders.end()) {
			peer_it = tor.leechers.find(peer_key);
			if (peer_it == tor.leechers.end()) {
				peer_it = add_peer(tor.seeders, peer_key);
				inserted = true;
			} else {
				p = &peer_it->second;
				std::pair<peer_list::iterator, bool> insert
				= tor.seeders.insert(std::pair<std::string, peer>(peer_key, *p));
				tor.leechers.erase(peer_it);
				peer_it = insert.first;
				peer_changed = true;
				dec_l = true;
			}
			inc_s = true;
		}
	}
	p = &peer_it->second;

	int64_t upspeed = 0;
	int64_t downspeed = 0;
	int64_t real_uploaded_change = 0;
	int64_t real_downloaded_change = 0;
	int64_t max_allowed_bytes_transferred = 999999999999999;
	if (params["event"] == "paused") paused_torrent = true;

	if (paused_torrent != p->paused) {
		// Account for paused peers
		p->paused = paused_torrent;
		if (paused_torrent) {
			tor.paused++;
		} else {
			tor.paused--;
		}
	}

	if (inserted || params["event"] == "started") {
		// New peer on this torrent (maybe)
		update_torrent = true;
		if (inserted) {
			// If this was an existing peer, the user pointer will be corrected later
			p->user = u;
			p->domain = d;
		}

		// Clamp the uploaded/downloaded
		uploaded   = uploaded > max_allowed_bytes_transferred ? max_allowed_bytes_transferred : uploaded;
		downloaded = downloaded > max_allowed_bytes_transferred ? max_allowed_bytes_transferred : downloaded;

		p->first_announced = cur_time;
		p->last_announced = 0;
		p->uploaded = uploaded;
		p->downloaded = downloaded;
		p->corrupt = corrupt;
		p->announces = 1;
		peer_changed = true;
	} else if (uploaded < p->uploaded || downloaded < p->downloaded) {
		p->announces++;
		p->uploaded = uploaded;
		p->downloaded = downloaded;
		peer_changed = true;
	} else {
		int64_t uploaded_change = 0;
		int64_t downloaded_change = 0;
		int64_t corrupt_change = 0;
		p->announces++;

		if (uploaded != p->uploaded) {
			uploaded_change = uploaded - p->uploaded;
			uploaded_change = uploaded_change > max_allowed_bytes_transferred ?
				max_allowed_bytes_transferred : uploaded_change;
			real_uploaded_change = uploaded_change;
			p->uploaded = uploaded;
		}
		if (downloaded != p->downloaded) {
			downloaded_change = downloaded - p->downloaded;
			downloaded_change = downloaded_change > max_allowed_bytes_transferred ?
				max_allowed_bytes_transferred : downloaded_change;
			real_downloaded_change = downloaded_change;
			p->downloaded = downloaded;
		}
		if (corrupt != p->corrupt) {
			corrupt_change = corrupt - p->corrupt;
			p->corrupt = corrupt;
			tor.balance -= corrupt_change;
			update_torrent = true;
		}
		peer_changed = peer_changed || uploaded_change || downloaded_change || corrupt_change;

		if (uploaded_change || downloaded_change) {
			tor.balance += uploaded_change;
			tor.balance -= downloaded_change;
			tor.balance -= corrupt;
			update_torrent = true;

			if (cur_time > p->last_announced) {
				upspeed = uploaded_change / (cur_time - p->last_announced);
				downspeed = downloaded_change / (cur_time - p->last_announced);
			}
			std::map<int, slots_t>::iterator sit = tor.tokened_users.find(userid);

			if (sit != tor.tokened_users.end()) {
				//expire_token = true;
				std::stringstream record;
				record << '(' << userid << ',' << tor.id << ',' << downloaded_change << ',' << uploaded_change << ')';
				std::string record_str = record.str();
				db->record_token(record_str);
			}

			if (tor.free_torrent == NEUTRAL) {
				downloaded_change = 0;
				uploaded_change = 0;
			} else if (tor.free_torrent == FREE || sitewide_freeleech ||
			   (sit != tor.tokened_users.end() && sit->second.free_leech >= now) || u->pfl() >= now) {
   				downloaded_change = 0;
			}

			if (tor.double_torrent == DOUBLE || sitewide_doubleseed ||
			   (sit != tor.tokened_users.end() && sit->second.double_seed >= now) || u->pds() >= now) {
				if(uploaded_change > max_allowed_bytes_transferred) {
					uploaded_change=max_allowed_bytes_transferred;
				}
				uploaded_change *= 2;
			}

			if (uploaded_change || downloaded_change || real_uploaded_change || real_downloaded_change) {
				std::stringstream record;
				record << '(' << userid << ',' << uploaded_change << ',' << downloaded_change << ',' << real_uploaded_change << ',' << real_downloaded_change << ')';
				std::string record_str = record.str();
				db->record_user(record_str);
			}
		}
	}
	p->left = left;

  // Reject leech forbidden peers early
	if (!u->can_leech() && left > 0) {
		return response_error("Access denied, leeching forbidden", client_opts);
	}

	auto param_ip = params.find("ip");
	if (param_ip != params.end()) {
		struct addrinfo hint, *res = NULL;
		memset(&hint, 0, sizeof hint);
		hint.ai_family = PF_UNSPEC;
		hint.ai_flags = AI_NUMERICHOST;
		getaddrinfo(ip.c_str(), NULL, &hint, &res);
		if(res->ai_family == AF_INET) {
			ipv4 = param_ip->second;
		} else if (res->ai_family == AF_INET6) {
			ipv6 = param_ip->second;
		}
		freeaddrinfo(res);
	}

	auto header_ip = headers.find("x-forwarded-for");
	if (header_ip != headers.end()) {
		std::string ip = header_ip->second;
		struct addrinfo hint, *res = NULL;
		memset(&hint, 0, sizeof hint);
		hint.ai_family = PF_UNSPEC;
		hint.ai_flags = AI_NUMERICHOST;
		getaddrinfo(ip.c_str(), NULL, &hint, &res);
		if(res->ai_family == AF_INET) {
			ipv4 = ip;
			public_ipv4=ip;
		} else if (res->ai_family == AF_INET6) {
			ipv6 = ip;
			public_ipv6=ip;
		}
		freeaddrinfo(res);
	}

	if ((param_ip = params.find("ipv4")) != params.end()) ipv4 = param_ip->second;
	if ((param_ip = params.find("ipv6")) != params.end()) ipv6 = param_ip->second;

	// Convert IPs to Binary representations
	struct sockaddr_in sa;
	if(inet_pton(AF_INET, hex_decode(ipv4).c_str(), &(sa.sin_addr)) != -1 && !ipv4.empty()){
		// IP is 4 bytes for IPv4
		if (ipv4_is_public(sa.sin_addr)) {
			ipv4.assign(reinterpret_cast<const char*>(&sa.sin_addr.s_addr), 4);
		} else {
			syslog(trace) << "Rejecting IP: " << hex_decode(ipv4);
			ipv4.clear();
		}
	}
	struct sockaddr_in public_sa;
	if(inet_pton(AF_INET, hex_decode(public_ipv4).c_str(), &(public_sa.sin_addr)) != -1 && !public_ipv4.empty()){
		// IP is 4 bytes for IPv4
		if (ipv4_is_public(public_sa.sin_addr)) {
			public_ipv4.assign(reinterpret_cast<const char*>(&public_sa.sin_addr.s_addr), 4);
		} else {
			syslog(trace) << "Rejecting IP: " << hex_decode(public_ipv4);
			public_ipv4.clear();
		}
	}
	struct sockaddr_in6 sa6;
	if(inet_pton(AF_INET6, hex_decode(ipv6).c_str(), &(sa6.sin6_addr)) != -1 && !ipv6.empty()){
		// IP is 16 bytes for IPv6
		if (ipv6_is_public(sa6.sin6_addr)) {
			ipv6.assign(reinterpret_cast<const char*>(&sa6.sin6_addr.s6_addr), 16);
		} else {
			syslog(trace) << "Rejecting IP: " << hex_decode(ipv6);
			ipv6.clear();
		}
	}
	struct sockaddr_in6 public_sa6;
	if(inet_pton(AF_INET6, hex_decode(public_ipv6).c_str(), &(public_sa6.sin6_addr)) != -1 && !public_ipv6.empty()){
		// IP is 16 bytes for IPv6
		if (ipv6_is_public(public_sa6.sin6_addr)) {
			public_ipv6.assign(reinterpret_cast<const char*>(&public_sa6.sin6_addr.s6_addr), 16);
		} else {
			syslog(trace) << "Rejecting IP: " << hex_decode(public_ipv6);
			public_ipv6.clear();
		}
	}

	if (ipv4.empty() && ipv6.empty()) {
		return response_error("Invalid IP detected", client_opts);
	}

	uint16_t port = strtoint32(params["port"]) & 0xFFFF;
	// Generate compact ip/port string
	if (inserted || port != p->port || ipv4 != p->ipv4 || ipv6 != p->ipv6) {
		p->port = port;
		p->ipv4 = "";
		p->ipv6 = "";
		p->ipv4_port = "";
		p->ipv6_port = "";

		// Validate IPv4 address and extract binary representation
		if(!ipv4.empty()){
			p->ipv4 = ipv4;

			// IP+Port is 6 bytes for IPv4
			p->ipv4_port = ipv4;
			p->ipv4_port.push_back(port >> 8);
			p->ipv4_port.push_back(port & 0xFF);
		}

		// Validate IPv6 address and extract binary representation
		if(!ipv6.empty()){
			p->ipv6 = ipv6;

			// IP+Port is 18 bytes for IPv6
			p->ipv6_port = ipv6;
			p->ipv6_port.push_back(port >> 8);
			p->ipv6_port.push_back(port & 0xFF);
		}
	}

	// Update the peer
	p->last_announced = cur_time;
	p->visible = peer_is_visible(u, p);

	// Add peer data to the database
	std::stringstream record;
	if (peer_changed) {
		record << '(' << userid << ',' << tor.id << ',' << active << ','
		       << uploaded << ',' << downloaded << ',' << upspeed << ',' << downspeed << ','
		       << left << ',' << corrupt << ',' << (cur_time - p->first_announced) << ','
		       << p->first_announced << ',' << p->last_announced << ',' << p->announces << ',';
		std::string record_str = record.str();
		std::string record_ipv4, record_ipv6;
		if (u->is_protected()) {
			record_ipv4 = "";
			record_ipv6 = "";
		} else {
			record_ipv4 = ipv4;
			record_ipv6 = ipv6;
		}

		db->record_peer(record_str, record_ipv4, record_ipv6, port, peer_id, headers["user-agent"]);
	} else {
		record << '(' << userid << ',' << tor.id << ',' << (cur_time - p->first_announced)
		       << ',' << p->last_announced << ',' << p->announces << ',';
		std::string record_str = record.str();
		db->record_peer(record_str, peer_id);
	}

	if (real_uploaded_change > 0 || real_downloaded_change > 0) {
		record.str("");
		record << '(' << userid << ',' << real_downloaded_change << ',' << left << ',' << real_uploaded_change << ',' << upspeed << ',' << downspeed << ',' << (cur_time - p->first_announced);
		std::string record_str = record.str();
		db->record_peer_hist(record_str, peer_id, ipv4, ipv6, tor.id);
	}


	// Select peers!
	uint32_t numwant;
	params_type::const_iterator param_numwant = params.find("numwant");
	if (param_numwant == params.end()) {
		numwant = numwant_limit;
	} else {
		numwant = std::min((int32_t)numwant_limit, strtoint32(param_numwant->second));
	}

	if (stopped_torrent) {
		numwant = 0;
		if (left > 0) {
			dec_l = true;
		} else {
			dec_s = true;
		}
	} else if (completed_torrent) {
		snatched = 1;
		update_torrent = true;
		tor.completed++;

		std::stringstream record;
		std::string record_ipv4, record_ipv6;
		if (u->is_protected()) {
			record_ipv4 = "";
			record_ipv6 = "";
		} else {
			record_ipv4 = ipv4;
			record_ipv6 = ipv6;
		}
		record << '(' << userid << ',' << tor.id << ',' << cur_time;
		std::string record_str = record.str();
		db->record_snatch(record_str, record_ipv4, record_ipv6);

		// User is a seeder now!
		if (!inserted) {
			std::pair<peer_list::iterator, bool> insert
			= tor.seeders.insert(std::pair<std::string, peer>(peer_key, *p));
			tor.leechers.erase(peer_it);
			peer_it = insert.first;
			p = &peer_it->second;
			dec_l = inc_s = true;
		}
		if (expire_token) {
			s_comm->expire_token(tor.id, userid);
			tor.tokened_users.erase(userid);
		}
	} else if (!u->can_leech() && left > 0) {
		numwant = 0;
	}

	std::string peers;
	std::string peers6;
	if (numwant > 0) {
		peers.reserve(numwant*6);
		peers6.reserve(numwant*18);
		unsigned int found_peers = 0;
		if (left > 0) { // Show seeders to leechers first
			if (!tor.seeders.empty()) {
				// Set the start position
				peer_list::const_iterator i;
				if (!tor.last_selected_seeder.empty()) {
					i = tor.seeders.begin();
				} else {
					i = tor.seeders.find(tor.last_selected_seeder);
					if (i == tor.seeders.end() || ++i == tor.seeders.end()) {
						i = tor.seeders.begin();
					}
				}

				// Set the end position
				peer_list::const_iterator end;
				if (i == tor.seeders.begin()) {
					end = tor.seeders.end();
				} else {
					end = tor.seeders.find(tor.last_selected_seeder);
					if (end == tor.seeders.begin()++) {
						++i;
					}
				}

				// Add seeders
				while (i != end && found_peers < numwant) {
					// Warap around
					if (i == tor.seeders.end()) {
						i = tor.seeders.begin();
					}

					// Don't show users themselves or staff (leech disabled seeders are fine)
					if ((i->second.ipv4_port == p->ipv4_port && !p->ipv4_port.empty()) ||
						(i->second.ipv6_port == p->ipv6_port && !p->ipv6_port.empty())||
						i->second.user->get_id() == userid || !i->second.visible) {
						++i;
						continue;
					}

					// Only show IPv6 peers to other IPv6 peers
					if ((!p->ipv6.empty()) && (!i->second.ipv6_port.empty()) &&
					     opts->get_bool("EnableIPv6Tracker") && i->second.user->track_ipv6()) {
						peers6.append(i->second.ipv6_port);
						found_peers++;
					} else if (!i->second.ipv4_port.empty()) {
						peers.append(i->second.ipv4_port);
						found_peers++;
					}

					// Record last seeder selected
					tor.last_selected_seeder = i->first;
					++i;
				}
			}
		}

		// Seeder or Leecher with not enough peers
		if (found_peers < numwant && !tor.leechers.empty()) {
			// Set the start position
			peer_list::const_iterator i;
			if (!tor.last_selected_leecher.empty()) {
				i = tor.leechers.begin();
			} else {
				i = tor.leechers.find(tor.last_selected_leecher);
				if (i == tor.leechers.end() || ++i == tor.leechers.end()) {
					i = tor.leechers.begin();
				}
			}

			// Set the end position
			peer_list::const_iterator end;
			if (i == tor.leechers.begin()) {
				end = tor.leechers.end();
			} else {
				end = tor.leechers.find(tor.last_selected_leecher);
				if (end == tor.leechers.begin()++) {
					++i;
				}
			}

			// Add Leechers
			while (i != end && found_peers < numwant) {
				// Warap around
				if (i == tor.leechers.end()) {
					i = tor.leechers.begin();
				}

				// Don't show users themselves, leech disabled users or staff
				if (i->second.user->is_deleted() ||
				    (i->second.ipv4_port == p->ipv4_port && !p->ipv4_port.empty()) ||
					(i->second.ipv6_port == p->ipv6_port && !p->ipv6_port.empty())||
					i->second.user->get_id() == userid || !i->second.visible) {
					++i;
					continue;
				}

				// Only show IPv6 peers to other IPv6 peers
				if ((!p->ipv6.empty()) && (!i->second.ipv6_port.empty()) &&
					opts->get_bool("EnableIPv6Tracker") && i->second.user->track_ipv6()) {
					peers6.append(i->second.ipv6_port);
					found_peers++;
				} else if (!i->second.ipv4_port.empty()) {
					peers.append(i->second.ipv4_port);
					found_peers++;
				}

				// Record last leecher selected
				tor.last_selected_leecher = i->first;
				++i;
			}
		}
	}

	// Update the stats
	stats.succ_announcements++;

	if (dec_l || dec_s || inc_l || inc_s) {
		if (inc_l) {
			p->user->incr_leeching();
			stats.leechers++;
		}
		if (inc_s) {
			p->user->incr_seeding();
			stats.seeders++;
		}
		if (dec_l) {
			p->user->decr_leeching();
			stats.leechers--;
		}
		if (dec_s) {
			p->user->decr_seeding();
			stats.seeders--;
		}
		if (inc_l || inc_s) {
			if(!p->ipv6.empty()){
				char str[INET6_ADDRSTRLEN];
				struct sockaddr_in6 sa6;
				inet_ntop(AF_INET6, p->ipv6.c_str(), str, INET6_ADDRSTRLEN);
				inet_pton(AF_INET6, str, &(sa6.sin6_addr));
				if (ipv6_is_public(sa6.sin6_addr)) {
					stats.ipv6_peers++;
					syslog(trace) << "Peer with IPv6 address " << str << " added.";
				}
			}
			if(!p->ipv4.empty()){
				char str[INET_ADDRSTRLEN];
				struct sockaddr_in sa;
				inet_ntop(AF_INET, p->ipv4.c_str(), str, INET_ADDRSTRLEN);
				inet_pton(AF_INET, str, &(sa.sin_addr));
				if (ipv4_is_public(sa.sin_addr)) {
					stats.ipv4_peers++;
 					syslog(trace) << "Peer with IPv4 address " << str << " added.";
				}
			}
		}
		if (dec_l || dec_s) {
			if(!p->ipv6.empty()){
				char str[INET6_ADDRSTRLEN];
				struct sockaddr_in6 sa6;
				inet_ntop(AF_INET6, p->ipv6.c_str(), str, INET6_ADDRSTRLEN);
				inet_pton(AF_INET6, str, &(sa6.sin6_addr));
				if (ipv6_is_public(sa6.sin6_addr)) {
					stats.ipv6_peers--;
					syslog(trace) << "Peer with IPv6 address " << str << " removed." ;
				}
			}
			if(!p->ipv4.empty()){
				char str[INET_ADDRSTRLEN];
				struct sockaddr_in sa;
				inet_ntop(AF_INET, p->ipv4.c_str(), str, INET_ADDRSTRLEN);
				inet_pton(AF_INET, str, &(sa.sin_addr));
				if (ipv4_is_public(sa.sin_addr)) {
					stats.ipv4_peers--;
 					syslog(trace) << "Peer with IPv4 address " << str << " removed.";
				}
			}
		}
	}

	// Correct the stats for the old user if the peer's user link has changed
	if (p->user != u) {
		if (!stopped_torrent) {
			if (left > 0) {
				u->incr_leeching();
				p->user->decr_leeching();
			} else {
				u->incr_seeding();
				p->user->decr_seeding();
			}
		}
		p->user = u;
	}

	// Delete peers as late as possible to prevent access problems
	if (stopped_torrent) {
		if (left > 0) {
			tor.leechers.erase(peer_it);
			dec_l = true;
		} else {
			tor.seeders.erase(peer_it);
			dec_s = true;
		}
	}

	// Putting this after the peer deletion gives us accurate swarm sizes
	if (update_torrent || tor.last_flushed + 3600 < cur_time) {
		tor.last_flushed = cur_time;

		std::stringstream record;
		record << '(' << tor.id << ',' << tor.seeders.size() << ',' << tor.leechers.size() << ',' << snatched << ',' << tor.balance << ')';
		std::string record_str = record.str();
		db->record_torrent(record_str);
	}

	// Bit torrent spec mandates that the keys are sorted.

	std::string output = "d";
	output.reserve(350);
	output += bencode_str("complete")   + bencode_int(tor.seeders.size());
	output += bencode_str("downloaded") + bencode_int(tor.completed);

	if (!public_ipv6.empty()) {
		output += bencode_str("external ip") + bencode_str(public_ipv6);
	} else 	if (!public_ipv4.empty()) {
		output += bencode_str("external ip") + bencode_str(public_ipv4);
	}

	output += bencode_str("incomplete")   + bencode_int(tor.leechers.size());
	output += bencode_str("interval")     + bencode_int(announce_interval + std::min((size_t)600, tor.seeders.size())); // ensure a more even distribution of announces/second
	output += bencode_str("min interval") + bencode_int(announce_interval);
	output += bencode_str("peers") + bencode_str(peers);

	if (!peers6.empty())
			output += bencode_str("peers6") + bencode_str(peers6);
	output += "e";

	/* gzip compression actually makes announce returns larger from our
	 * testing. Feel free to enable this here if you'd like but be aware of
	 * possibly inflated return size
	 */
	/*if (headers["accept-encoding"].find("gzip") != std::string::npos) {
		client_opts.gzip = true;
	}*/
	return response(output, client_opts, 200);
}

std::string worker::scrape(const std::list<std::string> &infohashes, params_type &headers, client_opts_t &client_opts) {
	std::string output = "d" + bencode_str("files") + "d";
	for (std::list<std::string>::const_iterator i = infohashes.begin(); i != infohashes.end(); ++i) {
		std::string infohash = *i;
		infohash = hex_decode(infohash);

		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		torrent_list::iterator tor = torrents_list.find(infohash);
		if (tor == torrents_list.end()) {
			continue;
		}
		torrent *t = &(tor->second);

		output += bencode_str(infohash);
		output += "d";
		output += bencode_str("complete")    + bencode_int(t->seeders.size());
		output += bencode_str("downloaded")  + bencode_int(t->completed);
		output += bencode_str("incomplete")  + bencode_int(t->leechers.size());
		output += bencode_str("downloaders") + bencode_int(t->leechers.size() - t->paused);
		output += "e";
	}
	output += "ee";
	if (headers["accept-encoding"].find("gzip") != std::string::npos) {
		client_opts.gzip = true;
	}

	return response(output, client_opts, 200);
}

//TODO: Restrict to local IPs
std::string worker::update(params_type &params, client_opts_t &client_opts) {
	uint16_t response_code = 204;
	if(params["action"].empty()) {
		syslog(error) << "Update called without action!";
		response_code = 500;
		return response("success", client_opts, response_code);
	} else {
		syslog(debug) << "Update called: " << params["action"];
	}

	if(params["action"] == "options") {
		opts->set("tracker", params["set"], params["value"].c_str());
		syslog(debug) << "Set option: " << params["set"] << " -> " << params["value"];
	} else if (params["action"] == "change_passkey") {
		std::string oldpasskey = params["oldpasskey"];
		std::string newpasskey = params["newpasskey"];
		std::lock_guard<std::mutex> ul_lock(db->user_list_mutex);
		auto u = users_list.find(oldpasskey);
		if (u == users_list.end()) {
			syslog(error) << "No user with passkey " << oldpasskey << " exists when attempting to change passkey to " << newpasskey;
			response_code = 500;
		} else {
			userid_t userid = u->second->get_id();
			users_list[newpasskey] = u->second;
			users_list.erase(oldpasskey);
			syslog(debug) << "Changed passkey from " << oldpasskey << " to " << newpasskey << " for user " << userid;
		}
	} else if (params["action"] == "add_torrent") {
		torrent *t;
		std::string info_hash = params["info_hash"];
		info_hash = hex_decode(info_hash);
		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		auto i = torrents_list.find(info_hash);
		if (i == torrents_list.end()) {
			t = &torrents_list[info_hash];
			t->id = strtoint32(params["id"]);
			t->balance = 0;
			t->completed = 0;
			t->last_selected_seeder = "";
		} else {
			t = &i->second;
		}
		if (params["freetorrent"] == "0") {
			t->free_torrent = NORMAL;
		} else if (params["freetorrent"] == "1") {
			t->free_torrent = FREE;
		} else {
			t->free_torrent = NEUTRAL;
		}
		if (params["doubletorrent"] == "0") {
			t->double_torrent = NORMAL;
		} else if (params["doubletorrent"] == "1") {
			t->double_torrent = DOUBLE;
		} else {
			t->double_torrent = NORMAL;
		}
		syslog(debug) << "Added torrent " << t->id
		             << ". FL: " << t->free_torrent   << " " << params["freetorrent"]
		             << ". DS: " << t->double_torrent << " " << params["doubletorrent"];
	} else if (params["action"] == "update_torrent") {
		std::string info_hash = params["info_hash"];
		info_hash = hex_decode(info_hash);
		freetype fl;
		freetype ds;
		if (params["freetorrent"] == "0") {
			fl = NORMAL;
		} else if (params["freetorrent"] == "1") {
			fl = FREE;
		} else {
			fl = NEUTRAL;
		}
		if (params["doubletorrent"] == "0") {
			ds = NORMAL;
		} else if (params["doubletorrent"] == "1") {
			ds = DOUBLE;
		} else {
			ds = NORMAL;
		}
		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		auto torrent_it = torrents_list.find(info_hash);
		if (torrent_it != torrents_list.end()) {
			torrent_it->second.free_torrent   = fl;
			torrent_it->second.double_torrent = ds;
			syslog(debug) << "Updated torrent " << torrent_it->second.id << " to FL " << fl << ", DS " << ds;
		} else {
			syslog(error) << "Failed to find torrent " << info_hash << " to FL " << fl << ", DS " << ds;
			response_code = 500;
		}
	} else if (params["action"] == "update_torrents") {
		// Each decoded infohash is exactly 20 characters long.
		std::string info_hashes = params["info_hashes"];
		info_hashes = hex_decode(info_hashes);
		freetype fl;
		freetype ds;
		if (params["freetorrent"] == "0") {
			fl = NORMAL;
		} else if (params["freetorrent"] == "1") {
			fl = FREE;
		} else {
			fl = NEUTRAL;
		}
		if (params["doubletorrent"] == "0") {
			ds = NORMAL;
		} else if (params["doubletorrent"] == "1") {
			ds = DOUBLE;
		} else {
			ds = NORMAL;
		}
		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		for (unsigned int pos = 0; pos < info_hashes.length(); pos += 20) {
			std::string info_hash = info_hashes.substr(pos, 20);
			auto torrent_it = torrents_list.find(info_hash);
			if (torrent_it != torrents_list.end()) {
				torrent_it->second.free_torrent = fl;
				syslog(debug) << "Updated torrent " << torrent_it->second.id << " to FL " << fl << ", DS " << ds;
			} else {
				syslog(error) << "Failed to find torrent " << info_hash << " to FL " << fl << ", DS " << ds;
				response_code = 500;
			}
		}
	} else if (params["action"] == "add_token_fl") {
		std::string info_hash = hex_decode(params["info_hash"]);
		int userid = atoi(params["userid"].c_str());
		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		auto torrent_it = torrents_list.find(info_hash);
		time_t time = (time_t)atoi(params["time"].c_str());

		if (torrent_it != torrents_list.end()) {
			std::map<int, slots_t>::iterator sit = torrent_it->second.tokened_users.find(userid);
			// The user already have a slot, update and mark the torrent as freeleech
			if (sit != torrent_it->second.tokened_users.end()) {
			    sit->second.free_leech = time;
			} else {
			    slots_t slots;
			    slots.free_leech = time;
			    slots.double_seed = 0;
			    torrent_it->second.tokened_users.insert(std::pair<int, slots_t>(userid, slots));
			}
		} else {
			syslog(error) << "Failed to find torrent to add a token for user " << userid;
			response_code = 500;
		}
	} else if (params["action"] == "add_token_ds") {
		std::string info_hash = hex_decode(params["info_hash"]);
		int userid = atoi(params["userid"].c_str());
		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		auto torrent_it = torrents_list.find(info_hash);
		time_t time = (time_t)atoi(params["time"].c_str());

		if (torrent_it != torrents_list.end()) {
			std::map<int, slots_t>::iterator sit = torrent_it->second.tokened_users.find(userid);
			// The user already have a slot, update and mark the torrent as freeleech
			if (sit != torrent_it->second.tokened_users.end()) {
			    sit->second.double_seed = time;
			} else {
			    slots_t slots;
			    slots.free_leech = 0;
			    slots.double_seed = time;
			    torrent_it->second.tokened_users.insert(std::pair<int, slots_t>(userid, slots));
			}
		} else {
			syslog(error) << "Failed to find torrent to add a token for user " << userid;
			response_code = 500;
		}
	} else if (params["action"] == "remove_tokens") {
		std::string info_hash = hex_decode(params["info_hash"]);
		int userid = atoi(params["userid"].c_str());
		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		auto torrent_it = torrents_list.find(info_hash);
		if (torrent_it != torrents_list.end()) {
			torrent_it->second.tokened_users.erase(userid);
		} else {
			syslog(error) << "Failed to find torrent " << info_hash << " to remove tokens for user " << userid;
			response_code = 500;
		}
	} else if (params["action"] == "delete_torrent") {
		std::string info_hash = params["info_hash"];
		info_hash = hex_decode(info_hash);
		int reason = -1;
		auto reason_it = params.find("reason");
		if (reason_it != params.end()) {
			reason = atoi(params["reason"].c_str());
		}
		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		auto torrent_it = torrents_list.find(info_hash);
		if (torrent_it != torrents_list.end()) {
			syslog(debug) << "Deleting torrent " << torrent_it->second.id << " for the reason '" << get_del_reason(reason) << "'";
			stats.leechers -= torrent_it->second.leechers.size();
			stats.seeders -= torrent_it->second.seeders.size();
			for (auto &p: torrent_it->second.leechers) {
				p.second.user->decr_leeching();
			}
			for (auto &p: torrent_it->second.seeders) {
				p.second.user->decr_seeding();
			}
			std::lock_guard<std::mutex> dr_lock(del_reasons_lock);
			del_message msg;
			msg.reason = reason;
			msg.time = time(NULL);
			del_reasons[info_hash] = msg;
			torrents_list.erase(torrent_it);
		} else {
			syslog(error) << "Failed to find torrent " << bintohex(info_hash) << " to delete ";
			response_code = 500;
		}
	} else if (params["action"] == "add_user") {
		std::string passkey = params["passkey"];
		userid_t userid = strtoint32(params["id"]);
		std::lock_guard<std::mutex> ul_lock(db->user_list_mutex);
		auto u = users_list.find(passkey);
		if (u == users_list.end()) {
			bool protect_ip = params["visible"] == "0";
			user_ptr tmp_user = std::make_shared<user>(userid, true, protect_ip, false, 0, 0);
			users_list.insert(std::pair<std::string, user_ptr>(passkey, tmp_user));
			syslog(debug) << "Added user " << passkey << " with id " << userid;
		} else {
			syslog(error) << "Tried to add already known user " << passkey << " with id " << userid;
			u->second->set_deleted(false);
			response_code = 500;
		}
	} else if (params["action"] == "remove_user") {
		std::string passkey = params["passkey"];
		std::lock_guard<std::mutex> ul_lock(db->user_list_mutex);
		auto u = users_list.find(passkey);
		if (u != users_list.end()) {
			syslog(debug) << "Removed user " << passkey << " with id " << u->second->get_id();
			u->second->set_deleted(true);
			users_list.erase(u);
		}
	} else if (params["action"] == "remove_users") {
		// Each passkey is exactly 32 characters long.
		std::string passkeys = params["passkeys"];
		std::lock_guard<std::mutex> ul_lock(db->user_list_mutex);
		for (unsigned int pos = 0; pos < passkeys.length(); pos += 32) {
			std::string passkey = passkeys.substr(pos, 32);
			auto u = users_list.find(passkey);
			if (u != users_list.end()) {
				syslog(debug) << "Removed user " << passkey;
				u->second->set_deleted(true);
				users_list.erase(passkey);
			}
		}
	} else if (params["action"] == "update_user") {
		std::string passkey = params["passkey"];

		std::lock_guard<std::mutex> ul_lock(db->user_list_mutex);
		user_list::iterator i = users_list.find(passkey);
		if (i == users_list.end()) {
			syslog(error) << "No user with passkey " << passkey << " found when attempting to change leeching status!";
			response_code = 500;
		} else {
			params_type::const_iterator peer_id_iterator;
			// Canleech delta
			bool can_leech = i->second->can_leech();
			peer_id_iterator = params.find("can_leech");
			if (peer_id_iterator != params.end()) {
				if (params["can_leech"] == "0") {
					can_leech = false;
				} else {
					can_leech = true;
				}
				i->second->set_leechstatus(can_leech);
			}
			// ProtectIP delta
			bool protect_ip = i->second->is_protected();
			peer_id_iterator = params.find("visible");
			if (peer_id_iterator != params.end()) {
				if (params["visible"] == "0") {
					protect_ip = true;
				} else {
					protect_ip = false;
				}
				i->second->set_protected(protect_ip);
			}
			// TrackIPv6 delta
			bool track_ipv6 = i->second->track_ipv6();
			peer_id_iterator = params.find("track_ipv6");
			if (peer_id_iterator != params.end()) {
				if (params["track_ipv6"] == "0") {
					track_ipv6 = false;
				} else {
					track_ipv6 = true;
				}
				i->second->set_track_ipv6(track_ipv6);
			}

			syslog(debug) << "Updated user " << passkey    << " "
			              << "leech: "      << can_leech   << " | "
			              << "protect: "    << protect_ip  << " | "
			              << "ipv6: "       << track_ipv6;
		}
	} else if (params["action"] == "set_personal_freeleech") {
		std::string passkey = params["passkey"];
		time_t pfl = (time_t)atoi(params["time"].c_str());

		std::lock_guard<std::mutex> ul_lock(db->user_list_mutex);
		user_list::iterator i = users_list.find(passkey);
		if (i == users_list.end()) {
			syslog(error) << "No user with passkey " << passkey << " found when attempting set personal freeleech!";
			response_code = 500;
		} else {
			i->second->set_personalfreeleech(pfl);
			syslog(debug) << "Personal freeleech set to user " << passkey << " until time: " << params["time"];
		}
	} else if (params["action"] == "set_personal_doubleseed") {
		std::string passkey = params["passkey"];
		time_t pds = (time_t)atoi(params["time"].c_str());

		std::lock_guard<std::mutex> ul_lock(db->user_list_mutex);
		user_list::iterator i = users_list.find(passkey);
		if (i == users_list.end()) {
			syslog(error) << "No user with passkey " << passkey << " found when attempting set personal doubleseed!";
			response_code = 500;
		} else {
			i->second->set_personaldoubleseed(pds);
			syslog(debug) << "Personal doubleseed set to user " << passkey << " until time: " << params["time"];
		}
	} else if (params["action"] == "add_blacklist") {
		std::string peer_id = params["peer_id"];
		std::lock_guard<std::mutex> wl_lock(db->blacklist_mutex);
		blacklist.push_back(peer_id);
		syslog(debug) << "Blacklisted " << peer_id;
	} else if (params["action"] == "remove_blacklist") {
		std::string peer_id = params["peer_id"];
		std::lock_guard<std::mutex> wl_lock(db->blacklist_mutex);
		for (unsigned int i = 0; i < blacklist.size(); i++) {
			if (blacklist[i].compare(peer_id) == 0) {
				blacklist.erase(blacklist.begin() + i);
				break;
			}
		}
		syslog(debug) << "De-blacklisted " << peer_id;
	} else if (params["action"] == "edit_blacklist") {
		std::string new_peer_id = params["new_peer_id"];
		std::string old_peer_id = params["old_peer_id"];
		std::lock_guard<std::mutex> wl_lock(db->blacklist_mutex);
		for (unsigned int i = 0; i < blacklist.size(); i++) {
			if (blacklist[i].compare(old_peer_id) == 0) {
				blacklist.erase(blacklist.begin() + i);
				break;
			}
		}
		blacklist.push_back(new_peer_id);
		syslog(debug) << "Edited blacklist item from " << old_peer_id << " to " << new_peer_id;
	} else if (params["action"] == "update_announce_interval") {
		const std::string interval = params["new_announce_interval"];
		conf->set("tracker", "announce_interval", interval);
		announce_interval = conf->get_uint("announce_interval");
		syslog(debug) << "Edited announce interval to " << announce_interval;
	} else if (params["action"] == "info_torrent") {
		std::string info_hash_hex = params["info_hash"];
		std::string info_hash = hex_decode(info_hash_hex);
		syslog(debug) << "Info for torrent '" << info_hash_hex << "'";
		std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
		auto torrent_it = torrents_list.find(info_hash);
		if (torrent_it != torrents_list.end()) {
			syslog(debug) << "Torrent " << torrent_it->second.id
				<< ", freetorrent = " << torrent_it->second.free_torrent;
		} else {
			syslog(error) << "Failed to find torrent " << info_hash_hex;
		}
	} else {
		syslog(error) << "Unknown command: " << params["action"];
		response_code = 500;
	}
	return response("success", client_opts, response_code);
}

peer_list::iterator worker::add_peer(peer_list &peer_list, const std::string &peer_key) {
	peer new_peer;
	auto it = peer_list.insert(std::pair<std::string, peer>(peer_key, new_peer));
	return it.first;
}

void worker::start_reaper() {
	if (!reaper_active) {
		std::thread thread(&worker::do_start_reaper, this);
		thread.detach();
	}
}

void worker::do_start_reaper() {
	reaper_active = true;
	reap_peers();
	reap_del_reasons();
	reaper_active = false;
}

void worker::reap_peers() {
	syslog(debug) << "Starting peer reaper";
	cur_time = time(NULL);
	unsigned int reaped_l = 0, reaped_v4l = 0, reaped_v6l = 0;
	unsigned int reaped_s = 0, reaped_v4s = 0, reaped_v6s = 0;
	unsigned int reaped_fl = 0;
	unsigned int cleared_torrents = 0;
	for (auto torrent = torrents_list.begin(); torrent != torrents_list.end(); torrent++) {
		bool reaped_this = false; // True if at least one peer was deleted from the current torrent
		auto p = torrent->second.leechers.begin();
		peer_list::iterator del_p;
		while (p != torrent->second.leechers.end()) {
			if (p->second.last_announced + peers_timeout < cur_time) {
				std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
				if(!p->second.ipv6.empty()) reaped_v6l++;
				if(!p->second.ipv4.empty()) reaped_v4l++;
				reaped_l++;
				reaped_this = true;
				del_p = p++;
				del_p->second.user->decr_leeching();
				torrent->second.leechers.erase(del_p);
			} else {
				++p;
			}
		}
		p = torrent->second.seeders.begin();
		while (p != torrent->second.seeders.end()) {
			if (p->second.last_announced + peers_timeout < cur_time) {
				std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
				if(!p->second.ipv6.empty()) reaped_v6s++;
				if(!p->second.ipv4.empty()) reaped_v4s++;
				reaped_s++;
				reaped_this = true;
				del_p = p++;
				del_p->second.user->decr_seeding();
				torrent->second.seeders.erase(del_p);
			} else {
				++p;
			}
		}
		auto fl = torrent->second.tokened_users.begin();
		slots_list::iterator del_fl;
		while (fl != torrent->second.tokened_users.end()) {
			if (fl->second.free_leech < cur_time && fl->second.double_seed < cur_time) {
				std::lock_guard<std::mutex> tl_lock(db->torrent_list_mutex);
				del_fl = fl++;
				torrent->second.tokened_users.erase(del_fl);
				reaped_this = true;
				reaped_fl++;
			} else {
				++fl;
			}
		}
		if (reaped_this) {
			syslog(trace) << "Reaped peers for torrent: " << torrent->second.id;
		} else {
			syslog(trace) << "Skipped torrent: " << torrent->second.id;
		}
		if (reaped_this && torrent->second.seeders.empty() && torrent->second.leechers.empty()) {
			std::stringstream record;
			record << '(' << torrent->second.id << ",0,0,0," << torrent->second.balance << ')';
			std::string record_str = record.str();
			db->record_torrent(record_str);
			cleared_torrents++;
		}
	}

	if (reaped_l || reaped_v4l || reaped_v6l || reaped_s || reaped_v4s || reaped_v6s) {
		stats.leechers   -= reaped_l;
		stats.seeders    -= reaped_s;
		stats.ipv4_peers -= (reaped_v4l + reaped_v4s);
		stats.ipv6_peers -= (reaped_v6l + reaped_v6s);
	}

	syslog(debug) << "Reaped " << reaped_l << " leechers, " << reaped_s << " seeders and " << reaped_fl << " tokens. Reset " << cleared_torrents << " torrents";
}

void worker::reap_del_reasons()
{
	syslog(debug) << "Starting del reason reaper";
	time_t max_time = time(NULL) - del_reason_lifetime;
	unsigned int reaped = 0;
	for (auto reason = del_reasons.begin(); reason != del_reasons.end();) {
		if (reason->second.time <= max_time) {
			auto del_it = reason++;
			std::lock_guard<std::mutex> dr_lock(del_reasons_lock);
			del_reasons.erase(del_it);
			reaped++;
			continue;
		}
		reason++;
	}
	syslog(debug) << "Reaped " << reaped << " del reasons";
}

std::string worker::get_del_reason(int code)
{
	switch (code) {
		case DUPE:
			return "Dupe";
			break;
		case TRUMP:
			return "Trump";
			break;
		case BAD_FILE_NAMES:
			return "Bad File Names";
			break;
		case BAD_FOLDER_NAMES:
			return "Bad Folder Names";
			break;
		case BAD_TAGS:
			return "Bad Tags";
			break;
		case BAD_FORMAT:
			return "Disallowed Format";
			break;
		case DISCS_MISSING:
			return "Discs Missing";
			break;
		case DISCOGRAPHY:
			return "Discography";
			break;
		case EDITED_LOG:
			return "Edited Log";
			break;
		case INACCURATE_BITRATE:
			return "Inaccurate Bitrate";
			break;
		case LOW_BITRATE:
			return "Low Bitrate";
			break;
		case MUTT_RIP:
			return "Mutt Rip";
			break;
		case BAD_SOURCE:
			return "Disallowed Source";
			break;
		case ENCODE_ERRORS:
			return "Encode Errors";
			break;
		case BANNED:
			return "Specifically Banned";
			break;
		case TRACKS_MISSING:
			return "Tracks Missing";
			break;
		case TRANSCODE:
			return "Transcode";
			break;
		case CASSETTE:
			return "Unapproved Cassette";
			break;
		case UNSPLIT_ALBUM:
			return "Unsplit Album";
			break;
		case USER_COMPILATION:
			return "User Compilation";
			break;
		case WRONG_FORMAT:
			return "Wrong Format";
			break;
		case WRONG_MEDIA:
			return "Wrong Media";
			break;
		case AUDIENCE:
			return "Audience Recording";
			break;
		default:
			return "";
			break;
	}
}

/* Peers should be invisible if they are a leecher without
   download privs or their IP is invalid */
bool worker::peer_is_visible(user_ptr &u, peer *p) {
	return (p->left == 0 || u->can_leech());
}

std::string worker::get_host(params_type &headers){

	std::string host = "unkown";
	// Search for host or x-forwarded-host headers
	auto head_itr = headers.find("x-forwarded-host");
	if (head_itr == headers.end())
		head_itr = headers.find("host");

	// If we found a header then record it
	if (head_itr != headers.end())
		host = head_itr->second;

	return trim(host);
}

std::string worker::bencode_int(int data) {
	std::string bencoded_int = "i";
	bencoded_int += inttostr(data);
	bencoded_int += "e";
	return bencoded_int;
}

std::string worker::bencode_str(std::string data) {
	std::string bencoded_str = inttostr(data.size());
	bencoded_str += ":";
	bencoded_str += data;
	return bencoded_str;
}

#if defined(__DEBUG_BUILD__)
/*
 *  Allow any addresses in a debug build, it's expected
 *  that we will generate local traffic for testing.
 */
bool worker::ipv4_is_public(in_addr addr){return true;}
bool worker::ipv6_is_public(in6_addr addr){return true;}
#else
bool worker::ipv4_is_public(in_addr addr){

	// Match against reserved ranges
	if ((ntohl(addr.s_addr) & 0xff000000) == 0x0a000000) return false; // 10.0.0.0/8
	if ((ntohl(addr.s_addr) & 0xfff00000) == 0xac100000) return false; // 172.16.0.0/12
	if ((ntohl(addr.s_addr) & 0xffff0000) == 0xc0a80000) return false; // 192.168.0.0/16
	if ((ntohl(addr.s_addr) & 0xffff0000) == 0xa9fe0000) return false; // 169.254.0.0/16
	if ((ntohl(addr.s_addr) & 0xffc00000) == 0x64400000) return false; // 100.64.0.0/10
	if ((ntohl(addr.s_addr) & 0xff000000) == 0x7f000000) return false; // 127.0.0.0/8
	return true;

}

bool worker::ipv6_is_public(in6_addr addr){

	// Match against reserved ranges
	if (ntohl(addr.s6_addr32[0]) == 0x00000000) return false; // Loopback / v4 compat v6
	if (ntohs(addr.s6_addr16[0]) == 0xfe80    ) return false; // Link local
	if (ntohs(addr.s6_addr16[0]) == 0xfc00    ) return false; // Unique Local - private subnet
	if (ntohs(addr.s6_addr16[0]) == 0xfec0    ) return false; // site-local [deprecated]
	if (ntohs(addr.s6_addr16[0]) == 0x3ffe    ) return false; // 6bone [deprecated]
	if (ntohl(addr.s6_addr32[0]) == 0x20010db8) return false; // documentation examples, unroutable
	if (ntohl(addr.s6_addr32[0]) == 0x20010000) return false; // Teredo
	if (ntohs(addr.s6_addr16[0]) == 0x2002    ) return false; // 6to4
	return true;

}
#endif
