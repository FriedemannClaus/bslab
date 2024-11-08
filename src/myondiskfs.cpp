//
// Created by Oliver Waldhorst on 20.03.20.
// Copyright © 2017-2020 Oliver Waldhorst. All rights reserved.
//

#include "myondiskfs.h"

// For documentation of FUSE methods see https://libfuse.github.io/doxygen/structfuse__operations.html

#undef DEBUG

// TODO: Comment lines to reduce debug messages
#define DEBUG
#define DEBUG_METHODS
#define DEBUG_RETURN_VALUES

#include <unistd.h>
#include <errno.h>
#include <string.h>
//Bei Problemen mit memcpy vll #include <cstring> wieder hinzufügen (ist eig ähnlich wie <string.h>)

#include "macros.h"
#include "myfs.h"
#include "myfs-info.h"
#include "blockdevice.h"


/// @brief Constructor of the on-disk file system class.
///
/// You may add your own constructor code here.
MyOnDiskFS::MyOnDiskFS() : MyFS() {
    // create a block device object
    this->blockDevice = new BlockDevice(BLOCK_SIZE);
    fat = (int *) malloc(FATSIZE); //Muss man ändern wenn man Blocksize ändern will
    dmap = (bool *) malloc(DMAPSIZE); //Muss man ändern wenn man Blocksize ändern will. Man kann 1012 hier nicht
    //abhängig von Blockdevicesize berechnen. Weil Kreisreferenzierung
    root = (file *) malloc(ROOTSIZE);
}

/// @brief Destructor of the on-disk file system class.
///
/// You may add your own destructor code here.
MyOnDiskFS::~MyOnDiskFS() {
    // free block device object
    delete this->blockDevice;


    free(fat);
    free(dmap);
    free(root);

}

/// @brief Create a new file.
///
/// Create a new file with given name and permissions.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] mode Permissions for file access.
/// \param [in] dev Can be ignored.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseMknod(const char *path, mode_t mode, dev_t dev) {
    LOGM();


    if (actualFiles >= NUM_DIR_ENTRIES) {
        RETURN(-ENOSPC);
    }
    int i = 0;
    while ((root[i].name[0] != '\0') && (i < NUM_DIR_ENTRIES)) {
        i++;
    }
    size_t pathLength = strlen(path);
    if (pathLength > NAME_LENGTH) {
        RETURN(-ENAMETOOLONG);
    } else if (fileExists(path)) {
        RETURN(-EEXIST);
    } else {
        strcpy(root[i].name, path);
        root[i].mode = mode;
        root[i].atime = time(NULL);
        root[i].mtime = time(NULL);

        //write root to disc
        writeRootToDisc();
    }

    RETURN(0);
}

/// @brief Delete a file.
///
/// Delete a file with given name from the file system.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseUnlink(const char *path) {
    LOGM();


    file *foundFile = findFile(path);
    if (foundFile == nullptr) {
        RETURN(-ENOENT);
    }
    if (foundFile->open) {
        RETURN(-EACCES);
    }


    if (foundFile->fat_data != -1) {
        int i = foundFile->fat_data;
        while (fat[i] != EOF) {
            int index = fat[i];
            fat[i] = INT32_MAX;
            dmap[i] = false;
            i = index;
        }
        fat[i] = INT32_MAX;
        dmap[i] = false;
    }

    foundFile->fat_data = -1;
    foundFile->dataSize = 0;
    foundFile->name[0] = '\0';

    writeFatToDisc();
    writeDmapToDisc();
    writeRootToDisc();

    actualFiles--;
    RETURN(0);
}

/// @brief Rename a file.
///
/// Rename the file with with a given name to a new name.
/// Note that if a file with the new name already exists it is replaced (i.e., removed
/// before renaming the file.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] newpath  New name of the file, starting with "/".
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseRename(const char *path, const char *newpath) {
    LOGM();


    file *foundFile = findFile(path);
    if (foundFile == nullptr) {
        RETURN(-ENOENT);
    }
    if (foundFile->open) {
        RETURN(-EACCES);
    }
    file *otherFile = findFile(newpath);
    if (otherFile != nullptr) {
        if (otherFile->open) {
            RETURN(-EACCES);
        }
        fuseUnlink(newpath);
    }
    strcpy(foundFile->name, newpath);
    foundFile->mtime = time(NULL);
    writeRootToDisc();

    RETURN(0);
}

