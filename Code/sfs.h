#pragma once

#include <stdint.h>
#include <time.h>

#include <ncurses.h>

#define DIR 0
#define FIL 1

#define TXT 0
#define BIN 1
#define ARH 2
#define ENC 3

#define MAX_BLOCK_COUNT 12
#define TOTAL_INODE 256
#define TOTAL_BLOCKS 256

#define ROOT_INODE 0

#define SFS_SIZE 1024 * 1024 * 32
#define BLOCK_SIZE 4096
#define INODE_SIZE sizeof(struct inode)

#define MAX_NAME_LEN 32
#define MAX_PATH_LEN 255

extern uint32_t fd;
extern struct superblock sb;

struct superblock {
    uint32_t magic;
    uint16_t block_size;
    uint16_t total_blocks; 
    uint16_t free_blocks;
    uint16_t total_inode;
    uint8_t bitmap_inode[TOTAL_INODE];
    uint8_t bitmap_blocks[TOTAL_BLOCKS];
};

struct inode {
    uint32_t type;
    uint32_t size;
    uint16_t blocks[MAX_BLOCK_COUNT];
    time_t create_time;
};

struct dirent {
    uint32_t inode_num;
    char name[MAX_NAME_LEN];
};

struct path_components {
    char** components;
    int count;
};

void sfs_init(const char* path);
void read_sb(struct superblock* sb);

uint8_t read_block(uint32_t block_num, void* buffer);
uint8_t write_block(uint32_t block_num, const void* buffer);
uint32_t find_free_block();
void set_block(uint32_t inode_num, uint8_t is_busy);

uint8_t create_inode();
void delete_inode();
uint8_t read_inode(uint32_t inode_num, struct inode* node);
uint8_t write_inode(uint32_t inode_num, const struct inode* node);
uint32_t find_free_inode();
void set_inode(uint32_t inode_num, uint8_t is_busy);

int32_t create_file(char* path);
int8_t read_file();
int8_t delete_file(char* path);
int8_t write_file(char* path);
void search_file(const char* name);

int8_t create_dir(char* path);
void read_dir();
char** print_dir(char* path);
int8_t delete_dir(char* path);

void check_inodes(uint32_t count, uint32_t inode_num, struct superblock* sb);
void check_dirs(uint32_t count, uint32_t inode_num);
void check_duplicates(WINDOW* win, int* row);
void check_metadata(WINDOW* win, int* row);
void check_blocks(WINDOW* win, int* row);

void clear_files_data();
void delete_all();
void defragment();
void change_sfs();

char* get_time_str(time_t t);
struct path_components parse_path(char* path);
void free_path_component_struct(struct path_components* s);
uint8_t compare_last_n_chars(const char* str, const char* substr, uint8_t n);
void print_bitmap_inode();
void print_bitmap_blocks();
uint32_t find_parent_dir(struct path_components path_c);

void encrypt_data(char* data, size_t size, const uint8_t* key);
void decrypt_data(char *data, size_t size, const uint8_t *key);