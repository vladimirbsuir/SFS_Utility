#include "sfs.h"
#include "aes.h"
#include "network.h"
#include "ui.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <termios.h>
#include <ctype.h>

uint32_t fd = 0;
struct superblock sb;

void sfs_init(const char* path) {
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);

    if (fd == -1) {
        perror("open file system");
        return;
    }

    if (ftruncate(fd, SFS_SIZE) < 0) {
        perror("ftruncate file system");
        return;
    }

    struct superblock sb = {
        0xDEADBEEF, BLOCK_SIZE, TOTAL_BLOCKS, TOTAL_BLOCKS, TOTAL_INODE, {0}, {0}
    };
    sb.bitmap_inode[0] = 1;
    sb.bitmap_blocks[0] = 1;

    if (lseek(fd, 0, SEEK_SET) == -1) {
        perror("lseek superblock");
        return;
    }
    write(fd, &sb, sizeof(struct superblock));

    struct inode root_inode = {
        .type = DIR, .size = 0, .create_time = time(NULL), .blocks = {0}
    };

    write_inode(0, &root_inode);
}

uint8_t read_inode(uint32_t inode_num, struct inode* buffer) {
    uint32_t offset = sizeof(struct superblock) + inode_num * sizeof(struct inode);
    if (lseek(fd, offset, SEEK_SET) == -1) {
        perror("lseek inode");
        return 0;
    }
    
    size_t readden = read(fd, buffer, INODE_SIZE);
    if (readden == -1) {
        perror("read inode");
        return 0;
    }

    return 1;
}

uint8_t write_inode(uint32_t inode_num, const struct inode* buffer) {
    uint32_t offset = sizeof(struct superblock) + inode_num * sizeof(struct inode);
    if (lseek(fd, offset, SEEK_SET) == -1) {
        perror("lseek inode");
        return 0;
    }

    size_t written = write(fd, buffer, INODE_SIZE);
    if (written == -1) {
        perror("write inode");
        return 0;
    }

    return 1;
}

uint8_t read_block(uint32_t block_num, void* buffer) {
    if (block_num >= TOTAL_BLOCKS) {
        printf("Error (read block): block number is bigger than total amount of blocks\n");
        return 0;
    }

    uint32_t offset = sizeof(struct superblock) + sizeof(struct inode) * sb.total_inode + block_num * BLOCK_SIZE;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        perror("lseek block");
        return 0;
    }
    size_t readden = read(fd, buffer, BLOCK_SIZE);
    if (readden == -1) {
        perror("read block");
        return 0;
    }

    return 1;
}

uint8_t write_block(uint32_t block_num, const void* buffer) {
    uint32_t offset = sizeof(struct superblock) + sizeof(struct inode) * sb.total_inode + block_num * BLOCK_SIZE;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        perror("lseek block");
        return 0;
    }

    size_t written = write(fd, buffer, BLOCK_SIZE);
    if (written == -1) {
        perror("write block");
        return 0;
    }

    return 1;
}

void print_bitmap_inode() {
    struct superblock sb;
    read_sb(&sb);

    printf("Inodes bitmap:\n");
    for (int i = 0; i < TOTAL_INODE; i++) {
        printf("%d ", sb.bitmap_inode[i]);
    }
    printf("\n");
}

void print_bitmap_blocks() {
    struct superblock sb;
    read_sb(&sb);

    printf("Blocks bitmap:\n");
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        printf("%d ", sb.bitmap_blocks[i]);
    }
    printf("\n");
}

void read_sb(struct superblock* sb) {
    if (lseek(fd, 0, SEEK_SET) == -1) {
        perror("lseek superblock");
        return;
    }
    read(fd, sb, sizeof(struct superblock));
}

void write_sb(const struct superblock sb) {
    if (lseek(fd, 0, SEEK_SET) == -1) {
        perror("lseek superblock");
        return;
    }

    write(fd, &sb, sizeof(struct superblock));
}

struct path_components parse_path(char* path) {
    struct path_components result = {.components = NULL, .count = 0};
    
    while(*path == '/') path++;

    char* path_copy = strdup(path);
    char* token = strtok(path_copy, "/");
    while (token) {
        result.components = realloc(result.components, (result.count + 1) * sizeof(char*));
        result.components[result.count] = strdup(token);
        result.count++;
        token = strtok(NULL, "/");
    }

    return result;
}

uint32_t find_parent_dir(struct path_components path_c) {
    uint32_t inode_num = 0;

    for (int i = 0; i < path_c.count - 1; i++) {
        struct inode inode;
        read_inode(inode_num, &inode);

        if (inode.type != DIR) {
            //printf("Error: '%s' is not a directory\n", path_c.components[i]);
            return -1;
        }

        if (inode.blocks[0] == -1) {
            //printf("Error: directory '%s' is empty\n", path_c.components[i]);
            return -1;
        }

        char buffer[BLOCK_SIZE];
        read_block(inode.blocks[0], buffer);
        struct dirent* objects = (struct dirent*)buffer;

        int found = 0;
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++) {
            if (objects[j].inode_num == 0) break;

            if (strcmp(objects[j].name, path_c.components[i]) == 0) {
                inode_num = objects[j].inode_num;
                found = 1;
                break;
            }
        }

        if (!found) {
            //printf("Error: directory '%s' not found\n", path_c.components[i]);
            return -1;
        }
    }

    return inode_num;
}

