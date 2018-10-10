#ifndef WORKER_H
#define WORKER_H

#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <iostream>
#include <mutex>
#include <ctime>

#include "radiance.h"
class database;
class site_comm;

enum tracker_status { OPEN, PAUSED, CLOSING }; // tracker status

class worker {
	private:
		database * db;
		site_comm * s_comm;
		torrent_list &torrents_list;
		user_list &users_list;
		domain_list &domains_list;
		std::vector<std::string> &blacklist;
		std::unordered_map<std::string, del_message> del_reasons;
		tracker_status status;
		bool reaper_active;
		time_t cur_time;

		unsigned int announce_interval;
		unsigned int del_reason_lifetime;
		unsigned int peers_timeout;
		unsigned int numwant_limit;
		bool keepalive_enabled;
		std::string site_password;
		std::string report_password;

		std::mutex del_reasons_lock;
		void load_config();
		void do_start_reaper();
		void reap_peers();
		void reap_del_reasons();
		bool ipv4_is_public(in_addr addr);
		bool ipv6_is_public(in6_addr addr);
		static std::string get_del_reason(int code);
		peer_list::iterator add_peer(peer_list &peer_list, const std::string &peer_id);
		static inline bool peer_is_visible(user_ptr &u, peer *p);
		std::string get_host(params_type &headers);
		std::string bencode_int(int data);
		std::string bencode_str(std::string data);

	public:
		worker(torrent_list &torrents, user_list &users, domain_list &domains, std::vector<std::string> &_blacklist, database * db_obj, site_comm * sc);
		void reload_config();
		std::string work(const std::string &input, std::string &ip, uint16_t &ip_ver, client_opts_t &client_opts);
		std::string announce(const std::string &input, torrent &tor, user_ptr &u, domain_ptr &d, params_type &params, params_type &headers, std::string &ip, uint16_t &ip_ver, client_opts_t &client_opts);
		std::string scrape(const std::list<std::string> &infohashes, params_type &headers, client_opts_t &client_opts);
		std::string update(params_type &params, client_opts_t &client_opts);

		void reload_lists();
		bool shutdown();

		const inline tracker_status get_status() { return status; }

		void start_reaper();
};
#endif
