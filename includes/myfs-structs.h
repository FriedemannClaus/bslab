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

struct file{
    char name[NAME_LENGTH]="";
    size_t dataSize;
    uid_t user;
    gid_t group;
    mode_t mode;
    time_t atime;
    time_t mtime;
    time_t ctime;
    char* data;
    bool open;
};

#endif /* myfs_structs_h */