void free_path_component_struct(struct path_components* s) {
    for (int i = 0; i < s->count; i++) free(s->components[i]);
    free(s->components);
}

void add_dirent_to_dir(uint32_t inode_num, struct dirent* object) {
    struct inode dir_inode;
    read_inode(inode_num, &dir_inode);

    if (dir_inode.blocks[0] == 0 && inode_num != 0) {
        uint32_t new_block_num = find_free_block();
        set_block(new_block_num, 1);
        dir_inode.blocks[0] = new_block_num;
    }

    char buffer[BLOCK_SIZE];
    read_block(dir_inode.blocks[0], buffer);
    struct dirent* objects = (struct dirent*)buffer;
    
    int i = 0;
    for (i; i < BLOCK_SIZE / sizeof(struct dirent); i++) {
        if (objects[i].inode_num == 0) break;
    }

    if (i == BLOCK_SIZE / sizeof(struct dirent)) {
        printf("Error; directory block is full\n");
        return;
    }

    objects[i] = *object;
    write_block(dir_inode.blocks[0], objects);

    dir_inode.size += sizeof(struct dirent);
    write_inode(inode_num, &dir_inode);
}

int8_t create_dir(char* path) {
    struct path_components path_c = parse_path(path);

    if (path_c.count == 0) {
        free_path_component_struct(&path_c);
        return -1;
    }

    uint32_t parent_inode_num = find_parent_dir(path_c);
    if (parent_inode_num == -1) {
        free_path_component_struct(&path_c);
        return -2;
    }

    struct inode new_dir = {
        .type = DIR, .size = 0, .blocks = {0}, .create_time = time(NULL)
    };

    int new_inode_num = find_free_inode();
    write_inode(new_inode_num, &new_dir);
    set_inode(new_inode_num, 1);

    struct dirent new_dirent = {
        .inode_num = new_inode_num
    };
    strncpy(new_dirent.name, path_c.components[path_c.count - 1], MAX_NAME_LEN);

    add_dirent_to_dir(parent_inode_num, &new_dirent);

    free_path_component_struct(&path_c);
    return 1;
}

uint8_t compare_last_n_chars(const char* str, const char* substr, uint8_t n) {
    uint8_t str_len = strlen(str);
    uint8_t substr_len = strlen(substr);

    if (n > str_len || n != substr_len) {
        printf("Error: invalid value of substring size\n");
        return 0;
    }

    const char* last_n_chars = str + str_len - n;
    if (strncmp(last_n_chars, substr, n) == 0) return 1;
    return 0;
}

int32_t create_file(char* path) {
    struct path_components path_c = parse_path(path);

    if (path_c.count == 0) {
        return -1;
    }

    if (strlen(path_c.components[path_c.count - 1]) >= 4 && (compare_last_n_chars(path_c.components[path_c.count - 1], ".txt", 4) == 0
        || compare_last_n_chars(path_c.components[path_c.count - 1], ".bin", 4) == 0
        || compare_last_n_chars(path_c.components[path_c.count - 1], ".enc", 4) == 0)) {
            //free_path_component_struct(&path_c);
            //return -2;
    } else {
        free_path_component_struct(&path_c);
        return -2;
    }

    uint32_t parent_inode_num = find_parent_dir(path_c);
    if (parent_inode_num == -1) {
        
        free_path_component_struct(&path_c);
        return -3;
    }

    struct inode parent_inode;
    char buffer[BLOCK_SIZE] = {0};
    read_inode(parent_inode_num, &parent_inode);
    read_block(parent_inode.blocks[0], buffer);
    struct dirent* dir_entries = (struct dirent*)buffer;

    for (int i = 0; i < BLOCK_SIZE / sizeof(struct dirent); i++) {
        if (dir_entries[i].inode_num == 0) break;

        if (strcmp(dir_entries[i].name, path_c.components[path_c.count - 1]) == 0) {
            free_path_component_struct(&path_c);
            return -4;
        }
    }

    struct inode new_file = {
        .type = FIL, .size = 0, .blocks = {0}, .create_time = time(NULL)
    };

    int new_inode_num = find_free_inode();
    write_inode(new_inode_num, &new_file);
    set_inode(new_inode_num, 1);

    struct dirent new_dirent = {
        .inode_num = new_inode_num
    };
    strncpy(new_dirent.name, path_c.components[path_c.count - 1], MAX_NAME_LEN);

    add_dirent_to_dir(parent_inode_num, &new_dirent);

    free_path_component_struct(&path_c);
    return new_inode_num;
}

