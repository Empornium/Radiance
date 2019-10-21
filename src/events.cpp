#include <cerrno>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "radiance.h"
#include "response.h"
#include "events.h"
#include "worker.h"
#include "schedule.h"
#include "site_comm.h"
#include "config.h"
#include "logger.h"
#include "misc_functions.h"

// Define the connection mother (first half) and connection middlemen (second half)

//TODO Better errors

//---------- Connection mother - spawns middlemen and lets them deal with the connection

connection_mother::connection_mother(worker * worker_obj, site_comm * sc_obj, schedule * sched) : work(worker_obj) {
	// Handle config stuff first
	load_config();

	if (create_listen_socket() == RESULT_ERR) exit(EXIT_FAILURE);

	for (const int listen_socket: listen_sockets) {
		ev::io *listen_event = new ev::io;

		listen_event->set<connection_mother, &connection_mother::handle_connect>(this);
		listen_event->start(listen_socket, ev::READ);
		listen_events.insert(std::pair<int, ev::io*>(listen_socket, listen_event));
	}
	// Create libev timer
	schedule_event.set<schedule, &schedule::handle>(sched);
	schedule_event.start(sched->schedule_interval, sched->schedule_interval); // After interval, every interval
}

void connection_mother::load_config() {
	listen_port	= conf->get_uint("listen_port");
	listen_hosts       = split(conf->get_str("listen_host"), ' ');
	max_connections    = conf->get_uint("max_connections");
	max_middlemen      = conf->get_uint("max_middlemen");
	connection_timeout = conf->get_uint("connection_timeout");
	keepalive_timeout  = conf->get_uint("keepalive_timeout");
	max_read_buffer    = conf->get_uint("max_read_buffer");
	max_request_size   = conf->get_uint("max_request_size");
}

void connection_mother::reload_config() {
	unsigned int old_listen_port = listen_port;
	unsigned int old_max_connections = max_connections;
	std::vector<std::string> old_listen_hosts = listen_hosts;
	std::vector<int> old_listen_sockets = listen_sockets;
	load_config();
	if (old_listen_port != listen_port) {
		syslog(info) << "Changing listen port from " << old_listen_port << " to " << listen_port;

		if (create_listen_socket() == RESULT_OK) {
			for (auto const &it: listen_events) {
				ev::io* listen_event = it.second;
				listen_event->stop();
				delete listen_event;
			}
			listen_events.clear();
			for (const int old_listen_socket: old_listen_sockets) {
				close(old_listen_socket);
				auto i = std::find(listen_sockets.begin(), listen_sockets.end(), old_listen_socket);
				listen_sockets.erase(i);
			}

			for (const int listen_socket: listen_sockets) {
				ev::io* listen_event = new ev::io;

				listen_event->set<connection_mother, &connection_mother::handle_connect>(this);
				listen_event->start(listen_socket, ev::READ);
				listen_events.insert(std::pair<int, ev::io*>(listen_socket, listen_event));
			}
		} else {
			syslog(error) << "Couldn't create new listen socket when reloading config";
		}
	}

	if (old_listen_hosts != listen_hosts) {
		std::ostringstream old_imploded, new_imploded;
		const char* delim = " ";
		std::copy(old_listen_hosts.begin(), old_listen_hosts.end(), std::ostream_iterator<std::string>(old_imploded, delim));
		std::copy(listen_hosts.begin(), listen_hosts.end(), std::ostream_iterator<std::string>(new_imploded, delim));
		syslog(info) << "Changing listen host from \"" << trim(old_imploded.str()) << "\" to \"" << trim(new_imploded.str()) << "\"";

		for (auto const &it: listen_events) {
			ev::io* listen_event = it.second;
			listen_event->stop();
			delete listen_event;
		}
		listen_events.clear();
		for (const int old_listen_socket: old_listen_sockets) {
			close(old_listen_socket);
		}
		listen_sockets.clear();

		if (create_listen_socket() == RESULT_OK) {
			for (const int listen_socket: listen_sockets) {
				ev::io* listen_event = new ev::io;

				listen_event->set<connection_mother, &connection_mother::handle_connect>(this);
				listen_event->start(listen_socket, ev::READ);
				listen_events.insert(std::pair<int, ev::io*>(listen_socket, listen_event));
			}
		} else {
			syslog(error) << "Couldn't create new listen socket when reloading config";
			exit(EXIT_FAILURE);
		}
	}

	if (old_max_connections != max_connections) {
		for (const int listen_socket: listen_sockets) {
			listen(listen_socket, max_connections);
		}
	}
}

