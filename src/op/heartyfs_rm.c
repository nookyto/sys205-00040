#include "../heartyfs.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define MAX_ENTRIES 14
#define MAX_NAME_LENGTH 27

// Function to get block from memory map
void* get_block(void* disk, int block_num) {
    return disk + (block_num * BLOCK_SIZE);
}

// Function to find directory entry by path
int find_dir_entry(void* disk, const char* path, struct heartyfs_directory** parent_dir, int* parent_block, char* target_name) {
    char* path_copy = strdup(path);
    if (path_copy == NULL) {
        perror("Error duplicating path");
        return -1;
    }

    char* token = strtok(path_copy, "/");
    struct heartyfs_directory* current_dir = (struct heartyfs_directory*)get_block(disk, 0);
    int current_block = 0;

    // Handle the case when path starts with '/'
    if (path[0] == '/') {
        current_dir = (struct heartyfs_directory*)get_block(disk, 0);
        current_block = 0;
    }

    char* prev_token = NULL;
    while (token != NULL) {
        prev_token = token;
        token = strtok(NULL, "/");

        if (token != NULL) {  // Still traversing directories
            int found = 0;
            for (int i = 0; i < MAX_ENTRIES; i++) {
                if (strcmp(current_dir->entries[i].file_name, prev_token) == 0) {
                    current_block = current_dir->entries[i].block_id;
                    current_dir = (struct heartyfs_directory*)get_block(disk, current_block);
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

    // Save the parent directory and target filename
    *parent_dir = current_dir;
    *parent_block = current_block;
    strncpy(target_name, prev_token, MAX_NAME_LENGTH);
    target_name[MAX_NAME_LENGTH] = '\0';

    free(path_copy);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <filepath>\n", argv[0]);
        return 1;
    }

    // Open the disk file
    int fd = open(DISK_FILE_PATH, O_RDWR);
    if (fd == -1) {
        perror("Error opening disk file");
        return 1;
    }

    // Map the disk file into memory
    void* disk = mmap(NULL, DISK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (disk == MAP_FAILED) {
        perror("Error mapping disk file");
        close(fd);
        return 1;
    }

    // Find the parent directory and target file
    struct heartyfs_directory* parent_dir;
    int parent_block;
    char target_name[MAX_NAME_LENGTH + 1];

    if (find_dir_entry(disk, argv[1], &parent_dir, &parent_block, target_name) != 0) {
        fprintf(stderr, "Failed to find parent directory\n");
        munmap(disk, DISK_SIZE);
        close(fd);
        return 1;
    }

    // Find and remove the target file entry
    int found = 0;
    for (int i = 0; i < MAX_ENTRIES; i++) {
        if (strcmp(parent_dir->entries[i].file_name, target_name) == 0) {
            // Get the inode of the file
            struct heartyfs_inode* file_inode = (struct heartyfs_inode*)get_block(disk, parent_dir->entries[i].block_id);

            // Clear the data blocks
            for (int j = 0; j < 119 && file_inode->data_blocks[j] != 0; j++) {
                memset(get_block(disk, file_inode->data_blocks[j]), 0, BLOCK_SIZE);
            }

            // Clear the inode
            memset(file_inode, 0, BLOCK_SIZE);

            // Clear the directory entry
            memset(&parent_dir->entries[i], 0, sizeof(struct heartyfs_dir_entry));

            found = 1;
            break;
        }
    }

    if (!found) {
        fprintf(stderr, "File not found: %s\n", target_name);
        munmap(disk, DISK_SIZE);
        close(fd);
        return 1;
    }

    // Sync changes to disk
    if (msync(disk, DISK_SIZE, MS_SYNC) != 0) {
        perror("Error syncing changes to disk");
        munmap(disk, DISK_SIZE);
        close(fd);
        return 1;
    }

    // Cleanup
    munmap(disk, DISK_SIZE);
    close(fd);

    printf("Successfully removed file: %s\n", argv[1]);
    return 0;
}