void write_data_to_file(const struct dirent object, char* filename) {
    struct inode file_inode;
    read_inode(object.inode_num, &file_inode);

    if (file_inode.type != FIL) {
        printf("Error: '%s' is not a file\n", object.name);
        return;
    }

    char data[BLOCK_SIZE] = {0};
    printf("Enter data to store (press Esc to finish):\n");
    size_t total_size = 0;
    int block_index = 0;

    struct termios old_termios, new_termios;
    tcgetattr(STDIN_FILENO, &old_termios);
    new_termios = old_termios;
    new_termios.c_lflag &= ~ICANON;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

    uint32_t new_block_num = find_free_block();
    if (new_block_num == -1) {
        printf("Error writing data to file\n");
        return;
    }
    file_inode.blocks[0] = new_block_num;
    set_block(new_block_num, 1);

    while (1) {
        char c = getchar();

        if (c == 27) {
            if (compare_last_n_chars(filename, ".enc", 4) == 1) {
                uint8_t key[] = {
                    0x00, 0x01, 0x02, 0x03,
                    0x04, 0x05, 0x06, 0x07,
                    0x08, 0x09, 0x0a, 0x0b,
                    0x0c, 0x0d, 0x0e, 0x0f,
                    0x10, 0x11, 0x12, 0x13,
                    0x14, 0x15, 0x16, 0x17,
                    0x18, 0x19, 0x1a, 0x1b,
                    0x1c, 0x1d, 0x1e, 0x1f};

                uint8_t* exp_key = aes_init(sizeof(key));
                aes_key_expansion(key, exp_key);
                char enc_data[BLOCK_SIZE] = {0};
                aes_encrypt(data, enc_data, BLOCK_SIZE, exp_key);
                write_block(file_inode.blocks[block_index], enc_data);
            } else write_block(file_inode.blocks[block_index], data);

            break;
        }

        data[total_size % BLOCK_SIZE] = c;
        total_size++;

        if (total_size % BLOCK_SIZE == 0) {
            if (compare_last_n_chars(filename, ".enc", 4) == 1) {
                uint8_t key[] = {
                    0x00, 0x01, 0x02, 0x03,
                    0x04, 0x05, 0x06, 0x07,
                    0x08, 0x09, 0x0a, 0x0b,
                    0x0c, 0x0d, 0x0e, 0x0f,
                    0x10, 0x11, 0x12, 0x13,
                    0x14, 0x15, 0x16, 0x17,
                    0x18, 0x19, 0x1a, 0x1b,
                    0x1c, 0x1d, 0x1e, 0x1f};

                uint8_t* exp_key = aes_init(sizeof(key));
                aes_key_expansion(key, exp_key);
                char enc_data[BLOCK_SIZE] = {0};
                aes_inv_cipher(enc_data, data, exp_key);
            }
            write_block(file_inode.blocks[block_index], data);
            block_index++;
            if (block_index == 12) {
                printf("Error: file size limit exceeded\n");
                break;
            }
            new_block_num = find_free_block();
            file_inode.blocks[block_index] = new_block_num;
            set_block(new_block_num, 1);
            for (int i = 0; i < BLOCK_SIZE; i++) {
                data[i] = '\0';
            }
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);

    file_inode.size = total_size;
    write_inode(object.inode_num, &file_inode);

    printf("\nData written successfully\n");
}

int8_t write_file(char* path) {
    struct path_components path_c = parse_path(path);

    if (path_c.count == 0) {
        printf("Error: empty path\n");
        return -1;
    }

    uint32_t parent_inode_num = find_parent_dir(path_c);
    if (parent_inode_num == -1) {
        free_path_component_struct(&path_c);
        return -2;
    }

    struct inode dir_inode;
    read_inode(parent_inode_num, &dir_inode);
    
    char buffer[BLOCK_SIZE];
    read_block(dir_inode.blocks[0], buffer);
    struct dirent* objects = (struct dirent*)buffer;

    for (int i = 0; i < BLOCK_SIZE / sizeof(struct dirent); i++) {
        if (objects[i].inode_num == 0) {
            printf("Error: there is no such file in this directory\n");
            free_path_component_struct(&path_c);
            return -3;
        }

        if (strcmp(objects[i].name, path_c.components[path_c.count - 1]) == 0) {
            write_data_to_file(objects[i], path_c.components[path_c.count - 1]);
            break;
        }
    }

    free_path_component_struct(&path_c);
    return 1;
}

void read_data_from_file(const struct dirent object, char* filename) {
    struct inode file_inode;
    read_inode(object.inode_num, &file_inode);

    if (file_inode.type != FIL) {
        printf("Error: '%s' is not a file\n", object.name);
        return;
    }

    char data[BLOCK_SIZE] = {0};
    printf("Data in file '%s':\n", object.name);

    size_t total_size = 0;
    int block_index = 0;

    while (file_inode.blocks[block_index] != 0 && block_index != MAX_BLOCK_COUNT) {
        read_block(file_inode.blocks[block_index], data);
        
        if (compare_last_n_chars(filename, ".enc", 4) == 1) {
            uint8_t key[] = {
                0x00, 0x01, 0x02, 0x03,
                0x04, 0x05, 0x06, 0x07,
                0x08, 0x09, 0x0a, 0x0b,
                0x0c, 0x0d, 0x0e, 0x0f,
                0x10, 0x11, 0x12, 0x13,
                0x14, 0x15, 0x16, 0x17,
                0x18, 0x19, 0x1a, 0x1b,
                0x1c, 0x1d, 0x1e, 0x1f};

            uint8_t* exp_key = aes_init(sizeof(key));
            aes_key_expansion(key, exp_key);
            char dec_data[BLOCK_SIZE] = {0};
            aes_decrypt(data, dec_data, BLOCK_SIZE, exp_key);
            printf("%s", dec_data);
        } else printf("%s", data);
        block_index++;
    }
    printf("\n\n");
}

int8_t read_file(char* path) {
    struct path_components path_c = parse_path(path);

    if (path_c.count == 0) {
        printf("Error: empty path\n");
        return -1;
    }

    uint32_t parent_inode_num = find_parent_dir(path_c);
    if (parent_inode_num == -1) {
        printf("Error: there is no directory '%s'\n", path_c.components[path_c.count - 2]);
        free_path_component_struct(&path_c);
        return -2;
    }

    struct inode dir_inode;
    read_inode(parent_inode_num, &dir_inode);
    
    char buffer[BLOCK_SIZE];
    read_block(dir_inode.blocks[0], buffer);
    struct dirent* objects = (struct dirent*)buffer;

    for (int i = 0; i < BLOCK_SIZE / sizeof(struct dirent); i++) {
        if (objects[i].inode_num == 0) {
            printf("Error: there is no file '%s' in directory '%s'\n", path_c.components[path_c.count - 1], path_c.count == 1 ? "root" : path_c.components[path_c.count - 2]);
            return -3;
        }

        if (strcmp(objects[i].name, path_c.components[path_c.count - 1]) == 0) {
            read_data_from_file(objects[i], path_c.components[path_c.count - 1]);
            break;
        }
    }

    free_path_component_struct(&path_c);
    return 1;
}

int8_t delete_file(char* path) {
    struct path_components path_c = parse_path(path);

    if (path_c.count == 0) {
        return -1;
    }

    uint32_t parent_inode_num = find_parent_dir(path_c);
    if (parent_inode_num == -1) {
        free_path_component_struct(&path_c);
        return -2;
    }

    struct inode dir_inode;
    read_inode(parent_inode_num, &dir_inode);
    
    char buffer[BLOCK_SIZE];
    read_block(dir_inode.blocks[0], buffer);
    struct dirent* objects = (struct dirent*)buffer;

    int8_t found = -1;
    for (int i = 0; i < BLOCK_SIZE / sizeof(struct dirent); i++) {
        if (objects[i].inode_num == 0 ) return -3;

        if (strcmp(objects[i].name, path_c.components[path_c.count - 1]) == 0) {
            struct inode file_inode;
            read_inode(objects[i].inode_num, &file_inode);

            char buffer[BLOCK_SIZE] = {0}; 
            for (int j = 0; j < MAX_BLOCK_COUNT; j++) {
                if (file_inode.blocks[j] == 0) break; 
                set_block(file_inode.blocks[j], 0);
                write_block(file_inode.blocks[j], buffer);
            }

            struct inode clear_node = {0};
            set_inode(objects[i].inode_num, 0);
            write_inode(objects[i].inode_num, &clear_node);

            for (int j = i; j < BLOCK_SIZE / sizeof(struct dirent); j++) {
                if (objects[j].inode_num == 0 && j != 0) {
                    break;
                }
                objects[j] = objects[j + 1];
            }
            write_block(dir_inode.blocks[0], objects);
            found = 1;
            break;
        }
    }

    free_path_component_struct(&path_c);
    if (found == -1) return -3;
    return 1;
}

void delete_file_in_dir(struct inode obj_inode) {
    printf("1");
    char buffer[BLOCK_SIZE] = {0}; 
    for (int i = 0; i < MAX_BLOCK_COUNT; i++) {
        if (obj_inode.blocks[i] == 0) break;
        set_block(obj_inode.blocks[i], 0);
        write_block(obj_inode.blocks[i], buffer);
    }
}

void delete_dir_inode(struct inode obj_inode) {
    printf("1");
    char buffer[BLOCK_SIZE];
    if (obj_inode.blocks[0] == 0) return;
    read_block(obj_inode.blocks[0], buffer);
    struct dirent* objects = (struct dirent*)buffer;

    for (int i = 0; i < BLOCK_SIZE / sizeof(struct dirent); i++) {
        if (objects[i].inode_num == 0) break;

        struct inode delete_inode;
        read_inode(objects[i].inode_num, &delete_inode);

        if (delete_inode.type == FIL) delete_file_in_dir(delete_inode);
        else if (delete_inode.type == DIR) delete_dir_inode(delete_inode);

        struct inode clear_inode = {0};
        set_inode(objects[i].inode_num, 0);
        write_inode(objects[i].inode_num, &clear_inode);
    }

    char clear_buffer[BLOCK_SIZE] = {0};
    set_block(obj_inode.blocks[0], 0);
    write_block(obj_inode.blocks[0], clear_buffer);
}

int8_t delete_dir(char* path) {
    struct path_components path_c = parse_path(path);
    printf("%s", path_c.components[path_c.count - 1]);
    if (path_c.count == 0) {
        return -1;
    }

    uint32_t parent_inode_num = find_parent_dir(path_c);
    if (parent_inode_num == -1) {
        free_path_component_struct(&path_c);
        return -2;
    }

    struct inode dir_inode;
    read_inode(parent_inode_num, &dir_inode);
    
    char buffer[BLOCK_SIZE];
    read_block(dir_inode.blocks[0], buffer);
    struct dirent* objects = (struct dirent*)buffer;

    for (int i = 0; i < BLOCK_SIZE / sizeof(struct dirent); i++) {
        if (objects[i].inode_num == 0 && path_c.count > 1) return -3;

        if (strcmp(objects[i].name, path_c.components[path_c.count - 1]) == 0) {
            struct inode dir_inode_to_delete;
            read_inode(objects[i].inode_num, &dir_inode_to_delete);

            char buffer1[BLOCK_SIZE];
            if (dir_inode_to_delete.blocks[0] != 0) {
                read_block(dir_inode_to_delete.blocks[0], &buffer1);
                struct dirent* dir_objects = (struct dirent*)buffer1;

                for (int j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++) {
                    if (dir_objects[j].inode_num == 0) break;
                    struct inode obj_inode;
                    read_inode(dir_objects[j].inode_num, &obj_inode);

                    if (obj_inode.type == DIR) delete_dir_inode(obj_inode);
                    else if (obj_inode.type == FIL) delete_file_in_dir(obj_inode);

                    struct inode clear_node = {0};
                    set_inode(dir_objects[j].inode_num, 0);
                    write_inode(dir_objects[j].inode_num, &clear_node);
                }
            }

            struct inode clear_node = {0};
            set_inode(objects[i].inode_num, 0);
            write_inode(objects[i].inode_num, &clear_node);

            for (int j = i; j < BLOCK_SIZE / sizeof(struct dirent); j++) {
                if (objects[j].inode_num == 0 && j != 0) {
                    break;
                }
                objects[j] = objects[j + 1];
            }
            
            if (dir_inode_to_delete.blocks[0] != 0) {
                char clear_buffer[BLOCK_SIZE] = {0};
                set_block(dir_inode_to_delete.blocks[0], 0);
                write_block(dir_inode_to_delete.blocks[0], clear_buffer);
            }
            write_block(dir_inode.blocks[0], objects);

            break;
        }
    }

    free_path_component_struct(&path_c);
    return 1;
}

int32_t find_dir_to_print(struct path_components path_c) {
    uint32_t inode_num = 0;

    for (int i = 0; i < path_c.count; i++) {
        struct inode inode;
        read_inode(inode_num, &inode);

        if (inode.type != DIR) {
            //printf("Error: '%s' is not a directory\n", path_c.components[i]);
            return -1;
        }

        if (inode.blocks[0] == -1) {
            //printf("Error: directory '%s' is empty\n", path_c.components[i]);
            return -1;
        }

        char buffer[BLOCK_SIZE];
        read_block(inode.blocks[0], buffer);
        struct dirent* objects = (struct dirent*)buffer;

        int found = 0;
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++) {
            if (objects[j].inode_num == 0) break;

            if (strcmp(objects[j].name, path_c.components[i]) == 0) {
                inode_num = objects[j].inode_num;
                found = 1;
                break;
            }
        }

        if (!found) {
            //printf("Error: directory '%s' not found\n", path_c.components[i]);
            return -1;
        }
    }

    return inode_num;
}

