#include <iostream>

#include "../autoconf.h"
#include "radiance.h"
#include "config.h"
#include "database.h"
#include "worker.h"
#include "logger.h"
#include "site_comm.h"
#include "schedule.h"

schedule::schedule(worker * worker_obj, database * db_obj, site_comm * sc_obj) : work(worker_obj), db(db_obj), sc(sc_obj) {
	load_config();
	counter = 0;
	last_opened_connections = 0;
	last_request_count = 0;
	next_reap_peers = reap_peers_interval;
}

void schedule::load_config() {
	reap_peers_interval = conf->get_uint("reap_peers_interval");
	schedule_interval = conf->get_uint("schedule_interval");
}

void schedule::reload_config() {
	load_config();
}

//---------- Schedule - gets called every schedule_interval seconds
void schedule::handle(ev::timer &watcher, int events_flags) {
	unsigned int cur_schedule_interval = watcher.repeat;
	stats.connection_rate = (stats.opened_connections - last_opened_connections) / cur_schedule_interval;
	stats.request_rate = (stats.requests - last_request_count) / cur_schedule_interval;
	if (counter % 20 == 0) {
		syslog(debug) << stats.open_connections << " open, "
		<< stats.opened_connections << " connections (" << stats.connection_rate << "/s), "
		<< stats.requests << " requests (" << stats.request_rate << "/s)";
	}

	if (work->get_status() == CLOSING && db->all_clear() && sc->all_clear()) {
		db->shutdown();

#if defined(__DEBUG_BUILD__)
		delete work;
//		delete sched;
//		delete mother;
		delete db;
		delete sc;
		delete conf;
		delete opts;
#endif

		syslog(info) << "all clear, shutting down";
		exit(EXIT_SUCCESS);
	}

	last_opened_connections = stats.opened_connections;
	last_request_count = stats.requests;

	db->flush();
	sc->flush_tokens();

	next_reap_peers -= cur_schedule_interval;
	if (next_reap_peers <= 0) {
		work->start_reaper();
		next_reap_peers = reap_peers_interval;
	}

	counter++;
	if (schedule_interval != cur_schedule_interval) {
		watcher.set(schedule_interval, schedule_interval);
	}

	flush_log();
}
