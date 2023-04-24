
#pragma once

typedef enum tic_error_enum {
    TIC_OK = 0,
    TIC_ERR,
    TIC_ERR_APP_INIT,
    TIC_ERR_INVALID_CHAR,
    TIC_ERR_CHECKSUM,
    TIC_ERR_OVERFLOW,
    TIC_ERR_MEMORY,
    TIC_ERR_QUEUEFULL,
    TIC_ERR_UNKNOWN_DATA,
    TIC_ERR_MISSING_DATA,
    TIC_ERR_BAD_DATA,
    TIC_ERR_CONSOLE_BAD_CMD,
    TIC_ERR_NVS
} tic_error_t;