char** print_dir(char* path) {
    int dirents_count = 0;
    char** dirents = malloc(sizeof(char*) * ++dirents_count);
    dirents[dirents_count - 1] = calloc(MAX_PATH_LEN, sizeof(char));
    strncpy(dirents[0], "0", MAX_PATH_LEN);

    struct path_components path_c = parse_path(path);

    int32_t inode_num = 0;
    if (path_c.count != 0) {
        inode_num = find_dir_to_print(path_c);
    }

    if (inode_num == -1) {
        strncpy(dirents[0], "Directory not found", MAX_PATH_LEN);
        free_path_component_struct(&path_c);
        return dirents;
    }

    struct inode dir_inode;
    read_inode(inode_num, &dir_inode);

    if (dir_inode.blocks[0] == 0 && inode_num != 0) {
        strncpy(dirents[0], "Directory is empty", MAX_PATH_LEN);
        return dirents;
    }

    char buffer[BLOCK_SIZE];
    read_block(dir_inode.blocks[0], buffer);
    struct dirent* objects = (struct dirent*)buffer;

    for (int i = 0; i < BLOCK_SIZE / sizeof(struct dirent); i++) {
        if (objects[i].inode_num == 0) break;
        
        dirents = realloc(dirents, sizeof(char*) * (++dirents_count));
        dirents[dirents_count - 1] = calloc(MAX_PATH_LEN, sizeof(char));
        snprintf(dirents[dirents_count - 1], MAX_PATH_LEN, "Inode number: %d, Name: %s", objects[i].inode_num, objects[i].name);
        snprintf(dirents[0], MAX_PATH_LEN, "%d", dirents_count);
    }

    return dirents;
}

