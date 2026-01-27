/**
 * FatFS Header Stub
 * Note: Full FatFS implementation from http://elm-chan.org/fsw/ff/
 * This is a minimal stub for compilation
 */
#ifndef FF_H
#define FF_H

#include "ffconf.h"
#include "diskio.h"
#include <stdint.h>
#include <stddef.h>

// File system object structure
typedef struct {
    BYTE    fs_type;
    BYTE    pdrv;
    BYTE    n_fats;
    BYTE    wflag;
    BYTE    fsi_flag;
    WORD    id;
    WORD    n_rootdir;
    WORD    csize;
    DWORD   last_clst;
    DWORD   free_clst;
    DWORD   n_fatent;
    DWORD   fsize;
    DWORD   volbase;
    DWORD   fatbase;
    DWORD   dirbase;
    DWORD   database;
    DWORD   winsect;
    BYTE    win[FF_MAX_SS];
} FATFS;

// File object structure
typedef struct {
    FATFS*  fs;
    WORD    id;
    BYTE    flag;
    BYTE    err;
    DWORD   fptr;
    DWORD   clust;
    DWORD   sect;
    DWORD   dir_sect;
    BYTE*   dir_ptr;
    DWORD   obj_size;
    DWORD   sclust;
} FIL;

// Directory object structure
typedef struct {
    FATFS*  fs;
    WORD    id;
    WORD    index;
    DWORD   sclust;
    DWORD   clust;
    DWORD   sect;
    BYTE*   dir;
    BYTE    fn[12];
} DIR;

// File information structure
typedef struct {
    DWORD   fsize;
    WORD    fdate;
    WORD    ftime;
    BYTE    fattrib;
    char    fname[13];
#if FF_USE_LFN
    char    altname[13];
    char*   lfname;
    UINT    lfsize;
#endif
} FILINFO;

// File function return code
typedef enum {
    FR_OK = 0,
    FR_DISK_ERR,
    FR_INT_ERR,
    FR_NOT_READY,
    FR_NO_FILE,
    FR_NO_PATH,
    FR_INVALID_NAME,
    FR_DENIED,
    FR_EXIST,
    FR_INVALID_OBJECT,
    FR_WRITE_PROTECTED,
    FR_INVALID_DRIVE,
    FR_NOT_ENABLED,
    FR_NO_FILESYSTEM,
    FR_MKFS_ABORTED,
    FR_TIMEOUT,
    FR_LOCKED,
    FR_NOT_ENOUGH_CORE,
    FR_TOO_MANY_OPEN_FILES,
    FR_INVALID_PARAMETER
} FRESULT;

// File access mode flags
#define FA_READ             0x01
#define FA_WRITE            0x02
#define FA_OPEN_EXISTING    0x00
#define FA_CREATE_NEW       0x04
#define FA_CREATE_ALWAYS    0x08
#define FA_OPEN_ALWAYS      0x10
#define FA_OPEN_APPEND      0x30

// File attribute bits
#define AM_RDO  0x01
#define AM_HID  0x02
#define AM_SYS  0x04
#define AM_DIR  0x10
#define AM_ARC  0x20

// FatFS API (stubs - link with actual FatFS library)
FRESULT f_open(FIL* fp, const char* path, BYTE mode);
FRESULT f_close(FIL* fp);
FRESULT f_read(FIL* fp, void* buff, UINT btr, UINT* br);
FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw);
FRESULT f_lseek(FIL* fp, DWORD ofs);
FRESULT f_sync(FIL* fp);
FRESULT f_truncate(FIL* fp);
FRESULT f_opendir(DIR* dp, const char* path);
FRESULT f_closedir(DIR* dp);
FRESULT f_readdir(DIR* dp, FILINFO* fno);
FRESULT f_mkdir(const char* path);
FRESULT f_unlink(const char* path);
FRESULT f_rename(const char* path_old, const char* path_new);
FRESULT f_stat(const char* path, FILINFO* fno);
FRESULT f_mount(FATFS* fs, const char* path, BYTE opt);
FRESULT f_mkfs(const char* path, BYTE opt, DWORD au, void* work, UINT len);

#define f_eof(fp) ((int)((fp)->fptr == (fp)->obj_size))
#define f_error(fp) ((fp)->err)
#define f_tell(fp) ((fp)->fptr)
#define f_size(fp) ((fp)->obj_size)

#endif