int connection_mother::socket_set_non_block(int fd) {
	// Set non-blocking
	int flags = fcntl(fd, F_GETFL);
	if (flags == -1) {
		syslog(info) << "Could not get socket flags: " << strerror(errno);
		return RESULT_ERR;
	}
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
		syslog(info) << "Could not set non-blocking: " << strerror(errno);
		return RESULT_ERR;
	}

	return RESULT_OK;
}

int connection_mother::socket_set_reuse_addr(int fd) {
	int yes = 1;

	// Stop old sockets from hogging the port
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
		syslog(fatal) << "Could not reuse socket: " << strerror(errno);
		return RESULT_ERR;
	}

	return RESULT_OK;
}

int connection_mother::socket_listen(int s, struct sockaddr *address, socklen_t address_len, int backlog) {
	// Bind
	if (bind(s, address, address_len) == -1) {
		close(s);
		std::string type;
		if (address->sa_family == AF_INET6) {
			type = "IPv6 Internet Socket";
		} else if (address->sa_family == AF_INET) {
			type = "IPv4 Internet Socket";
		} else if (address->sa_family == AF_UNIX) {
			type = "Unix Domain Socket";
		} else {
			type = "Unknown Domain Socket";
		}
		syslog(fatal) << "Bind failed on " << type << ": " << strerror(errno) << " (" << errno << ")";
		return RESULT_ERR;
	}

	// Listen
	if (listen(s, backlog) == -1) {
		syslog(info) << "Listen failed: " << strerror(errno);
		return RESULT_ERR;
	}

	return RESULT_OK;
}

int connection_mother::create_tcp_server(unsigned int port, const std::string &ip)
{
	struct addrinfo hints, *res, *p;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	const char *bindaddr = (ip.empty() || ip == "*") ? NULL : ip.c_str();
	getaddrinfo(bindaddr, std::to_string(port).c_str(), &hints, &res);

	for (p = res; p != NULL; p = p->ai_next) {
		int new_listen_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

		// Check for socket
		if (new_listen_socket == -1) {
			syslog(fatal) << "Failed to open socket.";
			return RESULT_ERR;
		}

		char ip_value[INET6_ADDRSTRLEN];

		// IPv4 address was found
		if (p->ai_family == PF_INET) {
			struct sockaddr_in *s = (struct sockaddr_in *)p->ai_addr;
			inet_ntop(AF_INET, (void*)&(s->sin_addr), ip_value, sizeof(ip_value));
			syslog(info) << "Listening with IPv4 INET socket on " << ip_value << ":" << port << ".";

			// IPv6 address was found
		} else if (p->ai_family == PF_INET6) {
			struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)p->ai_addr;
			inet_ntop(AF_INET6, (void*)&(s6->sin6_addr), ip_value, sizeof(ip_value));
			syslog(info) << "Listening with IPv6 INET socket on [" << ip_value << "]:" << port << ".";

#if defined IPV6_V6ONLY
			int yes = 1;
			// Attempt to disable Dual Stack
			if (setsockopt(new_listen_socket, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes)) == -1) {
				syslog(fatal) << "Failed to disable IPv6 Dual Stack mode: " << strerror(errno);
				return RESULT_ERR;
			}
#endif
		} else {
			syslog(fatal) << "Unknown address family.";
			return RESULT_ERR;
		}

		if (socket_set_non_block(new_listen_socket) == RESULT_ERR) {
			return RESULT_ERR;
		}

		// Stop old sockets from hogging the port
		if (socket_set_reuse_addr(new_listen_socket) == RESULT_ERR) {
			return RESULT_ERR;
		}

		if (socket_listen(new_listen_socket, p->ai_addr, p->ai_addrlen, max_connections) == RESULT_ERR) {
			return RESULT_ERR;
		}

		listen_sockets.push_back(new_listen_socket);
	}
	freeaddrinfo(res);

	return RESULT_OK;
}

