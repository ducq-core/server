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
#include <ducq_srv.h>

#include "sqlite_srv_logger.h"
#define SQLITE_FILENAME "ducq_log.db"



sql_logger_t *logger = NULL;
ducq_srv *srv = NULL;
int tcp = -1;
jmp_buf env;



void signal_handler(int sig) {
	fprintf(stderr, "received %d\n", sig);
	ducq_srv_log(srv, DUCQ_LOG_INFO, __func__, "server", "%d received %d", getpid(), sig);

	switch(sig) {
		case SIGTERM:
		case SIGINT :
			ducq_srv_log(srv, DUCQ_LOG_INFO, __func__, "server", "shutdown, pid %d", getpid());
			longjmp(env, -1);
			break;
		case  SIGQUIT:
			ducq_srv_log(srv, DUCQ_LOG_INFO, __func__, "server", "becoming daemon, pid %d", getpid());
			sql_logger_set_console_log(logger, false);
			if( daemon(0, 0) )
				fprintf(stderr, "daemon() failed: %s\n", strerror(errno));
			else
				ducq_srv_log(srv, DUCQ_LOG_INFO, __func__, "server", "became daemon, pid %d", getpid());
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
	
	return fd;
}


ducq_srv *build_ducq_srv(const char *commands_path, sql_logger_t **logger) {
	ducq_srv *srv = ducq_srv_new();
	if(!srv) {
		fprintf(stderr, "ducq_srv_new() failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	ducq_state state = ducq_srv_load_commands_path(srv, commands_path);
	if(state) {
		fprintf(stderr, "%s\n", ducq_state_tostr(state));
		exit(EXIT_FAILURE);
	}

	char *error = NULL;
	int rc = create_sql_logger(logger, SQLITE_FILENAME, &error);
	if(rc) {
		fprintf(stderr, "create_sql_logger() failed: %s\n", error);
		exit(EXIT_FAILURE);
	}
	ducq_srv_set_log(srv, *logger, sqlite_srv_logger);

	return srv;
}



int loop() {
	while(true) {
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(tcp, &readfds);

		int nready = select(tcp+1, &readfds, NULL, NULL, NULL);
		if( nready == -1) {
			fprintf(stderr, "select() failed: %s\n", strerror(errno));
			continue;
		}

		if(FD_ISSET(tcp, &readfds)) {
			struct sockaddr_storage cliaddr;
			socklen_t clilen = sizeof(cliaddr);

			int client = accept(tcp, (struct sockaddr*)&cliaddr, &clilen);
			if( client == -1 )
				ducq_srv_log(srv, DUCQ_LOG_INFO, __func__, "server",
					"accept() failed: %s\n", strerror(errno)
				);

			ducq_state state = ducq_tcp_apply(client, (ducq_apply_f)ducq_srv_dispatch, srv);
			if(state != DUCQ_OK)
				ducq_srv_log(srv, DUCQ_LOG_INFO, __func__, "server",
					"dispatch returned (%d) %s", state, ducq_state_tostr(state)
				);
		}
	}
}



int main(int argc, char **argv) {
	if(argc < 2) {
		fprintf(stderr, "usage\n%s <port>\n", argv[0]);
		return -1;
	}
	const char *port = argv[1];
	const char *commands_path = argc > 2 ? argv[2] : NULL;

	
	srv = build_ducq_srv(commands_path, &logger);
	tcp = tcp4_listen(port);
	if( tcp == -1) {
		fprintf(stderr, "tcp4_listen() failed: %s\n", strerror(errno));
		return -1;
	}

	if( setjmp(env) ) {
		close(tcp);
		ducq_srv_free(srv);
		free_sql_logger(logger);
		printf("all done.\n");
		exit(EXIT_SUCCESS);
	}
	set_signals();

	ducq_srv_log(srv, DUCQ_LOG_INFO, __func__, "server", "server started, pid: %d", getpid());

	loop();
}