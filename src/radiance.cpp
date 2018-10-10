#include <fstream>
#include <iostream>
#include <csignal>
#include <thread>
#include <sys/file.h>
#include <fcntl.h>

#include "../autoconf.h"
#include "radiance.h"
#include "database.h"
#include "worker.h"
#include "events.h"
#include "schedule.h"
#include "site_comm.h"
#include "misc_functions.h"
#include "debug.h"
#include "config.h"
#include "logger.h"

static connection_mother *mother;
static worker *work;
static database *db;
static site_comm *sc;
static schedule *sched;

static user_list    *users_list;
static torrent_list *torrents_list;
static domain_list  *domains_list;

// Shared structures & objects
struct stats_t stats;
settings *conf;
options  *opts;


int createPidFile(const char *progName, const char *pidFile, int flags) {
	int fd;
	char buf[128];

	fd = open(pidFile, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd == -1) {
		syslog(fatal) << "Could not open PID file " << pidFile;
		exit(EXIT_FAILURE);
	}

	if (flags) {

		/* Set the close-on-exec file descriptor flag */

		/* Instead of the following steps, we could (on Linux) have opened the
		   file with O_CLOEXEC flag. However, not all systems support open()
		   O_CLOEXEC (which was standardized only in SUSv4), so instead we use
		   fcntl() to set the close-on-exec flag after opening the file */

		flags = fcntl(fd, F_GETFD);
		if (flags == -1) {
			syslog(fatal) << "Could not get flags for PID file " << pidFile;
			exit(EXIT_FAILURE);
		}

		flags |= FD_CLOEXEC;

		if (fcntl(fd, F_SETFD, flags) == -1) {
			syslog(fatal) << "Could not set flags for PID file " << pidFile;
			exit(EXIT_FAILURE);
		}
	}

	if (lockRegion(fd, F_WRLCK, SEEK_SET, 0, 0) == -1) {
		if (errno  == EAGAIN || errno == EACCES) {
			syslog(fatal) << "PID file " << pidFile << " is locked; probably " << progName << " is already running";
			exit(EXIT_FAILURE);
		} else {
			syslog(fatal) << "Unable to lock PID file " << pidFile;
			exit(EXIT_FAILURE);
		}
	}

	if (ftruncate(fd, 0) == -1) {
		syslog(fatal) << "Could not truncate PID file " << pidFile;
		exit(EXIT_FAILURE);
	}

	snprintf(buf, 128, "%ld\n", (long) getpid());
	if (write(fd, buf, strlen(buf)) != (unsigned int) strlen(buf)) {
		syslog(fatal) << "Writing to PID file " << pidFile;
		exit(EXIT_FAILURE);
	}

	return fd;
}

static void sig_handler(int sig) {
	if (sig == SIGINT || sig == SIGTERM) {
		syslog(info) << "Caught SIGINT/SIGTERM";
		if (work->shutdown()) {
			exit(EXIT_SUCCESS);
		}
	} else if (sig == SIGHUP) {
		syslog(info) << "Reloading config";
		conf->reload();
		// Reinitialize logger
		rotate_log();
		db->reload_config();
		mother->reload_config();
		sc->reload_config();
		sched->reload_config();
		work->reload_config();
		syslog(info) << "Done reloading config";
	} else if (sig == SIGUSR1) {
		syslog(info) << "Reloading from database";
		std::thread w_thread(&worker::reload_lists, work);
		w_thread.detach();
#if defined(__DEBUG_BUILD__)
	}  else if (sig == SIGSEGV) {
		// print out all the frames to stderr
		syslog(fatal) << "SegFault:" << '\n' << backtrace(1);
		exit(EXIT_FAILURE);
#endif
	}
}