char* get_time_str(time_t t) {
    struct tm* creation_time = localtime(&t);
    char* str = calloc(20, sizeof(char));
    strftime(str, 20, "%d/%m/%y %T", creation_time);
    return str;
}

uint32_t find_free_block() {
    struct superblock sb;
    read_sb(&sb);

    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        if (sb.bitmap_blocks[i] == 0) return i;
    }

    printf("There is no free block to store data\n");
    return -1;
}

uint32_t find_free_inode() {
    struct superblock sb;
    read_sb(&sb);

    for (int i = 0; i < TOTAL_INODE; i++) {
        if (sb.bitmap_inode[i] == 0) return i;
    }

    printf("There is no free inode to store metadata\n");
    return -1;
}

void set_inode(uint32_t inode_num, uint8_t is_busy) {
    if (inode_num == 0) {
        printf("Error: unnable to set root inode as 0\n");
        return;
    }
    struct superblock sb;
    read_sb(&sb);
    sb.bitmap_inode[inode_num] = is_busy;
    write_sb(sb);
}

void set_block(uint32_t block_num, uint8_t is_busy) {
    if (block_num == 0) {
        printf("Error: unnable to set root block as 0\n");
        return;
    }
    struct superblock sb;
    read_sb(&sb);
    sb.bitmap_blocks[block_num] = is_busy;
    write_sb(sb);
}

