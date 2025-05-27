#include "network.h"
#include "sfs.h"

#include <ncurses.h>

int server_port;
pthread_mutex_t requests_mutex;
file_request pending_requests[MAX_PENDING_REQUESTS];
int request_count;

void* server_thread(void* arg) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(server_port),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return NULL;
    }
    if (listen(server_fd, MAX_PENDING_REQUESTS) < 0) {
        perror("listen");
        return NULL;
    }

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

        file_metadata fm;
        recv(client_fd, &fm, sizeof(file_metadata), 0);

        pthread_mutex_lock(&requests_mutex);
        if (request_count < MAX_PENDING_REQUESTS) {
            pending_requests[request_count].fm = fm;
            inet_ntop(AF_INET, &client_addr.sin_addr, pending_requests[request_count].sender_ip, INET_ADDRSTRLEN);
            pending_requests[request_count].socket_fd = client_fd;
            request_count++;
        } else {
            close(client_fd);
        }
        pthread_mutex_unlock(&requests_mutex);
    }
    return NULL;
}

int8_t send_file(char* filepath, const char* ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in receiver_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = inet_addr(ip)
    };

    if (connect(sock, (struct sockaddr*)&receiver_addr, sizeof(receiver_addr)) < 0) {
        return -1;
    }

    struct path_components path_c = parse_path(filepath);
    uint32_t parent_inode = find_parent_dir(path_c);
    if (parent_inode == -1) {
        close(sock);
        return -2;
    }
    struct inode file_inode;
    
    char buffer[BLOCK_SIZE];
    read_block(parent_inode, buffer);
    struct dirent* dir_entries = (struct dirent*)buffer;
    
    for (int i = 0; i < BLOCK_SIZE / sizeof(struct dirent); i++) {
        if (dir_entries[i].inode_num == 0) {
            close(sock);
            return -3;
        }

        if (strcmp(dir_entries[i].name, path_c.components[path_c.count-1]) == 0) {
            read_inode(dir_entries[i].inode_num, &file_inode);
            break;
        }
    }

    file_metadata fm = {
        .send_time = time(NULL)
    };
    strncpy(fm.filename,  path_c.components[path_c.count - 1], MAX_NAME_LEN);
    send(sock, &fm, sizeof(file_metadata), 0);

    char response[2];
    struct pollfd pfd = { .fd = sock, .events = POLLIN };
    if (poll(&pfd, 1, 10000) == 0) {
        close(sock);
        return -4;
    }
    recv(sock, response, 1, 0);
    
    if (response[0] != 'y') {
        close(sock);
        return -5;
    }

    for (int i = 0; i < MAX_BLOCK_COUNT && file_inode.blocks[i] != 0; i++) {
        char data[BLOCK_SIZE] = {0};
        read_block(file_inode.blocks[i], data);
        send(sock, data, BLOCK_SIZE, 0);
    }
    
    close(sock);
    free_path_component_struct(&path_c);
    return 1;
}

void check_incoming_requests(WINDOW* win, int* row) {
    pthread_mutex_lock(&requests_mutex);

    time_t current_time = time(NULL);
    
    for (int i = 0; i < request_count; ) {
        if (current_time - pending_requests[i].fm.send_time >= 10) {
            close(pending_requests[i].socket_fd);
            
            for (int j = i; j < request_count - 1; j++) {
                pending_requests[j] = pending_requests[j+1];
            }
            request_count--;
        } else {
            i++;
        }
    }

    for (int i = 0; i < request_count; i++) {
        mvwprintw(win, *row, 2, "File: %s, From: %s", pending_requests[i].fm.filename, pending_requests[i].sender_ip);
        (*row)++;
        mvwprintw(win, *row, 2, "Accept? (y/n):");
        char choice;
        int flag = 0;
        
        nodelay(win, TRUE);

        while (time(NULL) - pending_requests[i].fm.send_time < 10) {
            choice = wgetch(win);
            if (choice == 'y' || choice == 'n') {
                flag = 1;
                break;
            }

            napms(100);
        }

        nodelay(win, FALSE);

        if (choice == 'y' && flag) {
            int sock = pending_requests[i].socket_fd;
            send(sock, "y", 1, 0);
            
            struct inode parent_inode;
            char buffer[BLOCK_SIZE] = {0};
            read_inode(0, &parent_inode); 
            read_block(parent_inode.blocks[0], buffer);
            struct dirent* dir_entries = (struct dirent*)buffer;
        
            for (int i = 0; i < BLOCK_SIZE / sizeof(struct dirent); i++) {
                if (dir_entries[i].inode_num == 0) break;
        
                if (strcmp(dir_entries[i].name, pending_requests[i].fm.filename) == 0) {
                    (*row)++;
                    mvwprintw(win, *row, 2, "File with this name already exists");
                    close(pending_requests[i].socket_fd);
                    return;
                }
            }

            uint32_t file_inode_num = create_file(pending_requests[i].fm.filename);
            struct inode file_inode;
            read_inode(file_inode_num, &file_inode);
            
            for (int j = 0; j < MAX_BLOCK_COUNT; j++) {
                char data[BLOCK_SIZE];
                if (recv(sock, data, BLOCK_SIZE, 0) == 0) break;
                uint32_t block_num = find_free_block();
                set_block(block_num, 1);
                write_block(block_num, data);
                file_inode.blocks[j] = block_num;
            }
            write_inode(file_inode_num, &file_inode);
        } else {
            send(pending_requests[i].socket_fd, "n", 1, 0);
        }
        close(pending_requests[i].socket_fd);
    }
    request_count = 0;
    pthread_mutex_unlock(&requests_mutex);
}