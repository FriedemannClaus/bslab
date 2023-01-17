//
//  myfs-structs.h
//  myfs
//
//  Created by Oliver Waldhorst on 07.09.17.
//  Copyright © 2017 Oliver Waldhorst. All rights reserved.
//

#ifndef myfs_structs_h
#define myfs_structs_h

#define NAME_LENGTH 255
#define BLOCK_SIZE 512
#define NUM_DIR_ENTRIES 64
#define NUM_OPEN_FILES 64
#define BLOCK_DEVICE_SIZE 1024

struct file{
    char name[NAME_LENGTH]=""; //255 bytes lang max
    size_t dataSize; //unsigned long
    uid_t user; //insigned int
    gid_t group; //unsigned int
    mode_t mode; //unsigned int
    time_t atime; //long
    time_t mtime;
    time_t ctime;
    char *data; //64bit für Pointer in 64-bit Betriebssystem = 8 bytes
    int fat_data;
    bool open; //1bit bzw < 1byte
}; // 320 bytes laut sizeof. 320 * 64 /512 = 40 Blöcke für file root[64]

struct superblock {
    int dmapAddress; // = 1
    int fatAddress; // = 3
    int rootAddress; // = 11 // 320 bytes laut sizeof. 320 * 64 /512 = 40 Blöcke für file root[64]
    int dataAddress; //ab Block 52 Filesystem
    int blockDeviceSize; //= 1024 (including metadata(fat, root, ...))
    int dataSize; //1012
};

struct OpenFile {
    char buffer[BLOCK_SIZE];
    int blockNo = -1;
    bool isOpen = false;
};

#endif /* myfs_structs_h */
