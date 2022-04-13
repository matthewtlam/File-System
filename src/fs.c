#include "disk.h"
#include "fs.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#define min(a,b) (((a) < (b)) ? (a) : (b))
#define max(a,b) (((a) > (b)) ? (a) : (b))

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

    // Create new superblock
    Block block;
    memset(&block, 0, sizeof(Block));

    block.Super.MagicNumber = MAGIC_NUMBER;
    block.Super.Blocks = (uint32_t)disk_size(disk);
    // Set 10% of blocks for inodes
    block.Super.InodeBlocks = (uint32_t)ceil(block.Super.Blocks / 10.0);
    block.Super.Inodes = block.Super.InodeBlocks * INODES_PER_BLOCK;

    // Writes to Superblock 
    disk_write(disk, 0, block.Data);

    Block inodeBlock;
    for(int j = 0; j < INODES_PER_BLOCK; j++){
        inodeBlock.Inodes[j].Valid = false;
        inodeBlock.Inodes[j].Size = 0;

        // Clears Direct Pointers
        for(int k = 0; k < POINTERS_PER_INODE; k++)   
            inodeBlock.Inodes[j].Direct[k] = 0;

        // Clears Indirect Pointer
        inodeBlock.Inodes[j].Indirect = 0;
    }

    // Clears inode Table
    for (int i = 1; i <= block.Super.InodeBlocks; i++) {
        disk_write(disk, i, inodeBlock.Data);
    }

    // Free all other blocks on disk
    Block dataBlock;
    memset(dataBlock.Data, 0, BLOCK_SIZE);
    for(uint32_t i = (block.Super.InodeBlocks) + 1; i < block.Super.Blocks; i++){
        disk_write(disk, i, dataBlock.Data);
    }

    return true;
}

// FileSystem constructor 
FileSystem *new_fs() {
    FileSystem *fs = malloc(sizeof(FileSystem));
    fs->bitmap = NULL;
    fs->inodeTracker = NULL;
    fs->disk = NULL;
    return fs;
}

// FileSystem destructor 
void free_fs(FileSystem *fs) {
    if (fs->inodeTracker != NULL) {
        free(fs->inodeTracker);
    }
    if (fs->bitmap != NULL) {
        free(fs->bitmap);
    }
    free(fs);
}

// Mount file system -----------------------------------------------------------

