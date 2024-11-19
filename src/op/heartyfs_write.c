#include "../heartyfs.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define MAX_ENTRIES 14
#define MAX_NAME_LENGTH 27
#define MAX_FILE_SIZE (119 * BLOCK_SIZE)  // Maximum file size based on direct blocks only
#define BITMAP_OFFSET 1024  // Offset for the bitmap in bytes

// Get a pointer to a specific block
void* get_block(void* disk, int block_num) {
    return disk + (block_num * BLOCK_SIZE);
}

// Find directory entry by path
int find_dir_entry(void* disk, const char* path, struct heartyfs_directory** parent_dir, 
                   int* parent_block, char* target_name) {
    char* path_copy = strdup(path);
    if (!path_copy) {
        return -1;
    }

    struct heartyfs_directory* current_dir = get_block(disk, 0);
    int current_block = 0;
    
    char* token = strtok(path_copy, "/");
    char* prev_token = NULL;
    
    while (token) {
        prev_token = token;
        token = strtok(NULL, "/");
        
        if (token) {  // Still traversing directories
            int found = 0;
            for (int i = 0; i < MAX_ENTRIES; i++) {
                if (strcmp(current_dir->entries[i].file_name, prev_token) == 0) {
                    current_block = current_dir->entries[i].block_id;
                    current_dir = get_block(disk, current_block);
                    found = 1;
                    break;
                }
            }
            if (!found) {
                free(path_copy);
                return -1;
            }
        }
    }

    *parent_dir = current_dir;
    *parent_block = current_block;
    strncpy(target_name, prev_token, MAX_NAME_LENGTH);
    target_name[MAX_NAME_LENGTH] = '\0';

    free(path_copy);
    return 0;
}

// Bitmap operations
void mark_block_allocated(void* disk, int block_num) {
    unsigned char* bitmap = (unsigned char*)disk + BITMAP_OFFSET;
    bitmap[block_num / 8] |= (1 << (block_num % 8));
}

void mark_block_free(void* disk, int block_num) {
    unsigned char* bitmap = (unsigned char*)disk + BITMAP_OFFSET;
    bitmap[block_num / 8] &= ~(1 << (block_num % 8));
}

int find_free_block(void* disk) {
    unsigned char* bitmap = (unsigned char*)disk + BITMAP_OFFSET;
    for (int i = 0; i < NUM_BLOCK / 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (!(bitmap[i] & (1 << j))) {
                return i * 8 + j;
            }
        }
    }
    return -1;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <heartyfs_path> <source_file_path>\n", argv[0]);
        return 1;
    }

    // Open and check source file
    int src_fd = open(argv[2], O_RDONLY);
    if (src_fd == -1) {
        perror("Error opening source file");
        return 1;
    }

    // Get source file size
    off_t src_size = lseek(src_fd, 0, SEEK_END);
    lseek(src_fd, 0, SEEK_SET);

    if (src_size > MAX_FILE_SIZE) {
        fprintf(stderr, "Error: Source file exceeds maximum size of %d bytes\n", MAX_FILE_SIZE);
        close(src_fd);
        return 1;
    }

    // Open and map heartyfs disk
    int disk_fd = open(DISK_FILE_PATH, O_RDWR);
    if (disk_fd == -1) {
        perror("Error opening heartyfs disk");
        close(src_fd);
        return 1;
    }

    void* disk = mmap(NULL, DISK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, disk_fd, 0);
    if (disk == MAP_FAILED) {
        perror("Error mapping disk");
        close(disk_fd);
        close(src_fd);
        return 1;
    }

    // Find parent directory and target name
    struct heartyfs_directory* parent_dir;
    int parent_block;
    char target_name[MAX_NAME_LENGTH + 1];

    if (find_dir_entry(disk, argv[1], &parent_dir, &parent_block, target_name) != 0) {
        fprintf(stderr, "Error: Invalid path or directory not found\n");
        goto cleanup;
    }

    // Check if directory has space
    if (parent_dir->size >= MAX_ENTRIES) {
        fprintf(stderr, "Error: Directory is full\n");
        goto cleanup;
    }

    // Allocate inode block
    int inode_block = find_free_block(disk);
    if (inode_block == -1) {
        fprintf(stderr, "Error: No free blocks for inode\n");
        goto cleanup;
    }
    mark_block_allocated(disk, inode_block);

    // Initialize inode
    struct heartyfs_inode* inode = get_block(disk, inode_block);
    memset(inode, 0, sizeof(struct heartyfs_inode));
    inode->type = 1;  // Regular file
    strncpy(inode->name, target_name, MAX_NAME_LENGTH);
    inode->size = src_size;

    // Read and write file data
    char buffer[BLOCK_SIZE];
    int bytes_read;
    int block_count = 0;

    while ((bytes_read = read(src_fd, buffer, BLOCK_SIZE)) > 0) {
        if (block_count >= 119) {
            fprintf(stderr, "Error: File requires too many blocks\n");
            goto cleanup;
        }

        int data_block = find_free_block(disk);
        if (data_block == -1) {
            fprintf(stderr, "Error: No free blocks for data\n");
            goto cleanup;
        }
        mark_block_allocated(disk, data_block);

        struct heartyfs_data_block* db = get_block(disk, data_block);
        db->size = bytes_read;
        memcpy(db->data, buffer, bytes_read);
        
        inode->data_blocks[block_count++] = data_block;
    }

    // Add directory entry
    for (int i = 0; i < MAX_ENTRIES; i++) {
        if (parent_dir->entries[i].block_id == 0) {
            strncpy(parent_dir->entries[i].file_name, target_name, MAX_NAME_LENGTH);
            parent_dir->entries[i].block_id = inode_block;
            parent_dir->size++;
            break;
        }
    }

    // Sync changes
    if (msync(disk, DISK_SIZE, MS_SYNC) != 0) {
        perror("Error syncing to disk");
        goto cleanup;
    }

    printf("Successfully wrote file: %s\n", argv[1]);
    
    munmap(disk, DISK_SIZE);
    close(disk_fd);
    close(src_fd);
    return 0;

cleanup:
    munmap(disk, DISK_SIZE);
    close(disk_fd);
    close(src_fd);
    return 1;
}