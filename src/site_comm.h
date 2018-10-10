#ifndef RADIANCE_SITE_COMM_H
#define RADIANCE_SITE_COMM_H
#include <string>
#include <boost/asio.hpp>
#include <queue>
#include <mutex>

using boost::asio::ip::tcp;

class site_comm {
	private:
		std::string site_host;
		std::string site_path;
		std::string site_password;
		std::mutex expire_queue_lock;
		std::string expire_token_buffer;
		std::queue<std::string> token_queue;
		bool readonly;
		bool t_active;
		void load_config();
		void do_flush_tokens();

	public:
		site_comm();
		void reload_config();
		bool all_clear();
		void expire_token(int torrent, int user);
		void flush_tokens();
		~site_comm();
};
#endif
