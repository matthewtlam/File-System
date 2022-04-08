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
                printf("    size: %u bytes\n", block.Inodes[j].Size);
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

bool fs_format(Disk *disk) {
    // Checks if disk is already mounted
    if (disk_mounted(disk)) { 
        // Already mounted, so it fails
        return false;
    }

     // Creates a new file system,
    /*
    FileSystem *fs = new_fs();
    */

    // Create new superblock
    Block block;
    memset(&block, 0, sizeof(Block));

    block.Super.MagicNumber = MAGIC_NUMBER;
    block.Super.Blocks = (uint32_t)disk_size(disk);
    // Set 10% of blocks for inodes
    block.Super.InodeBlocks = (uint32_t)ceil((block.Super.Blocks) / 10.0);
    block.Super.Inodes = block.Super.InodeBlocks * INODES_PER_BLOCK;

    /*
    // Clear individual inodes
    Block inodeBlock;
    for (int j = 0; j < INODES_PER_BLOCK; j++) {
        inodeBlock.Inodes[j].Valid = 0;
        inodeBlock.Inodes[j].Size = 0;

        // Clears all direct pointers
        for (int k = 0; k < POINTERS_PER_INODE; k++) {
                inodeBlock.Inodes[j].Direct[k] = 0;
        }

        // Clears the indirect pointer
        inodeBlock.Inodes[j].Indirect = 0;
    }
    */

    // Clears inode Table
    for (int i = 1; i <= block.Super.InodeBlocks; i++) {
        Block inodeBlock;
        memset(inodeBlock.Data, 0, BLOCK_SIZE);
        disk_write(disk, i, inodeBlock.Data);
    }

    // Writes to Superblock 
    disk_write(disk, 0, block.Data);

    return true;
}

// FileSystem constructor 
FileSystem *new_fs() {
    FileSystem *fs = malloc(sizeof(FileSystem));
    return fs;
}

// FileSystem destructor 
void free_fs(FileSystem *fs) {
    // FIXME: free resources and allocated memory in FileSystem

    free(fs->free_blocks);
    free(fs);
}

// Mount file system -----------------------------------------------------------

bool fs_mount(FileSystem *fs, Disk *disk) {
    if (disk_mounted(disk)) { 
        // Already mounted, so it fails
        return false;
    }
    
    // Read Superblock
    Block block;
    disk_read(disk, 0, block.Data);
    if (block.Super.MagicNumber != MAGIC_NUMBER || block.Super.InodeBlocks != ceil((block.Super.Blocks) / 10.0) || block.Super.Inodes != block.Super.InodeBlocks * INODES_PER_BLOCK) {
        return false;
    }

    // Set device and mount
    fs->disk = disk;

    // Increment mounts
    disk_mount(disk);

    // Copy metadata
    //fs->metadata = block.Super;

    // Allocate free block bitmap
    //fs->free_blocks.resize(block.Super.Blocks, 0);
    fs->free_blocks = malloc(block.Super.Blocks);
    for (int i = 0; i < block.Super.Blocks; i++) {
        fs->free_blocks[i] = false;
    }

    fs->free_blocks[0] = true;

    // Reading inode blocks
    for (int i = 1; i <= block.Super.InodeBlocks; i++) {
        Block inodeBlock;
        disk_read(disk, i, inodeBlock.Data);

        // Set bit map for inode blocks
        for (int j = 0; j < INODES_PER_BLOCK; j++) {
            if (inodeBlock.Inodes[j].Valid) {
                fs->free_blocks[i] = true;

                // Set bitmap for direct pointers
                for (int k = 0; k < POINTERS_PER_INODE; k++) {
                    if (inodeBlock.Inodes[j].Direct[k] && inodeBlock.Inodes[j].Direct[k] < block.Super.Blocks) {
                        fs->free_blocks[inodeBlock.Inodes[j].Direct[k]] = true;
                    }
                    else if (inodeBlock.Inodes[j].Direct[k]) {
                        return false;
                    }
                }

                // Set bitmap for indirect pointers
                if (inodeBlock.Inodes[j].Indirect && inodeBlock.Inodes[j].Indirect < block.Super.Blocks) {
                    fs->free_blocks[inodeBlock.Inodes[j].Indirect] = true;
                    Block inDirBlock;
                    disk_read(fs->disk, inodeBlock.Inodes[j].Indirect, inDirBlock.Data);
                    for (int k = 0; k < POINTERS_PER_BLOCK; k++) {
                        if (inDirBlock.Pointers[k] < block.Super.Blocks) {
                            fs->free_blocks[inDirBlock.Pointers[k]] = true;
                        }
                        else {
                            return false;
                        }
                    }
                }
                else if (inodeBlock.Inodes[j].Indirect) {
                    return false;
                }
            }
        }
    }

    return true;
}