/// @brief Get file meta data.
///
/// Get the metadata of a file (user & group id, modification times, permissions, ...).
/// \param [in] path Name of the file, starting with "/".
/// \param [out] statbuf Structure containing the meta data, for details type "man 2 stat" in a terminal.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseGetattr(const char *path, struct stat *statbuf) {
    LOGM();


    LOGF("\tAttributes of %s requested\n", path);

    // GNU's definitions of the attributes (http://www.gnu.org/software/libc/manual/html_node/Attribute-Meanings.html):
    // 		st_uid: 	The user ID of the file’s owner.
    //		st_gid: 	The group ID of the file.
    //		st_atime: 	This is the last access time for the file.
    //		st_mtime: 	This is the time of the last modification to the contents of the file.
    //		st_mode: 	Specifies the mode of the file. This includes file type information (see Testing File Type) and
    //		            the file permission bits (see Permission Bits).
    //		st_nlink: 	The number of hard links to the file. This count keeps track of how many directories have
    //	             	entries for this file. If the count is ever decremented to zero, then the file itself is
    //	             	discarded as soon as no process still holds it open. Symbolic links are not counted in the
    //	             	total.
    //		st_size:	This specifies the size of a regular file in bytes. For files that are really devices this field
    //		            isn’t usually meaningful. For symbolic links this specifies the length of the file name the link
    //		            refers to.


    if (strcmp(path, "/") == 0) {
        statbuf->st_mode = S_IFDIR | 0755;
        statbuf->st_nlink = 2; // Why "two" hardlinks instead of "one"? The answer is here: http://unix.stackexchange.com/a/101536
        RETURN(0);
    }
    file *myFile = findFile(path);
    if (myFile != nullptr) {
        statbuf->st_uid = getuid(); // The owner of the file/directory is the user who mounted the filesystem
        statbuf->st_gid = getgid(); // The group of the file/directory is the same as the group of the user who mounted the filesystem
        statbuf->st_atime = time(NULL); // The last "a"ccess of the file/directory is right now
        statbuf->st_mtime = time(NULL); // The last "m"odification of the file/directory is right now
        myFile->atime = time(NULL);
        myFile->mtime = time(NULL);

        statbuf->st_mode = myFile->mode;
        statbuf->st_nlink = 1;
        statbuf->st_size = myFile->dataSize;
    } else {
        RETURN(-ENOENT);
    }

    RETURN(0);
}

/// @brief Change file permissions.
///
/// Set new permissions for a file.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] mode New mode of the file.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseChmod(const char *path, mode_t mode) {
    LOGM();

    file *myFile = findFile(path);
    if (myFile != nullptr) {
        if (!myFile->open) {
            myFile->mode = mode;
        } else {
            RETURN(-EACCES);
        }
    } else {
        RETURN(-ENOENT);
    }
    writeRootToDisc();

    RETURN(0);
}

/// @brief Change the owner of a file.
///
/// Change the user and group identifier in the meta data of a file.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] uid New user id.
/// \param [in] gid New group id.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseChown(const char *path, uid_t uid, gid_t gid) {
    LOGM();

    file *myFile = findFile(path);
    if (myFile != nullptr) {
        if (!myFile->open) {
            myFile->user = uid;
            myFile->group = gid;
        } else {
            RETURN(-EACCES);
        }
    } else {
        RETURN(-ENOENT);
    }
    writeRootToDisc();
    RETURN(0);
}

/// @brief Open a file.
///
/// Open a file for reading or writing. This includes checking the permissions of the current user and incrementing the
/// open file count.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [out] fileInfo Can be ignored in Part 1
/// \return 0 on success, -ERRNO on failure.

