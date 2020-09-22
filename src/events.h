#include <iostream>
#include <string>
#include <cstring>

// libev
#include <ev++.h>

// Sockets
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#define RESULT_OK 0
#define RESULT_ERR -1

// Forward declarations
class worker;
class schedule;
class site_comm;

/*
We have three classes - the mother, the middlemen, and the worker
THE MOTHER
	The mother is called when a client opens a connection to the server.
	It creates a middleman for every new connection, which will be called
	when its socket is ready for reading.
THE MIDDLEMEN
	Each middleman hang around until data is written to its socket. It then
	reads the data and sends it to the worker. When it gets the response, it
	gets called to write its data back to the client.
THE WORKER
	The worker gets data from the middleman, and returns the response. It
	doesn't concern itself with silly things like sockets.

	see worker.h for the worker.
*/

// THE MOTHER - Spawns connection middlemen
class connection_mother {
	private:
		void load_config();
		void set_rlimit();
		int socket_set_non_block(int fd);
		int socket_set_reuse_addr(int fd);
		int socket_listen(int s, struct sockaddr *address, socklen_t address_len, int backlog);
		int create_tcp_server(unsigned int port, const std::string &bindaddr);
		int create_unix_server(const std::string &path);
		unsigned int listen_port;
		unsigned int max_connections;
		std::vector<std::string> listen_hosts;
		std::vector<int> listen_sockets;

		worker * work;
		std::map<int, ev::io*> listen_events;
		ev::timer schedule_event;

	public:
		connection_mother(worker * worker_obj, site_comm * sc_obj, schedule * sched_obj);
		~connection_mother();
		void reload_config();
		int create_listen_socket();
		const void run();
		void handle_connect(ev::io &watcher, int events_flags);

		unsigned int max_middlemen;
		unsigned int connection_timeout;
		unsigned int keepalive_timeout;
		unsigned int max_read_buffer;
		unsigned int max_request_size;
};

// THE MIDDLEMAN
// Created by connection_mother
// Add their own watchers to see when sockets become readable
class connection_middleman {
	private:
		int connect_sock;
		client_opts_t client_opts;
		unsigned int written;
		ev::io read_event;
		ev::io write_event;
		ev::timer timeout_event;
		std::string request;
		std::string response;

		connection_mother * mother;
		worker * work;

	public:
		connection_middleman(int &listen_socket, worker* work, connection_mother * mother_arg);
		~connection_middleman();

		void handle_read(ev::io &watcher, int events_flags);
		void handle_write(ev::io &watcher, int events_flags);
		void handle_timeout(ev::timer &watcher, int events_flags);
};
