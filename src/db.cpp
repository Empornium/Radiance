#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "radiance.h"
#include "logger.h"
#include "db.h"
#include "user.h"
#include "misc_functions.h"
#include "config.h"
#include "logger.h"
#include <string>
#include <chrono>
#include <iostream>
#include <queue>
#include <unistd.h>
#include <ctime>
#include <mutex>
#include <thread>
#include <unordered_set>

#define DB_LOCK_TIMEOUT 50

mysql::mysql() : u_active(false), t_active(false), p_active(false), s_active(false), h_active(false), tok_active(false) {
	load_config();
	if (mysql_db == "") {
		syslog(info) << "No database selected";
		return;
	}

	try {
		conn.set_option(new mysqlpp::ReconnectOption(true));
		conn.connect(mysql_db.c_str(), mysql_host.c_str(), mysql_username.c_str(), mysql_password.c_str(), 0);
	} catch (const mysqlpp::Exception &er) {
		syslog(error) << "Failed to connect to MySQL (" << er.what() << ')';
		return;
	}
	syslog(info) << "Connected to MySQL";

	if (!readonly && !load_peerlists) {
		syslog(info) << "Clearing xbt_files_users and resetting peer counts...";
		clear_peer_data();
		syslog(info) << "done";
	}
}

void mysql::shutdown() {
	conn.disconnect();
	conn.thread_end();
	mysql_library_end();
}

void mysql::load_config() {
	mysql_db       = conf->get_str("mysql_db");
	mysql_host     = conf->get_str("mysql_host");
	mysql_username = conf->get_str("mysql_username");
	mysql_password = conf->get_str("mysql_password");
	readonly       = conf->get_bool("readonly");
	load_peerlists = conf->get_bool("load_peerlists");
}

void mysql::reload_config() {
	load_config();
}

bool mysql::connected() {
	return conn.connected();
}

void mysql::clear_peer_data() {
	try {
		mysqlpp::Query query = conn.query("TRUNCATE xbt_files_users;");
		if (!query.exec()) {
			syslog(error) << "Unable to truncate xbt_files_users!";
		}
		query = conn.query("UPDATE torrents SET Seeders = 0, Leechers = 0;");
		if (!query.exec()) {
			syslog(error) << "Unable to reset seeder and leecher count!";
		}
	} catch (const mysqlpp::BadQuery &er) {
		syslog(error) << "Query error in clear_peer_data: " << er.what();
	} catch (const mysqlpp::Exception &er) {
		syslog(error) << "Query error in clear_peer_data: " << er.what();
	}
}

void mysql::load_site_options() {
	for(auto &opt: opts->get_settings()) {
		try {
			mysqlpp::Query query = conn.query();
			query << "SELECT Value FROM options WHERE Name=" << mysqlpp::quote << opt.first;
			if(mysqlpp::StoreQueryResult res = query.store()) {
				for (size_t i = 0; i < res.num_rows(); i++) {
					std::string value(res[i][0]);
					opts->set("tracker", opt.first, value);
				}
			}
		} catch (const mysqlpp::BadQuery &er) {
			syslog(error) << "Query error in load_site_options: " << er.what();
		} catch (const mysqlpp::BadConversion &er) {
			syslog(error) << "Query error in load_site_options: " << er.what();
		} catch (const mysqlpp::Exception &er) {
			syslog(error) << "Query error in load_site_options: " << er.what();
		}
	}
}

void mysql::load_torrents(torrent_list &torrents) {
	mysqlpp::Query query = conn.query("SELECT ID, info_hash, freetorrent, doubletorrent, Snatched FROM torrents ORDER BY ID;");
	try {
		mysqlpp::StoreQueryResult res = query.store();
		std::unordered_set<std::string> cur_keys;
		size_t num_rows = res.num_rows();
		std::lock_guard<std::mutex> tl_lock(torrent_list_mutex);
		if (torrents.empty()) {
			torrents.reserve(num_rows * 1.05); // Reserve 5% extra space to prevent rehashing
		} else {
			// Create set with all currently known info hashes to remove nonexistent ones later
			cur_keys.reserve(torrents.size());
			for (auto const &it: torrents) {
				cur_keys.insert(it.first);
			}
		}
		for (size_t i = 0; i < num_rows; i++) {
			std::string info_hash;
			res[i][1].to_string(info_hash);
			if (info_hash == "") {
				continue;
			}
			mysqlpp::sql_enum free_torrent(res[i][2]);
			mysqlpp::sql_enum double_seed(res[i][3]);

			torrent tmp_tor;
			auto it = torrents.insert(std::pair<std::string, torrent>(info_hash, tmp_tor));
			torrent &tor = (it.first)->second;
			if (it.second) {
				tor.id = res[i][0];
				tor.balance = 0;
				tor.completed = res[i][4];
				tor.last_selected_seeder = "";
			} else {
				tor.tokened_users.clear();
				cur_keys.erase(info_hash);
			}
			if (free_torrent == "1") {
				tor.free_torrent = FREE;
			} else if (free_torrent == "2") {
				tor.free_torrent = NEUTRAL;
			} else {
				tor.free_torrent = NORMAL;
			}
			if (double_seed == "1") {
				tor.double_torrent = DOUBLE;
			} else {
				tor.double_torrent = NORMAL;
			}

		}
		for (auto const &info_hash: cur_keys) {
			// Remove tracked torrents that weren't found in the database
			auto it = torrents.find(info_hash);
			if (it != torrents.end()) {
				torrent &tor = it->second;
				stats.leechers -= tor.leechers.size();
				stats.seeders -= tor.seeders.size();
				for (auto &p: tor.leechers) {
					p.second.user->decr_leeching();
				}
				for (auto &p: tor.seeders) {
					p.second.user->decr_seeding();
				}
				torrents.erase(it);
			}
		}
	} catch (const mysqlpp::BadQuery &er) {
		syslog(error) << "Query error in load_torrents: " << er.what();
		return;
	}
	syslog(trace) << "Loaded " << torrents.size() << " torrents";
	load_tokens(torrents);
}

