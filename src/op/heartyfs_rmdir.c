#include "../heartyfs.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>

// Define BITMAP_OFFSET
#define BITMAP_OFFSET 1024  // Adjust this value as needed

struct heartyfs_directory* find_directory(void *buffer, struct heartyfs_directory *current_dir, char *path) {
    printf("[DEBUG] Searching for directory: %s\n", path);
    // Base case: if path is empty or NULL, return current directory
    if (path == NULL || *path == '\0' || strcmp(path, "/") == 0) {
        printf("[DEBUG] Base case: Path is empty\n");
        return current_dir;
    }

    // Make a copy of path since strtok modifies the original string
    char *path_copy = strdup(path);
    if (path_copy == NULL) {
        printf("[ERROR] Memory allocation failed\n");
        return NULL;
    }

    // Skip leading slash if present
    char *current_path = path_copy;
    if (*current_path == '/') {
        printf("[DEBUG] Skipping leading slash\n");
        current_path++;
    }

    // Get the first directory name in the path
    char *token = strtok(current_path, "/");
    if (token == NULL) {
        printf("[DEBUG] No token found\n");
        free(path_copy);
        return current_dir;
    }

    // Special cases for . and ..
    if (strcmp(token, ".") == 0) {
        printf("[DEBUG] Found .\n");
        char *remaining_path = strtok(NULL, "");
        free(path_copy);
        return find_directory(buffer, current_dir, remaining_path);
    }
    printf("[DEBUG] Token: %s\n", token);
    // Search for the directory entry in current directory
    for (int i = 0; i < current_dir->size; i++) {
        printf("[DEBUG] Checking directory entry: %s\n", current_dir->entries[i].file_name);
        if (strcmp(current_dir->entries[i].file_name, token) == 0) {
            printf("[DEBUG] Found matching entry: %s\n", token);
            // Found matching entry, get the next directory from the buffer
            struct heartyfs_directory *next_dir = 
                (struct heartyfs_directory *)(buffer + current_dir->entries[i].block_id * BLOCK_SIZE);
            
            // Get remaining path after the current token
            char *remaining_path = strtok(NULL, "");
            printf("[DEBUG] Remaining path: %s\n", remaining_path ? remaining_path : "(null)");
            
            // Free the path copy before recursive call
            free(path_copy);
            
            // Recursively search in the next directory
            return find_directory(buffer, next_dir, remaining_path);
        }
    }
    printf("[DEBUG] Directory entry %s not found in current directory\n", token);

    // Directory not found
    free(path_copy);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <directory_path>\n", argv[0]);
        return 1;
    }

    // Open the disk file
    int fd = open(DISK_FILE_PATH, O_RDWR);
    if (fd < 0) {
        perror("[ERROR] Cannot open the disk file");
        return 1;
    }

    // Map the disk file onto memory
    void *buffer = mmap(NULL, DISK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer == MAP_FAILED) {
        perror("[ERROR] Cannot map the disk file onto memory");
        close(fd);
        return 1;
    }

    // Get the root directory
    struct heartyfs_directory *root = (struct heartyfs_directory *)buffer;

    // Parse the directory path
    char *dir_path = strdup(argv[1]);
    char *dir_name = strrchr(dir_path, '/');
    if (dir_name == NULL) {
        fprintf(stderr, "[ERROR] Invalid directory path\n");
        free(dir_path);
        munmap(buffer, DISK_SIZE);
        close(fd);
        return 1;
    }
    *dir_name = '\0';  // Split into parent directory path and target name
    dir_name++;

    // Handle the case where the parent directory is the root directory
    printf("[DEBUG] Finding parent directory: %s\n", dir_path);
    struct heartyfs_directory *parent_dir = root;
    if (strlen(dir_path) > 1) {
        printf("[DEBUG] Finding parent directory: %s\n", dir_path);
        parent_dir = find_directory(buffer, root, dir_path);
        if (parent_dir == NULL) {
            fprintf(stderr, "[ERROR] Parent directory %s not found\n", dir_path);
            free(dir_path);
            munmap(buffer, DISK_SIZE);
            close(fd);
            return 1;
        }
    }

    printf("[DEBUG] Parent directory found: %s\n", parent_dir->name);

    // Find the target directory in the parent directory
    struct heartyfs_directory *target_dir = NULL;
    int target_index = -1;
    int target_block_id = -1;

    for (int i = 0; i < parent_dir->size; i++) {
        if (strcmp(parent_dir->entries[i].file_name, dir_name) == 0) {
            target_dir = (struct heartyfs_directory *)(buffer + parent_dir->entries[i].block_id * BLOCK_SIZE);
            target_index = i;
            target_block_id = parent_dir->entries[i].block_id;
            break;
        }
    }

    if (target_dir == NULL) {
        fprintf(stderr, "[ERROR] Directory %s not found\n", argv[1]);
        free(dir_path);
        munmap(buffer, DISK_SIZE);
        close(fd);
        return 1;
    }

    // Check if the directory is empty
    printf("[DEBUG] Checking if directory %s is empty\n", dir_name);
    if (target_dir->size > 2) {  // Only . and .. should be present
        fprintf(stderr, "[ERROR] Directory %s is not empty\n", argv[1]);
        free(dir_path);
        munmap(buffer, DISK_SIZE);
        close(fd);
        return 1;
    }

    // Remove the directory entry from the parent directory
    printf("[DEBUG] Removing directory entry from parent directory\n");
    memset(&parent_dir->entries[target_index], 0, sizeof(struct heartyfs_dir_entry));
    parent_dir->size--;

    // Mark the block as free in the bitmap
    printf("[DEBUG] Marked block %d as free in the bitmap\n", target_block_id);
    // Define and initialize the bitmap
    unsigned char *bitmap = (unsigned char *)(buffer + BITMAP_OFFSET);
    bitmap[target_block_id] = 0;

    // Sync changes to disk
    if (msync(buffer, DISK_SIZE, MS_SYNC) != 0) {
        perror("Error syncing changes to disk");
        free(dir_path);
        munmap(buffer, DISK_SIZE);
        close(fd);
        return 1;
    }

    printf("[DEBUG] Removed directory %s\n", argv[1]);

    free(dir_path);
    munmap(buffer, DISK_SIZE);
    close(fd);
    return 0;
}
