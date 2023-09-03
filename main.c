#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>

#include <unistd.h>     // daemon()
#include <sys/socket.h> // comm layer, should not be here
#include <sys/select.h> // select, fd_set
#include <netinet/in.h>

#include <ducq.h>
#include <ducq_tcp.h>
#include <ducq_http.h>
#include <ducq_reactor.h>
#include <ducq_dispatcher.h>

#include "sqlite_srv_logger.h"
#define SQLITE_FILENAME "ducq_log.db"


int tcp                   = -1;
int http                  = -1;
const char *tcp_port      = NULL;
const char *http_port     = NULL;
const char *commands_path = NULL;

sql_logger_t *logger      = NULL;
ducq_reactor *reactor     = NULL;
jmp_buf env;



void signal_handler(int sig) {
	fprintf(stderr, "received %d\n", sig);
	ducq_reactor_log(reactor, DUCQ_LOG_INFO, __func__, "server", "%d received %d", getpid(), sig);

	switch(sig) {
		case SIGTERM:
		case SIGINT :
			ducq_reactor_log(reactor, DUCQ_LOG_INFO, __func__, "server", "shutdown, pid %d", getpid());
			longjmp(env, -1);
			break;
		case  SIGQUIT:
			ducq_reactor_log(reactor, DUCQ_LOG_INFO, __func__, "server", "becoming daemon, pid %d", getpid());
			sql_logger_set_console_log(logger, false);
			if( daemon(0, 0) )
				fprintf(stderr, "daemon() failed: %s\n", strerror(errno));
			else
				ducq_reactor_log(reactor, DUCQ_LOG_INFO, __func__, "server", "became daemon, pid %d", getpid());
			break;
	}
};


void set_signals(void) {
	if(signal(SIGTERM, signal_handler) == SIG_ERR
	|| signal(SIGINT,  signal_handler) == SIG_ERR
	|| signal(SIGQUIT, signal_handler) == SIG_ERR
	|| signal(SIGPIPE, SIG_IGN       ) == SIG_ERR) {
		fprintf(stderr, "signal() failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
}


int tcp4_listen(const char *serv) {
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_ANY),
		.sin_port = htons(atoi(serv))
	};

	int fd;
	if( (fd = socket(AF_INET, SOCK_STREAM, 0)) == -1 )
		return -1;
	if(bind(fd, (struct sockaddr*) &addr, sizeof(addr)) == -1)
		return -1;
	if(listen(fd, 10) == -1)
		return -1;

	int reuse = 1;
	socklen_t reuse_size = sizeof(reuse);
	if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, reuse_size) == -1)
		return -1;

	struct timeval accept_timeout = {.tv_sec = 5};
	socklen_t accept_timeout_size = sizeof(accept_timeout);
	if(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &accept_timeout, accept_timeout_size) == -1)
		return -1;

	int keepalive = 1;
	socklen_t keepalive_size = sizeof(keepalive);
	if(setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, keepalive_size) == -1)
		return -1;

	return fd;
}


ducq_reactor *build_reactor() {
	// log
	char *error = NULL;
	int rc = create_sql_logger(&logger, SQLITE_FILENAME, &error);
	if(rc) {
		fprintf(stderr, "create_sql_logger() failed: %s\n", error);
		exit(EXIT_FAILURE);
	}

	// reactor
	reactor = ducq_reactor_new_with_log(sqlite_srv_logger, logger);
	if(!reactor) {
		fprintf(stderr, "ducq_reactor_new() failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	// commands/dispatcher
	ducq_dispatcher *dispatcher = ducq_reactor_get_dispatcher(reactor);
	ducq_state state;
	state = ducq_dispatcher_load_commands_path(dispatcher, commands_path);
	if(state) {
		fprintf(stderr, "ducq_dispatcher_load_commands_path() failed: %s\n", ducq_state_tostr(state));
		exit(EXIT_FAILURE);
	}
	state = ducq_dispatcher_add(dispatcher, "./extensions");
	//state = ducq_dispatcher_add(dispatcher, ".");
	if(state) {
		fprintf(stderr, "ducq_dispatcher_add() failed: %s (%s)\n",
			ducq_state_tostr(state),
			strerror(errno)
		);
		exit(EXIT_FAILURE);
	}


	return reactor;
}


typedef ducq_i* (*wrap_connection_f)(int fd);

void tcp_accept(ducq_reactor *reactor, int tcp, void *ctx) {
	wrap_connection_f wrap_connection = (wrap_connection_f)ctx;

	struct sockaddr_storage cliaddr;
	socklen_t clilen = sizeof(cliaddr);

	int client = accept(tcp, (struct sockaddr*)&cliaddr, &clilen);
	if(client == -1) {
		ducq_reactor_log(reactor, DUCQ_LOG_INFO, __func__, "server",
			"accept() failed: %s\n", strerror(errno)
		);
		return;
	}

	ducq_i *ducq = wrap_connection(client);
	if(!ducq) {
		close(client);
		ducq_reactor_log(reactor, DUCQ_LOG_INFO, __func__, "server",
			"ducq_new_tcp"
		);
		return;
	}

	ducq_state state = ducq_reactor_add_client(reactor, client, ducq);
	if(state != DUCQ_OK)
		ducq_reactor_log(reactor, DUCQ_LOG_INFO, __func__, "server",
			"dispatch returned (%d) %s", state, ducq_state_tostr(state)
		);
}

void load_listeners_in_reactor() {
	ducq_state state = DUCQ_OK;

	tcp = tcp4_listen(tcp_port);
	if( tcp == -1) {
		fprintf(stderr, "tcp4_listen() failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	state = ducq_reactor_add_server(reactor, tcp, tcp_accept, ducq_new_tcp_connection);
	if(state) {
		fprintf(stderr, "ducq_reactor_add_server() failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	printf("http port: %s\n", http_port);
	if(http_port == NULL) return;
	http = tcp4_listen(http_port);
	if( http == -1) {
		fprintf(stderr, "tcp4_listen() failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	state = ducq_reactor_add_server(reactor, http, tcp_accept, ducq_new_http_connection);
	if(state) {
		fprintf(stderr, "ducq_reactor_add_server() failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}


	
}

int main(int argc, char **argv) {
	if(argc < 2) {
		fprintf(stderr, "usage\n%s <tcp-port> [http-port] [commands-path]\n", argv[0]);
		return -1;
	}
	tcp_port      =            argv[1];
	http_port     = argc > 2 ? argv[2] : NULL;
	commands_path = argc > 3 ? argv[3] : NULL;

	build_reactor();
	load_listeners_in_reactor();

	if( setjmp(env) ) {
		close(tcp);
		close(http);
		ducq_reactor_free(reactor);
		free_sql_logger(logger);
		printf("all done.\n");
		exit(EXIT_SUCCESS);
	}
	set_signals();

	ducq_reactor_log(reactor, DUCQ_LOG_INFO, __func__, "server", "server started, pid: %d", getpid());

	ducq_loop(reactor);
}