void mysql::load_users(user_list &users) {
	mysqlpp::Query query = conn.query("SELECT ID, can_leech, torrent_pass, (Visible='0' OR IP='127.0.0.1') AS Protected, track_ipv6, personal_freeleech, personal_doubleseed FROM users_main WHERE Enabled='1'");
	try {
		mysqlpp::StoreQueryResult res = query.store();
		size_t num_rows = res.num_rows();
		std::unordered_set<std::string> cur_keys;
		std::lock_guard<std::mutex> ul_lock(user_list_mutex);
		if (users.empty()) {
			users.reserve(num_rows * 1.05); // Reserve 5% extra space to prevent rehashing
		} else {
			// Create set with all currently known user keys to remove nonexistent ones later
			cur_keys.reserve(users.size());
			for (auto const &it: users) {
				cur_keys.insert(it.first);
			}
		}
		for (size_t i = 0; i < num_rows; i++) {
			std::string passkey(res[i][2]);
			bool protect_ip = res[i][3];
			bool track_ipv6 = res[i][4];
			mysqlpp::DateTime pfl = res[i][5];
			mysqlpp::DateTime pds = res[i][6];
			user_ptr tmp_user = std::make_shared<user>(res[i][0], res[i][1], protect_ip, track_ipv6, pfl, pds);
			auto it = users.insert(std::pair<std::string, user_ptr>(passkey, tmp_user));
			if (!it.second) {
				user_ptr &u = (it.first)->second;
				u->set_personalfreeleech(pfl);
				u->set_personaldoubleseed(pds);
				u->set_leechstatus(res[i][1]);
				u->set_protected(protect_ip);
				u->set_track_ipv6(track_ipv6);
				u->set_deleted(false);
				cur_keys.erase(passkey);
			}
		}
		for (auto const &passkey: cur_keys) {
			// Remove users that weren't found in the database
			auto it = users.find(passkey);
			if (it != users.end()) {
				it->second->set_deleted(true);
				users.erase(it);
			}
		}
	} catch (const mysqlpp::BadQuery &er) {
		syslog(error) << "Query error in load_users: " << er.what();
		return;
	} catch (const mysqlpp::BadConversion &er) {
		syslog(error) << "Query error in load_users: " << er.what();
	}
	syslog(trace) << "Loaded " << users.size() << " users";
}

void mysql::load_peers(torrent_list &torrents, user_list &users) {
	if (!load_peerlists) return;
	load_seeders(torrents, users);
	load_leechers(torrents, users);
}