void delete_all() {
    struct superblock sb;
    read_sb(&sb);
    
    char clear_buffer[BLOCK_SIZE] = {0};
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        if (sb.bitmap_blocks[i] != 0) {
            write_block(i, clear_buffer);
        }
    }

    struct inode clear_inode = {0};
    for (int i = 1; i < TOTAL_INODE; i++) {
        if (sb.bitmap_inode[i] != 0) {
            write_inode(i, &clear_inode);
        }
    }

    printf("Filesystem was cleared successfully\n");
}

void clear_files_data() {
    struct superblock sb;
    read_sb(&sb);
    
    char clear_buffer[BLOCK_SIZE] = {0};
    for (int i = 1; i < TOTAL_BLOCKS; i++) {
        if (sb.bitmap_blocks[i] != 0) {
            write_block(i, clear_buffer);
        }
    }

    printf("Files were cleared successfully\n");
}

void defragment(WINDOW* win, int* row) {
    int count = 0;
    struct inode object;
    struct superblock sb;

    read_sb(&sb);
    
    for (int i = 1; i < TOTAL_INODE; i++) {
        read_inode(i, &object);

        for (int j = 0; j < MAX_BLOCK_COUNT; j++) {
            if (object.blocks[j] == 0) break;

            for (int k = 1; k < TOTAL_BLOCKS; k++) {
                if (k == object.blocks[j]) break;

                if (sb.bitmap_blocks[k] == 0) {
                    char buffer[BLOCK_SIZE] = {0};
                    read_block(object.blocks[j], buffer);
                    write_block(k, buffer);
                    sb.bitmap_blocks[k] = 1;
                    char clear_buffer[BLOCK_SIZE] = {0};
                    sb.bitmap_blocks[object.blocks[j]] = 0;
                    write_block(object.blocks[j], clear_buffer);
                    object.blocks[j] = k;

                    count++;
                    break;
                }
            }
        }

        write_inode(i, &object);
    }

    write_sb(sb);
    mvwprintw(win, *row, 2, "Amount of corrected blocks of memory: %d", count);
}

