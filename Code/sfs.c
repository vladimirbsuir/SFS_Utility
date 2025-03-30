#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h> 
#include <fcntl.h>

#include "sfs.h"

void sfs_format(int fd);
void sfs_create_file(int fd, const char *path);
void sfs_create_dir(int fd, const char *path);

void sfs_format(int fd) {
    struct sfs_superblock sb = {
        .magic = SFS_MAGIC,
        .block_size = BLOCK_SIZE,
        .inode_count = NUM_INODES,
        .free_inodes = NUM_INODES - 1, 
        .free_blocks = NUM_DATA_BLOCKS,
        .root_inode = 0,
        
        .inode_bitmap_block = 1,   
        .block_bitmap_block = 2, 
        .inode_table_block = 3,      
        .data_blocks_start = 3 + (NUM_INODES * sizeof(struct sfs_inode)) / BLOCK_SIZE + 1
    };

    lseek(fd, 0, SEEK_SET);
    write(fd, &sb, sizeof(sb));

    uint8_t bitmap[BLOCK_SIZE] = {0};
  
    bitmap[0] |= 0x01; 
    lseek(fd, BLOCK_SIZE * sb.inode_bitmap_block, SEEK_SET);
    write(fd, bitmap, BLOCK_SIZE);
    
    memset(bitmap, 0, BLOCK_SIZE);

    for(int i = 0; i < sb.data_blocks_start; i++) {
        bitmap[i/8] |= (1 << (i%8));
    }
    lseek(fd, BLOCK_SIZE * sb.block_bitmap_block, SEEK_SET);
    write(fd, bitmap, BLOCK_SIZE);

    struct sfs_dir_entry dir_entries[BLOCK_SIZE / sizeof(struct sfs_dir_entry)] = {{".", 0}, {"..", 0}};
    lseek(fd, 2 * BLOCK_SIZE, SEEK_SET);
    write(fd, dir_entries, BLOCK_SIZE);
}

uint32_t find_inode_by_path(int fd, const char *path) {
    struct sfs_superblock sb;
    lseek(fd, 0, SEEK_SET);
    read(fd, &sb, sizeof(sb));

    uint32_t current_inode = sb.root_inode;
    char *path_copy = strdup(path);
    char *component = strtok(path_copy, "/");

    while(component != NULL) {
        struct sfs_inode inode;
        lseek(fd, sb.inode_table_block * BLOCK_SIZE + current_inode * sizeof(struct sfs_inode), SEEK_SET);
        read(fd, &inode, sizeof(inode));

        if(inode.type != SFS_TYPE_DIR) {
            free(path_copy);
            return (uint32_t)-1; 
        }

        int found = 0;
        for(uint32_t block_idx = 0; block_idx < inode.size/BLOCK_SIZE; block_idx++) {
            lseek(fd, inode.blocks[block_idx] * BLOCK_SIZE, SEEK_SET);
            
            for(int i = 0; i < BLOCK_SIZE/sizeof(struct sfs_dir_entry); i++) {
                struct sfs_dir_entry entry;
                read(fd, &entry, sizeof(entry));
                
                if(entry.inode_num != 0 && strcmp(entry.name, component) == 0) {
                    current_inode = entry.inode_num;
                    found = 1;
                    break;
                }
            }
            if(found) break;
        }

        if(!found) {
            free(path_copy);
            return (uint32_t)-1;
        }

        component = strtok(NULL, "/");
    }

    free(path_copy);
    return current_inode;
}

