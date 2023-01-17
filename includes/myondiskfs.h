//
// Created by Oliver Waldhorst on 20.03.20.
// Copyright © 2017-2020 Oliver Waldhorst. All rights reserved.
//

#ifndef MYFS_MYONDISKFS_H
#define MYFS_MYONDISKFS_H

#include "myfs.h"
#include <map>
using namespace std;

/// @brief On-disk implementation of a simple file system.
class MyOnDiskFS : public MyFS {
private:
    virtual bool fileExists(const char *path);
    virtual file* findFile(const char *name);
    virtual void writeFatToDisc();
    virtual void writeDmapToDisc();
    virtual void writeRootToDisc();

    virtual int findEmptyDataBlock();

protected:
    //BlockDevice blockDevice; (Eig mit *)

public:
    static MyOnDiskFS *Instance();

    // TODO: [PART 2] Add attributes of your file system here
    size_t FATSIZE = 1012 * sizeof(int);
    size_t DMAPSIZE = 1012 * sizeof(bool);
    size_t ROOTSIZE = NUM_DIR_ENTRIES * sizeof(file);
    int *fat;
    bool *dmap;
    file *root;
    superblock sBlock;
    OpenFile openFiles[NUM_OPEN_FILES];

    MyOnDiskFS();

    ~MyOnDiskFS();

    static void SetInstance();

    // --- Methods called by FUSE ---
    // For Documentation see https://libfuse.github.io/doxygen/structfuse__operations.html
    virtual int fuseGetattr(const char *path, struct stat *statbuf);

    virtual int fuseMknod(const char *path, mode_t mode, dev_t dev);

    virtual int fuseUnlink(const char *path);
    virtual int fuseRename(const char *path, const char *newpath);
    virtual int fuseChmod(const char *path, mode_t mode);
    virtual int fuseChown(const char *path, uid_t uid, gid_t gid);
    virtual int fuseTruncate(const char *path, off_t newSize);
    virtual int fuseOpen(const char *path, struct fuse_file_info *fileInfo);
    virtual int fuseRead(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fileInfo);
    virtual int fuseWrite(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fileInfo);
    virtual int fuseRelease(const char *path, struct fuse_file_info *fileInfo);
    virtual void* fuseInit(struct fuse_conn_info *conn);
    virtual int fuseReaddir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fileInfo);
    virtual int fuseTruncate(const char *path, off_t offset, struct fuse_file_info *fileInfo);
    virtual void fuseDestroy();

    // TODO: Add methods of your file system here

};

#endif //MYFS_MYONDISKFS_H