void check_blocks(WINDOW* win, int* row) {
    uint32_t count = 0;
    uint32_t inode_num = 0;
    struct inode object;
    struct superblock sb;

    read_sb(&sb);

    for (int i = 0; i < TOTAL_INODE; i++) {
        read_inode(inode_num, &object);

        for (int j = 0; j < MAX_BLOCK_COUNT; j++) {
            if (object.blocks[j] == 0) break;

            if (sb.bitmap_blocks[object.blocks[j]] == 0) {
                count++;
                mvwprintw(win, (*row)++, 2, "Correcting block (%d)", object.blocks[j]);
                sb.bitmap_blocks[object.blocks[j]] = 1;
            }
        }

        write_inode(inode_num, &object);
    }

    mvwprintw(win, *row, 2, "Amount of corrected blocks: %d", count);

    write_sb(sb);
}

void check_metadata(WINDOW* win, int* row) {
    struct superblock sb;
    uint32_t free_blocks_amount = 0;
    uint32_t free_inodes_amount = 0;

    read_sb(&sb);

    for (int i = 1; i < sb.total_blocks; i++) {
        free_blocks_amount += !sb.bitmap_blocks[i];
    }

    for (int i = 1; i < sb.total_inode; i++) {
        free_inodes_amount += !sb.bitmap_inode[i];
    }

    if (free_blocks_amount != sb.free_blocks) {
        mvwprintw(win, (*row)++, 2, "Correcting blocks count (was: %d, new: %d)", sb.free_blocks, free_blocks_amount);
        sb.free_blocks = free_blocks_amount;
        write_sb(sb);
    }

    char buffer[7] = {0};
    float space = ((float)free_inodes_amount / sb.total_inode) * 100;
    sprintf(buffer, "%.2f", space);
    buffer[5] = '%';
    mvwprintw(win, *row, 2, "Amount of free space in filesystem: %s", buffer);
}

void check_inodes(uint32_t count, uint32_t inode_num, struct superblock* sb) {
    struct inode dir_inode;
    struct inode object;
    read_inode(inode_num, &dir_inode);

    char buffer[BLOCK_SIZE] = {0};
    read_block(dir_inode.blocks[0], buffer);
    struct dirent* objects = (struct dirent*)buffer;

    for (int i = 0; i < BLOCK_SIZE / sizeof(struct dirent); i++) {
        if (objects[i].inode_num == 0) break;
        
        if (sb->bitmap_inode[objects[i].inode_num] == 0) {
            count++;
            printf("Correcting inode %d\n", objects[i].inode_num);
            sb->bitmap_inode[objects[i].inode_num] = 1;
        }

        read_inode(objects[i].inode_num, &object);
        if (object.type == DIR) check_inodes(count, objects[i].inode_num, sb);
    }

    if (inode_num == 0) {
        printf("Amount of corrected inodes: %d\n", count);
        if (count > 0) {
            write_sb(*sb);
        }
    } 
}

void check_dirs(uint32_t count, uint32_t inode_num) {
    struct inode dir;
    struct dirent* objects;
    struct inode object;

    read_inode(inode_num, &dir);

    char buffer[BLOCK_SIZE] = {0};
    read_block(dir.blocks[0], buffer);
    objects = (struct dirent*)buffer;

    for (int i = 0; i < BLOCK_SIZE / sizeof(struct dirent); i++) {
        if (objects[i].inode_num == 0) break;

        if (objects[i].inode_num >= TOTAL_INODE) {
            count++;
            printf("Correcting directory entry with inode number %d\n", objects[i].inode_num);

            for (int j = i; j < BLOCK_SIZE / sizeof(struct dirent) - 1; j++) {
                if (objects[j].inode_num == 0) break;

                objects[j] = objects[j + 1];
            }
        }

        read_inode(objects[i].inode_num, &object);
        if (object.type == DIR) check_dirs(count, objects[i].inode_num);
    }

    if (count > 0) {
        write_inode(inode_num, &dir);
    }

    if (inode_num == 0) {
        printf("Amount of corrected directory entries: %d\n", count);
    }
}

void check_duplicates(WINDOW* win, int* row) {
    struct superblock sb;
    struct inode object;
    uint8_t blocks_usage[TOTAL_BLOCKS] = {0};
    uint8_t** blocks_num_inodes = calloc(TOTAL_BLOCKS, sizeof(uint8_t*));

    for (int i = 0; i < TOTAL_BLOCKS; i++) blocks_num_inodes[i] = NULL;
    
    read_sb(&sb);

    for (int i = 1; i < TOTAL_INODE; i++) {
        if (sb.bitmap_inode[i] != 0) {
            read_inode(i, &object);

            for (int j = 0; j < MAX_BLOCK_COUNT; j++) {
                if (object.blocks[j] == 0) break;

                blocks_usage[object.blocks[j]]++;
                blocks_num_inodes[object.blocks[j]] = realloc(blocks_num_inodes[object.blocks[j]], sizeof(uint8_t) * blocks_usage[object.blocks[j]]);
                blocks_num_inodes[object.blocks[j]][blocks_usage[object.blocks[j]] - 1] = i;
            }
        }
    }

    uint16_t count = 0;
    for (int i = 1; i < TOTAL_BLOCKS; i++) {
        if (blocks_usage[i] > 1) {
            count++;
            mvwprintw(win, (*row)++, 2, "Block %d is used by several inodes", i);
        }
    }

    mvwprintw(win, *row, 2, "Amount of total duplicates: %d", count);
    
    for (int i = 0; i < TOTAL_BLOCKS; i++) free(blocks_num_inodes[i]);
    free(blocks_num_inodes);
}
/* WRITE TO FILE */