void sfs_create_file(int fd, const char *path) {
    struct sfs_superblock sb;
    lseek(fd, 0, SEEK_SET);
    read(fd, &sb, sizeof(sb));

    char *path_copy = strdup(path);
    char *dir_name = dirname(path_copy);
    char *file_name = basename(path_copy);

    uint32_t parent_inode = find_inode_by_path(fd, dir_name);
    if(parent_inode == (uint32_t)-1) {
        fprintf(stderr, "Parent directory not found\n");
        free(path_copy);
        return;
    }

    struct sfs_inode parent_inode_data;
    lseek(fd, sb.inode_table_block * BLOCK_SIZE + parent_inode * sizeof(struct sfs_inode), SEEK_SET);
    read(fd, &parent_inode_data, sizeof(parent_inode_data));
      
    if(parent_inode_data.type != SFS_TYPE_DIR) {
        fprintf(stderr, "Parent is not a directory\n");
        free(path_copy);
        return;
    }

    struct sfs_inode parent_inode_data;
    lseek(fd, sb.inode_table_block * BLOCK_SIZE + parent_inode * sizeof(struct sfs_inode), SEEK_SET);
    read(fd, &parent_inode_data, sizeof(parent_inode_data));

    struct sfs_dir_entry entry;
    uint32_t block_idx = 0;
    int found_slot = -1;
    for(block_idx; block_idx < parent_inode_data.size/BLOCK_SIZE; block_idx++) {
        lseek(fd, parent_inode_data.blocks[block_idx] * BLOCK_SIZE, SEEK_SET);
        
        for(int i = 0; i < BLOCK_SIZE/sizeof(struct sfs_dir_entry); i++) {
            read(fd, &entry, sizeof(entry));
            
            if(entry.inode_num == 0) {
                found_slot = i;
                break;
            }
        }
        if(found_slot != -1) break;
    }

    if(found_slot == -1) {
        uint32_t new_block = allocate_block(fd, &sb);
        parent_inode_data.blocks[parent_inode_data.size/BLOCK_SIZE] = new_block;
        parent_inode_data.size += BLOCK_SIZE;
        found_slot = 0;
    }

    uint32_t new_inode_num = find_free_inode(fd, &sb);
    if(new_inode_num == (uint32_t)-1) {
        fprintf(stderr, "No free inodes\n");
        free(path_copy);
        return;
    }

    struct sfs_inode new_inode = {
        .type = SFS_TYPE_FILE,
        .size = 0,
        .ctime = time(NULL),
        .mtime = time(NULL),
        .indirect_block = 0
    };
    memset(new_inode.blocks, 0, sizeof(new_inode.blocks));

    struct sfs_dir_entry new_entry;
    strncpy(new_entry.name, file_name, MAX_NAME_LEN);
    new_entry.inode_num = new_inode_num;

    lseek(fd, parent_inode_data.blocks[block_idx] * BLOCK_SIZE + found_slot * sizeof(struct sfs_dir_entry), SEEK_SET);
    write(fd, &new_entry, sizeof(new_entry));

    parent_inode_data.mtime = time(NULL);
    lseek(fd, sb.inode_table_block * BLOCK_SIZE + parent_inode * sizeof(struct sfs_inode), SEEK_SET);
    write(fd, &parent_inode_data, sizeof(parent_inode_data));

    lseek(fd, sb.inode_table_block * BLOCK_SIZE + new_inode_num * sizeof(struct sfs_inode), SEEK_SET);
    write(fd, &new_inode, sizeof(new_inode));

    sb.free_inodes--;
    lseek(fd, 0, SEEK_SET);
    write(fd, &sb, sizeof(sb));

    free(path_copy);
}

int sfs_check(int fd, int repair) {
    struct sfs_superblock sb;
    int errors = 0;
    
    lseek(fd, 0, SEEK_SET);
    if(read(fd, &sb, sizeof(sb)) != sizeof(sb)) {
        fprintf(stderr, "Superblock read failed\n");
        return -1;
    }

    if(sb.magic != SFS_MAGIC) {
        fprintf(stderr, "Invalid magic number: 0x%08X\n", sb.magic);
        return -1;
    }

    uint8_t inode_bitmap[BLOCK_SIZE];
    lseek(fd, sb.inode_bitmap_block * BLOCK_SIZE, SEEK_SET);
    read(fd, inode_bitmap, BLOCK_SIZE);

    uint8_t block_bitmap[BLOCK_SIZE];
    lseek(fd, sb.block_bitmap_block * BLOCK_SIZE, SEEK_SET);
    read(fd, block_bitmap, BLOCK_SIZE);

    uint32_t inodes_used = 0;
    for(uint32_t i = 0; i < sb.inode_count; i++) {
        uint32_t byte = i / 8;
        uint32_t bit = i % 8;
        int allocated = inode_bitmap[byte] & (1 << bit);

        struct sfs_inode inode;
        lseek(fd, sb.inode_table_block * BLOCK_SIZE + i * sizeof(inode), SEEK_SET);
        read(fd, &inode, sizeof(inode));

        if(allocated && inode.type == 0) {
            fprintf(stderr, "Inode %d marked but not used\n", i);
            errors |= SFS_ERROR_INODE;
            if(repair) inode_bitmap[byte] &= ~(1 << bit);
        }
        
        if(!allocated && inode.type != 0) {
            fprintf(stderr, "Inode %d used but not marked\n", i);
            errors |= SFS_ERROR_INODE;
            if(repair) inode_bitmap[byte] |= (1 << bit);
        }

        if(inode.type != 0) inodes_used++;
    }

    uint32_t blocks_used = 0;
    for(uint32_t i = 0; i < NUM_DATA_BLOCKS; i++) {
        uint32_t byte = i / 8;
        uint32_t bit = i % 8;
        int allocated = block_bitmap[byte] & (1 << bit);
    }
    if(sb.free_inodes != (sb.inode_count - inodes_used)) {
        fprintf(stderr, "Incorrect free inodes count\n");
        errors |= SFS_ERROR_BITMAP;
        if(repair) sb.free_inodes = sb.inode_count - inodes_used;
    }

    if(repair && errors) {
        lseek(fd, sb.inode_bitmap_block * BLOCK_SIZE, SEEK_SET);
        write(fd, inode_bitmap, BLOCK_SIZE);
        
        lseek(fd, sb.block_bitmap_block * BLOCK_SIZE, SEEK_SET);
        write(fd, block_bitmap, BLOCK_SIZE);

        lseek(fd, 0, SEEK_SET);
        write(fd, &sb, sizeof(sb));
    }

    return errors;
}

