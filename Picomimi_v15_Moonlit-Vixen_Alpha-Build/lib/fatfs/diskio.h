/**
 * FatFS Disk I/O Header
 */
#ifndef DISKIO_H
#define DISKIO_H

#include <stdint.h>

// Status of Disk Functions
typedef uint8_t DSTATUS;
#define STA_NOINIT      0x01
#define STA_NODISK      0x02
#define STA_PROTECT     0x04

// Results of Disk Functions
typedef enum {
    RES_OK = 0,
    RES_ERROR,
    RES_WRPRT,
    RES_NOTRDY,
    RES_PARERR
} DRESULT;

// Disk I/O control commands
#define CTRL_SYNC           0
#define GET_SECTOR_COUNT    1
#define GET_SECTOR_SIZE     2
#define GET_BLOCK_SIZE      3
#define CTRL_TRIM           4

// Types
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint32_t LBA_t;
typedef unsigned int UINT;

// Function prototypes
DSTATUS disk_initialize(BYTE pdrv);
DSTATUS disk_status(BYTE pdrv);
DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count);
DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count);
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff);
DWORD get_fattime(void);

#endif