// Add ability to use Backspace to change already written symbols
/*void write_data_to_file(const struct dirent object, char* filename) {
    struct inode file_inode;
    read_inode(object.inode_num, &file_inode);

    if (file_inode.type != FIL) {
        printf("Error: '%s' is not a file\n", object.name);
        return;
    }

    char data[BLOCK_SIZE] = {0};
    printf("Enter data to store (press Esc to finish):\n");
    size_t total_size = 0;
    int block_index = 0;

    struct termios old_termios, new_termios;
    tcgetattr(STDIN_FILENO, &old_termios);
    new_termios = old_termios;
    new_termios.c_lflag &= ~ICANON;//~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

    uint32_t new_block_num = find_free_block();
    if (new_block_num == -1) {
        printf("Error writing data to file\n");
        return;
    }
    file_inode.blocks[0] = new_block_num;
    set_block(new_block_num, 1);

    while (1) {
        char c = getchar();

        if (c == 27) {
            if (compare_last_n_chars(filename, ".enc", 4) == 1) {
                uint8_t key[] = {
                    0x00, 0x01, 0x02, 0x03,
                    0x04, 0x05, 0x06, 0x07,
                    0x08, 0x09, 0x0a, 0x0b,
                    0x0c, 0x0d, 0x0e, 0x0f,
                    0x10, 0x11, 0x12, 0x13,
                    0x14, 0x15, 0x16, 0x17,
                    0x18, 0x19, 0x1a, 0x1b,
                    0x1c, 0x1d, 0x1e, 0x1f};

                uint8_t* exp_key = aes_init(sizeof(key));
                aes_key_expansion(key, exp_key);
                char enc_data[BLOCK_SIZE] = {0};
                aes_encrypt(data, enc_data, BLOCK_SIZE, exp_key);
                write_block(file_inode.blocks[block_index], enc_data);
            } else write_block(file_inode.blocks[block_index], data);

            break;
        }

        data[total_size % BLOCK_SIZE] = c;
        total_size++;

        if (total_size % BLOCK_SIZE == 0) {
            if (compare_last_n_chars(filename, ".enc", 4) == 1) {
                uint8_t key[] = {
                    0x00, 0x01, 0x02, 0x03,
                    0x04, 0x05, 0x06, 0x07,
                    0x08, 0x09, 0x0a, 0x0b,
                    0x0c, 0x0d, 0x0e, 0x0f,
                    0x10, 0x11, 0x12, 0x13,
                    0x14, 0x15, 0x16, 0x17,
                    0x18, 0x19, 0x1a, 0x1b,
                    0x1c, 0x1d, 0x1e, 0x1f};

                uint8_t* exp_key = aes_init(sizeof(key));
                aes_key_expansion(key, exp_key);
                char enc_data[BLOCK_SIZE] = {0};
                aes_inv_cipher(enc_data, data, exp_key);
            }
            write_block(file_inode.blocks[block_index], data);
            block_index++;
            if (block_index == 12) {
                printf("Error: file size limit exceeded\n");
                break;
            }
            new_block_num = find_free_block(); // Check if there is free block
            file_inode.blocks[block_index] = new_block_num;
            set_block(new_block_num, 1);
            for (int i = 0; i < BLOCK_SIZE; i++) {
                data[i] = '\0';
            }
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);

    file_inode.size = total_size;
    write_inode(object.inode_num, &file_inode);

    printf("\nData written successfully\n");
}

int8_t write_file(char* path) {
    struct path_components path_c = parse_path(path);

    if (path_c.count == 0) {
        printf("Error: empty path\n");
        return -1;
    }

    uint32_t parent_inode_num = find_parent_dir(path_c);
    if (parent_inode_num == -1) {
        free_path_component_struct(&path_c);
        return -2;
    }

    struct inode dir_inode;
    read_inode(parent_inode_num, &dir_inode);
    
    char buffer[BLOCK_SIZE];
    read_block(dir_inode.blocks[0], buffer);
    struct dirent* objects = (struct dirent*)buffer;

    for (int i = 0; i < BLOCK_SIZE / sizeof(struct dirent); i++) {
        if (objects[i].inode_num == 0) {
            printf("Error: there is no such file in this directory\n");
            free_path_component_struct(&path_c);
            return -3;
        }

        if (strcmp(objects[i].name, path_c.components[path_c.count - 1]) == 0) {
            write_data_to_file(objects[i], path_c.components[path_c.count - 1]);
            break;
        }
    }

    free_path_component_struct(&path_c);
    return 1;
}*/