int MyOnDiskFS::fuseOpen(const char *path, struct fuse_file_info *fileInfo) {
    LOGM();


    file *myFile = findFile(path);
    if (myFile != nullptr) {
        if (openFilesCount < NUM_OPEN_FILES) {
            if (myFile->open == false) {
                myFile->open = true;
                int i = 0;
                while (i < NUM_OPEN_FILES) {
                    if (openFiles[i].isOpen == false) {
                        break;
                    } else {
                        i++;
                    }
                }
                openFiles[i].isOpen = true;
                fileInfo->fh = i;
                myFile->atime = time(NULL);
                openFilesCount++;
            }
        } else {
            RETURN(-EMFILE);
        }
    } else {
        RETURN(-ENOENT);
    }

    writeRootToDisc();

    RETURN(0);
}

/// @brief Read from a file.
///
/// Read a given number of bytes from a file starting form a given position.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// Note that the file content is an array of bytes, not a string. I.e., it is not (!) necessarily terminated by '\0'
/// and may contain an arbitrary number of '\0'at any position. Thus, you should not use strlen(), strcpy(), strcmp(),
/// ... on both the file content and buf, but explicitly store the length of the file and all buffers somewhere and use
/// memcpy(), memcmp(), ... to process the content.
/// \param [in] path Name of the file, starting with "/".
/// \param [out] buf The data read from the file is stored in this array. You can assume that the size of buffer is at
/// least 'size'
/// \param [in] size Number of bytes to read
/// \param [in] offset Starting position in the file, i.e., number of the first byte to read relative to the first byte of
/// the file
/// \param [in] fileInfo Can be ignored in Part 1
/// \return The Number of bytes read on success. This may be less than size if the file does not contain sufficient bytes.
/// -ERRNO on failure.
int MyOnDiskFS::fuseRead(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fileInfo) {
    LOGM();


    LOGF("--> Trying to read %s, %lu, %lu\n", path, (unsigned long) offset, size);

    file *myFile = findFile(path);
    if (myFile != nullptr) {
        if (myFile->open) {
            if (myFile->fat_data != -1) {
                int calculatedSize;
                if (myFile->dataSize <= size + offset) {
                    calculatedSize = myFile->dataSize - offset;
                } else {
                    calculatedSize = size;
                }

                int firstBlockIndex = (offset / BLOCK_SIZE);//0
                int firstBlockOffset = 0;
                if (offset % BLOCK_SIZE != 0) {
                    firstBlockOffset = offset - (firstBlockIndex * BLOCK_SIZE);
                }
                int fatIndex = myFile->fat_data;
                for (int i = 0; i < firstBlockIndex; i++) {
                    fatIndex = fat[fatIndex];
                }

                char *dataBlock = new char[BLOCK_SIZE];

                size_t offsetBuf = 0;
                size_t bytesToRead = calculatedSize;
                while (bytesToRead > 0) {
                    if (fatIndex == openFiles[fileInfo->fh].blockNo) {
                        memcpy(dataBlock, openFiles[fileInfo->fh].buffer, BLOCK_SIZE);
                    } else {
                        blockDevice->read(sBlock.dataAddress + fatIndex, dataBlock);
                    }
                    if (bytesToRead == calculatedSize) {
                        if (bytesToRead > BLOCK_SIZE - firstBlockOffset) {
                            memcpy(buf, dataBlock + offset, BLOCK_SIZE - firstBlockOffset);
                            bytesToRead -= BLOCK_SIZE - firstBlockOffset;
                            offsetBuf += BLOCK_SIZE - firstBlockOffset;
                        } else {
                            memcpy(buf, dataBlock + firstBlockOffset, bytesToRead);
                            bytesToRead = 0;
                        }
                    } else {
                        if (bytesToRead < BLOCK_SIZE) {
                            memcpy(buf + offsetBuf, dataBlock, bytesToRead);
                            bytesToRead = 0;
                        } else {
                            memcpy(buf + offsetBuf, dataBlock, BLOCK_SIZE);
                            offsetBuf += BLOCK_SIZE;
                            bytesToRead -= BLOCK_SIZE;
                        }
                    }
                    if (bytesToRead != 0) fatIndex = fat[fatIndex];
                }
                memcpy(openFiles[fileInfo->fh].buffer, dataBlock, BLOCK_SIZE);
                openFiles[fileInfo->fh].blockNo = fatIndex;
                delete[] dataBlock;

                RETURN(calculatedSize);
            } else {
                RETURN(-EBADF);
            }
        } else {
            RETURN(-EACCES);
        }
    } else {
        RETURN(-ENOENT);
    }
}