bool fs_mount(FileSystem *fs, Disk *disk) {
    // Already mounted, so it fails
    if (disk_mounted(disk)) { 
        return false;
    }
    
    // Read Superblock
    Block block;
    disk_read(disk, 0, block.Data);
    uint32_t nInodeBlocks = block.Super.InodeBlocks;

    if (block.Super.MagicNumber != MAGIC_NUMBER || nInodeBlocks != ceil((block.Super.Blocks) / 10.0) || block.Super.Inodes != nInodeBlocks * INODES_PER_BLOCK) {
        return false;
    }

    // Set device and mount
    fs->disk = disk;

    // Increment mounts
    disk_mount(disk);

    fs->metadata = block.Super;

    // Allocate inode tracker
    fs->bitmap = calloc(fs->metadata.Blocks, sizeof(fs->metadata.Blocks));
    fs->inodeTracker = calloc(fs->metadata.InodeBlocks, sizeof(fs->metadata.InodeBlocks));

    fs->bitmap[0] = true;

    // Reading inode blocks
    for (int i = 1; i <= fs->metadata.InodeBlocks; i++) {
        Block inodeBlock;
        disk_read(disk, i, inodeBlock.Data);

        // Set bit map for inode blocks
        for (int j = 0; j < INODES_PER_BLOCK; j++) {
            if (inodeBlock.Inodes[j].Valid) {
                fs->bitmap[i] = true;
                fs->inodeTracker[i-1]++;

                // Set bitmap for direct pointers
                for (int k = 0; k < POINTERS_PER_INODE; k++) {
                    uint32_t inodeDirVal = inodeBlock.Inodes[j].Direct[k];
                    if (inodeDirVal && inodeDirVal < fs->metadata.Blocks) {
                        fs->bitmap[inodeDirVal] = true;
                    }
                    else if (inodeDirVal) {
                        return false;
                    }
                }

                // Set bitmap for indirect pointers
                uint32_t inodeIndirVal = inodeBlock.Inodes[j].Indirect;
                if (inodeIndirVal && inodeIndirVal < fs->metadata.Blocks) {

                    fs->bitmap[inodeIndirVal] = true;
                    Block inDirBlock;
                    disk_read(fs->disk, inodeIndirVal, inDirBlock.Data);
                    for (int k = 0; k < POINTERS_PER_BLOCK; k++) {
                        if (inDirBlock.Pointers[k] < fs->metadata.Blocks) {
                            fs->bitmap[inDirBlock.Pointers[k]] = true;
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
        return -1;
    }

    Block block;

    // Locate free inode in inode table
    for (int i = 1; i <= fs->metadata.InodeBlocks; i++) {
        // Read from disk
        if (fs->inodeTracker[i-1] != INODES_PER_BLOCK) {
            disk_read(fs->disk, i, block.Data);
        }
        else {
            continue;
        }

        // Find first empty inode
        for (int j = 0; j < INODES_PER_BLOCK; j++) {

            // Record inode if found
            if (!block.Inodes[j].Valid) {

                fs->bitmap[i] = true;
                fs->inodeTracker[i-1]++;

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

bool find_inode(FileSystem *fs, size_t inumber, Inode *inode) {
    
    if (inumber < 0 || inumber > fs->metadata.Inodes || !fs->inodeTracker[inumber / INODES_PER_BLOCK]) {
        return false;
    }

    Block block;
    disk_read(fs->disk, inumber / INODES_PER_BLOCK + 1, block.Data);
    if (block.Inodes[inumber % INODES_PER_BLOCK].Valid) {
        *inode = block.Inodes[inumber % INODES_PER_BLOCK];
        return true;
    }

    return false;
}

bool store_inode(FileSystem *fs, size_t inumber, Inode *inode) {

    if (inumber < 0 || inumber > fs->metadata.Inodes) {
        return false;
    }

    // store the node into the block
    Block block;
    disk_read(fs->disk, (int)(inumber / INODES_PER_BLOCK) + 1, block.Data);
    block.Inodes[inumber % INODES_PER_BLOCK] = *inode; 
    disk_write(fs->disk, (int)(inumber / INODES_PER_BLOCK) + 1, block.Data);
    return true;
}

// Remove inode ----------------------------------------------------------------

bool fs_remove(FileSystem *fs, size_t inumber) {
    
    // Load inode information
    Inode inode;

    if (!disk_mounted(fs->disk) || !find_inode(fs, inumber, &inode)) {
        return false;
    }

    inode.Valid = false;
    inode.Size = 0;

    // Set the free blocks array appropriately
    if (!(--fs->inodeTracker[inumber / INODES_PER_BLOCK])) {
        fs->bitmap[inumber / INODES_PER_BLOCK + 1] = false;
    }

    // Free direct blocks
    for (int i = 0; i < POINTERS_PER_INODE; i++) {
        fs->bitmap[inode.Direct[i]] = false;
        inode.Direct[i] = 0;
    }

    // Free indirect blocks
    if (inode.Indirect) {
        fs->bitmap[inode.Indirect] = false;
        Block inDirBlock;
        disk_read(fs->disk, inode.Indirect, inDirBlock.Data);
        inode.Indirect = 0;

        for (int i = 0; i < POINTERS_PER_BLOCK; i++) {
            uint32_t inDirBlockPtr = inDirBlock.Pointers[i];
            if (inDirBlockPtr) {
                fs->bitmap[inDirBlockPtr] = false;
            }
        }
    }

    Block block;
    disk_read(fs->disk, inumber / INODES_PER_BLOCK + 1, block.Data);
    block.Inodes[inumber % INODES_PER_BLOCK] = inode;
    disk_write(fs->disk, inumber / INODES_PER_BLOCK + 1, block.Data);

    return true;
}

// Inode stat ------------------------------------------------------------------

ssize_t fs_stat(FileSystem *fs, size_t inumber) {

    if (!disk_mounted(fs->disk)) {
        return -1;
    }

    // Load inode information
    Inode inode;
    
    if (find_inode(fs, inumber, &inode)) {
        return inode.Size;
    }

    return -1;
}

// Read from inode -------------------------------------------------------------

ssize_t fs_read(FileSystem *fs, size_t inumber, char *data, int length, size_t offset) {

    if (!disk_mounted(fs->disk)) {
        return -1;
    }

    Inode inode;    

    // Loads the inode
    if(!find_inode(fs, inumber, &inode)) {
        return -1;
    }

    // Get size of inode
    int inodeSize = fs_stat(fs, inumber);
    
    // No data can be read when offset too large
    if((int)offset >= inodeSize) {
        return 0;
    }
    // Adjust length accordingly when exceed inodeSize
    else if(length + (int)offset > inodeSize) {
        length = inodeSize - offset;
    }
    
    // ptr used to traverse
    char *ptr = data;     
    int leftToRead = length;

    // Offset is within direct pointers 
    if(offset < POINTERS_PER_INODE * BLOCK_SIZE) {
        // Calculate starting node for reading
        uint32_t dirNode = offset / BLOCK_SIZE;
        offset %= BLOCK_SIZE;

        // Direct node is valid 
        if(inode.Direct[dirNode]) { 
            disk_read(fs->disk, inode.Direct[dirNode++], ptr);
            data += offset;
            ptr += BLOCK_SIZE;
            length -= (BLOCK_SIZE - offset);

            // Read block and copy to data
            // Read the direct blocks 
            while(length > 0 && dirNode < POINTERS_PER_INODE && inode.Direct[dirNode]) {
                disk_read(fs->disk, inode.Direct[dirNode++], ptr);
                ptr += BLOCK_SIZE;
                length -= (BLOCK_SIZE - offset);
            }

            // No more room for data to be read 
            if(length <= 0) {
                return leftToRead;
            }
            else {
                // Read indirect nodes if dirNode has all been read
                if(dirNode == POINTERS_PER_INODE && inode.Indirect) {
                    Block indirect;
                    disk_read(fs->disk, inode.Indirect, indirect.Data);

                    // Read the indirect nodes 
                    for(uint32_t i = 0; i < POINTERS_PER_BLOCK; i++) {
                        if(indirect.Pointers[i] && length > 0) {
                            disk_read(fs->disk, indirect.Pointers[i], ptr);
                            ptr += BLOCK_SIZE;
                            length -= (BLOCK_SIZE - offset);
                        }
                        else {
                            break;
                        }
                    }

                    // No more room for data to be read 
                    if(length <= 0) {
                        return leftToRead;
                    }
                }
            }
        }
    }
    // Indirect node is valid and offset starts in the indirect block
    else if (inode.Indirect){
        // Calculate starting node for reading
        offset -= POINTERS_PER_INODE * BLOCK_SIZE;
        uint32_t indirNode = offset / BLOCK_SIZE;
        offset %= BLOCK_SIZE;

        Block indirect;
        disk_read(fs->disk, inode.Indirect, indirect.Data);

        if(indirect.Pointers[indirNode] && length > 0) {
            disk_read(fs->disk, indirect.Pointers[indirNode++], ptr);
            data += offset;
            ptr += BLOCK_SIZE;
            length -= BLOCK_SIZE - offset;
        }

        // Read the indirect nodes 
        for(uint32_t i = indirNode; i < POINTERS_PER_BLOCK; i++) {
            if(indirect.Pointers[i] && length > 0) {
                disk_read(fs->disk, indirect.Pointers[indirNode++], ptr);
                ptr += BLOCK_SIZE;
                length -= BLOCK_SIZE - offset;
            }
            else break;
        }
        
        // No more room for data to be read 
        if(length <= 0) {
            return leftToRead;
        }
    }
    
    // Inode has no stored data or indirect node is invalid
    return 0;
}

ssize_t fs_allocate_block(FileSystem *fs) {

    // Iterate through the free bit map and allocate the first free block
    for(int i = fs->metadata.InodeBlocks + 1; i < fs->metadata.Blocks; i++) {
        if(fs->bitmap[i] == 0) {
            fs->bitmap[i] = true;
            return i;
        }
    }
    return 0;
}

// Write to inode --------------------------------------------------------------
void read_buffer(FileSystem *fs, int offset, int *read, int length, char *data, uint32_t blocknum) {
    
    // Allocate memory that acts as buffer for reading from disk
    char* ptr = (char *)calloc(BLOCK_SIZE, sizeof(char));

    // Read data into ptr
    for(int i = offset; i < (int)BLOCK_SIZE && *read < length; i++) {
        ptr[i] = data[*read];
        *read = *read + 1;
    }
    disk_write(fs->disk, blocknum, ptr);

    // Free memory 
    free(ptr);
}

ssize_t fs_write(FileSystem *fs, size_t inumber, char *data, size_t length, size_t offset) {

    if (!disk_mounted(fs->disk)) {
        return -1;
    }

    Inode inode;
    Block indirect;
    int read = 0;
    int ogOffset = offset;

    // Insufficient size
    if(length + offset > (POINTERS_PER_BLOCK + POINTERS_PER_INODE) * BLOCK_SIZE) {
        return -1;
    }

    // Load and validate inode and allocate if doesn't exist
    if(!find_inode(fs, inumber, &inode)) {
        inode.Valid = true;
        inode.Size = length + offset;
        for(uint32_t ii = 0; ii < POINTERS_PER_INODE; ii++) {
            inode.Direct[ii] = 0;
        }
        inode.Indirect = 0;
        fs->inodeTracker[inumber / INODES_PER_BLOCK]++;
        fs->bitmap[inumber / INODES_PER_BLOCK + 1] = true;
    }
    // Set node size
    else {
        inode.Size = max((int)inode.Size, length + (int)offset);
    }

    // Offset is within direct pointers 
    if(offset < POINTERS_PER_INODE * BLOCK_SIZE) {
        // Calculate starting node for writing 
        int dirNode = offset / BLOCK_SIZE;
        offset %= BLOCK_SIZE;

        // Allocates a block if one doesn't exist
        if (!inode.Direct[dirNode]) {
           inode.Direct[dirNode] = fs_allocate_block(fs);
           if (!inode.Direct[dirNode]) {
                inode.Size = read + ogOffset;
                store_inode(fs, inumber, &inode);
                return read;
           }
        }    

        // Read from data buffer      
        read_buffer(fs, offset, &read, length, data, inode.Direct[dirNode++]);

        // Done reading from data buffer
        if(read == length) {
            store_inode(fs, inumber, &inode);
            return length;
        }
        else {
            // Writing into direct pointers 
            for(int i = dirNode; i < (int)POINTERS_PER_INODE; i++) {
                // Allocates a block if one doesn't exist
                if (!inode.Direct[dirNode]) {
                    inode.Direct[dirNode] = fs_allocate_block(fs);
                    if (!inode.Direct[dirNode]) {
                            inode.Size = read + ogOffset;
                            store_inode(fs, inumber, &inode);
                            return read;
                    }
                }   
                // Read from data buffer 
                read_buffer(fs, 0, &read, length, data, inode.Direct[dirNode++]);

                // Done reading from data buffer
                if(read == length) {
                    store_inode(fs, inumber, &inode);
                    return length;
                }
            }

            // Indirect node is valid 
            if(inode.Indirect) {
                disk_read(fs->disk, inode.Indirect, indirect.Data);
            }
            else {
                // Allocates a block if one doesn't exist
                if (!inode.Indirect) {
                    inode.Indirect = fs_allocate_block(fs);
                    if (!inode.Indirect) {
                            inode.Size = read + ogOffset;
                            store_inode(fs, inumber, &inode);
                            return read;
                    }
                }   
                disk_read(fs->disk, inode.Indirect, indirect.Data);

                // Initialize the indirect pointers 
                for(int i = 0; i < (int)POINTERS_PER_BLOCK; i++) {
                    indirect.Pointers[i] = 0;
                }
            }
            
            // Write into indirect pointers 
            for(int j = 0; j < (int)POINTERS_PER_BLOCK; j++) {
               // Allocates a block if one doesn't exist
                if (!indirect.Pointers[j]) {
                    indirect.Pointers[j] = fs_allocate_block(fs);
                    if (!indirect.Pointers[j]) {
                            inode.Size = read + ogOffset;
                            disk_write(fs->disk, inode.Indirect, indirect.Data);
                            store_inode(fs, inumber, &inode);
                            return read;
                    }
                }  
                
                // Read from data buffer 
                read_buffer(fs, 0, &read, length, data, indirect.Pointers[j]);

                // Done reading from data buffer
                if(read == length) {
                    disk_write(fs->disk, inode.Indirect, indirect.Data);
                    store_inode(fs, inumber, &inode);
                    return length;
                }
            }

            // No more space 
            disk_write(fs->disk, inode.Indirect, indirect.Data);
            store_inode(fs, inumber, &inode);
            return read;
        }
    }
    // Offset begins in indirect blocks 
    else {
        // Calculate starting node for writing 
        offset -= BLOCK_SIZE * POINTERS_PER_INODE;
        int indirNode = offset / BLOCK_SIZE;
        offset %= BLOCK_SIZE;

        // Indirect node is valid 
        if(inode.Indirect) {
            disk_read(fs->disk, inode.Indirect, indirect.Data);
        }
        else {
           // Allocates a block if one doesn't exist
            if (!inode.Indirect) {
                inode.Indirect = fs_allocate_block(fs);
                if (!inode.Indirect) {
                        inode.Size = read + ogOffset;
                        store_inode(fs, inumber, &inode);
                        return read;
                }
            }  
            
            disk_read(fs->disk, inode.Indirect, indirect.Data);

            // Initialize the indirect pointers 
            for(int i = 0; i < (int)POINTERS_PER_BLOCK; i++) {
                indirect.Pointers[i] = 0;
            }
        }

        // Allocates a block if one doesn't exist
        if (!indirect.Pointers[indirNode]) {
            indirect.Pointers[indirNode] = fs_allocate_block(fs);
            if (!indirect.Pointers[indirNode]) {
                    inode.Size = read + ogOffset;
                    disk_write(fs->disk, inode.Indirect, indirect.Data);
                    store_inode(fs, inumber, &inode);
                    return read;
            }
        }  

        // Read from data buffer 
        read_buffer(fs, offset, &read, length, data, indirect.Pointers[indirNode++]);

        // Done reading from data buffer
        if(read == length) {
            disk_write(fs->disk, inode.Indirect, indirect.Data);
            store_inode(fs, inumber, &inode);
            return length;
        }
        // Write into indirect nodes 
        else {
            for(int j = indirNode; j < (int)POINTERS_PER_BLOCK; j++) {
                // Allocates a block if one doesn't exist
                if (!indirect.Pointers[j]) {
                    indirect.Pointers[j] = fs_allocate_block(fs);
                    if (!indirect.Pointers[j]) {
                        inode.Size = read + ogOffset;
                        disk_write(fs->disk, inode.Indirect, indirect.Data);
                        store_inode(fs, inumber, &inode);
                        return read;
                    }
                }  

                // Read from data buffer 
                read_buffer(fs, 0, &read, length, data, indirect.Pointers[j]);

                // Done reading from data buffer 
                if(read == length) {
                    disk_write(fs->disk, inode.Indirect, indirect.Data);
                    store_inode(fs, inumber, &inode);
                    return length;
                }
            }

            // No more space
            disk_write(fs->disk, inode.Indirect, indirect.Data);
            store_inode(fs, inumber, &inode);
            return read;
        }
    }

    // returns an error
    return -1;
}
