#include "heartyfs.h"
#include <string.h>
#include <unistd.h>

int main() {
    // Open the disk file
    int fd = open(DISK_FILE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Cannot open the disk file\n");
        exit(1);
    }

    // Zero out the entire disk file
    void *zero_buffer = calloc(1, DISK_SIZE);
    if (zero_buffer == NULL) {
        perror("Cannot allocate memory\n");
        close(fd);
        exit(1);
    }
    if (write(fd, zero_buffer, DISK_SIZE) != DISK_SIZE) {
        perror("Cannot write to the disk file\n");
        free(zero_buffer);
        close(fd);
        exit(1);
    }
    free(zero_buffer);

    // Map the disk file onto memory
    void *buffer = mmap(NULL, DISK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer == MAP_FAILED) {
        perror("Cannot map the disk file onto memory\n");
        close(fd);
        exit(1);
    }

    // Initialize the superblock
    struct heartyfs_directory *root = (struct heartyfs_directory *)buffer;
    root->type = 1;
    strcpy(root->name, "/");
    root->size = 2;
    root->entries[0].block_id = 0;
    strcpy(root->entries[0].file_name, ".");
    root->entries[1].block_id = 0;
    strcpy(root->entries[1].file_name, "..");

    // Initialize the bitmap
    unsigned char *bitmap = (unsigned char *)(buffer + BLOCK_SIZE);
    memset(bitmap, 0xFF, 256); // Set all bits to 1 (free)

    // Flush changes to disk
    msync(buffer, DISK_SIZE, MS_SYNC);

    // Clean up
    munmap(buffer, DISK_SIZE);
    close(fd);

    return 0;
}
