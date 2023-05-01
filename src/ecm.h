/*******************************************************************************
 *
 * Created by Daniel Carrasco at https://www.electrosoftcloud.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the Mozilla Public License 2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Mozilla Public License 2.0 for more details.
 *
 * You should have received a copy of the Mozilla Public License 2.0
 * along with this program.  If not, see <https://www.mozilla.org/en-US/MPL/2.0/>.
 *
 ******************************************************************************/

//////////////////////////////////////////////////////////////////
//
// Sector Detector class
//
// This class allows to detect the sector type of a CD-ROM sector
// From now is only able to detect the following sectors types:
//   * CDDA: If the sector type is unrecognized, will be threated as raw (like CDDA)
//   * CDDA_GAP: As above sector type, unrecognized type. The difference is that GAP is zeroed

////////////////////////////////////////////////////////////////////////////////
//
// Sector types
//
// CDDA
// -----------------------------------------------------
//        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
// 0000h [---DATA...
// ...
// 0920h                                     ...DATA---]
// -----------------------------------------------------
//
// Mode 1
// -----------------------------------------------------
//        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
// 0000h 00 FF FF FF FF FF FF FF FF FF FF 00 [-MSF -] 01
// 0010h [---DATA...
// ...
// 0800h                                     ...DATA---]
// 0810h [---EDC---] 00 00 00 00 00 00 00 00 [---ECC...
// ...
// 0920h                                      ...ECC---]
// -----------------------------------------------------
//
// Mode 2: This mode is not widely used
// -----------------------------------------------------
//        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
// 0000h 00 FF FF FF FF FF FF FF FF FF FF 00 [-MSF -] 02
// 0010h [---DATA...
// ...
// 0920h                                     ...DATA---]
// -----------------------------------------------------
//
// Mode 2 (XA), form 1
// -----------------------------------------------------
//        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
// 0000h 00 FF FF FF FF FF FF FF FF FF FF 00 [-MSF -] 02
// 0010h [--FLAGS--] [--FLAGS--] [---DATA...
// ...
// 0810h             ...DATA---] [---EDC---] [---ECC...
// ...
// 0920h                                      ...ECC---]
// -----------------------------------------------------
//
// Mode 2 (XA), form 2
// -----------------------------------------------------
//        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
// 0000h 00 FF FF FF FF FF FF FF FF FF FF 00 [-MSF -] 02
// 0010h [--FLAGS--] [--FLAGS--] [---DATA...
// ...
// 0920h                         ...DATA---] [---EDC---]
// -----------------------------------------------------
//
// MSF:  Sector address, encoded as minutes:seconds:frames in BCD
// FLAGS: Used in Mode 2 (XA) sectors describing the type of sector; repeated
//        twice for redundancy
// DATA:  Area of the sector which contains the actual data itself
// EDC:   Error Detection Code
// ECC:   Error Correction Code
//
// First sector address looks like is always 00:02:00, so we will use this number on it

#include <cstddef>
#include <stdint.h>
#include <cstring>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "ecm_buffer.h"

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
        ST_MODE2_1,
        ST_MODE2_1_GAP,
        ST_MODE2_2,
        ST_MODE2_2_GAP,
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

    class processor
    {
    public:
        processor();

        sector_type detect(
            uint8_t *sector);

        status_code clean_stream(
            data_buffer<char> &input,
            data_buffer<char> &output,
            data_buffer<sector_type> &sectors_index,
            uint32_t input_sectors_number,
            uint32_t start_sector_number,
            optimizations &options,
            bool use_the_best_optimizations = true);

        status_code regenerate_stream(
            data_buffer<char> &input,
            data_buffer<char> &output,
            data_buffer<sector_type> &sectors_index,
            uint32_t input_sectors,
            uint32_t start_sector_number,
            optimizations options);

        status_code clean_sector(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);

        status_code regenerate_sector(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            uint32_t sector_number,
            optimizations options);

        std::vector<char> inline sector_to_time(uint32_t sector_number);

        uint32_t time_to_sector(std::vector<char> msf);

        static size_t get_encoded_sector_size(
            sector_type type,
            optimizations options);

    private:
        bool inline is_gap(
            uint8_t *sector,
            uint16_t length);
        optimizations check_optimizations(
            uint8_t *sector,
            uint32_t sector_number,
            optimizations options,
            sector_type type);
        static inline uint32_t get32lsb(
            const uint8_t *src);
        static inline void put32lsb(
            uint8_t *dest,
            uint32_t value);
        inline uint32_t edc_compute(
            uint32_t edc,
            const uint8_t *src,
            size_t size);
        int8_t ecc_checkpq(
            const uint8_t *address,
            const uint8_t *data,
            size_t major_count,
            size_t minor_count,
            size_t major_mult,
            size_t minor_inc,
            const uint8_t *ecc);
        int8_t ecc_check_sector(
            const uint8_t *address,
            const uint8_t *data,
            const uint8_t *ecc);
        void ecc_write_pq(
            const uint8_t *address,
            const uint8_t *data,
            size_t major_count,
            size_t minor_count,
            size_t major_mult,
            size_t minor_inc,
            uint8_t *ecc);
        void ecc_write_sector(
            const uint8_t *address,
            const uint8_t *data,
            uint8_t *ecc);

        // sector cleaner CDDA
        status_code clean_sector_cdda(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t &output_size,
            optimizations options);
        // sector cleaner Mode 1
        status_code clean_sector_mode_1(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t &output_size,
            optimizations options);
        // sector cleaner Mode 2
        status_code clean_sector_mode_2(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t &output_size,
            optimizations options);
        // sector cleaner Mode 2 XA 1
        status_code clean_sector_mode_2_xa_1(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t &output_size,
            optimizations options);
        // sector cleaner Mode 2 XA 1
        status_code clean_sector_mode_2_xa_2(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t &output_size,
            optimizations options);
        // sector cleaner Unknown Mode
        status_code clean_sector_mode_X(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t &output_size,
            optimizations options);
        // sector regenerator CDDA
        status_code regenerate_sector_cdda(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        //  sector regenerator Mode 1
        status_code regenerate_sector_mode_1(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        //  sector regenerator Mode 2
        status_code regenerate_sector_mode_2(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        //  sector regenerator Mode 2 XA 1
        status_code regenerate_sector_mode_2_xa_1(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        //  sector regenerator Mode 2 XA 2
        status_code regenerate_sector_mode_2_xa_2(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        //  sector regenerator Unknown mode
        status_code regenerate_sector_mode_X(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);

        // Private attributes
        //
        const uint8_t zeroaddress[4] = {0, 0, 0, 0};
        // LUTs used for computing ECC/EDC
        uint8_t ecc_f_lut[256];
        uint8_t ecc_b_lut[256];
        uint32_t edc_lut[256];
    };
}