void mysql::load_seeders(torrent_list &torrents, user_list &users) {
	if (!load_peerlists) return;
	size_t num_seeders = 0;
	try {
		for (auto &torrent_it: torrents) {
			torrent torrent = torrent_it.second;
			mysqlpp::Query query = conn.query();
			query << "SELECT um.torrent_pass, xfu.peer_id, xfu.port, xfu.ipv4, xfu.ipv6, xfu.uploaded,"
			      << " xfu.downloaded, xfu.remaining, xfu.corrupt, xfu.announced, xfu.ctime, xfu.mtime"
			      << " FROM xbt_files_users AS xfu INNER JOIN users_main AS um ON xfu.uid=um.ID"
			      << " WHERE xfu.active='1' AND um.Enabled='1' AND xfu.remaining=0 AND xfu.fid=" << torrent.id;
			size_t num_rows = 0;
			std::unordered_set<std::string> cur_keys;
			mysqlpp::StoreQueryResult res = query.store();
			num_rows = res.num_rows();
			num_seeders += num_rows;
			std::lock_guard<std::mutex> ul_lock(user_list_mutex);
			std::lock_guard<std::mutex> tl_lock(torrent_list_mutex);
			if (torrent.seeders.empty()) {
				torrent.seeders.reserve(num_rows * 1.05); // Reserve 5% extra space to prevent rehashing
			} else {
				// Create set with all currently known user keys to remove nonexistent ones later
				cur_keys.reserve(torrent.seeders.size());
				for (auto const &it: torrent.seeders) {
					cur_keys.insert(it.first);
				}
			}
			for (size_t i = 0; i < num_rows; i++) {
				std::string passkey(res[i][0]);
				std::string peer_id(res[i][1]);

				peer * p;
				peer_list::iterator peer_it;
				user_ptr u  = users.find(passkey)->second;
				userid_t userid = u->get_id();

				std::stringstream peer_key_stream;
				peer_key_stream << peer_id[12 + (torrent.id & 7)] // "Randomize" the element order in the peer map by prefixing with a peer id byte
					<< userid // Include user id in the key to lower chance of peer id collisions
					<< peer_id;
				const std::string peer_key(peer_key_stream.str());
				peer_it = torrent.seeders.find(peer_key);
				if (peer_it == torrent.seeders.end()) {
					peer_it = add_peer(torrent.seeders, peer_key);

				}

				p = &peer_it->second;
				p->user = u;
				p->user->incr_seeding();
				stats.seeders++;

				p->port			= res[i][2];
				res[i][3].to_string(p->ipv4);
				p->ipv4_port		= "";
				res[i][4].to_string(p->ipv6);
				p->ipv6_port		= "";
				p->uploaded		= res[i][5];
				p->downloaded		= res[i][6];
				p->left			= res[i][7];
				p->corrupt		= res[i][8];
				p->announces		= res[i][9];
				p->first_announced	= res[i][10];
				p->last_announced	= res[i][11];

				// Validate IPv4 address and extract binary representation
				if(!p->ipv4.empty()){
					// IP+Port is 6 bytes for IPv4
					p->ipv4_port = p->ipv4;
					p->ipv4_port.push_back(p->port >> 8);
					p->ipv4_port.push_back(p->port & 0xFF);
				}

				// Validate IPv6 address and extract binary representation
				if(!p->ipv6.empty()){
					// IP+Port is 18 bytes for IPv6
					p->ipv6_port = p->ipv6;
					p->ipv6_port.push_back(p->port >> 8);
					p->ipv6_port.push_back(p->port & 0xFF);
				}

				p->visible		= peer_is_visible(u, p);
			}
		}
	} catch (const mysqlpp::BadQuery &er) {
		syslog(error) << "Query error in load_seeders: " << er.what();
		return;
	} catch (const mysqlpp::BadConversion &er) {
		syslog(error) << "Query error in load_seeders: " << er.what();
	}
	syslog(trace) << "Loaded " << num_seeders << " seeders";
}

void mysql::load_leechers(torrent_list &torrents, user_list &users) {
	if (!load_peerlists) return;
	size_t num_leechers = 0;
	try {
		for (auto &torrent_it: torrents) {
			torrent torrent = torrent_it.second;
			mysqlpp::Query query = conn.query();
			query << "SELECT um.torrent_pass, xfu.peer_id, xfu.port, xfu.ipv4, xfu.ipv6, xfu.uploaded,"
			      << " xfu.downloaded, xfu.remaining, xfu.corrupt, xfu.announced, xfu.ctime, xfu.mtime"
			      << " FROM xbt_files_users AS xfu INNER JOIN users_main AS um ON xfu.uid=um.ID"
			      << " WHERE xfu.active='1' AND um.Enabled='1' AND um.can_leech='1' AND xfu.remaining!=0 AND xfu.fid=" << torrent.id;
			size_t num_rows = 0;
			std::unordered_set<std::string> cur_keys;
			mysqlpp::StoreQueryResult res = query.store();
			num_rows = res.num_rows();
			num_leechers += num_rows;
			std::lock_guard<std::mutex> ul_lock(user_list_mutex);
			std::lock_guard<std::mutex> tl_lock(torrent_list_mutex);
			if (torrent.leechers.empty()) {
				torrent.leechers.reserve(num_rows * 1.05); // Reserve 5% extra space to prevent rehashing
			} else {
				// Create set with all currently known user keys to remove nonexistent ones later
				cur_keys.reserve(torrent.leechers.size());
				for (auto const &it: torrent.leechers) {
					cur_keys.insert(it.first);
				}
			}
			for (size_t i = 0; i < num_rows; i++) {
				std::string passkey(res[i][0]);
				std::string peer_id(res[i][1]);

				peer * p;
				peer_list::iterator peer_it;
				user_ptr u  = users.find(passkey)->second;
				userid_t userid = u->get_id();

				std::stringstream peer_key_stream;
				peer_key_stream << peer_id[12 + (torrent.id & 7)] // "Randomize" the element order in the peer map by prefixing with a peer id byte
					<< userid // Include user id in the key to lower chance of peer id collisions
					<< peer_id;
				const std::string peer_key(peer_key_stream.str());
				peer_it = torrent.leechers.find(peer_key);
				if (peer_it == torrent.leechers.end()) {
					peer_it = add_peer(torrent.leechers, peer_key);

				}

				p = &peer_it->second;
				p->user = u;
				p->user->incr_leeching();
				stats.leechers++;

				p->port			= res[i][2];
				res[i][3].to_string(p->ipv4);
				p->ipv4_port		= "";
				res[i][4].to_string(p->ipv6);
				p->ipv6_port		= "";
				p->uploaded		= res[i][5];
				p->downloaded		= res[i][6];
				p->left			= res[i][7];
				p->corrupt		= res[i][8];
				p->announces		= res[i][9];
				p->first_announced	= res[i][10];
				p->last_announced	= res[i][11];

				// Validate IPv4 address and extract binary representation
				if(!p->ipv4.empty()){
					// IP+Port is 6 bytes for IPv4
					p->ipv4_port = p->ipv4;
					p->ipv4_port.push_back(p->port >> 8);
					p->ipv4_port.push_back(p->port & 0xFF);
				}

				// Validate IPv6 address and extract binary representation
				if(!p->ipv6.empty()){
					// IP+Port is 18 bytes for IPv6
					p->ipv6_port = p->ipv6;
					p->ipv6_port.push_back(p->port >> 8);
					p->ipv6_port.push_back(p->port & 0xFF);
				}

				p->visible		= peer_is_visible(u, p);
			}
		}
	} catch (const mysqlpp::BadQuery &er) {
		syslog(error) << "Query error in load_leechers: " << er.what();
		return;
	} catch (const mysqlpp::BadConversion &er) {
		syslog(error) << "Query error in load_leechers: " << er.what();
	}
	syslog(trace) << "Loaded " << num_leechers << " leechers";
}

