#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <ev++.h>
class schedule {
	private:
		void load_config();

		unsigned int reap_peers_interval;
		worker * work;
		database * db;
		site_comm * sc;
		uint64_t last_opened_connections;
		uint64_t last_request_count;
		unsigned int counter;
		int next_reap_peers;
	public:
		schedule(worker * worker_obj, database * db_obj, site_comm * sc_obj);
		void reload_config();
		void handle(ev::timer &watcher, int events_flags);

		unsigned int schedule_interval;
};
#endif
