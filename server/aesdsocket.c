#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <netinet/in.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#define WRITE_FILE "/var/tmp/aesdsocketdata"
#define BUF_LEN 1000

struct thread_args{
	int clientfd;
	struct sockaddr_in client_addr;
};
struct thread_node{
	pthread_t thread;
	struct thread_args *args;
	TAILQ_ENTRY(thread_node) entries;
};
volatile sig_atomic_t exit_requested = 0;
void signal_handler(int signo){
	if(signo == SIGINT || signo == SIGTERM){
		exit_requested = 1;
	}
}

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

void* thread_func(void *args){
	struct thread_args *targs = (struct thread_args*)args;
	struct sockaddr_in client_addr = targs->client_addr;
	int clientfd = targs->clientfd;
	char client_ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
	syslog(LOG_INFO, "Accepted connection from %s", client_ip);

	char buffer[BUF_LEN];
	char *packet = NULL;
	int packet_len = 0;
	FILE *fd = fopen(WRITE_FILE, "a");
	if(!fd){
		syslog(LOG_ERR, "Failed to fopen(fd)");
		return NULL;
	}
	
	while(1){
		ssize_t bytes_read = recv(clientfd, buffer, BUF_LEN, 0);
		if(bytes_read == -1){
			syslog(LOG_ERR, "Failed to recv()");
			close(clientfd);
			if(packet != NULL)
				free(packet);
			fclose(fd);
			return NULL;
		}
		if(bytes_read == 0){
			syslog(LOG_INFO,"Closed connection from %s", client_ip);
			close(clientfd);
			if(packet != NULL)
				free(packet);
			fclose(fd);
			break;
		}

		char *tmp = realloc(packet, packet_len + bytes_read);
		if(tmp == NULL){
			syslog(LOG_ERR, "Failed to realloc()");
			close(clientfd);
			if(packet != NULL)
				free(packet);
			fclose(fd);
			return NULL;
		}

		packet = tmp;
		memcpy(packet + packet_len, buffer, bytes_read);
		packet_len += bytes_read;

		//if(buffer[bytes_read-1] == '\n'){
		if(packet_len > 0 && packet[packet_len - 1] == '\n'){
			pthread_mutex_lock(&file_mutex);

			size_t bytes_wrote = fwrite(packet, 1, packet_len, fd);
			fflush(fd);

			pthread_mutex_unlock(&file_mutex);

			if(bytes_wrote != packet_len){
				syslog(LOG_ERR, "Failed to fwrite()");
				close(clientfd);
				if(packet != NULL)
					free(packet);
				fclose(fd);
				return NULL;
			}
			free(packet);
			packet = NULL;
			packet_len = 0;


			FILE *readfd = fopen(WRITE_FILE, "r");
			if(readfd == NULL){
				syslog(LOG_ERR, "Failed to fopen(readfd)");
				close(clientfd);
				if(packet != NULL)
					free(packet);
				fclose(fd);
				return NULL;
			}

			pthread_mutex_lock(&file_mutex);

			while((bytes_read = fread(buffer, 1, BUF_LEN, readfd)) > 0){
				send(clientfd, buffer, bytes_read, 0);
			}

			pthread_mutex_unlock(&file_mutex);

			fclose(readfd);
			readfd = NULL;
		}
	}
	return NULL;
}

void* timestamp_thread(void* args){
	FILE* fd = fopen(WRITE_FILE, "a");
	if(fd == NULL){
		syslog(LOG_ERR, "Failed to fopen(fd)");
		return NULL;
	}
	char outstr[200];

	while(!exit_requested){
		time_t t = time(NULL);
		struct tm tmp_time;
			
		if(localtime_r(&t, &tmp_time) == NULL){
			syslog(LOG_ERR, "Failed to localtime_r()");
			return NULL;
		}

		if(strftime(outstr, sizeof(outstr), 
					"%a, %d %b %Y %H:%M:%S %z", 
					&tmp_time) == 0){
			syslog(LOG_ERR, "Failed to strftime()");
			break;
		}

		pthread_mutex_lock(&file_mutex);
		fprintf(fd, "timestamp:%s\n", outstr);
		fflush(fd);
		pthread_mutex_unlock(&file_mutex);

		sleep(10);
	}
	fclose(fd);
	return NULL;
}


int main(int argc, char **argv){
	remove(WRITE_FILE);
	bool daemon_mode = 0;
	if(argc > 2 || (argc == 2 && strcmp(argv[1], "-d") != 0)){
		printf("Usage: %s [-d]\n", argv[0]);
		return -1;
	}else if(argc == 2 && strcmp(argv[1], "-d")==0){
		daemon_mode = true;
	}
	
	printf("Starting server\n");
	openlog(argv[0], LOG_PID, LOG_USER);

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	int sockfd;
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
		close(sockfd);
		return -1;
	}
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(9000);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0){
		syslog(LOG_ERR, "Failed to Bind");
		close(sockfd);
		return -1;
	}
	if(listen(sockfd, 10) != 0){
		syslog(LOG_ERR, "Failed to listen()");
		close(sockfd);
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
	//------------------------------------------------------------------
	pthread_t timestamper;
	pthread_create(&timestamper, NULL, timestamp_thread, NULL);
	TAILQ_HEAD(thread_list, thread_node) head;
	TAILQ_INIT(&head);
	
	while(!exit_requested){
		struct sockaddr_in client_addr;
		socklen_t client_addr_size = sizeof(client_addr);
		int clientfd = accept(
			sockfd, (struct sockaddr*)&client_addr, &client_addr_size
		);
		if(clientfd == -1){
			if(exit_requested) break;
			if(errno == EINTR) continue;
			syslog(LOG_ERR, "Failed to accept()");
			continue;
		}
		struct thread_node *node = malloc(sizeof(*node));
		node->args = malloc(sizeof(struct thread_args));
		node->args->clientfd = clientfd;
		node->args->client_addr = client_addr;
		if(pthread_create(&node->thread, NULL, thread_func, node->args) != 0)
			syslog(LOG_ERR, "Failed to pthread_create()");
		else 
			TAILQ_INSERT_TAIL(&head, node, entries);
	}
	close(sockfd);
	syslog(LOG_INFO, "Received signal, exiting");
	struct thread_node *node;
	while(!TAILQ_EMPTY(&head)){
		node = TAILQ_FIRST(&head);
		pthread_join(node->thread, NULL);
		TAILQ_REMOVE(&head, node, entries);
		free(node->args);
		free(node);
	}
	pthread_join(timestamper, NULL);
    return 0;
}