static uint32_t allocate_block(int fd, struct sfs_superblock *sb) {
    uint8_t bitmap[BLOCK_SIZE];
    lseek(fd, sb->block_bitmap_block * BLOCK_SIZE, SEEK_SET);
    read(fd, bitmap, BLOCK_SIZE);
    
    for(uint32_t i = 0; i < NUM_DATA_BLOCKS; i++) {
        uint32_t byte = i / 8;
        uint32_t bit = i % 8;
        if(!(bitmap[byte] & (1 << bit))) {
            bitmap[byte] |= (1 << bit);
            lseek(fd, sb->block_bitmap_block * BLOCK_SIZE, SEEK_SET);
            write(fd, bitmap, BLOCK_SIZE);
            sb->free_blocks--;
            return i + sb->data_blocks_start;
        }
    }
    return (uint32_t)-1;
}

int sfs_write(int fd, uint32_t inode_num, const void *buf, size_t size, off_t offset) {
    struct sfs_superblock sb;
    lseek(fd, 0, SEEK_SET);
    read(fd, &sb, sizeof(sb));

    struct sfs_inode inode;
    lseek(fd, sb.inode_table_block * BLOCK_SIZE + inode_num * sizeof(inode), SEEK_SET);
    read(fd, &inode, sizeof(inode));

    size_t total_written = 0;
    while(total_written < size) {
        uint32_t block_idx = (offset + total_written) / BLOCK_SIZE;
        uint32_t block_offset = (offset + total_written) % BLOCK_SIZE;
        size_t to_write = MIN(size - total_written, BLOCK_SIZE - block_offset);

        if(block_idx >= DIRECT_BLOCKS) {
            return -1;
        }

        if(inode.blocks[block_idx] == 0) {
            uint32_t new_block = allocate_block(fd, &sb);
            if(new_block == (uint32_t)-1) return -1;
            inode.blocks[block_idx] = new_block;
        }

        lseek(fd, inode.blocks[block_idx] * BLOCK_SIZE + block_offset, SEEK_SET);
        write(fd, (char*)buf + total_written, to_write);
        
        total_written += to_write;
    }

    if(offset + total_written > inode.size) {
        inode.size = offset + total_written;
    }
    inode.mtime = time(NULL);
    lseek(fd, sb.inode_table_block * BLOCK_SIZE + inode_num * sizeof(inode), SEEK_SET);
    write(fd, &inode, sizeof(inode));

    return total_written;
}

int sfs_read(int fd, uint32_t inode_num, void *buf, size_t size, off_t offset) {
    struct sfs_superblock sb;
    lseek(fd, 0, SEEK_SET);
    read(fd, &sb, sizeof(sb));

    struct sfs_inode inode;
    lseek(fd, sb.inode_table_block * BLOCK_SIZE + inode_num * sizeof(inode), SEEK_SET);
    read(fd, &inode, sizeof(inode));

    size_t total_read = 0;
    while(total_read < size && offset + total_read < inode.size) {
        uint32_t block_idx = (offset + total_read) / BLOCK_SIZE;
        uint32_t block_offset = (offset + total_read) % BLOCK_SIZE;
        size_t to_read = MIN(size - total_read, BLOCK_SIZE - block_offset);
        to_read = MIN(to_read, inode.size - (offset + total_read));

        if(block_idx >= DIRECT_BLOCKS || inode.blocks[block_idx] == 0) break;

        lseek(fd, inode.blocks[block_idx] * BLOCK_SIZE + block_offset, SEEK_SET);
        read(fd, (char*)buf + total_read, to_read);
        
        total_read += to_read;
    }

    return total_read;
}

static uint32_t find_free_inode(int fd, struct sfs_superblock *sb) {
    uint8_t bitmap[BLOCK_SIZE];
    lseek(fd, sb->inode_bitmap_block * BLOCK_SIZE, SEEK_SET);
    read(fd, bitmap, BLOCK_SIZE);
    
    for(uint32_t i = 0; i < sb->inode_count; i++) {
        uint32_t byte = i / 8;
        uint32_t bit = i % 8;
        if(!(bitmap[byte] & (1 << bit))) {
            bitmap[byte] |= (1 << bit);
            lseek(fd, sb->inode_bitmap_block * BLOCK_SIZE, SEEK_SET);
            write(fd, bitmap, BLOCK_SIZE);
            return i;
        }
    }
    return (uint32_t)-1;
}