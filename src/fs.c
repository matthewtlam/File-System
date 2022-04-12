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
    // memset(dataBlock.Data, 0, BLOCK_SIZE);
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
    fs->disk = NULL;
    return fs;
}

// FileSystem destructor 
void free_fs(FileSystem *fs) {
    // FIXME: free resources and allocated memory in FileSystem

    free(fs->bitmap);
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

    fs->metadata = block.Super;

    // Allocate inode tracker
    fs->inodeTracker = malloc(block.Super.InodeBlocks);
    for (int i = 0; i < block.Super.InodeBlocks; i++) {
        fs->inodeTracker[i] = 0;
    }

    // Allocate free block bitmap
    fs->bitmap = malloc(block.Super.Blocks);
    for (int i = 0; i < block.Super.Blocks; i++) {
        fs->bitmap[i] = false;
    }

    fs->bitmap[0] = true;

    // Reading inode blocks
    for (int i = 1; i <= block.Super.InodeBlocks; i++) {
        Block inodeBlock;
        disk_read(disk, i, inodeBlock.Data);

        // Set bit map for inode blocks
        for (int j = 0; j < INODES_PER_BLOCK; j++) {
            if (inodeBlock.Inodes[j].Valid) {
                fs->bitmap[i] = true;
                fs->inodeTracker[i-1]++;

                // Set bitmap for direct pointers
                for (int k = 0; k < POINTERS_PER_INODE; k++) {
                    if (inodeBlock.Inodes[j].Direct[k] && inodeBlock.Inodes[j].Direct[k] < block.Super.Blocks) {
                        fs->bitmap[inodeBlock.Inodes[j].Direct[k]] = true;
                    }
                    else if (inodeBlock.Inodes[j].Direct[k]) {
                        return false;
                    }
                }

                // Set bitmap for indirect pointers
                if (inodeBlock.Inodes[j].Indirect && inodeBlock.Inodes[j].Indirect < block.Super.Blocks) {

                    fs->bitmap[inodeBlock.Inodes[j].Indirect] = true;
                    Block inDirBlock;
                    disk_read(fs->disk, inodeBlock.Inodes[j].Indirect, inDirBlock.Data);
                    for (int k = 0; k < POINTERS_PER_BLOCK; k++) {
                        if (inDirBlock.Pointers[k] < block.Super.Blocks) {
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
                //return i * INODES_PER_BLOCK + j;
            }
        }
    }

    return -1;
}

bool find_inode(FileSystem *fs, size_t inumber, Inode *inode) {

    if (!disk_mounted(fs->disk)) {
        return false;
    }

    // Read from Superblock
    Block block;
    disk_write(fs->disk, 0, block.Data);

    if ((inumber < 0) || (inumber > fs->metadata.Inodes)) {
        return false;
    }
    
    if (!fs->inodeTracker[inumber / INODES_PER_BLOCK]) {
        return false;
    }

    disk_read(fs->disk, (int)(inumber / INODES_PER_BLOCK) + 1, block.Data);
    if (block.Inodes[(inumber % INODES_PER_BLOCK)].Valid) {
        printf("Condition1.5 \n");

        *inode = block.Inodes[inumber % INODES_PER_BLOCK];
        printf("Condition2\n");
        return true;
    }

    printf("Condition3\n");
    return false;
}

bool store_inode(FileSystem *fs, size_t inumber, Inode *inode) {
    // store the node into the block
    Block block;
    disk_read(fs->disk, (int)(inumber / INODES_PER_BLOCK) + 1, block.Data);
    block.Inodes[inumber % INODES_PER_BLOCK] = *inode; 
    disk_write(fs->disk, (int)(inumber / INODES_PER_BLOCK) + 1, block.Data);
    return true;
}

// Remove inode ----------------------------------------------------------------

bool fs_remove(FileSystem *fs, size_t inumber) {
    
    if (!disk_mounted(fs->disk)) {
        return false;
    }

    // Load inode information
    Inode * inode = NULL;

    if (!find_inode(fs, inumber+1, inode)) {
        return false;
    }

    inode->Valid = false;
    inode->Size = 0;

    // Set the free blocks array appropriately
    /*
    Block block;
    disk_read(fs->disk, (int)(inumber / INODES_PER_BLOCK) + 1, block.Data);
    int validInodeCounter = 0;

    for(int i = 0; i < INODES_PER_BLOCK; i++) {
        if(block.Inodes[i].Valid) {
            validInodeCounter++;
        }
    }

    if (validInodeCounter == 0) {
        fs->bitmap[inumber / INODES_PER_BLOCK + 1] = false;
    }
    */
    if (!(--fs->inodeTracker[inumber / INODES_PER_BLOCK])) {
        fs->bitmap[inumber / INODES_PER_BLOCK + 1] = false;
    }

    // Free direct blocks
    for (int i = 0; i < POINTERS_PER_INODE; i++) {
        fs->bitmap[inode->Direct[i]] = false;
    }

    // Free indirect blocks
    if (inode->Indirect) {
        fs->bitmap[inode->Indirect] = false;
        Block inDirBlock;
        disk_read(fs->disk, inode->Indirect, inDirBlock.Data);

        for (int i = 0; i < POINTERS_PER_BLOCK; i++) {
            uint32_t inDirBlockPtr = inDirBlock.Pointers[i];
            if (inDirBlockPtr) {
                fs->bitmap[inDirBlockPtr] = false;
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
        return -1;
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
        return -1;
    }

    // Load inode information

    int size_inode = fs_stat(fs, inumber);
    
    // if offset is greater than size of inode, then no data can be read 
    // if length + offset exceeds the size of inode, adjust length accordingly
    if((int)offset >= size_inode) {
        return 0;
    }
    else if(length + (int)offset > size_inode) {
        length = size_inode - offset;
    }

    Inode node;    

    // data is head; ptr is tail
    char *ptr = data;     
    int to_read = length;

    // load inode; if invalid, return error 
    if(!find_inode(fs, inumber, &node)) {
        return -1;
    }
    // Adjust length

    // Read block and copy to data

    // the offset is within direct pointers 
    if(offset < POINTERS_PER_INODE * BLOCK_SIZE) {
        // calculate the node to start reading from
        uint32_t direct_node = offset / BLOCK_SIZE;
        offset %= BLOCK_SIZE;

        // check if the direct node is valid 
        if(node.Direct[direct_node]) {
            disk_read(fs->disk, node.Direct[direct_node++], ptr);
            data += offset;
            ptr += BLOCK_SIZE;
            length -= BLOCK_SIZE - offset;

            // read the direct blocks 
            while(length > 0 && direct_node < POINTERS_PER_INODE && node.Direct[direct_node]) {
                disk_read(fs->disk, node.Direct[direct_node++], ptr);
                ptr += BLOCK_SIZE;
                length -= BLOCK_SIZE - offset;
            }

            // if length <= 0, then enough data has been read 
            if(length <= 0) {
                return to_read;
            }
            else {
                // more data is to be read

                // check if all the direct nodes have been read completely 
                //  and if the indirect pointer is valid
                if(direct_node == POINTERS_PER_INODE && node.Indirect) {
                    Block indirect;
                    disk_read(fs->disk, node.Indirect, indirect.Data);

                    // read the indirect nodes 
                    for(uint32_t i = 0; i < POINTERS_PER_BLOCK; i++) {
                        if(indirect.Pointers[i] && length > 0) {
                            disk_read(fs->disk, indirect.Pointers[i], ptr);
                            ptr += BLOCK_SIZE;
                            length -= BLOCK_SIZE - offset;
                        }
                        else {
                            break;
                        }
                    }

                    // if length <= 0, then enough data has been read
                    if(length <= 0) {
                        return to_read;
                    }
                }
            }
        }
        else {
            // inode has no stored data
            return 0;
        }
    }
    else if (node.Indirect){
        // offset begins in the indirect block
        // check if the indirect node is valid 

        //change offset accordingly and find the indirect node to start reading from 
        offset -= POINTERS_PER_INODE * BLOCK_SIZE;
        uint32_t indirect_node = offset / BLOCK_SIZE;
        offset %= BLOCK_SIZE;

        Block indirect;
        disk_read(fs->disk, node.Indirect, indirect.Data);

        if(indirect.Pointers[indirect_node] && length > 0) {
            disk_read(fs->disk, indirect.Pointers[indirect_node++], ptr);
            data += offset;
            ptr += BLOCK_SIZE;
            length -= BLOCK_SIZE - offset;
        }

        // iterate through the indirect nodes 
        for(uint32_t i = indirect_node; i < POINTERS_PER_BLOCK; i++) {
            if(indirect.Pointers[i] && length > 0) {
                disk_read(fs->disk, indirect.Pointers[indirect_node++], ptr);
                ptr += BLOCK_SIZE;
                length -= BLOCK_SIZE - offset;
            }
            else break;
        }
        
        // if length <= 0, then enough data has been read
        if(length <= 0) {
            return to_read;
        }
    }
    
    // the indirect node is invalid
    return 0;
}

ssize_t fs_allocate_block(FileSystem *fs) {
    if (!disk_mounted(fs->disk)) {
        return 0;
    }

    // Read from Superblock
    Block block;
    disk_write(fs->disk, 0, block.Data);

    // iterate through the free bit map and allocate the first free block
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
    
    // allocate memory to ptr which acts as buffer for reading from disk
    char* ptr = (char *)calloc(BLOCK_SIZE, sizeof(char));

    // read data into ptr and change pointers accordingly
    for(int i = offset; i < (int)BLOCK_SIZE && *read < length; i++) {
        ptr[i] = data[*read];
        *read = *read + 1;
    }
    disk_write(fs->disk, blocknum, ptr);

    // free the allocated memory 
    free(ptr);
}

ssize_t fs_write(FileSystem *fs, size_t inumber, char *data, size_t length, size_t offset) {
    // Load inode
    
    // Write block and copy to data

    if (!disk_mounted(fs->disk)) {
        return -1;
    }

    Inode node;
    Block indirect;
    int read = 0;
    int orig_offset = offset;

    // insufficient size
    if(length + offset > (POINTERS_PER_BLOCK + POINTERS_PER_INODE) * BLOCK_SIZE) {
        return -1;
    }

    // if the inode is invalid, allocate inode. need not write to disk right now; will be taken care of in write_ret()
     
    if(!find_inode(fs, inumber, &node)) {
        node.Valid = true;
        node.Size = length + offset;
        for(uint32_t ii = 0; ii < POINTERS_PER_INODE; ii++) {
            node.Direct[ii] = 0;
        }
        node.Indirect = 0;
        fs->inodeTracker[inumber / INODES_PER_BLOCK]++;
        fs->bitmap[inumber / INODES_PER_BLOCK + 1] = true;
    }
    else {
        // set size of the node
        node.Size = max((int)node.Size, length + (int)offset);
        //node.Size = max((int)node.Size, length + (int)offset);
    }

    // check if the offset is within direct pointers 
    if(offset < POINTERS_PER_INODE * BLOCK_SIZE) {
        // find the first node to start writing at and change offset accordingly 
        int direct_node = offset / BLOCK_SIZE;
        offset %= BLOCK_SIZE;

        // check if the node is valid; if invalid; allocates a block and if no block is available, returns false 


        /*
        if(!check_allocation(&node, read, orig_offset, node.Direct[direct_node], false, indirect)) { 
            store_inode(fs, inumber, &node);
            return read;
        }
        */

       if (!node.Direct[direct_node]) {
           node.Direct[direct_node] = fs_allocate_block(fs);
           if (!node.Direct[direct_node]) {
                node.Size = read + orig_offset;
                store_inode(fs, inumber, &node);
                return read;
           }
       }      
        // read from data buffer      
        read_buffer(fs, offset, &read, length, data, node.Direct[direct_node++]);

        // enough data has been read from data buffer
        if(read == length) {
            store_inode(fs, inumber, &node);
            return length;
        }
        // store in direct pointers till either one of the two things happen:
        // 1. all the data is stored in the direct pointers
        // 2. the data is stored in indirect pointers
        
        else {
            // start writing into direct nodes 
            for(int i = direct_node; i < (int)POINTERS_PER_INODE; i++) {
                // check if the node is valid; if invalid; allocates a block and if no block is available, returns false
                /*
                if(!check_allocation(&node, read, orig_offset, node.Direct[direct_node], false, indirect)) { 
                    store_inode(fs, inumber, &node);
                    return read;
                }
                */
                if (!node.Direct[direct_node]) {
                    node.Direct[direct_node] = fs_allocate_block(fs);
                    if (!node.Direct[direct_node]) {
                            node.Size = read + orig_offset;
                            store_inode(fs, inumber, &node);
                            return read;
                    }
                }   
                read_buffer(fs, 0, &read, length, data, node.Direct[direct_node++]);

                // enough data has been read from data buffer
                if(read == length) {
                    store_inode(fs, inumber, &node);
                    return length;
                }
            }

            // check if the indirect node is valid 
            if(node.Indirect) {
                disk_read(fs->disk, node.Indirect, indirect.Data);
            }
            else {
                // check if the node is valid; if invalid; allocates a block and if no block is available, returns false 
                /*
                if(!check_allocation(&node, read, orig_offset, node.Indirect, false, indirect)) { 
                    store_inode(fs, inumber, &node);
                    return read;
                }
                */
                if (!node.Indirect) {
                    node.Indirect = fs_allocate_block(fs);
                    if (!node.Indirect) {
                            node.Size = read + orig_offset;
                            store_inode(fs, inumber, &node);
                            return read;
                    }
                }   
                disk_read(fs->disk, node.Indirect, indirect.Data);

                // initialise the indirect nodes 
                for(int i = 0; i < (int)POINTERS_PER_BLOCK; i++) {
                    indirect.Pointers[i] = 0;
                }
            }
            
            // write into indirect nodes 
            for(int j = 0; j < (int)POINTERS_PER_BLOCK; j++) {
                // check if the node is valid; if invalid; allocates a block and if no block is available, returns false 
                /*
                if(!check_allocation(fs, &node, read, orig_offset, indirect.Pointers[j], true, indirect)) { 
                    store_inode(fs, inumber, &node);
                    return read;
                }
                */
                if (!indirect.Pointers[j]) {
                    indirect.Pointers[j] = fs_allocate_block(fs);
                    if (!indirect.Pointers[j]) {
                            node.Size = read + orig_offset;
                            disk_write(fs->disk, node.Indirect, indirect.Data);
                            store_inode(fs, inumber, &node);
                            return read;
                    }
                }  
                read_buffer(fs, 0, &read, length, data, indirect.Pointers[j]);

                // enough data has been read from data buffer
                if(read == length) {
                    disk_write(fs->disk, node.Indirect, indirect.Data);
                    store_inode(fs, inumber, &node);
                    return length;
                }
            }

            // space exhausted 
            disk_write(fs->disk, node.Indirect, indirect.Data);
            store_inode(fs, inumber, &node);
            return read;
        }
    }
    // offset begins in indirect blocks 
    else {
        // find the first indirect node to write into and change offset accordingly 
        offset -= BLOCK_SIZE * POINTERS_PER_INODE;
        int indirect_node = offset / BLOCK_SIZE;
        offset %= BLOCK_SIZE;

        // check if the indirect node is valid 
        if(node.Indirect) {
            disk_read(fs->disk, node.Indirect, indirect.Data);
        }
        else {
            // check if the node is valid; if invalid; allocates a block and if no block is available, returns false 
            /*
            if(!check_allocation(fs, &node, read, orig_offset, &node.Indirect, false, indirect)) { 
                store_inode(fs, inumber, &node);
                return read;
            }
            */
            if (!node.Indirect) {
                node.Indirect = fs_allocate_block(fs);
                if (!node.Indirect) {
                        node.Size = read + orig_offset;
                        store_inode(fs, inumber, &node);
                        return read;
                }
            }  
            
            disk_read(fs->disk, node.Indirect, indirect.Data);

            // initialise the indirect nodes 
            for(int i = 0; i < (int)POINTERS_PER_BLOCK; i++) {
                indirect.Pointers[i] = 0;
            }
        }

        // check if the node is valid; if invalid; allocates a block and if no block is available, returns false
        /*
        if(!check_allocation(fs, &node, read, orig_offset, indirect.Pointers[indirect_node], true, indirect)) { 
            store_inode(fs, inumber, &node);
            return read;
        }
        */
        if (!indirect.Pointers[indirect_node]) {
            indirect.Pointers[indirect_node] = fs_allocate_block(fs);
            if (!indirect.Pointers[indirect_node]) {
                    node.Size = read + orig_offset;
                    disk_write(fs->disk, indirect.Pointers[indirect_node], indirect.Data);
                    store_inode(fs, inumber, &node);
                    return read;
            }
        }  
        read_buffer(fs, offset, &read, length, data, indirect.Pointers[indirect_node++]);

        // enough data has been read from data buffer 
        if(read == length) {
            disk_write(fs->disk, node.Indirect, indirect.Data);
            store_inode(fs, inumber, &node);
            return length;
        }
        // write into indirect nodes 
        else {
            for(int j = indirect_node; j < (int)POINTERS_PER_BLOCK; j++) {
                // check if the node is valid; if invalid; allocates a block and if no block is available, returns false 
                /*
                if(!check_allocation(fs, &node, read, orig_offset, indirect.Pointers[j], true, indirect)) { 
                    store_inode(fs, inumber, &node);
                    return read;
                }
                */
                if (!indirect.Pointers[j]) {
                    indirect.Pointers[j] = fs_allocate_block(fs);
                    if (!indirect.Pointers[j]) {
                        node.Size = read + orig_offset;
                        disk_write(fs->disk, indirect.Pointers[j], indirect.Data);
                        store_inode(fs, inumber, &node);
                        return read;
                    }
                }  
                read_buffer(fs, 0, &read, length, data, indirect.Pointers[j]);

                // enough data has been read from data buffer 
                if(read == length) {
                    disk_write(fs->disk, node.Indirect, indirect.Data);
                    store_inode(fs, inumber, &node);
                    return length;
                }
            }

            // space exhausted
            disk_write(fs->disk, node.Indirect, indirect.Data);
            store_inode(fs, inumber, &node);
            return read;
        }
    }

    // error
    return -1;
}