peer_list::iterator mysql::add_peer(peer_list &peer_list, const std::string &peer_key) {
	peer new_peer;
	auto it = peer_list.insert(std::pair<std::string, peer>(peer_key, new_peer));
	return it.first;
}

/* Peers should be invisible if they are a leecher without
   download privs or their IP is invalid */
bool mysql::peer_is_visible(user_ptr &u, peer *p) {
	return (p->left == 0 || u->can_leech());
}

void mysql::load_tokens(torrent_list &torrents) {
	mysqlpp::Query query = conn.query("SELECT us.UserID, us.FreeLeech, us.DoubleSeed, t.info_hash FROM users_slots AS us JOIN torrents AS t ON t.ID = us.TorrentID WHERE FreeLeech >= NOW() OR DoubleSeed >= NOW();");
	int token_count = 0;
	try {
		mysqlpp::StoreQueryResult res = query.store();
		size_t num_rows = res.num_rows();
		std::lock_guard<std::mutex> tl_lock(torrent_list_mutex);
		for (size_t i = 0; i < num_rows; i++) {
			std::string info_hash;
			res[i][3].to_string(info_hash);
			auto it = torrents.find(info_hash);
			if (it != torrents.end()) {
				mysqlpp::DateTime fl = res[i][1];
				mysqlpp::DateTime ds = res[i][2];
				slots_t slots;
				slots.free_leech = fl;
				slots.double_seed = ds;

				torrent &tor = it->second;
				tor.tokened_users.insert(std::pair<int, slots_t>(res[i][0], slots));
				++token_count;
			}
		}
	} catch (const mysqlpp::BadQuery &er) {
		syslog(error) << "Query error in load_tokens: " << er.what();
		return;
	}
	syslog(trace) << "Loaded " << token_count << " tokens";
}


void mysql::load_blacklist(std::vector<std::string> &blacklist) {
	mysqlpp::Query query = conn.query("SELECT peer_id FROM xbt_client_blacklist;");
	try {
		mysqlpp::StoreQueryResult res = query.store();
		size_t num_rows = res.num_rows();
		std::lock_guard<std::mutex> wl_lock(blacklist_mutex);
		blacklist.clear();
		for (size_t i = 0; i<num_rows; i++) {
			std::string peer_id;
			res[i][0].to_string(peer_id);
			blacklist.push_back(peer_id);
		}
	} catch (const mysqlpp::BadQuery &er) {
		syslog(error) << "Query error in load_blacklist: " << er.what();
		return;
	}
	if (blacklist.empty()) {
		syslog(info) << "Assuming no blacklist desired, disabling";
	} else {
		syslog(trace) << "Loaded " << blacklist.size() << " clients into the blacklist";
	}
}

void mysql::record_token(const std::string &record) {
	if (update_token_buffer != "") {
		update_token_buffer += ",";
	}
	update_token_buffer += record;
}

void mysql::record_user(const std::string &record) {
	if (update_user_buffer != "") {
		update_user_buffer += ",";
	}
	update_user_buffer += record;
}

void mysql::record_torrent(const std::string &record) {
	std::lock_guard<std::mutex> tb_lock(torrent_buffer_lock);
	if (update_torrent_buffer != "") {
		update_torrent_buffer += ",";
	}
	update_torrent_buffer += record;
}

