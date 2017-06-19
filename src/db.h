#ifndef RADIANCE_DB_H
#define RADIANCE_DB_H
#pragma GCC visibility push(default)
#include <mysql++/mysql++.h>
#include <string>
#include <unordered_map>
#include <queue>
#include <mutex>
#include "config.h"

class mysql {
	private:
		mysqlpp::Connection conn;
		std::string update_user_buffer;
		std::string update_torrent_buffer;
		std::string update_peer_heavy_buffer;
		std::string update_peer_light_buffer;
		std::string update_peer_hist_buffer;
		std::string update_snatch_buffer;
		std::string update_token_buffer;

		std::queue<std::string> user_queue;
		std::queue<std::string> torrent_queue;
		std::queue<std::string> peer_queue;
		std::queue<std::string> peer_hist_queue;
		std::queue<std::string> snatch_queue;
		std::queue<std::string> token_queue;

		std::string mysql_db, mysql_host, mysql_username, mysql_password;
		bool u_active, t_active, p_active, s_active, h_active, tok_active;
		bool readonly, load_peerlists;

		// These locks prevent more than one thread from reading/writing the buffers.
		// These should be held for the minimum time possible.
		std::mutex user_queue_lock;
		std::mutex torrent_buffer_lock;
		std::mutex torrent_queue_lock;
		std::mutex peer_queue_lock;
		std::mutex peer_hist_queue_lock;
		std::mutex snatch_queue_lock;
		std::mutex token_queue_lock;

		void load_config();
		void load_tokens(torrent_list &torrents);

		void do_flush_users();
		void do_flush_torrents();
		void do_flush_snatches();
		void do_flush_peers();
		void do_flush_peer_hist();
		void do_flush_tokens();

		void flush_users();
		void flush_torrents();
		void flush_snatches();
		void flush_peers();
		void flush_peer_hist();
		void flush_tokens();
		void clear_peer_data();

		peer_list::iterator add_peer(peer_list &peer_list, const std::string &peer_id);
		static inline bool peer_is_visible(user_ptr &u, peer *p);

	public:
		mysql();
		void shutdown();
		void reload_config();
		bool connected();
		void load_site_options();
		void load_torrents(torrent_list &torrents);
		void load_users(user_list &users);
		void load_peers(torrent_list &torrents, user_list &users);
		void load_seeders(torrent_list &torrents, user_list &users);
		void load_leechers(torrent_list &torrents, user_list &users);
		void load_blacklist(std::vector<std::string> &blacklist);

		void record_user(const std::string &record); // (id,uploaded_change,downloaded_change)
		void record_torrent(const std::string &record); // (id,seeders,leechers,snatched_change,balance)
		void record_snatch(const std::string &record, const std::string &ipv4, const std::string &ipv6); // (uid,fid,tstamp)
		void record_peer(const std::string &record, const std::string &ipv4, const std::string &ipv6, int port, const std::string &peer_id, const std::string &useragent); // (uid,fid,active,peerid,useragent,ip,port,uploaded,downloaded,upspeed,downspeed,left,timespent,announces,tstamp)
		void record_peer(const std::string &record, const std::string &peer_id); // (fid,peerid,timespent,announces,tstamp)
		void record_peer_hist(const std::string &record, const std::string &peer_id, const std::string &ipv4, const std::string &ipv6, int tid);
		void record_token(const std::string &record);

		void flush();

		bool all_clear();

		void report();

		std::mutex torrent_list_mutex;
		std::mutex user_list_mutex;
		std::mutex domain_list_mutex;
		std::mutex blacklist_mutex;
};

#pragma GCC visibility pop
#endif