// Create inode ----------------------------------------------------------------

ssize_t fs_create(FileSystem *fs) {

    if (!disk_mounted(fs->disk)) {
        return false;
    }

    // Read from Superblock
    Block block;
    disk_write(fs->disk, 0, block.Data);

    // Locate free inode in inode table
    for (int i = 1; i <= block.Super.InodeBlocks; i++) {
        // Read from disk
        disk_read(fs->disk, i, block.Data);

        // Find first empty inode
        for (int j = 0; j < INODES_PER_BLOCK; j++) {

            // Record inode if found
            if (!block.Inodes[j].Valid) {

                fs->free_blocks[i] = true;
                block.Inodes[j].Valid = true;
                block.Inodes[j].Size = 0;
                block.Inodes[j].Indirect = 0;

                for (int k = 0; k < POINTERS_PER_INODE; k++) {
                    block.Inodes[j].Direct[k] = 0;
                }
                
                disk_write(fs->disk, i, block.Data);

                return (i-1) * INODES_PER_BLOCK + j;
            }
        }
    }

    return -1;
}

// Optional: the following two helper functions may be useful. 

bool find_inode(FileSystem *fs, size_t inumber, Inode *inode) {
    
    if (!disk_mounted(fs->disk)) {
        return false;
    }

    // Read from Superblock
    Block block;
    disk_write(fs->disk, 0, block.Data);

    if (inumber < 1 || inumber > block.Super.Inodes) {
        return false;
    }

    disk_read(fs->disk, inumber / INODES_PER_BLOCK + 1, block.Data);
    if (block.Inodes[inumber % INODES_PER_BLOCK].Valid) {
        *inode = block.Inodes[inumber % INODES_PER_BLOCK];
        return true;
    }
    
    return false;
}

// bool store_inode(FileSystem *fs, size_t inumber, Inode *inode)
// {
//     return true;
// }

// Remove inode ----------------------------------------------------------------

bool fs_remove(FileSystem *fs, size_t inumber) {

    if (!disk_mounted(fs->disk)) {
        return false;
    }

    // Load inode information
    Inode * inode = NULL;
    if (!find_inode(fs, inumber, inode)) {
        return false;
    }

    // Set the free blocks array appropriately
    // fs->free_blocks[inumber / INODES_PER_BLOCK + 1] = false;

    // Free direct blocks
    for (int i = 0; i < POINTERS_PER_INODE; i++) {
        fs->free_blocks[inode->Direct[i]] = false;
    }

    // Free indirect blocks
    if (inode->Indirect) {
        fs->free_blocks[inode->Indirect] = false;
        Block inDirBlock;
        disk_read(fs->disk, inode->Indirect, inDirBlock.Data);

        for (int i = 0; i < POINTERS_PER_BLOCK; i++) {
            uint32_t inDirBlockPtr = inDirBlock.Pointers[i];
            if (inDirBlockPtr) {
                fs->free_blocks[inDirBlockPtr] = false;
            }
        }
    }
        
    // Clear inode in inode table
    for (int i = 0; i < POINTERS_PER_INODE; i++) {
        inode->Direct[i] = 0;
    }

    if (inode->Indirect) {
        inode->Indirect = 0;
    }

    Block block;
    disk_read(fs->disk, inumber / INODES_PER_BLOCK + 1, block.Data);
    block.Inodes[inumber & INODES_PER_BLOCK] = *inode;
    disk_write(fs->disk, inumber / INODES_PER_BLOCK + 1, block.Data);

    return true;
}

// Inode stat ------------------------------------------------------------------

ssize_t fs_stat(FileSystem *fs, size_t inumber) {

    if (!disk_mounted(fs->disk)) {
        return false;
    }

    // Load inode information
    Inode * inode = NULL;
    
    if (find_inode(fs, inumber, inode)) {
        return inode->Size;
    }

    return -1;
}

// Read from inode -------------------------------------------------------------

ssize_t fs_read(FileSystem *fs, size_t inumber, char *data, size_t length, size_t offset) {

    if (!disk_mounted(fs->disk)) {
        return false;
    }


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

ssize_t fs_write(FileSystem *fs, size_t inumber, char *data, size_t length, size_t offset) {
    // Load inode
    
    // Write block and copy to data

    return 0;
}