void mysql::record_peer(const std::string &record, const std::string &ipv4, const std::string &ipv6, int port, const std::string &peer_id, const std::string &useragent) {
	std::lock_guard<std::mutex> pb_lock(peer_queue_lock);
	if (update_peer_heavy_buffer != "") {
		update_peer_heavy_buffer += ",";
	}
	mysqlpp::Query q = conn.query();
	q << record << mysqlpp::quote << ipv4 << ',' << mysqlpp::quote << ipv6 << ',' << port << ',' << mysqlpp::quote << peer_id << ',' << mysqlpp::quote << useragent << ')';

	update_peer_heavy_buffer += q.str();
}
void mysql::record_peer(const std::string &record, const std::string &peer_id) {
	std::lock_guard<std::mutex> pb_lock(peer_queue_lock);
	if (update_peer_light_buffer != "") {
		update_peer_light_buffer += ",";
	}
	mysqlpp::Query q = conn.query();
	q << record << mysqlpp::quote << peer_id << ')';

	update_peer_light_buffer += q.str();
}

void mysql::record_peer_hist(const std::string &record, const std::string &peer_id, const std::string &ipv4, const std::string &ipv6, int tid){
	std::lock_guard<std::mutex> ph_lock(peer_hist_queue_lock);
	if (update_peer_hist_buffer != "") {
		update_peer_hist_buffer += ",";
	}
	mysqlpp::Query q = conn.query();
	q << record << ',' << mysqlpp::quote << peer_id << ',' << mysqlpp::quote << ipv4 << ',' << mysqlpp::quote << ipv6 << ',' << tid << ',' << time(NULL) << ')';
	update_peer_hist_buffer += q.str();
}

void mysql::record_snatch(const std::string &record, const std::string &ipv4, const std::string &ipv6) {
	if (update_snatch_buffer != "") {
		update_snatch_buffer += ",";
	}
	mysqlpp::Query q = conn.query();
	q << record << ',' << mysqlpp::quote << ipv4 << ',' << mysqlpp::quote << ipv6 << ')';
	update_snatch_buffer += q.str();
}

bool mysql::all_clear() {
	return (user_queue.empty() && torrent_queue.empty() && peer_queue.empty() && snatch_queue.empty() && token_queue.empty());
}

void mysql::flush() {
	flush_users();
	flush_torrents();
	flush_snatches();
	flush_peers();
	flush_peer_hist();
	flush_tokens();
}

void mysql::flush_users() {
	if (readonly) {
		update_user_buffer.clear();
		return;
	}
	std::string sql;
	std::lock_guard<std::mutex> uq_lock(user_queue_lock);
	size_t qsize = user_queue.size();
	if (qsize > 0) {
		syslog(trace) << "User flush queue size: " << qsize << ", next query length: " << user_queue.front().size();
	}
	if (update_user_buffer == "") {
		return;
	}
	sql = "INSERT INTO users_main (ID, Uploaded, Downloaded, UploadedDaily, DownloadedDaily) VALUES " + update_user_buffer +
		" ON DUPLICATE KEY UPDATE" +
		" Uploaded = Uploaded + VALUES(Uploaded)," +
		" Downloaded = Downloaded + VALUES(Downloaded)," +
		" UploadedDaily = UploadedDaily + VALUES(UploadedDaily)," +
		" DownloadedDaily = DownloadedDaily + VALUES(DownloadedDaily)";
	user_queue.push(sql);
	update_user_buffer.clear();
	if (u_active == false) {
		std::thread thread(&mysql::do_flush_users, this);
		thread.detach();
	}
}

void mysql::flush_torrents() {
	std::lock_guard<std::mutex> tb_lock(torrent_buffer_lock);
	if (readonly) {
		update_torrent_buffer.clear();
		return;
	}
	std::string sql;
	std::lock_guard<std::mutex> tq_lock(torrent_queue_lock);
	size_t qsize = torrent_queue.size();
	if (qsize > 0) {
		syslog(trace) << "Torrent flush queue size: " << qsize << ", next query length: " << torrent_queue.front().size();
	}
	if (update_torrent_buffer == "") {
		return;
	}
	sql = "INSERT INTO torrents (ID,Seeders,Leechers,Snatched,Balance) VALUES " + update_torrent_buffer +
		" ON DUPLICATE KEY UPDATE Seeders=VALUES(Seeders), Leechers=VALUES(Leechers), " +
		"Snatched=Snatched+VALUES(Snatched), Balance=VALUES(Balance), last_action = " +
		"IF(VALUES(Seeders) > 0, NOW(), last_action)";
	torrent_queue.push(sql);
	update_torrent_buffer.clear();
	sql.clear();
	sql = "DELETE FROM torrents WHERE info_hash = ''";
	torrent_queue.push(sql);
	if (t_active == false) {
		std::thread thread(&mysql::do_flush_torrents, this);
		thread.detach();
	}
}

