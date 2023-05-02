#include <stdint.h>

#ifndef __ECM_ENUMS_H__
#define __ECM_ENUMS_H__
namespace ecm
{
    enum status_code
    {
        STATUS_ERROR_NO_ENOUGH_OUTPUT_BUFFER_SPACE = -4,
        STATUS_ERROR_NO_ENOUGH_OUTPUT_INDEX_SPACE,
        STATUS_ERROR_NO_ENOUGH_INPUT_DATA,
        STATUS_ERROR_NO_ENOUGH_SECTORS,
        STATUS_ERROR_WRONG_INDEX_DATA,
        STATUS_OK = 0
    };

    enum sector_type : uint8_t
    {
        ST_UNKNOWN = 0,
        ST_CDDA,
        ST_CDDA_GAP,
        ST_MODE1,
        ST_MODE1_GAP,
        ST_MODE1_RAW,
        ST_MODE2,
        ST_MODE2_GAP,
        ST_MODE2_XA_GAP, // Detected in some games. The sector contains the XA flags, but is fully zeroed including the EDC/ECC data, then will be detected as non GAP Mode2 sector
        ST_MODE2_XA1,
        ST_MODE2_XA1_GAP,
        ST_MODE2_XA2,
        ST_MODE2_XA2_GAP,
        ST_MODEX
    };

    enum stream_type : uint8_t
    {
        STRT_UNKNOWN = 0,
        STRT_AUDIO,
        STRT_DATA
    };

    enum optimizations : uint8_t
    {
        OO_NONE = 0,                       // Just copy the input. Surelly will not be used
        OO_REMOVE_SYNC = 1,                // Remove sync bytes (a.k.a first 12 bytes)
        OO_REMOVE_MSF = 1 << 1,            // Remove the MSF bytes
        OO_REMOVE_MODE = 1 << 2,           // Remove the MODE byte
        OO_REMOVE_BLANKS = 1 << 3,         // Remove the Mode 1 zeroed section of the sector
        OO_REMOVE_REDUNDANT_FLAG = 1 << 4, // Remove the redundant copy of FLAG bytes in Mode 2 XA sectors
        OO_REMOVE_ECC = 1 << 5,            // Remove the ECC
        OO_REMOVE_EDC = 1 << 6,            // Remove the EDC
        OO_REMOVE_GAP = 1 << 7             // If sector type is a GAP, remove the data

    };
    optimizations inline operator|(optimizations a, optimizations b)
    {
        return static_cast<optimizations>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }
}
#endif