#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <errno.h>

#define WRITE_FILE "/var/tmp/aesdsocketdata"

volatile sig_atomic_t exit_requested = 0;
void signal_handler(int signo){
	if(signo == SIGINT || signo == SIGTERM){
		exit_requested = 1;
	}
}

#define BUF_LEN 1000
char buffer[BUF_LEN];

int sockfd, clientfd;
FILE *fd, *readfd;
char *packet;
size_t packet_len;

void cleanup(){
	closelog();
	if(sockfd >= 0) close(sockfd);
	if(clientfd >= 0)close(clientfd);
	if(packet) free(packet);
	if(fd) fclose(fd);
	if(readfd) fclose(readfd);
	remove(WRITE_FILE);

	sockfd = -1; clientfd = -1;
	packet = NULL; fd = NULL; readfd = NULL;
	packet_len = 0;
}

int main(int argc, char **argv){
	int daemon_mode = 0;
	if(argc > 2 || (argc == 2 && strcmp(argv[1], "-d") != 0)){
		printf("Usage: %s [-d]\n", argv[0]);
		return -1;
	}else if(argc == 2 && strcmp(argv[1], "-d") == 0){
		daemon_mode = 1;
	}
	printf("Starting server\n");
	openlog(argv[0], LOG_PID, LOG_USER);
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);


	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd == -1){
		syslog(LOG_ERR, "Failed to create socket");
		closelog();
		return -1;
	}

	int opt = 1;
	struct sockaddr_in server_addr;
	if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
		syslog(LOG_ERR, "Failed to setsockopt()");
		cleanup();
		return -1;
	}
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(9000);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0){
		syslog(LOG_ERR, "Failed to Bind");
		cleanup();
		return -1;
	}

	if (daemon_mode){
		pid_t pid = fork();
		if(pid < 0){
			syslog(LOG_ERR, "Failed to fork()");
			return -1;
		}

		if(pid > 0) return 0;//Parent exits

		if(setsid() < 0){
			syslog(LOG_ERR, "Failed to setsid()");
			return -1;
		}

		chdir("/");
		umask(0);
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
	}
	packet = NULL;
	packet_len = 0;
	fd = fopen(WRITE_FILE, "w");
	if(!fd){
		syslog(LOG_ERR, "Failed to fopen(fd)");
		cleanup();
		return -1;
	}
	if(listen(sockfd, 10) != 0){
		syslog(LOG_ERR, "Failed to listen()");
		cleanup();
		return -1;
	}	

	while(!exit_requested){ //till SIGINT or SIGTERM
		struct sockaddr_in client_addr;
		socklen_t client_addr_size = sizeof(client_addr);
		clientfd = accept(
			sockfd, 
			(struct sockaddr*)&client_addr, 
			&client_addr_size
		);
		if(clientfd == -1){
			if(errno == EINTR && exit_requested) break;
			syslog(LOG_ERR, "Failed to accept()");
			cleanup();
			return -1;
		}

		char client_ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
		syslog(LOG_INFO, "Accepted connection from %s", client_ip);

		while(1){//Handle current client
			ssize_t bytes_read = recv(clientfd, buffer, BUF_LEN, 0);
			if(bytes_read == -1){
				syslog(LOG_ERR, "Failed to recv()");
				cleanup();
				return -1;
			}if(bytes_read == 0){
				syslog(LOG_INFO, "Closed connection from %s", client_ip);
				close(clientfd);
				free(packet);
				packet = NULL;
				packet_len = 0;
				break;
			}
			char *tmp = realloc(packet, packet_len + bytes_read);
			if(!tmp){
				syslog(LOG_ERR, "Failed to realloc()");
				cleanup();
				return -1;
			}
			packet = tmp;
			memcpy(packet + packet_len, buffer, bytes_read);
			packet_len += bytes_read;

			if(buffer[bytes_read-1] == '\n'){
				size_t bytes_wrote = fwrite(packet, 1, packet_len, fd);
				fflush(fd);
				if(bytes_wrote != packet_len){
					syslog(LOG_ERR, "Failed to fwrite()");
					cleanup();
					return -1;
				}
				free(packet);
				packet = NULL;
				packet_len = 0;
				readfd = fopen(WRITE_FILE, "r");
				if(!readfd){
					syslog(LOG_ERR, "Failed to fopen(readfd)");
					cleanup();
					return -1;
				}

				while((bytes_read = fread(buffer, 1, BUF_LEN, readfd)) > 0){
					send(clientfd, buffer, bytes_read, 0);
				}
				fclose(readfd);
				readfd = NULL;
			}
		}
	}

	syslog(LOG_INFO, "Caught signal, exiting");
	cleanup();
	return 0;
}