void mysql::flush_snatches() {
	if (readonly) {
		update_snatch_buffer.clear();
		return;
	}
	std::string sql;
	std::lock_guard<std::mutex> sq_lock(snatch_queue_lock);
	size_t qsize = snatch_queue.size();
	if (qsize > 0) {
		syslog(trace) << "Snatch flush queue size: " << qsize << ", next query length: " << snatch_queue.front().size();
	}
	if (update_snatch_buffer == "" ) {
		return;
	}
	sql = "INSERT INTO xbt_snatched (uid, fid, tstamp, ipv4, ipv6) VALUES " + update_snatch_buffer;
	snatch_queue.push(sql);
	update_snatch_buffer.clear();
	if (s_active == false) {
		std::thread thread(&mysql::do_flush_snatches, this);
		thread.detach();
	}
}

void mysql::flush_peers() {
	if (readonly) {
		update_peer_light_buffer.clear();
		update_peer_heavy_buffer.clear();
		return;
	}
	std::string sql;
	std::lock_guard<std::mutex> pq_lock(peer_queue_lock);
	size_t qsize = peer_queue.size();
	if (qsize > 0) {
		syslog(trace) << "Peer flush queue size: " << qsize << ", next query length: " << peer_queue.front().size();
	}

	// Nothing to do
	if (update_peer_light_buffer == "" && update_peer_heavy_buffer == "") {
		return;
	}

	if (update_peer_heavy_buffer != "") {
		// Because xfu inserts are slow and ram is not infinite we need to
		// limit this queue's size
		// xfu will be messed up if the light query inserts a new row,
		// but that's better than an oom crash
		if (qsize >= 1000) {
			peer_queue.pop();
		}
		sql = "INSERT INTO xbt_files_users (uid,fid,active,uploaded,downloaded,upspeed,downspeed,remaining,corrupt," +
			std::string("timespent,ctime,mtime,announced,ipv4,ipv6,port,peer_id,useragent) VALUES ") + update_peer_heavy_buffer +
					" ON DUPLICATE KEY UPDATE active=VALUES(active), uploaded=VALUES(uploaded), " +
					"downloaded=VALUES(downloaded), upspeed=VALUES(upspeed), " +
					"downspeed=VALUES(downspeed), remaining=VALUES(remaining), " +
					"corrupt=VALUES(corrupt), timespent=VALUES(timespent), " +
					"announced=VALUES(announced), mtime=VALUES(mtime), port=VALUES(port)";
		peer_queue.push(sql);
		update_peer_heavy_buffer.clear();
		sql.clear();
	}
	if (update_peer_light_buffer != "") {
		// See comment above
		if (qsize >= 1000) {
			peer_queue.pop();
		}
		sql = "INSERT INTO xbt_files_users (uid,fid,timespent,mtime,announced,peer_id) VALUES " +
					update_peer_light_buffer +
					" ON DUPLICATE KEY UPDATE upspeed=0, downspeed=0, timespent=VALUES(timespent), " +
					"announced=VALUES(announced), mtime=VALUES(mtime)";
		peer_queue.push(sql);
		update_peer_light_buffer.clear();
		sql.clear();
	}

	if (p_active == false) {
		std::thread thread(&mysql::do_flush_peers, this);
		thread.detach();
	}
}

void mysql::flush_peer_hist() {
	if (readonly) {
		update_token_buffer.clear();
		return;
	}
	std::string sql;
	std::lock_guard<std::mutex> ph_lock(peer_hist_queue_lock);
	if (update_peer_hist_buffer == "") {
		return;
	}

	sql = "INSERT IGNORE INTO xbt_peers_history (uid, downloaded, remaining, uploaded, upspeed, downspeed, timespent, peer_id, ipv4, ipv6, fid, mtime) VALUES " + update_peer_hist_buffer;
	peer_hist_queue.push(sql);
	update_peer_hist_buffer.clear();
	if (h_active == false) {
		std::thread thread(&mysql::do_flush_peer_hist, this);
		thread.detach();
	}
}

void mysql::flush_tokens() {
	if (readonly) {
		update_token_buffer.clear();
		return;
	}
	std::string sql;
	std::lock_guard<std::mutex> tq_lock(token_queue_lock);
	size_t qsize = token_queue.size();
	if (qsize > 0) {
		syslog(trace) << "Token flush queue size: " << qsize << ", next query length: " << token_queue.front().size();
	}
	if (update_token_buffer == "") {
		return;
	}
	sql = "INSERT INTO users_freeleeches (UserID, TorrentID, Downloaded, Uploaded) VALUES " + update_token_buffer +
		" ON DUPLICATE KEY UPDATE Downloaded = Downloaded + VALUES(Downloaded), Uploaded = Uploaded + VALUES(Uploaded)";
	token_queue.push(sql);
	update_token_buffer.clear();
	if (tok_active == false) {
		std::thread thread(&mysql::do_flush_tokens, this);
		thread.detach();
	}
}