/// @brief Write to a file.
///
/// Write a given number of bytes to a file starting at a given position.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// Note that the file content is an array of bytes, not a string. I.e., it is not (!) necessarily terminated by '\0'
/// and may contain an arbitrary number of '\0'at any position. Thus, you should not use strlen(), strcpy(), strcmp(),
/// ... on both the file content and buf, but explicitly store the length of the file and all buffers somewhere and use
/// memcpy(), memcmp(), ... to process the content.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] buf An array containing the bytes that should be written.
/// \param [in] size Number of bytes to write.
/// \param [in] offset Starting position in the file, i.e., number of the first byte to read relative to the first byte of
/// the file.
/// \param [in] fileInfo Can be ignored in Part 1 .
/// \return Number of bytes written on success, -ERRNO on failure.
int
MyOnDiskFS::fuseWrite(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fileInfo) {
    LOGM();


    file *myFile = findFile(path);
    if (myFile != nullptr) {
        if (myFile->open) {
            if (myFile->dataSize < (size + offset)) {
                fuseTruncate(path, size + offset, fileInfo);
            }

            int firstBlockIndex = (offset / BLOCK_SIZE); //Anzahl der vollständigen Blöcke vor dem unvollständigen Block 8
            int firstBlockOffset = 0;
            if (offset % BLOCK_SIZE != 0) {
                firstBlockOffset = offset - (firstBlockIndex * BLOCK_SIZE); //Offset im unvollständigen Block
            }
            int fatIndex = myFile->fat_data;
            for (int i = 0; i < firstBlockIndex; ++i) {
                fatIndex = fat[fatIndex];
            }

            char *dataBlock = (char *) malloc(BLOCK_SIZE);
            size_t offsetBuf = 0;
            size_t bytesToWrite = size;
            while (bytesToWrite > 0) {
                if (fatIndex == openFiles[fileInfo->fh].blockNo) {  //Pufferlesen
                    memcpy(dataBlock, openFiles[fileInfo->fh].buffer, BLOCK_SIZE);
                } else {
                    blockDevice->read(sBlock.dataAddress + fatIndex, dataBlock);
                }
                if (bytesToWrite == size) { // if bytesToWrite > BLOCK_SIZE
                    if (bytesToWrite > BLOCK_SIZE - firstBlockOffset) {
                        memcpy(dataBlock + firstBlockOffset, buf, BLOCK_SIZE - firstBlockOffset);
                        offsetBuf += BLOCK_SIZE - firstBlockOffset;
                        bytesToWrite -= BLOCK_SIZE - firstBlockOffset;
                    } else {
                        memcpy(dataBlock + firstBlockOffset, buf, bytesToWrite);
                        bytesToWrite = 0;
                    }
                } else {
                    if (bytesToWrite < BLOCK_SIZE) {
                        memcpy(dataBlock, buf + offsetBuf, bytesToWrite);
                        bytesToWrite = 0;
                    } else {
                        memcpy(dataBlock, buf + offsetBuf, BLOCK_SIZE);
                        offsetBuf += BLOCK_SIZE;
                        bytesToWrite -= BLOCK_SIZE;
                    }
                }
                blockDevice->write(sBlock.dataAddress + fatIndex, dataBlock);
                if (bytesToWrite != 0) fatIndex = fat[fatIndex];
            }
            memcpy(openFiles[fileInfo->fh].buffer, dataBlock, BLOCK_SIZE);
            openFiles[fileInfo->fh].blockNo = fatIndex;
            free(dataBlock);

            myFile->mtime = time(NULL);
            writeRootToDisc();
            RETURN(size);
        } else {
            RETURN(-EBADF);
        }
    } else {
        RETURN(-ENOENT);
    }

    RETURN(0);
}

/// @brief Close a file.
///
/// \param [in] path Name of the file, starting with "/".
/// \param [in] File handel for the file set by fuseOpen.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseRelease(const char *path, struct fuse_file_info *fileInfo) {
    LOGM();

    file *myFile = findFile(path);
    if (myFile != nullptr) {
        if (myFile->open == true) {
            myFile->open = false;
            openFiles[fileInfo->fh].blockNo = -1;
            openFiles[fileInfo->fh].isOpen = false;
            openFilesCount--;
        }
    } else {
        RETURN(-ENOENT);
    }
    writeRootToDisc();

    RETURN(0);
}

