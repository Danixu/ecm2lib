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
#include <stdio.h>
#include <string.h>
#include <vector>

namespace ecm
{
    enum return_code
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

    template <typename T>
    struct data_buffer
    {
        std::vector<T> buffer;
        size_t current_position = 0;

        data_buffer(size_t buffer_size = 0)
        {
            if (buffer_size > 0)
            {
                buffer.resize(buffer_size);
            }
        }

        T *get_current_data_position()
        {
            /* The current position is higher than the size of the buffer */
            if (current_position > buffer.size())
            {
                return nullptr;
            }

            return (T *)(buffer.data() + (current_position * sizeof(T)));
        }

        /**
         * @brief Get the items left in the buffer
         *
         * @return size_t The buffer items available in the buffer
         */
        size_t get_available_items()
        {
            if (current_position > buffer.size())
            {
                return 0;
            }
            else
            {
                return buffer.size() - current_position;
            }
        }

        /**
         * @brief Moves the data in the buffer vector from the source position to the destination one
         *
         * @param source (uint64_t) Source position of the data to be moved
         * @param destination (uint64_t) Destination position where the data will be moved
         * @param bytes_to_move (uint64_t) How much data to move
         * @param resize_buffer If true, the buffer will be resize if required on data move
         * @return true The data was moved sucessfully
         * @return false There was an error moving the data
         */
        inline int move_data(
            uint64_t source,
            uint64_t destination,
            uint64_t elements_to_move,
            bool resize_buffer = false)
        {
            /* Check if source and destination are equals */
            if (source == destination)
            {
                /* Really? */
                return -1;
            }
            /* Check the source position */
            if ((source + elements_to_move) > buffer.size())
            {
                /* The source data is out of bounds */
                return -2;
            }
            /* Check if a resize is required */
            if ((destination + elements_to_move) > buffer.size())
            {
                if (resize_buffer == false)
                {
                    // The buffer doesn't have enough space to move the data and the resize option is false
                    return -3;
                }
                else
                {
                    buffer.resize(destination + elements_to_move);
                }
            }

            /* Move the data checking the best way */
            if ((source + elements_to_move) >= destination)
            {
                /* The destination overlap the source from the end, so we will start moving from the end. */
                std::move_backward(buffer.data() + (source * sizeof(T)), buffer.data() + ((source + elements_to_move) * sizeof(T)), buffer.data() + (destination * sizeof(T)));
            }
            else
            {
                /* The source and destination doesn't overlap, or the overlapping happens from the start */
                std::move(buffer.data() + (source * sizeof(T)), buffer.data() + ((source + elements_to_move) * sizeof(T)), buffer.data() + (destination * sizeof(T)));
            }

            return 0;
        }
    };

    class processor
    {
    public:
        processor();

        sector_type detect(uint8_t *sector);

        int8_t cleanStream(
            data_buffer<char> &input,
            data_buffer<char> &output,
            data_buffer<sector_type> &sectorsIndex,
            uint32_t inputSectorsNumber,
            uint32_t startSectorNumber,
            optimizations &options,
            bool useTheBestOptimizations = true);

        int8_t regenerateStream(
            data_buffer<char> &input,
            data_buffer<char> &output,
            data_buffer<sector_type> &sectorsIndex,
            uint32_t inputSectors,
            uint32_t startSectorNumber,
            optimizations options);

        int8_t cleanSector(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t &output_size,
            optimizations options);

        int8_t regenerateSector(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint32_t sector_number,
            uint16_t &bytes_readed,
            optimizations options);

        void inline sectorToTime(
            uint8_t *out,
            uint32_t sectorNumber);

        uint32_t timeToSector(uint8_t *in);

        static int8_t getEncodedSectorSize(
            sector_type type,
            size_t &output_size,
            optimizations options);

    private:
        bool inline is_gap(uint8_t *sector, uint16_t length);
        optimizations checkOptimizations(uint8_t *sector, uint32_t sectorNumber, optimizations options, sector_type type);
        static inline uint32_t get32lsb(const uint8_t *src);
        static inline void put32lsb(uint8_t *dest, uint32_t value);
        inline uint32_t edcCompute(
            uint32_t edc,
            const uint8_t *src,
            size_t size);
        int8_t eccCheckpq(
            const uint8_t *address,
            const uint8_t *data,
            size_t major_count,
            size_t minor_count,
            size_t major_mult,
            size_t minor_inc,
            const uint8_t *ecc);
        int8_t eccChecksector(
            const uint8_t *address,
            const uint8_t *data,
            const uint8_t *ecc);
        void eccWritepq(
            const uint8_t *address,
            const uint8_t *data,
            size_t major_count,
            size_t minor_count,
            size_t major_mult,
            size_t minor_inc,
            uint8_t *ecc);
        void eccWritesector(
            const uint8_t *address,
            const uint8_t *data,
            uint8_t *ecc);

        // sector cleaner CDDA
        int8_t cleanSectorCDDA(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t &output_size,
            optimizations options);
        // sector cleaner Mode 1
        int8_t cleanSectorMode1(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t &output_size,
            optimizations options);
        // sector cleaner Mode 2
        int8_t cleanSectorMode2(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t &output_size,
            optimizations options);
        // sector cleaner Mode 2 XA 1
        int8_t cleanSectorMode2XA1(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t &output_size,
            optimizations options);
        // sector cleaner Mode 2 XA 1
        int8_t cleanSectorMode2XA2(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t &output_size,
            optimizations options);
        // sector cleaner Unknown Mode
        int8_t cleanSectorModeX(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t &output_size,
            optimizations options);
        // sector regenerator CDDA
        int8_t regenerateSectorCDDA(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t current_pos,
            uint16_t &bytes_readed,
            optimizations options);
        //  sector regenerator Mode 1
        int8_t regenerateSectorMode1(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t current_pos,
            uint16_t &bytes_readed,
            optimizations options);
        //  sector regenerator Mode 2
        int8_t regenerateSectorMode2(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t current_pos,
            uint16_t &bytes_readed,
            optimizations options);
        //  sector regenerator Mode 2 XA 1
        int8_t regenerateSectorMode2XA1(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t current_pos,
            uint16_t &bytes_readed,
            optimizations options);
        //  sector regenerator Mode 2 XA 2
        int8_t regenerateSectorMode2XA2(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t current_pos,
            uint16_t &bytes_readed,
            optimizations options);
        //  sector regenerator Unknown mode
        int8_t regenerateSectorModeX(
            uint8_t *out,
            uint8_t *sector,
            sector_type type,
            uint16_t current_pos,
            uint16_t &bytes_readed,
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