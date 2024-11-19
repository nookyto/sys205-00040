#include "../heartyfs.h"
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file_path>\n", argv[0]);
        return 1;
    }

    // Open the disk file
    int fd = open(DISK_FILE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Cannot open the disk file\n");
        return 1;
    }

    // Map the disk file onto memory
    void *buffer = mmap(NULL, DISK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer == MAP_FAILED) {
        perror("Cannot map the disk file onto memory\n");
        close(fd);
        return 1;
    }

    // Parse the file path to get the parent directory and file name
    char *file_path = argv[1];
    char *dir_path = strdup(file_path);
    char *file_name = strrchr(dir_path, '/');
    if (file_name == NULL) {
        fprintf(stderr, "Invalid file path\n");
        munmap(buffer, DISK_SIZE);
        close(fd);
        return 1;
    }
    *file_name = '\0';
    file_name++;

    // Get the root directory
    struct heartyfs_directory *root = (struct heartyfs_directory *)buffer;

    // Find the parent directory
    struct heartyfs_directory *parent_dir = root;
    if (strlen(dir_path) > 1) {
        char *token = strtok(dir_path, "/");
        while (token != NULL) {
            int found = 0;
            for (int i = 0; i < parent_dir->size; i++) {
                if (strcmp(parent_dir->entries[i].file_name, token) == 0) {
                    parent_dir = (struct heartyfs_directory *)(buffer + parent_dir->entries[i].block_id * BLOCK_SIZE);
                    found = 1;
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "Directory %s not found\n", dir_path);
                munmap(buffer, DISK_SIZE);
                close(fd);
                return 1;
            }
            token = strtok(NULL, "/");
        }
    }

    // Find a free block for the new inode
    unsigned char *bitmap = (unsigned char *)(buffer + BLOCK_SIZE);
    int free_block = -1;
    for (int i = 2; i < NUM_BLOCK; i++) {
        if (bitmap[i / 8] & (1 << (i % 8))) {
            free_block = i;
            bitmap[i / 8] &= ~(1 << (i % 8)); // Mark the block as used
            break;
        }
    }

    if (free_block == -1) {
        fprintf(stderr, "No free blocks available\n");
        munmap(buffer, DISK_SIZE);
        close(fd);
        return 1;
    }

    // Initialize the new inode
    struct heartyfs_inode *new_inode = (struct heartyfs_inode *)(buffer + free_block * BLOCK_SIZE);
    new_inode->type = 0;
    strcpy(new_inode->name, file_name);
    new_inode->size = 0;
    memset(new_inode->data_blocks, 0, sizeof(new_inode->data_blocks));

    // Add the new file entry to the parent directory
    parent_dir->entries[parent_dir->size].block_id = free_block;
    strcpy(parent_dir->entries[parent_dir->size].file_name, file_name);
    parent_dir->size++;

    // Flush changes to disk
    msync(buffer, DISK_SIZE, MS_SYNC);

    // Clean up
    munmap(buffer, DISK_SIZE);
    close(fd);
    free(dir_path);

    printf("Created file %s\n", argv[1]);
    return 0;
}