/// @brief Truncate a file.
///
/// Set the size of a file to the new size. If the new size is smaller than the old size, spare bytes are removed. If
/// the new size is larger than the old size, the new bytes may be random.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] newSize New size of the file.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseTruncate(const char *path, off_t newSize) {
    LOGM();
    LOGF("--> Trying to truncate %s, %ld\n", path, newSize);


    file *myFile = findFile(path);
    if (myFile == nullptr) {
        RETURN(-ENOENT);
    }

    int oldBlockCount = ceil((double) myFile->dataSize / BLOCK_SIZE);
    int newBlockCount = ceil((double) newSize / BLOCK_SIZE);
    int allocateCount = abs(newBlockCount - oldBlockCount);//8
    if (newSize > myFile->dataSize) { //Vergroessern
        if (allocateCount > 0) { // Wenn neue Size außerhalb der alten Blockgrenze liegt
            int actualIndex = 0;
            int i = 0;
            if (myFile->dataSize == 0) {   //Leeres file
                int index = findEmptyDataBlock();
                if (index < 0) {
                    RETURN(index); //=ENOSPACE
                }
                fat[index] = EOF;
                dmap[index] = true;
                actualIndex = index;
                myFile->fat_data = actualIndex;
                i++;
            } else { //normales, nichtleeres file: actualIndex bestimmen
                actualIndex = myFile->fat_data;
                while (fat[actualIndex] != EOF) {
                    actualIndex = fat[actualIndex];
                }
            }
            while (i < allocateCount) {
                int index = findEmptyDataBlock();
                if (index < 0) {
                    RETURN(index); //=ENOSPACE
                }
                fat[index] = EOF;
                dmap[index] = true;
                fat[actualIndex] = index;
                actualIndex = index;
                i++;
            }
        }
    } else { //Verkleinern
        if (allocateCount > 0) {
            int startToDelete = oldBlockCount - allocateCount;

            //wo ist der letzte Block (actualIndex)
            int actualIndex = myFile->fat_data;
            int blocks = 0;
            while (blocks < startToDelete) {
                actualIndex = fat[actualIndex];
                blocks++;
            }

            //verkleinern ausfuehren
            int index = fat[actualIndex];//Index hinter neuem letztem Block
            if (startToDelete == 0) {
                if (fat[actualIndex] != EOF) {
                    fat[actualIndex] = INT32_MAX;
                    dmap[actualIndex] = false;
                }
            }
            while (fat[index] != EOF) {
                actualIndex = index;
                index = fat[actualIndex];
                fat[actualIndex] = INT32_MAX;
                dmap[actualIndex] = false;
            }
            fat[actualIndex] = INT32_MAX;
            dmap[actualIndex] = false;
        }
    }
    myFile->dataSize = newSize;
    myFile->mtime = time(NULL);

    writeRootToDisc();
    writeDmapToDisc();
    writeFatToDisc();
    RETURN(0);
}

