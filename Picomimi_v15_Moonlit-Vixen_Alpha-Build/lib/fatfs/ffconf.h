/**
 * FatFS Configuration for PICOMIMI
 */
#ifndef FFCONF_H
#define FFCONF_H

// Function Configuration
#define FF_FS_READONLY      0
#define FF_FS_MINIMIZE      0
#define FF_USE_FIND         1
#define FF_USE_MKFS         1
#define FF_USE_FASTSEEK     1
#define FF_USE_EXPAND       0
#define FF_USE_CHMOD        0
#define FF_USE_LABEL        1
#define FF_USE_FORWARD      0
#define FF_USE_STRFUNC      1
#define FF_PRINT_LLI        1
#define FF_PRINT_FLOAT      1
#define FF_STRF_ENCODE      3

// Locale and Namespace Configuration
#define FF_CODE_PAGE        437
#define FF_USE_LFN          2
#define FF_MAX_LFN          255
#define FF_LFN_UNICODE      0
#define FF_LFN_BUF          255
#define FF_SFN_BUF          12
#define FF_FS_RPATH         2

// Drive/Volume Configuration
#define FF_VOLUMES          1
#define FF_STR_VOLUME_ID    0
#define FF_VOLUME_STRS      "SD"
#define FF_MULTI_PARTITION  0
#define FF_MIN_SS           512
#define FF_MAX_SS           512

// System Configuration
#define FF_FS_TINY          0
#define FF_FS_EXFAT         0
#define FF_FS_NORTC         1
#define FF_NORTC_MON        1
#define FF_NORTC_MDAY       1
#define FF_NORTC_YEAR       2024
#define FF_FS_NOFSINFO      0
#define FF_FS_LOCK          0
#define FF_FS_REENTRANT     0

#endif
