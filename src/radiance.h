#ifndef RADIANCE_H
#define RADIANCE_H

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#include <string>
#include <map>
#include <vector>
#include <unordered_map>
#include <set>
#include <memory>
#include <atomic>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef uint32_t torid_t;
typedef uint32_t userid_t;

class user;
typedef std::shared_ptr<user> user_ptr;

class domain;
typedef std::shared_ptr<domain> domain_ptr;

class settings;
extern settings *conf;

class options;
extern options  *opts;

typedef struct {
	int64_t uploaded;
	int64_t downloaded;
	int64_t corrupt;
	int64_t left;
	time_t last_announced;
	time_t first_announced;
	uint32_t announces;
	uint16_t port;
	bool visible;
	bool paused;
	user_ptr user;
	domain_ptr domain;
	std::string ipv4;
	std::string ipv4_port;
	std::string ipv6;
	std::string ipv6_port;
} peer;

typedef std::unordered_map<std::string, peer> peer_list;

enum freetype { NORMAL, FREE, DOUBLE, NEUTRAL };

typedef struct {
	time_t free_leech;
	time_t double_seed;
} slots_t;

typedef std::map<int, slots_t> slots_list;

typedef struct {
	torid_t id;
	uint32_t completed;
	uint32_t paused;
	int64_t balance;
	freetype free_torrent;
	freetype double_torrent;
	time_t last_flushed;
	peer_list seeders;
	peer_list leechers;
	std::string last_selected_seeder;
	std::string last_selected_leecher;
	std::map<int, slots_t> tokened_users;
} torrent;

enum {
	DUPE, // 0
	TRUMP, // 1
	BAD_FILE_NAMES, // 2
	BAD_FOLDER_NAMES, // 3
	BAD_TAGS, // 4
	BAD_FORMAT, // 5
	DISCS_MISSING, // 6
	DISCOGRAPHY,// 7
	EDITED_LOG,// 8
	INACCURATE_BITRATE, // 9
	LOW_BITRATE, // 10
	MUTT_RIP,// 11
	BAD_SOURCE,// 12
	ENCODE_ERRORS,// 13
	BANNED, // 14
	TRACKS_MISSING,// 15
	TRANSCODE, // 16
	CASSETTE, // 17
	UNSPLIT_ALBUM, // 18
	USER_COMPILATION, // 19
	WRONG_FORMAT, // 20
	WRONG_MEDIA, // 21
	AUDIENCE // 22
};

typedef struct {
	int reason;
	time_t time;
} del_message;

typedef struct {
	bool gzip;
	bool html;
	bool json;
	bool http_close;
} client_opts_t;

typedef std::unordered_map<std::string, torrent> torrent_list;
typedef std::unordered_map<std::string, user_ptr> user_list;
typedef std::unordered_map<std::string, domain_ptr> domain_list;
typedef std::unordered_map<std::string, std::string> params_type;


struct stats_t {
	std::atomic<uint32_t> open_connections;
	std::atomic<uint64_t> opened_connections;
	std::atomic<uint64_t> connection_rate;
	std::atomic<uint32_t> leechers;
	std::atomic<uint32_t> seeders;
	std::atomic<uint64_t> requests;
	std::atomic<uint64_t> request_rate;
	std::atomic<uint64_t> announcements;
	std::atomic<uint64_t> succ_announcements;
	std::atomic<uint64_t> scrapes;
	std::atomic<uint64_t> bytes_read;
	std::atomic<uint64_t> bytes_written;
	std::atomic<uint64_t> ipv6_peers;
	std::atomic<uint64_t> ipv4_peers;
	std::atomic<uint64_t> torrent_queue;
	std::atomic<uint64_t> user_queue;
	std::atomic<uint64_t> peer_queue;
	std::atomic<uint64_t> peer_hist_queue;
	std::atomic<uint64_t> snatch_queue;
	std::atomic<uint64_t> token_queue;
	time_t start_time;
};
extern struct stats_t stats;
#endif
