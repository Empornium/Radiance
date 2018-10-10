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

// Define the connection mother (first half) and connection middlemen (second half)

//TODO Better errors

//---------- Connection mother - spawns middlemen and lets them deal with the connection

connection_mother::connection_mother(worker * worker_obj, site_comm * sc_obj, schedule * sched) : work(worker_obj) {
	// Handle config stuff first
	load_config();

	listen_socket = create_listen_socket();
	if (listen_socket == -1) exit(EXIT_FAILURE);

	listen_event.set<connection_mother, &connection_mother::handle_connect>(this);
	listen_event.start(listen_socket, ev::READ);
	// Create libev timer
	schedule_event.set<schedule, &schedule::handle>(sched);
	schedule_event.start(sched->schedule_interval, sched->schedule_interval); // After interval, every interval
}

void connection_mother::load_config() {
	listen_port	= conf->get_uint("listen_port");
	listen_host	= conf->get_str("listen_host");
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
	load_config();
	if (old_listen_port != listen_port) {
		syslog(info) << "Changing listen port from " << old_listen_port << " to " << listen_port;
		int new_listen_socket = create_listen_socket();
		if (new_listen_socket != 0) {
			listen_event.stop();
			listen_event.start(new_listen_socket, ev::READ);
			close(listen_socket);
			listen_socket = new_listen_socket;
		} else {
			syslog(info) << "Couldn't create new listen socket when reloading config";
		}
	} else if (old_max_connections != max_connections) {
		listen(listen_socket, max_connections);
	}
}

int connection_mother::create_listen_socket() {
	int new_listen_socket = 0;
	sockaddr_un  unix_address;
	sockaddr* address;
	size_t address_len;

	memset(&unix_address,  0, sizeof(unix_address));

	if (!strncmp(listen_host.c_str(), "unix://", strlen("unix://"))) {
		// Prepare a Unix socket
		unix_address.sun_family = AF_UNIX;
		strcpy(unix_address.sun_path, listen_host.c_str());
		syslog(info) << "Listening with UNIX socket.";
		address = (sockaddr*) &unix_address;
		address_len = sizeof(*address);

	} else {
		// Prepare an Internet socket
		int opt = 0;
		struct addrinfo hints, *res;
		memset(&hints, 0, sizeof hints);
		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		getaddrinfo(listen_host.c_str(), std::to_string(listen_port).c_str(), &hints, &res);
		new_listen_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

		// IPv4 address was found
		if (res->ai_family == PF_INET) {
			syslog(info) << "Listening with IPv4 INET socket.";

		// IPv6 address was found
		} else if(res->ai_family == PF_INET6) {
			syslog(info) << "Listening with IPv6 INET socket.";

			// Attempt to enable Dual Stack
			if (setsockopt(new_listen_socket, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) < 0) {
				syslog(fatal) << "Insufficient OS support for Dual Stacking.";
			} else {
				syslog(info) << "Enabled IPv4/IPv6 Dual Stack mode.";
			}
		} else {
			syslog(fatal) << "Unkown address family.";
			return -1;
		}

		address = res->ai_addr;
		address_len = res->ai_addrlen;
		freeaddrinfo(res);
	}

	// Check for socket
	if (new_listen_socket == -1) {
		syslog(fatal) << "Failed to open socket.";
		return -1;
	}

	// Stop old sockets from hogging the port
	int yes = 1;
	if (setsockopt(new_listen_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
		syslog(fatal) << "Could not reuse socket: " << strerror(errno);
		return -1;
	}

	// Bind
	if (bind(new_listen_socket, address, address_len) == -1) {
		close(new_listen_socket);
		std::string type;
		if (address->sa_family == AF_INET6) {
			type = "IPv6 Internet Socket";
		} else if (address->sa_family == AF_INET) {
			type = "IPv4 Internet Socket";
		} else if (address->sa_family == AF_UNIX) {
			type = "Unix Domain Socket";
		} else {
			type = "Unkown Domain Socket";
		}
		syslog(fatal) << "Bind failed on " << type << ": " << strerror(errno) << " (" << errno << ")";
		return -1;
	}

	// Listen
	if (listen(new_listen_socket, max_connections) == -1) {
		syslog(info) << "Listen failed: " << strerror(errno);
		return -1;
	}

	// Set non-blocking
	int flags = fcntl(new_listen_socket, F_GETFL);
	if (flags == -1) {
		syslog(info) << "Could not get socket flags: " << strerror(errno);
		return -1;
	}
	if (fcntl(new_listen_socket, F_SETFL, flags | O_NONBLOCK) == -1) {
		syslog(info) << "Could not set non-blocking: " << strerror(errno);
		return -1;
	}

	return new_listen_socket;
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
		new connection_middleman(listen_socket, work, this);
	}
}

connection_mother::~connection_mother()
{
	close(listen_socket);
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

void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
	return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
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
			char ip[INET_ADDRSTRLEN];
			sockaddr_storage client_addr;
			socklen_t addr_len = sizeof(client_addr);
			uint16_t ip_ver = 4;
			getpeername(connect_sock, (sockaddr *) &client_addr, &addr_len);
			if(client_addr.ss_family == AF_INET) {
				ip_ver = 4;
			} else {
				ip_ver = 6;
			}
			inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr *)&client_addr), ip, sizeof ip);
			std::string ip_str = ip;

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
