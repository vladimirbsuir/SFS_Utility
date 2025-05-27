#pragma once

#include "sfs.h"

#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>

#include <ncurses.h>

#define SERVER_PORT 8080
#define MAX_PENDING_REQUESTS 5

extern int server_port;

typedef struct {
    char filename[MAX_NAME_LEN];
    time_t send_time;
} file_metadata;

typedef struct {
    //char filename[32];
    file_metadata fm;
    char sender_ip[INET_ADDRSTRLEN];
    int socket_fd;
} file_request;

extern pthread_mutex_t requests_mutex;
extern file_request pending_requests[MAX_PENDING_REQUESTS];
extern int request_count;

void* server_thread(void* arg);
int8_t send_file(char* filepath, const char* ip, int port);
void check_incoming_requests(WINDOW* win, int* row);