int main(int argc, char **argv) {
	// BREAKS our pretty stream redirection scheme, also isn't
	// thread safe... BOOO!
	// we don't use printf so make cout/cerr a little bit faster
//	std::ios_base::sync_with_stdio(false);

	conf = new settings();
	opts = new options();

	bool conf_arg = false, daemonize = false;
	std::string conf_file_path("./radiance.conf");
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v")) {
			std::cout << "Radiance BitTorrent Tracker v" << PACKAGE_VERSION << std::endl;
			return 0;
		} else if (!strcmp(argv[i], "-d")) {
			daemonize = true;
		} else if (!strcmp(argv[i], "-c") && i < argc - 1) {
			conf_arg = true;
			conf_file_path = argv[++i];
		} else {
			std::cout << "Usage: " << argv[0] << "[-v] [-d] [-c configfile]" << std::endl;
			return 0;
		}
	}

	std::ifstream conf_file(conf_file_path);
	if (conf_file.fail()) {
		std::cout << "Using default config because '" << conf_file_path << "' couldn't be opened" << std::endl;
		if (!conf_arg) {
			std::cout << "Start Radiance with -c <path> to specify config file if necessary" << std::endl;
		}
	} else {
		conf->load(conf_file_path, conf_file);
	}

	// Start logger
	init_log();

	if (conf->get_bool("daemonize") || daemonize) {
		syslog(info) << "Running in Daemon Mode";
		pid_t pid, sid;
		pid = fork();

		if (pid < 0) {
			exit(EXIT_FAILURE);
		}
		if (pid > 0) {
			exit(EXIT_SUCCESS);
		}

		umask(0);

		sid = setsid();
		if (sid < 0) {
			/* Log the failure */
			exit(EXIT_FAILURE);
		}
		if ((chdir("/")) < 0) {
			exit(EXIT_FAILURE);
		}

		if (conf->get_str("pid_file") != "none") {
			createPidFile("radiance", conf->get_str("pid_file").c_str(), LOCK_EX | LOCK_NB);
		}

		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
	} else {
		syslog(info) << "Running in Foreground";
	}

	db = new database();
	sc = new site_comm();

	users_list    = new user_list;
	torrents_list = new torrent_list;
	domains_list  = new domain_list;
	std::vector<std::string> blacklist;

	db->load_site_options();
	db->load_users(*users_list);
	db->load_torrents(*torrents_list);
	db->load_tokens(*torrents_list);
	db->load_peers(*torrents_list, *users_list);
	db->load_blacklist(blacklist);

	stats.open_connections = 0;
	stats.opened_connections = 0;
	stats.connection_rate = 0;
	stats.requests = 0;
	stats.request_rate = 0;
	stats.leechers = 0;
	stats.seeders = 0;
	stats.announcements = 0;
	stats.succ_announcements = 0;
	stats.scrapes = 0;
	stats.bytes_read = 0;
	stats.bytes_written = 0;

	stats.torrent_queue = 0;
	stats.user_queue = 0;
	stats.peer_queue = 0;
	stats.peer_hist_queue = 0;
	stats.snatch_queue = 0;
	stats.token_queue = 0;

	stats.start_time = time(NULL);

	// Create worker object, which handles announces and scrapes and all that jazz
	work = new worker(*torrents_list, *users_list, *domains_list, blacklist, db, sc);

	// Create schedule object
	sched = new schedule(work, db, sc);

	// Create connection mother, which binds to its socket and handles the event stuff
	mother = new connection_mother(work, sc, sched);

	// Add signal handlers now that all objects have been created
	struct sigaction handler;
	handler.sa_handler = sig_handler;
	sigemptyset(&handler.sa_mask);
	handler.sa_flags = 0;

	sigaction(SIGINT,  &handler, NULL);
	sigaction(SIGTERM, &handler, NULL);
	sigaction(SIGHUP,  &handler, NULL);
	sigaction(SIGUSR1, &handler, NULL);
	sigaction(SIGUSR2, &handler, NULL);
	sigaction(SIGSEGV, &handler, NULL);

	mother->run();

	return 0;
}
