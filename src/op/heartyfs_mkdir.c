#include "../heartyfs.h"
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <directory_path>\n", argv[0]);
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

    // Get the root directory
    struct heartyfs_directory *root = (struct heartyfs_directory *)buffer;

    // Check if heartyfs is initialized
    if (root->type != 1 || strcmp(root->name, "/") != 0) {
        fprintf(stderr, "heartyfs is not initialized\n");
        munmap(buffer, DISK_SIZE);
        close(fd);
        return 1;
    }

    // Parse the directory path to get the parent directory and new directory name
    char *dir_path = strdup(argv[1]);
    char *dir_name = strrchr(dir_path, '/');
    if (dir_name == NULL) {
        fprintf(stderr, "Invalid directory path\n");
        munmap(buffer, DISK_SIZE);
        close(fd);
        return 1;
    }
    *dir_name = '\0';
    dir_name++;

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

    // Check if the parent directory is full
    if (parent_dir->size >= 14) {
        fprintf(stderr, "Parent directory is full\n");
        munmap(buffer, DISK_SIZE);
        close(fd);
        return 1;
    }

    // Find a free block for the new directory
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

    // Initialize the new directory
    struct heartyfs_directory *new_dir = (struct heartyfs_directory *)(buffer + free_block * BLOCK_SIZE);
    new_dir->type = 1;
    strcpy(new_dir->name, dir_name);
    new_dir->size = 2;
    new_dir->entries[0].block_id = free_block;
    strcpy(new_dir->entries[0].file_name, ".");
    new_dir->entries[1].block_id = parent_dir->entries[0].block_id;
    strcpy(new_dir->entries[1].file_name, "..");

    // Add the new directory to the parent directory
    parent_dir->entries[parent_dir->size].block_id = free_block;
    strcpy(parent_dir->entries[parent_dir->size].file_name, dir_name);
    parent_dir->size++;

    // Log the changes
    printf("Created directory %s at block %d\n", argv[1], free_block);

    // Flush changes to disk
    msync(buffer, DISK_SIZE, MS_SYNC);

    // Clean up
    munmap(buffer, DISK_SIZE);
    close(fd);
    free(dir_path);

    return 0;
}