int connection_mother::create_unix_server(const std::string &path)
{
	struct sockaddr_un unix_address;
	mode_t mode;

	// Remove previous socket if exists
	unlink(path.c_str());

	int new_listen_socket = socket(AF_UNIX, SOCK_STREAM, 0);

	// Check for socket
	if (new_listen_socket == -1) {
		syslog(fatal) << "Failed to open UNIX socket: " << strerror(errno);
		return RESULT_ERR;
	}

	if (socket_set_non_block(new_listen_socket) == RESULT_ERR) {
		return RESULT_ERR;
	}

	if (socket_set_reuse_addr(new_listen_socket) == RESULT_ERR) {
		return RESULT_ERR;
	}

	memset(&unix_address, 0, sizeof(unix_address));

	// Prepare a Unix socket
	unix_address.sun_family = AF_UNIX;
	strncpy(unix_address.sun_path, path.c_str(), sizeof(unix_address.sun_path)-1);
	syslog(info) << "Listening with UNIX socket.";

	if (socket_listen(new_listen_socket, (struct sockaddr*)&unix_address, sizeof(unix_address), max_connections) == RESULT_ERR) {
		return RESULT_ERR;
	}

	mode = (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);

	if (chmod(path.c_str(), mode) == -1) {
		syslog(fatal) << "chmod() \"" << path << "\" failed";
		return RESULT_ERR;
	}

	listen_sockets.push_back(new_listen_socket);

	return RESULT_OK;
}

int connection_mother::create_listen_socket()
{
	std::string listen_host_conf = conf->get_str("listen_host");
	if (trim(listen_host_conf).empty() || listen_host_conf == "*") {
		if (create_tcp_server(listen_port, "*") == RESULT_ERR) {
			return RESULT_ERR;
		}
	} else {
		for (const std::string &listen_host: listen_hosts) {
			if (listen_host.empty()) {
				continue;
			}

			if (!strncmp(listen_host.c_str(), "unix:", strlen("unix:"))) {
				if (create_unix_server(listen_host.substr(strlen("unix:"))) == RESULT_ERR) {
					return RESULT_ERR;
				}
			} else {
				if (create_tcp_server(listen_port, listen_host) == RESULT_ERR) {
					return RESULT_ERR;
				}
			}
		}
	}

	if (listen_sockets.empty()) {
		syslog(fatal) << "Configured to not listen anywhere.";
		return RESULT_ERR;
	}

	return RESULT_OK;
}

const void connection_mother::run() {
	syslog(info) << "Sockets up on port " << listen_port << ", starting event loop!";
	ev_loop(ev_default_loop(0), 0);
}

void connection_mother::handle_connect(ev::io &watcher, int events_flags) {
	// Spawn a new middleman
	if (stats.open_connections < max_middlemen) {
		stats.opened_connections++;
		stats.open_connections++;
		new connection_middleman(watcher.fd, work, this);
	}
}

connection_mother::~connection_mother()
{
	for (auto const &it: listen_events) {
		ev::io* listen_event = it.second;
		listen_event->stop();
		delete listen_event;
	}

	for (const int listen_socket: listen_sockets) {
		close(listen_socket);
	}
}







//---------- Connection middlemen - these little guys live until their connection is closed

connection_middleman::connection_middleman(int &listen_socket, worker * new_work, connection_mother * mother_arg) :
	written(0), mother(mother_arg), work(new_work)
{
	client_opts = {false, false, false, false};
	connect_sock = accept(listen_socket, NULL, NULL);
	if (connect_sock == -1) {
		syslog(info) << "Accept failed, errno " << errno << ": " << strerror(errno);
		delete this;
		return;
	}

	// Set non-blocking
	int flags = fcntl(connect_sock, F_GETFL);
	if (flags == -1) {
		syslog(info) << "Could not get connect socket flags";
	}
	if (fcntl(connect_sock, F_SETFL, flags | O_NONBLOCK) == -1) {
		syslog(info) << "Could not set non-blocking";
	}

	// Get their info
	request.reserve(mother->max_read_buffer);
	written = 0;

	read_event.set<connection_middleman, &connection_middleman::handle_read>(this);
	read_event.start(connect_sock, ev::READ);

	// Let the socket timeout in timeout_interval seconds
	timeout_event.set<connection_middleman, &connection_middleman::handle_timeout>(this);
	timeout_event.set(mother->connection_timeout, mother->keepalive_timeout);
	timeout_event.start();
}