/// @brief Truncate a file.
///
/// Set the size of a file to the new size. If the new size is smaller than the old size, spare bytes are removed. If
/// the new size is larger than the old size, the new bytes may be random. This function is called for files that are
/// open.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] newSize New size of the file.
/// \param [in] fileInfo Can be ignored in Part 1.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseTruncate(const char *path, off_t newSize, struct fuse_file_info *fileInfo) {
    LOGM();
    LOGF("--> Trying to truncate %s, %ld\n", path, newSize);

    file *myFile = findFile(path);
    if (myFile == nullptr) {
        RETURN(-ENOENT);
    }

    int oldBlockCount = ceil((double) myFile->dataSize / BLOCK_SIZE);
    int newBlockCount = ceil((double) newSize / BLOCK_SIZE);
    int allocateCount = newBlockCount - oldBlockCount;
    if (newSize > myFile->dataSize) { //Vergroessern
        if (allocateCount > 0) {
            int actualIndex = 0;
            int i = 0;
            if (myFile->dataSize == 0) {   //Leeres file
                int index = findEmptyDataBlock();
                if (index < 0) {
                    RETURN(index); //=ENOSPACE
                }
                fat[index] = EOF;
                dmap[index] = true;
                actualIndex = index;
                myFile->fat_data = actualIndex;
                i++;
            } else { //normales, nichtleeres file: actualIndex bestimmen
                actualIndex = myFile->fat_data;
                while (fat[actualIndex] != EOF) {
                    actualIndex = fat[actualIndex];
                }
            }
            while (i < allocateCount) {
                int index = findEmptyDataBlock();
                if (index < 0) {
                    RETURN(index); //=ENOSPACE
                }
                fat[index] = EOF;
                dmap[index] = true;
                fat[actualIndex] = index;
                actualIndex = index;
                i++;
            }
        }
    } else { //Verkleinern
        if (allocateCount > 0) {
            int startToDelete = oldBlockCount - allocateCount;

            //wo ist der letzte Block (actualIndex)
            int actualIndex = myFile->fat_data;
            int blocks = 0;
            while (blocks < startToDelete) {
                actualIndex = fat[actualIndex];
                blocks++;
            }

            //verkleinern ausfuehren
            int index = fat[actualIndex];//Index hinter neuem letztem Block
            if (startToDelete == 0) {
                if (fat[actualIndex] != EOF) {
                    if (openFiles[fileInfo->fh].blockNo == actualIndex) {
                        openFiles[fileInfo->fh].blockNo = -1;
                    }
                    fat[actualIndex] = INT32_MAX;
                    dmap[actualIndex] = false;
                }
            }
            while (fat[index] != EOF) {
                if (openFiles[fileInfo->fh].blockNo == index) {
                    openFiles[fileInfo->fh].blockNo = -1;
                }
                actualIndex = index;
                index = fat[actualIndex];
                fat[actualIndex] = INT32_MAX;
                dmap[actualIndex] = false;
            }
            if (openFiles[fileInfo->fh].blockNo == actualIndex) {
                openFiles[fileInfo->fh].blockNo = -1;
            }
            fat[actualIndex] = INT32_MAX;
            dmap[actualIndex] = false;
        }
    }
    myFile->dataSize = newSize;
    myFile->mtime = time(NULL);
    writeRootToDisc();
    writeDmapToDisc();
    writeFatToDisc();

    RETURN(0);
}

/// @brief Read a directory.
///
/// Read the content of the (only) directory.
/// You do not have to check file permissions, but can assume that it is always ok to access the directory.
/// \param [in] path Path of the directory. Should be "/" in our case.
/// \param [out] buf A buffer for storing the directory entries.
/// \param [in] filler A function for putting entries into the buffer.
/// \param [in] offset Can be ignored.
/// \param [in] fileInfo Can be ignored.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseReaddir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                            struct fuse_file_info *fileInfo) {
    LOGM();

    LOGF("--> Getting The List of Files of %s\n", path);

    filler(buf, ".", NULL, 0); // Current Directory
    filler(buf, "..", NULL, 0); // Parent Directory

    if (strcmp(path, "/") ==
        0) { // If the user is trying to show the files/directories of the root directory show the following
        for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
            if (root[i].name[0] == '/') {
                //char tmp[NAME_LENGTH] = "";
                //strcpy(tmp, myFiles[i].name + 1);
                filler(buf, root[i].name, NULL, 0);
                LOGF("Found file: %s", root[i].name);
            }
        }
    }

    RETURN(0);
}

