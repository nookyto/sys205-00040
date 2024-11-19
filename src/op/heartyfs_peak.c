#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define DISK_FILE_PATH "/tmp/heartyfs"
#define BLOCK_SIZE (1 << 9)
#define DISK_SIZE (1 << 20)
#define NUM_BLOCK (DISK_SIZE / BLOCK_SIZE)
#define MAX_ENTRIES 14
#define MAX_DEPTH 10  // Add maximum depth to prevent infinite recursion

struct heartyfs_dir_entry {
    int block_id;
    char file_name[28];
};

struct heartyfs_directory {
    int type;
    char name[28];
    int size;
    struct heartyfs_dir_entry entries[14];
};

struct heartyfs_inode {
    int type;
    char name[28];
    int size;
    int data_blocks[119];
};

// Function to get block from memory map
void* get_block(void* disk, int block_num) {
    if (block_num < 0 || block_num >= NUM_BLOCK) {
        return NULL;
    }
    return disk + (block_num * BLOCK_SIZE);
}

// Function to print directory structure
void print_directory_structure(void* disk, int block_num, int level) {
    // Check for maximum recursion depth
    if (level >= MAX_DEPTH) {
        return;
    }

    // Get directory block
    struct heartyfs_directory* dir = (struct heartyfs_directory*)get_block(disk, block_num);
    if (!dir) {
        return;
    }

    // Print the root directory differently
    if (level == 0) {
        printf("/\n");
    }

    // Print entries
    for (int i = 0; i < MAX_ENTRIES; i++) {
        // Skip invalid entries
        if (dir->entries[i].block_id <= 0 || dir->entries[i].file_name[0] == '\0') {
            continue;
        }

        // Skip self and parent directory entries
        if (strcmp(dir->entries[i].file_name, ".") == 0 || 
            strcmp(dir->entries[i].file_name, "..") == 0) {
            continue;
        }

        // Print indentation
        for (int j = 0; j < level; j++) {
            printf("    ");
        }

        // Get the inode for the entry
        struct heartyfs_inode* inode = (struct heartyfs_inode*)get_block(disk, dir->entries[i].block_id);
        if (!inode) {
            continue;
        }

        // Print the entry name
        printf("├── %s", dir->entries[i].file_name);

        // If it's a directory, print / and recurse
        if (inode->type == 1) {
            printf("/\n");
            // Only recurse if the block_id is different from current block
            if (dir->entries[i].block_id != block_num) {
                print_directory_structure(disk, dir->entries[i].block_id, level + 1);
            }
        } else {
            printf("\n");
        }
    }
}

int main() {
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

    printf("HeartyFS Directory Structure:\n");
    print_directory_structure(disk, 0, 0);

    // Cleanup
    munmap(disk, DISK_SIZE);
    close(fd);
    return 0;
}