#include <stdint.h>
#include <time.h>

#define SFS_MAGIC 0x11111111
#define BLOCK_SIZE 4096
#define MAX_NAME_LEN 255
#define NUM_INODES 1024
#define NUM_DATA_BLOCKS 8192
#define DIRECT_BLOCKS 16
#define INDIRECT_BLOCKS (BLOCK_SIZE / sizeof(uint32_t))

#define SFS_ERROR_NONE 0
#define SFS_ERROR_BITMAP 1
#define SFS_ERROR_INODE 2
#define SFS_ERROR_BLOCKS 3
#define SFS_ERROR_LINKS 4

#define SFS_TYPE_FILE 1
#define SFS_TYPE_DIR 2

struct sfs_superblock {
    uint32_t magic;            
    uint32_t block_size;        
    uint32_t inode_count;     
    uint32_t free_inodes;      
    uint32_t free_blocks;      
    uint32_t root_inode;        

    uint32_t inode_bitmap_block;  
    uint32_t block_bitmap_block;  
    uint32_t inode_table_block; 
    uint32_t data_blocks_start;  
};

struct sfs_inode {
    uint32_t type;          
    uint32_t size;          
    uint32_t blocks[DIRECT_BLOCKS];
    uint32_t indirect_block;    
    time_t ctime;          
    time_t mtime;          
};

struct sfs_dir_entry {
    char name[MAX_NAME_LEN];
    uint32_t inode_num;
};