connection_middleman::~connection_middleman() {
	close(connect_sock);
	stats.open_connections--;
}

// Handler to read data from the socket, called by event loop when socket is readable
void connection_middleman::handle_read(ev::io &watcher, int events_flags) {
	char buffer[mother->max_read_buffer + 1];
	memset(buffer, 0, mother->max_read_buffer + 1);
	int ret = recv(connect_sock, &buffer, mother->max_read_buffer, 0);

	if (ret <= 0) {
		delete this;
		return;
	}
	stats.bytes_read += ret;
	request.append(buffer, ret);
	size_t request_size = request.size();
	if (request_size > mother->max_request_size || (request_size >= 4 && request.compare(request_size - 4, std::string::npos, "\r\n\r\n") == 0)) {
		stats.requests++;
		read_event.stop();
		client_opts.gzip = false;
		client_opts.html = false;
//		This causes nginx persistent connections to
//		close early, was this intentional?
//		client_opts.http_close = true;

		if (request_size > mother->max_request_size) {
			shutdown(connect_sock, SHUT_RD);
			response = response_error("GET string too long", client_opts);
		} else {
			struct sockaddr_storage client_addr;
			char ip[INET6_ADDRSTRLEN];
			socklen_t addr_len = sizeof(client_addr);
			uint16_t ip_ver = 0;
			getpeername(connect_sock, (struct sockaddr *) &client_addr, &addr_len);
			std::string ip_str;
			if (client_addr.ss_family == AF_INET) {
				struct sockaddr_in *s = (struct sockaddr_in *)&client_addr;
				ip_ver = 4;
				inet_ntop(AF_INET, (void*)&(s->sin_addr), ip, sizeof(ip));
				ip_str = ip;
			} else if (client_addr.ss_family == AF_INET6) {
				struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)&client_addr;
				ip_ver = 6;
				inet_ntop(AF_INET6, (void*)&(s6->sin6_addr), ip, sizeof(ip));
				ip_str = ip;

				// Handle IPv6-mapped IPv4
				if (ip_str.substr(0, 7) == "::ffff:") {
					ip_ver = 4;
					ip_str = ip_str.substr(7);
				}
			} else if (client_addr.ss_family == AF_UNIX) {
				ip_str = ""; // Empty, should be taken from additional headers
			} else {
				shutdown(connect_sock, SHUT_RD);
				response = response_error("Unknown Domain Socket", client_opts);
			}

			//--- CALL WORKER
			response = work->work(request, ip_str, ip_ver, client_opts);
			request.clear();
			request_size = 0;
		}

		// Find out when the socket is writeable.
		// The loop in connection_mother will call handle_write when it is.
		write_event.set<connection_middleman, &connection_middleman::handle_write>(this);
		write_event.start(connect_sock, ev::WRITE);
	}
}

// Handler to write data to the socket, called by event loop when socket is writeable
void connection_middleman::handle_write(ev::io &watcher, int events_flags) {
	int ret = send(connect_sock, response.c_str()+written, response.size()-written, MSG_NOSIGNAL);
	if (ret == -1) {
		return;
	}
	stats.bytes_written += ret;
	written += ret;
	if (written == response.size()) {
		write_event.stop();
		if (client_opts.http_close) {
			timeout_event.stop();
			delete this;
			return;
		}
		timeout_event.again();
		read_event.start();
		response.clear();
		written = 0;
	}
}

// After a middleman has been alive for timout_interval seconds, this is called
void connection_middleman::handle_timeout(ev::timer &watcher, int events_flags) {
	timeout_event.stop();
	read_event.stop();
	write_event.stop();
	delete this;
}