/// Initialize a file system.
///
/// This function is called when the file system is mounted. You may add some initializing code here.
/// \param [in] conn Can be ignored.
/// \return 0.
void *MyOnDiskFS::fuseInit(struct fuse_conn_info *conn) {
    // Open logfile
    this->logFile = fopen(((MyFsInfo *) fuse_get_context()->private_data)->logFile, "w+");
    if (this->logFile == NULL) {
        fprintf(stderr, "ERROR: Cannot open logfile %s\n", ((MyFsInfo *) fuse_get_context()->private_data)->logFile);
    } else {
        // turn of logfile buffering
        setvbuf(this->logFile, NULL, _IOLBF, 0);

        LOG("Starting logging...\n");

        LOG("Using on-disk mode");

        LOGF("Container file name: %s", ((MyFsInfo *) fuse_get_context()->private_data)->contFile);

        int ret = this->blockDevice->open(((MyFsInfo *) fuse_get_context()->private_data)->contFile);

        if (ret >= 0) {
            LOG("Container file does exist, reading");

            // Read existing structures form file

            //Blocksize 512
            char puffer[BLOCK_SIZE];
            blockDevice->read(0, puffer); //Block 0 = superblock (immer, per def.) lesen
            memcpy(&sBlock, puffer, sizeof(superblock));
            // Read dmap
            int i = 0;
            while (i < DMAPSIZE / BLOCK_SIZE) {
                blockDevice->read(sBlock.dmapAddress + i, puffer); //Block 1 = dmapAddress lesen
                memcpy(&dmap + i * BLOCK_SIZE, puffer, BLOCK_SIZE);
                i++;
            }
            if ((DMAPSIZE % BLOCK_SIZE) != 0) {
                blockDevice->read(sBlock.dmapAddress + i, puffer);
                memcpy(&dmap + i * BLOCK_SIZE, puffer, DMAPSIZE - i * BLOCK_SIZE);
            }
            // Read fat
            i = 0;
            while (i < FATSIZE / BLOCK_SIZE) {
                blockDevice->read(sBlock.fatAddress + i, puffer); //Block 3ff. = fatAddress lesen
                memcpy(&fat + i * BLOCK_SIZE, puffer, BLOCK_SIZE);
                i++;
            }
            if ((FATSIZE % BLOCK_SIZE) != 0) {
                blockDevice->read(sBlock.fatAddress + i, puffer);
                memcpy(&fat + i * BLOCK_SIZE, puffer, FATSIZE - i * BLOCK_SIZE);
            }
            // Read root
            i = 0;
            while (i < ROOTSIZE / BLOCK_SIZE) {
                blockDevice->read(sBlock.rootAddress + i, puffer); //Block 11ff. = rootAddress lesen
                memcpy(&root + i * BLOCK_SIZE, puffer, BLOCK_SIZE);
                i++;
            }
            if ((ROOTSIZE % BLOCK_SIZE) != 0) { //unreachable, da immer 20480/512=40 mit jetziger Konfig.
                blockDevice->read(sBlock.rootAddress + i, puffer);
                memcpy(&root + i * BLOCK_SIZE, puffer, ROOTSIZE - i * BLOCK_SIZE);
            }

        } else if (ret == -ENOENT) {
            LOG("Container file does not exist, creating a new one");

            ret = this->blockDevice->create(((MyFsInfo *) fuse_get_context()->private_data)->contFile);

            if (ret >= 0) {
                // Create empty structures in file

                LOGF("FATSIZE: %f", (double) FATSIZE / BLOCK_SIZE);
                LOGF("DMAPSIZE: %f", (double) DMAPSIZE / BLOCK_SIZE);
                LOGF("ROOTSIZE: %f", (double) ROOTSIZE / BLOCK_SIZE);

                //(superblockAdress = Block 0)
                int dmapAddress = 1;
                int fatAddress = dmapAddress + ceil((double) DMAPSIZE / BLOCK_SIZE);
                int rootAddress = fatAddress + ceil((double) FATSIZE / BLOCK_SIZE);
                int dataAddress = rootAddress + ceil((double) ROOTSIZE / BLOCK_SIZE);
                int dataSize = BLOCK_DEVICE_SIZE - dataAddress - 1;
                sBlock.dmapAddress = 1;
                sBlock.fatAddress = fatAddress;
                sBlock.rootAddress = rootAddress;
                sBlock.dataAddress = dataAddress;
                sBlock.blockDeviceSize = BLOCK_DEVICE_SIZE;
                sBlock.dataSize = dataSize;
                //{1, fatAddress, rootAddress, dataAddress, BLOCK_DEVICE_SIZE,
                //     dataSize}; //ab Block 52 Filesystem
                //Blocksize 512
                char puffer[BLOCK_SIZE];
                memcpy(puffer, &sBlock, sizeof(superblock));
                blockDevice->write(0, puffer); //Block 0 = superblock (immer, per def.) lesen

                //dmap Initialisierung true
                //dmap[sBlock.dataSize];
                for (int i = 0; i < sBlock.blockDeviceSize - sBlock.dataAddress - 1; i++) {
                    dmap[i] = false;
                }
                //fat Initialisierung 0xffff..
                //fat[sBlock.dataSize];
                for (int i = 0; i < sBlock.dataSize; i++) {
                    fat[i] = INT32_MAX; //Das sind 16 "f"s, je 4 bit
                }

                //root Initialisierung
                actualFiles = 0;
                openFilesCount = 0;
                //root in myondiskfs.h initialisiert
                for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
                    root[i].fat_data = -1;
                    root[i].dataSize = 0;
                }

                writeDmapToDisc();
                writeFatToDisc();
                writeRootToDisc();

                LOG("Container file created");
            }
        }

        if (ret < 0) {
            LOGF("ERROR: Access to container file failed with error %d", ret);
        }
    }

    return 0;
}

