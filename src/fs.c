#include "disk.h"
#include "fs.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#define min(a,b) (((a) < (b)) ? (a) : (b))

#ifdef DEBUG
#define DEBUG_PRINT(fmt, args...)    fprintf(stderr, fmt, ## args)
#else
#define DEBUG_PRINT(fmt, args...)    /* Don't do anything in release builds */
#endif

// Debug file system -----------------------------------------------------------

void fs_debug(Disk *disk) {
    if (disk == 0)
        return;

    Block block;

    // Read Superblock
    disk_read(disk, 0, block.Data);

    uint32_t magic_num = block.Super.MagicNumber;
    uint32_t num_blocks = block.Super.Blocks;
    uint32_t num_inodeBlocks = block.Super.InodeBlocks;
    uint32_t num_inodes = block.Super.Inodes;

    if (magic_num != MAGIC_NUMBER) {
        printf("Magic number is valid: %c\n", magic_num);
        return;
    }

    printf("SuperBlock:\n");
    printf("    magic number is valid\n");
    printf("    %u blocks\n", num_blocks);
    printf("    %u inode blocks\n", num_inodeBlocks);
    printf("    %u inodes\n", num_inodes);

    uint32_t expected_num_inodeBlocks = round((float)num_blocks / 10);

    if (expected_num_inodeBlocks != num_inodeBlocks) {
        printf("SuperBlock declairs %u InodeBlocks but expect %u InodeBlocks!\n", num_inodeBlocks, expected_num_inodeBlocks);
    }

    uint32_t expect_num_inodes = num_inodeBlocks * INODES_PER_BLOCK;
    if (expect_num_inodes != num_inodes) {
        printf("SuperBlock declairs %u Inodes but expect %u Inodes!\n", num_inodes, expect_num_inodes);
    }

    // Read Inode blocks
    int idx = 0;
    for (int i = 1; i <= num_inodeBlocks; i++) {
        disk_read(disk, i, block.Data);

        // Iterating over all the INODE_PER_BLOCK
        for (int j = 0; j < INODES_PER_BLOCK; j++) {
            if (block.Inodes[j].Valid) {
                printf("Inode %d:\n", idx);
                printf("    size: %u bytes\n", block.Inodes[i].Size);
                printf("    direct blocks:");

                // Iterating through direct nodes
                for (int k = 0; k < POINTERS_PER_INODE; k++) {
                    if (block.Inodes[j].Direct[k]) {
                        printf(" %u", block.Inodes[j].Direct[k]);
                    }
                }
                printf("\n");

                // Iterating through indirect nodes
                if(block.Inodes[j].Indirect) {
                    printf("    indirect block: %u\n",block.Inodes[j].Indirect);
                    printf("    indirect data blocks:");

                    Block inDirBlock;
                    disk_read(disk, block.Inodes[j].Indirect, inDirBlock.Data);

                    for(int k = 0; k < POINTERS_PER_BLOCK; k++) {
                        if(inDirBlock.Pointers[k]) {
                            printf(" %u", inDirBlock.Pointers[k]);
                        }
                    }
                    printf("\n");
                }
            }
            idx++;
        }
    }
}

// Format file system ----------------------------------------------------------

bool fs_format(Disk *disk)
{
    // Checks if disk is already mounted
    if (disk_mounted(disk)) { 
        // Already mounted, so it fails
        return false;
    }

    // Creates a new file system,
    FileSystem *fs = new_fs();
    fs->disk = disk;
    printf("FD: %d \n", disk->FileDescriptor); 
    printf("Blocks: %lu \n", disk->Blocks); 
    printf("Reads: %lu \n", disk->Reads); 
    printf("Writes: %lu \n", disk->Writes); 
    printf("Mounts: %lu \n", disk->Mounts); 

    // TODO: destroy any data already present

    // Set 10% of blocks for inodes. clear inode table

    // Writes Superblock 
    

    return false;
}

// FileSystem constructor 
FileSystem *new_fs()
{
    FileSystem *fs = malloc(sizeof(FileSystem));
    return fs;
}

// FileSystem destructor 
void free_fs(FileSystem *fs)
{
    // FIXME: free resources and allocated memory in FileSystem

    free(fs);
}

// Mount file system -----------------------------------------------------------

bool fs_mount(FileSystem *fs, Disk *disk)
{
    // Read superblock

    // Set device and mount

    // Increment mounts
    disk_mount(disk);

    // Copy metadata

    // Allocate free block bitmap

    return false;
}

// Create inode ----------------------------------------------------------------

ssize_t fs_create(FileSystem *fs)
{
    // Locate free inode in inode table

    // Record inode if found

    return -1;
}

// Optional: the following two helper functions may be useful. 

// bool find_inode(FileSystem *fs, size_t inumber, Inode *inode)
// {
//     return true;
// }

// bool store_inode(FileSystem *fs, size_t inumber, Inode *inode)
// {
//     return true;
// }

// Remove inode ----------------------------------------------------------------

bool fs_remove(FileSystem *fs, size_t inumber)
{
    // Load inode information

    // Free direct blocks

    // Free indirect blocks

    // Clear inode in inode table

    return false;
}

// Inode stat ------------------------------------------------------------------

ssize_t fs_stat(FileSystem *fs, size_t inumber)
{
    // Load inode information
    return 0;
}

// Read from inode -------------------------------------------------------------

ssize_t fs_read(FileSystem *fs, size_t inumber, char *data, size_t length, size_t offset)
{
    // Load inode information

    // Adjust length

    // Read block and copy to data
    
    return 0;
}

// Optional: the following helper function may be useful. 

// ssize_t fs_allocate_block(FileSystem *fs)
// {
//     return -1;
// }

// Write to inode --------------------------------------------------------------

ssize_t fs_write(FileSystem *fs, size_t inumber, char *data, size_t length, size_t offset)
{
    // Load inode
    
    // Write block and copy to data

    return 0;
}