void mysql::do_flush_users() {
	u_active = true;
	try {
		mysqlpp::Connection c(mysql_db.c_str(), mysql_host.c_str(), mysql_username.c_str(), mysql_password.c_str(), 0);
		while (!user_queue.empty()) {
			try {
				std::string sql = user_queue.front();
				if (sql == "") {
					user_queue.pop();
					continue;
				}
				mysqlpp::Query query = c.query(sql);
				auto start_time = std::chrono::high_resolution_clock::now();
				if (!query.exec()) {
					syslog(error) << "User flush failed (" << user_queue.size() << " remain)";
					sleep(3);
					continue;
				} else {
					std::lock_guard<std::mutex> uq_lock(user_queue_lock);
					user_queue.pop();
				}
				auto end_time = std::chrono::high_resolution_clock::now();
				syslog(trace) << "Users flushed in " << std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() << " microseconds.";
			}
			catch (const mysqlpp::BadQuery &er) {
				syslog(error) << "Query error: " << er.what() << " in flush users with a qlength: " << user_queue.front().size() << " queue size: " << user_queue.size();
				sleep(3);
				continue;
			} catch (const mysqlpp::Exception &er) {
				syslog(error) << "Query error: " << er.what() << " in flush users with a qlength: " << user_queue.front().size() <<  " queue size: " << user_queue.size();
				sleep(3);
				continue;
			}
		}
		c.thread_end();
	}
	catch (const mysqlpp::Exception &er) {
		syslog(error) << "MySQL error in flush_users: " << er.what();
	}
	u_active = false;
}

void mysql::do_flush_torrents() {
	t_active = true;
	try {
		mysqlpp::Connection c(mysql_db.c_str(), mysql_host.c_str(), mysql_username.c_str(), mysql_password.c_str(), 0);
		while (!torrent_queue.empty()) {
			try {
				std::string sql = torrent_queue.front();
				if (sql == "") {
					torrent_queue.pop();
					continue;
				}
				mysqlpp::Query query = c.query(sql);
				auto start_time = std::chrono::high_resolution_clock::now();
				if (!query.exec()) {
					syslog(error) << "Torrent flush failed (" << torrent_queue.size() << " remain)";
					sleep(3);
					continue;
				} else {
					std::lock_guard<std::mutex> tq_lock(torrent_queue_lock);
					torrent_queue.pop();
				}
				auto end_time = std::chrono::high_resolution_clock::now();
				syslog(trace) << "Torrents flushed in " << std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() << " microseconds.";
			}
			catch (const mysqlpp::BadQuery &er) {
				syslog(error) << "Query error: " << er.what() << " in flush torrents with a qlength: " << torrent_queue.front().size() << " queue size: " << torrent_queue.size();
				sleep(3);
				continue;
			} catch (const mysqlpp::Exception &er) {
				syslog(error) << "Query error: " << er.what() << " in flush torrents with a qlength: " << torrent_queue.front().size() << " queue size: " << torrent_queue.size();
				sleep(3);
				continue;
			}
		}
		c.thread_end();
	}
	catch (const mysqlpp::Exception &er) {
		syslog(error) << "MySQL error in flush_torrents: " << er.what();
	}
	t_active = false;
}

void mysql::do_flush_peers() {
	p_active = true;
	try {
		mysqlpp::Connection c(mysql_db.c_str(), mysql_host.c_str(), mysql_username.c_str(), mysql_password.c_str(), 0);
		while (!peer_queue.empty()) {
			try {
				std::string sql = peer_queue.front();
				if (sql == "") {
					peer_queue.pop();
					continue;
				}
				mysqlpp::Query query = c.query(sql);
				auto start_time = std::chrono::high_resolution_clock::now();
				if (!query.exec()) {
					syslog(error) << "Peer flush failed (" << peer_queue.size() << " remain)";
					sleep(3);
					continue;
				} else {
					std::lock_guard<std::mutex> pq_lock(peer_queue_lock);
					peer_queue.pop();
				}
				auto end_time = std::chrono::high_resolution_clock::now();
				syslog(trace) << "Peers flushed in " << std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() << " microseconds.";
			}
			catch (const mysqlpp::BadQuery &er) {
				syslog(error) << "Query error: " << er.what() << " in flush peers with a qlength: " << peer_queue.front().size() << " queue size: " << peer_queue.size();
				sleep(3);
				continue;
			} catch (const mysqlpp::Exception &er) {
				syslog(error) << "Query error: " << er.what() << " in flush peers with a qlength: " << peer_queue.front().size() << " queue size: " << peer_queue.size();
				sleep(3);
				continue;
			}
		}
		c.thread_end();
	}
	catch (const mysqlpp::Exception &er) {
		syslog(error) << "MySQL error in flush_peers: " << er.what();
	}
	p_active = false;
}