/// @brief Clean up a file system.
///
/// This function is called when the file system is unmounted. You may add some cleanup code here.
void MyOnDiskFS::fuseDestroy() {
    LOGM();

    // Implement this!

}


// Additional methods:

bool MyOnDiskFS::fileExists(const char *path) {
    for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
        if (root[i].name[0] != '\0') {
            if (strcmp(root[i].name, path) == 0) {
                return true;
            }
        }
    }
    return false;
}

file *MyOnDiskFS::findFile(const char *path) {
    for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
        if (root[i].name[0] != '\0') {
            if (strcmp(root[i].name, path) == 0) {
                return &root[i];
            }
        }
    }
    return nullptr;
}

void MyOnDiskFS::writeFatToDisc() {
    char puffer[BLOCK_SIZE];
    int i = 0;
    while (i < FATSIZE / BLOCK_SIZE) {
        memcpy(puffer, &fat + i * BLOCK_SIZE, BLOCK_SIZE);
        blockDevice->write(sBlock.fatAddress + i, puffer); //Block 3ff. = fatAddress lesen
        i++;
    }
    if ((FATSIZE % BLOCK_SIZE) != 0) {
        memcpy(puffer, &fat + i * BLOCK_SIZE, FATSIZE - i * BLOCK_SIZE);
        blockDevice->write(sBlock.fatAddress + i, puffer);
    }
}

void MyOnDiskFS::writeRootToDisc() {
    char puffer[BLOCK_SIZE];
    int i = 0;
    while (i < ROOTSIZE / BLOCK_SIZE) {
        memcpy(puffer, &root + i * BLOCK_SIZE, BLOCK_SIZE);
        blockDevice->write(sBlock.rootAddress + i, puffer);
        i++;
    }
    if ((ROOTSIZE % BLOCK_SIZE) != 0) { //unreachable, da immer 20480/512=40 mit jetziger Konfig.
        memcpy(puffer, &root + i * BLOCK_SIZE, ROOTSIZE - i * BLOCK_SIZE);
        blockDevice->write(sBlock.rootAddress + i, puffer);
    }
}

void MyOnDiskFS::writeDmapToDisc() {
    char puffer[BLOCK_SIZE];
    int i = 0;
    while (i < DMAPSIZE / BLOCK_SIZE) {
        memcpy(puffer, &dmap + i * BLOCK_SIZE, BLOCK_SIZE);
        blockDevice->write(sBlock.dmapAddress + i, puffer); //Block 1 = dmapAddress lesen
        i++;
    }
    if ((DMAPSIZE % BLOCK_SIZE) != 0) {
        memcpy(puffer, &dmap + i * BLOCK_SIZE, DMAPSIZE - i * BLOCK_SIZE);
        blockDevice->write(sBlock.dmapAddress + i, puffer);
    }
}

int MyOnDiskFS::findEmptyDataBlock() {
    for (int j = 0; j < sBlock.dataSize; ++j) {
        if (dmap[j] == false) {
            return j;
        }
    }
    return -ENOSPC;
}

// DO NOT EDIT ANYTHING BELOW THIS LINE!!!

/// @brief Set the static instance of the file system.
///
/// Do not edit this method!
void MyOnDiskFS::SetInstance() {
    MyFS::_instance = new MyOnDiskFS();
}