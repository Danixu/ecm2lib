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
#include <cstring>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <math.h>

#include "ecm_enums.h"
#include "ecm_buffer.h"

#ifndef __ECM_H__
#define __ECM_H__
namespace ecm
{
    class processor
    {
    public:
        processor();

        sector_type detect(data_buffer<char> &input);

        status_code encode_stream(
            data_buffer<char> &input,
            data_buffer<char> &output,
            data_buffer<sector_type> &sectors_index,
            uint32_t input_sectors_number,
            uint32_t start_sector_number,
            optimizations &options,
            bool use_the_best_optimizations = true);

        status_code decode_stream(
            data_buffer<char> &input,
            data_buffer<char> &output,
            data_buffer<sector_type> &sectors_index,
            uint32_t input_sectors,
            uint32_t start_sector_number,
            optimizations options);

        status_code encode_sector(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);

        status_code decode_sector(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            uint32_t sector_number,
            optimizations options);

        std::vector<char> pack_header(data_buffer<sector_type> &index, uint8_t bytes_to_store_number = 3);
        ecm::data_buffer<sector_type> unpack_header(std::vector<char> &index, uint8_t bytes_to_store_number = 3);

        std::vector<char> inline sector_to_time(uint32_t sector_number);

        uint32_t time_to_sector(data_buffer<char> &input);

        static size_t get_encoded_sector_size(
            sector_type type,
            optimizations options);

    private:
        bool inline is_gap(
            char *sector,
            size_t length);
        optimizations check_optimizations(
            data_buffer<char> &input,
            uint32_t sector_number,
            optimizations options,
            sector_type type);
        static inline uint32_t get32lsb(const char *src);
        static inline void put32lsb(
            data_buffer<char> &output,
            uint32_t value);
        inline uint32_t edc_compute(
            const char *src,
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
        status_code encode_sector_cdda(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        // sector cleaner Mode 1
        status_code encode_sector_mode_1(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        // sector cleaner Mode 2
        status_code encode_sector_mode_2(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        // sector cleaner Mode 2 XA GAP
        status_code encode_sector_mode_2_xa_gap(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        // sector cleaner Mode 2 XA 1
        status_code encode_sector_mode_2_xa_1(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        // sector cleaner Mode 2 XA 1
        status_code encode_sector_mode_2_xa_2(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        // sector cleaner Unknown Mode
        status_code encode_sector_mode_X(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        // sector regenerator CDDA
        status_code decode_sector_cdda(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        //  sector regenerator Mode 1
        status_code decode_sector_mode_1(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        //  sector regenerator Mode 2
        status_code decode_sector_mode_2(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        //  sector regenerator Mode 2 XA GAP
        status_code decode_sector_mode_2_xa_gap(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        //  sector regenerator Mode 2 XA 1
        status_code decode_sector_mode_2_xa_1(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        //  sector regenerator Mode 2 XA 2
        status_code decode_sector_mode_2_xa_2(
            data_buffer<char> &input,
            data_buffer<char> &output,
            sector_type type,
            optimizations options);
        //  sector regenerator Unknown mode
        status_code decode_sector_mode_X(
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
#endif