void mysql::do_flush_peer_hist() {
	h_active = true;
	try {
		mysqlpp::Connection c(mysql_db.c_str(), mysql_host.c_str(), mysql_username.c_str(), mysql_password.c_str(), 0);
		while (!peer_hist_queue.empty()) {
			try {
				std::string sql = peer_hist_queue.front();
				if (sql == "") {
					peer_hist_queue.pop();
					continue;
				}
				mysqlpp::Query query = c.query(sql);
				auto start_time = std::chrono::high_resolution_clock::now();
				if (!query.exec()) {
					syslog(error) << "Peer history flush failed (" << peer_hist_queue.size() << " remain)";
					sleep(3);
					continue;
				} else {
					std::lock_guard<std::mutex> ph_lock(peer_hist_queue_lock);
					peer_hist_queue.pop();
				}
				auto end_time = std::chrono::high_resolution_clock::now();
				syslog(trace) << "Peer History flushed in " << std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() << " microseconds.";
			}
			catch (const mysqlpp::BadQuery &er) {
				syslog(error) << "Query error: " << er.what() << " in flush peer history with a qlength: " << peer_hist_queue.front().size() << " queue size: " << peer_hist_queue.size();
				sleep(3);
				continue;
			} catch (const mysqlpp::Exception &er) {
				syslog(error) << "Query error: " << er.what() << " in flush peer history with a qlength: " << peer_hist_queue.front().size() << " queue size: " << peer_hist_queue.size();
				sleep(3);
				continue;
			}
		}
		c.thread_end();
	}
	catch (const mysqlpp::Exception &er) {
		syslog(error) << "MySQL error in flush_peer_hist: " << er.what();
	}
	h_active = false;
}

void mysql::do_flush_snatches() {
	s_active = true;
	try {
		mysqlpp::Connection c(mysql_db.c_str(), mysql_host.c_str(), mysql_username.c_str(), mysql_password.c_str(), 0);
		while (!snatch_queue.empty()) {
			try {
				std::string sql = snatch_queue.front();
				if (sql == "") {
					snatch_queue.pop();
					continue;
				}
				mysqlpp::Query query = c.query(sql);
				auto start_time = std::chrono::high_resolution_clock::now();
				if (!query.exec()) {
					syslog(error) << "Snatch flush failed (" << snatch_queue.size() << " remain)";
					sleep(3);
					continue;
				} else {
					std::lock_guard<std::mutex> sq_lock(snatch_queue_lock);
					snatch_queue.pop();
				}
				auto end_time = std::chrono::high_resolution_clock::now();
				syslog(trace) << "Snatches flushed in " << std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() << " microseconds.";
			}
			catch (const mysqlpp::BadQuery &er) {
				syslog(error) << "Query error: " << er.what() << " in flush snatches with a qlength: " << snatch_queue.front().size() << " queue size: " << snatch_queue.size();
				sleep(3);
				continue;
			} catch (const mysqlpp::Exception &er) {
				syslog(error) << "Query error: " << er.what() << " in flush snatches with a qlength: " << snatch_queue.front().size() << " queue size: " << snatch_queue.size();
				sleep(3);
				continue;
			}
		}
		c.thread_end();
	}
	catch (const mysqlpp::Exception &er) {
		syslog(error) << "MySQL error in flush_snatches: " << er.what();
	}
	s_active = false;
}

void mysql::do_flush_tokens() {
	tok_active = true;
	try {
		mysqlpp::Connection c(mysql_db.c_str(), mysql_host.c_str(), mysql_username.c_str(), mysql_password.c_str(), 0);
		while (!token_queue.empty()) {
			try {
				std::string sql = token_queue.front();
				if (sql == "") {
					token_queue.pop();
					continue;
				}
				mysqlpp::Query query = c.query(sql);
				auto start_time = std::chrono::high_resolution_clock::now();
				if (!query.exec()) {
					syslog(error) << "Token flush failed (" << token_queue.size() << " remain)";
					sleep(3);
					continue;
				} else {
					std::lock_guard<std::mutex> tq_lock(token_queue_lock);
					token_queue.pop();
				}
				auto end_time = std::chrono::high_resolution_clock::now();
				syslog(trace) << "Tokens flushed in " << std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() << " microseconds.";
			}
			catch (const mysqlpp::BadQuery &er) {
				syslog(error) << "Query error: " << er.what() << " in flush tokens with a qlength: " << token_queue.front().size() << " queue size: " << token_queue.size();
				sleep(3);
				continue;
			} catch (const mysqlpp::Exception &er) {
				syslog(error) << "Query error: " << er.what() << " in flush tokens with a qlength: " << token_queue.front().size() << " queue size: " << token_queue.size();
				sleep(3);
				continue;
			}
		}
		c.thread_end();
	}
	catch (const mysqlpp::Exception &er) {
		syslog(error) << "MySQL error in flush_tokens: " << er.what();
	}
	tok_active = false;
}

void mysql::report() {
	stats.db.torrent_queue   = torrent_queue.size();
	stats.db.user_queue      = user_queue.size();
	stats.db.peer_queue      = peer_queue.size();
	stats.db.peer_hist_queue = peer_hist_queue.size();
	stats.db.snatch_queue    = snatch_queue.size();
	stats.db.token_queue     = token_queue.size();
}
