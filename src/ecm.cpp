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

#include "ecm.h"

namespace ecm
{
    /**
     * @brief Construct a new processor::processor object
     *
     */
    processor::processor()
    {
        size_t i;
        for (i = 0; i < 256; i++)
        {
            uint32_t edc = i;
            size_t j = (i << 1) ^ (i & 0x80 ? 0x11D : 0);
            ecc_f_lut[i] = j;
            ecc_b_lut[i ^ j] = i;
            for (j = 0; j < 8; j++)
            {
                edc = (edc >> 1) ^ (edc & 1 ? 0xD8018001 : 0);
            }
            edc_lut[i] = edc;
        }
    }

    /**
     * @brief Removes the non critical data from all the sectors in a Stream (ECM Encoding). It requires full sectors or will fail.
     *
     * @param out (uint8_t*) Output buffer where the data will be stored.
     * @param outSize (size_t) Size of the output buffer. This variable will be updated with the space left in the buffer after the operation.
     * @param in (uint8_t*) Input buffer with the sectors to optimize.
     * @param inSize (size_t) Size of the input buffer.
     * @param startSectorNumber (uint32_t) Sector number of the first sector in the stream, required to decode the MSF optimization.
     * @param options (optimizations) Optimizations to use on sectors optimizations
     * @param sectorsIndex
     * @param sectorsIndexSize
     * @param useTheBestOptimizations (bool) Check if the data integrity of the sectors can be maintained with the desired optimizations. If not, the "options" argument will be updated with the best optimizations for the stream.
     * @return int8_t int8_t Returns 0 if everything was OK. Otherwise a negative number will be returned.
     */
    status_code processor::encode_stream(
        data_buffer<char> &input,
        data_buffer<char> &output,
        data_buffer<sector_type> &sectorsIndex,
        uint32_t inputSectorsNumber,
        uint32_t startSectorNumber,
        optimizations &options,
        bool useTheBestOptimizations)
    {
        /* The input size doesn't fit in a full sectors size */
        if (input.buffer.size() < (inputSectorsNumber * 2352))
        {
            return STATUS_ERROR_NO_ENOUGH_INPUT_DATA;
        }

        /* Check if the index buffer have enough space and is not null */
        if (sectorsIndex.get_available_items() < inputSectorsNumber)
        {
            return STATUS_ERROR_NO_ENOUGH_OUTPUT_INDEX_SPACE;
        }

        /* Try to encode the sectors one by one to test if they can be lossly recovered using the current optimizations.
           If not, detect the optimization which has caused the error and deactivate it */
        for (uint32_t i = 0; i < inputSectorsNumber; i++)
        {
            /* Try to detect the sector type */
            sectorsIndex[i] = detect(input);

            if (useTheBestOptimizations)
            {
                /* Call the function which will determine if those optimizations are the best for that sector */
                options = check_optimizations(input, startSectorNumber + i, options, sectorsIndex[i]);
            }
            input.current_position += 2352;
        }

        /* Go back to the start point after analizing */
        input.revert_current_position();

        /* Do a fast calculation to see if the stream fits the output buffer. Otherwise, return an error */
        size_t outputCalculatedSize = 0;
        for (uint32_t i = 0; i < inputSectorsNumber; i++)
        {
            size_t blockCalculatedSize = get_encoded_sector_size(sectorsIndex[i], options);
            outputCalculatedSize += blockCalculatedSize;
        }

        if (outputCalculatedSize > output.get_available_items())
        {
            return STATUS_ERROR_NO_ENOUGH_OUTPUT_BUFFER_SPACE;
        }

        /* Optimize the stream into the output buffer */
        for (uint32_t i = 0; i < inputSectorsNumber; i++)
        {
            encode_sector(input, output, *sectorsIndex.get_current_data_position(), options);
            sectorsIndex.current_position++;
        }

        return STATUS_OK;
    }

    status_code processor::decode_stream(
        data_buffer<char> &input,
        data_buffer<char> &output,
        data_buffer<sector_type> &sectorsIndex,
        uint32_t inputSectorsNumber,
        uint32_t startSectorNumber,
        optimizations options)
    {
        /* Check if the index buffer is not empty */
        if (sectorsIndex.buffer.size() == 0)
        {
            return STATUS_ERROR_WRONG_INDEX_DATA;
        }

        /* Check if there is enough space into the output buffer */
        if ((inputSectorsNumber * 2352) > output.buffer.size())
        {
            return STATUS_ERROR_NO_ENOUGH_OUTPUT_BUFFER_SPACE;
        }

        /* Do a fast calculation to see if the input stream fits the required data size. Otherwise, return an error */
        size_t inputCalculatedSize = 0;
        for (uint32_t i = 0; i < inputSectorsNumber; i++)
        {
            size_t blockCalculatedSize = get_encoded_sector_size(sectorsIndex[i], options);
            inputCalculatedSize += blockCalculatedSize;
        }

        if (inputCalculatedSize > input.get_available_items())
        {
            return STATUS_ERROR_NO_ENOUGH_INPUT_DATA;
        }

        /* Start to decode every sector and place it in the output buffer */
        for (uint32_t i = 0; i < inputSectorsNumber; i++)
        {
            decode_sector(input, output, sectorsIndex[i], startSectorNumber + i, options);
        }

        sectorsIndex.current_position += inputSectorsNumber;

        return STATUS_OK;
    }

    /**
     * @brief Removes the non critical data from a sector (ECM encoding)
     *
     * @param out (uint8_t*) Output buffer where the data will be stored.
     * @param sector (uint8_t*) Input buffer with the sector data to encode
     * @param type (sector_type) Sector type to be processed
     * @param outputSize (uint16_t) Size of the sector in the output buffer
     * @param options (optimizations) Optimizations that will be used in this sector
     * @return int8_t Returns 0 if everything was Ok, otherwise will return a negative number.
     */
    status_code processor::encode_sector(
        data_buffer<char> &input,
        data_buffer<char> &output,
        sector_type type,
        optimizations options)
    {
        switch (type)
        {
        case ST_CDDA:
        case ST_CDDA_GAP:
            return encode_sector_cdda(input, output, type, options);
            break;

        case ST_MODE1:
        case ST_MODE1_GAP:
        case ST_MODE1_RAW:
            return encode_sector_mode_1(input, output, type, options);
            break;

        case ST_MODE2:
        case ST_MODE2_GAP:
            return encode_sector_mode_2(input, output, type, options);
            break;

        case ST_MODE2_XA_GAP:
            return encode_sector_mode_2_xa_gap(input, output, type, options);
            break;

        case ST_MODE2_XA1:
        case ST_MODE2_XA1_GAP:
            return encode_sector_mode_2_xa_1(input, output, type, options);
            break;

        case ST_MODE2_XA2:
        case ST_MODE2_XA2_GAP:
            return encode_sector_mode_2_xa_2(input, output, type, options);
            break;

        case ST_MODEX:
            return encode_sector_mode_X(input, output, type, options);
            break;
        }

        return STATUS_UNKNOWN_ERROR;
    }

    /**
     * @brief
     *
     * @param input
     * @param output
     * @param type
     * @param sector_number
     * @param options
     * @return status_code
     */
    status_code processor::decode_sector(
        data_buffer<char> &input,
        data_buffer<char> &output,
        sector_type type,
        uint32_t sector_number,
        optimizations options)
    {
        // sync and address bytes in data sectors, common to almost all types
        if (type >= ST_MODE1)
        {
            // SYNC bytes
            if (!(options & OO_REMOVE_SYNC))
            {
                std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x0C);
                input.current_position += 0x0C;
            }
            else
            {
                output.get_current_data_position()[0] = 0x00;
                std::memset(output.get_current_data_position() + 1, 0xFF, 0x0A);
                output.get_current_data_position()[11] = 0x00;
            }
            output.current_position += 0x0C;
            // Address bytes
            if (!(options & OO_REMOVE_MSF))
            {
                std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x03);
                input.current_position += 0x03;
            }
            else
            {
                std::vector<char> msf = sector_to_time(sector_number);
                output.write(msf);
            }
            output.current_position += 0x03;
        }

        // The rest of the sector
        switch (type)
        {
        case ST_CDDA:
        case ST_CDDA_GAP:
            return decode_sector_cdda(input, output, type, options);

        case ST_MODE1:
        case ST_MODE1_GAP:
        case ST_MODE1_RAW:
            return decode_sector_mode_1(input, output, type, options);

        case ST_MODE2:
        case ST_MODE2_GAP:
            return decode_sector_mode_2(input, output, type, options);

        case ST_MODE2_XA_GAP:
            return decode_sector_mode_2_xa_gap(input, output, type, options);

        case ST_MODE2_XA1:
        case ST_MODE2_XA1_GAP:
            return decode_sector_mode_2_xa_1(input, output, type, options);

        case ST_MODE2_XA2:
        case ST_MODE2_XA2_GAP:
            return decode_sector_mode_2_xa_2(input, output, type, options);

        case ST_MODEX:
            return decode_sector_mode_X(input, output, type, options);
        }

        return STATUS_OK;
    }

    /**
     * @brief Pack the "One sector - One byte" header into a Type + Counter style header, which will do a noticeable size reduction which will depend of the types mixture.
     *
     * @param index One sector - One byte vector containing the index of all the sectors in the stream
     * @param bytes_for_counter Bytes reserved for the counter of sectors of every type. A lower number normally will reduce the header size, but depends of the types mixture. Max recommended 3, and max accepted 4.
     * @return std::vector<char> Returns the header stream containig the same header info but packed
     */
    std::vector<char> processor::pack_header(data_buffer<sector_type> &index, uint8_t bytes_for_counter)
    {
        std::vector<char> packed_header;

        /* to have more than 4 bytes for the counter is a big waste (even 4 is a waste), and less than 1 is not possible */
        if (bytes_for_counter > 4)
        {
            bytes_for_counter = 4;
        }
        else if (bytes_for_counter < 1)
        {
            bytes_for_counter = 1;
        }

        sector_type current = ST_UNKNOWN;
        size_t count = 0;
        size_t max_count = pow(256, bytes_for_counter) - 1;

        for (size_t i = 0; i < index.buffer.size(); i++)
        {
            /* If not sector was set (first iteration), update it */
            if (current == ST_UNKNOWN)
            {
                current = index[i];
            }

            /* Last iteration so must be pushed to the vector */
            if (count > 0 &&
                ((i + 1) == index.buffer.size() || count == max_count || current != index[i]))
            {
                /* Get current size and position, and increase the size by 4 chars (bytes) */
                size_t current_header_size = packed_header.size();
                packed_header.resize(current_header_size + bytes_for_counter + 1);
                /* Store the current type and 3 bytes of the uint32_t variable */
                packed_header.data()[current_header_size] = current;
                memcpy(packed_header.data() + current_header_size + 1, &count, bytes_for_counter);

                current = index[i];
                count = 0;
            }
            count++;
        }

        return packed_header;
    }

    /**
     * @brief Unpack the data packed by ultrapack_header function, returning it back into a data_buffer<sector_type> vector.
     *
     * @param index Packed Index data vector in a char format
     * @param bytes_for_counter Bytes that were used to store the counter of every type. Must match the used in the pack_header function or the header will be corrupted.
     * @return data_buffer<sector_type> The unpacked data in "One sector - One byte" mode, stored in a data_buffer<sector_type> format
     */
    data_buffer<sector_type> processor::unpack_header(std::vector<char> &index, uint8_t bytes_for_counter)
    {
        data_buffer<sector_type> unpacked_header;

        /* Go to the compacted header one by one */
        for (size_t i = 0; i < index.size(); i += (bytes_for_counter + 1))
        {
            sector_type type = (sector_type)index.data()[i];
            size_t count = 0;

            memcpy(&count, index.data() + (i + 1), bytes_for_counter);

            /* Append one entry by sector */
            for (uint32_t j = 0; j < count; j++)
            {
                unpacked_header.buffer.push_back(type);
            }
        }

        return unpacked_header;
    }

    /**
     * @brief Detects if the sector is a gap or contains data
     *
     * @param sector (uint8_t*) Stream containing all the bytes which will be compared
     * @param length (uint16_t) Length of the stream to check
     * @return true The sectors are a gap
     * @return false Any or all sectors are not zeroed, so is not a gap.
     */
    bool inline processor::is_gap(char *sector, size_t length)
    {
        for (size_t i = 0; i < length; i++)
        {
            if ((sector[i]) != 0x00)
            {
                return false; // Sector contains data, so is not a GAP
            }
        }

        return true;
    }

    sector_type processor::detect(data_buffer<char> &input)
    {
        if (
            input[0x000] == (char)0x00 && // sync (12 bytes)
            input[0x001] == (char)0xFF &&
            input[0x002] == (char)0xFF &&
            input[0x003] == (char)0xFF &&
            input[0x004] == (char)0xFF &&
            input[0x005] == (char)0xFF &&
            input[0x006] == (char)0xFF &&
            input[0x007] == (char)0xFF &&
            input[0x008] == (char)0xFF &&
            input[0x009] == (char)0xFF &&
            input[0x00A] == (char)0xFF &&
            input[0x00B] == (char)0x00)
        {
            // Sector is a MODE1/MODE2 sector
            if (
                input[0x00F] == (char)0x01 && // mode (1 byte)
                input[0x814] == (char)0x00 && // reserved (8 bytes)
                input[0x815] == (char)0x00 &&
                input[0x816] == (char)0x00 &&
                input[0x817] == (char)0x00 &&
                input[0x818] == (char)0x00 &&
                input[0x819] == (char)0x00 &&
                input[0x81A] == (char)0x00 &&
                input[0x81B] == (char)0x00)
            {
                //  The sector is surelly MODE1 but we will check the EDC
                if (
                    ecc_check_sector(
                        reinterpret_cast<uint8_t *>(input + 0xC),
                        reinterpret_cast<uint8_t *>(input + 0x10),
                        reinterpret_cast<uint8_t *>(input + 0x81C)) &&
                    edc_compute(input.get_current_data_position(), 0x810) == get32lsb((input + 0x810)))
                {
                    if (is_gap((input + 0x10), 0x800))
                    {
                        return ST_MODE1_GAP;
                    }
                    else
                    {
                        return ST_MODE1; // Mode 1
                    }
                }

                // If EDC doesn't match, then the sector is damaged. It can be a protection method, so will be threated as RAW.
                return ST_MODE1_RAW;
            }
            else if (
                input[0x00F] == (char)0x02 // mode (1 byte)
            )
            {
                //  The sector is MODE2, and now we will detect what kind
                //
                /* First we will check if the sector is a gap, because can be confused with a ST_MODE2_1_GAP */
                if (is_gap((input + 0x10), 0x920))
                {
                    return ST_MODE2_GAP;
                }

                // Might be Mode 2, Form 1
                //
                if (
                    ecc_check_sector(zeroaddress, reinterpret_cast<uint8_t *>(input + 0x10), reinterpret_cast<uint8_t *>(input + 0x81C)) &&
                    edc_compute((input + 0x10), 0x808) == get32lsb((input + 0x818)))
                {
                    if (is_gap((input + 0x18), 0x800))
                    {
                        return ST_MODE2_XA1_GAP;
                    }
                    else
                    {
                        return ST_MODE2_XA1; //  Mode 2, Form 1
                    }
                }
                //
                // Might be Mode 2, Form 2
                //
                if (
                    edc_compute((input + 0x10), 0x91C) == get32lsb((input + 0x92C)))
                {
                    if (is_gap((input + 0x18), 0x914))
                    {
                        return ST_MODE2_XA2_GAP;
                    }
                    else
                    {
                        return ST_MODE2_XA2; // Mode 2, Form 2
                    }
                }
                //
                // Maybe is an strange Mode2 full gap sector
                //
                if (input[0x10] == input[0x14] &&
                    input[0x11] == input[0x15] &&
                    input[0x12] == input[0x16] &&
                    input[0x13] == input[0x17] &&
                    is_gap((input + 0x18), 0x918))
                {
                    // Sector is an XA sector, but doesn't match the standard of EDC/ECC and is fully zeroed
                    return ST_MODE2_XA_GAP;
                }

                /* If doesn't fit in any of the above modes, then should be just a MODE2 sector which is full of data */
                return ST_MODE2;
            }

            // Data sector detected but was not possible to determine the mode. Maybe is a copy protection sector.
            return ST_MODEX;
        }
        else
        {
            // Sector is not recognized, so might be a CDDA sector
            if (is_gap(input.get_current_data_position(), 0x930))
            {
                return ST_CDDA_GAP;
            }
            else
            {
                return ST_CDDA;
            }
        }

        return ST_UNKNOWN;
    }

    optimizations processor::check_optimizations(
        data_buffer<char> &input,
        uint32_t sectorNumber,
        optimizations options,
        sector_type sectorType)
    {
        if (sectorType == ST_CDDA_GAP || sectorType == ST_CDDA)
        {
            // Audio is a raw sector which will never be optimized, so global optimizations must not change.
            return options;
        }

        if (sectorType == ST_UNKNOWN)
        {
            // Unknown sector will not be optimized, so the options will not be affected
            return options;
        }

        optimizations newOptions = options;

        /* Common data optimizations check */
        if (newOptions & OO_REMOVE_MSF && (sectorType == ST_MODEX ||
                                           sectorType == ST_MODE1 ||
                                           sectorType == ST_MODE1_RAW ||
                                           sectorType == ST_MODE1_GAP ||
                                           sectorType == ST_MODE2 ||
                                           sectorType == ST_MODE2_XA_GAP ||
                                           sectorType == ST_MODE2_XA1 ||
                                           sectorType == ST_MODE2_XA1_GAP ||
                                           sectorType == ST_MODE2_XA2 ||
                                           sectorType == ST_MODE2_XA2_GAP))
        {
            /* The sync part is always used to detect the sector, so always will be OK */
            /* We will chech the MSF part */
            std::vector<char> generatedMSF = sector_to_time(sectorNumber);

            if (input[0x0C] != generatedMSF[0] ||
                input[0x0D] != generatedMSF[1] ||
                input[0x0E] != generatedMSF[2])
            {
                newOptions = (optimizations)(newOptions & (~OO_REMOVE_MSF));
            }
        }

        /* Mode 2 optimizations check */
        if (sectorType == ST_MODE2_XA_GAP ||
            sectorType == ST_MODE2_XA1 ||
            sectorType == ST_MODE2_XA1_GAP ||
            sectorType == ST_MODE2_XA2 ||
            sectorType == ST_MODE2_XA2_GAP)
        {
            /* This mode is detected using the EDC and ECC, so will not be checked */
            if (newOptions & OO_REMOVE_REDUNDANT_FLAG &&
                (input[0x10] != input[0x14] ||
                 input[0x11] != input[0x15] ||
                 input[0x12] != input[0x16] ||
                 input[0x13] != input[0x17]))
            {
                newOptions = (optimizations)(newOptions & (~OO_REMOVE_REDUNDANT_FLAG));
            }
        }

        /* Return the optimizations before all checks */
        return newOptions;
    }

    inline uint32_t processor::get32lsb(const char *src)
    {
        return (uint32_t)(static_cast<uint8_t>(src[0]) << 0 |
                          static_cast<uint8_t>(src[1]) << 8 |
                          static_cast<uint8_t>(src[2]) << 16 |
                          static_cast<uint8_t>(src[3]) << 24);
    }

    inline void processor::put32lsb(data_buffer<char> &output, uint32_t value)
    {
        output[0] = (char)(value);
        output[1] = (char)(value >> 8);
        output[2] = (char)(value >> 16);
        output[3] = (char)(value >> 24);
    }

    inline uint32_t processor::edc_compute(
        const char *src,
        size_t size)
    {
        uint32_t edc = 0;
        for (; size; size--)
        {
            edc = (edc >> 8) ^ edc_lut[(edc ^ (*src++)) & 0xFF];
        }
        return edc;
    }

    int8_t processor::ecc_checkpq(
        const uint8_t *address,
        const uint8_t *data,
        size_t majorCount,
        size_t minorCount,
        size_t majorMult,
        size_t minorInc,
        const uint8_t *ecc)
    {
        size_t size = majorCount * minorCount;
        size_t major;
        for (major = 0; major < majorCount; major++)
        {
            size_t index = (major >> 1) * majorMult + (major & 1);
            uint8_t ecc_a = 0;
            uint8_t ecc_b = 0;
            size_t minor;
            for (minor = 0; minor < minorCount; minor++)
            {
                uint8_t temp;
                if (index < 4)
                {
                    temp = address[index];
                }
                else
                {
                    temp = data[index - 4];
                }
                index += minorInc;
                if (index >= size)
                {
                    index -= size;
                }
                ecc_a ^= temp;
                ecc_b ^= temp;
                ecc_a = ecc_f_lut[ecc_a];
            }
            ecc_a = ecc_b_lut[ecc_f_lut[ecc_a] ^ ecc_b];
            if (
                ecc[major] != (ecc_a) ||
                ecc[major + majorCount] != (ecc_a ^ ecc_b))
            {
                return 0;
            }
        }
        return 1;
    }

    void processor::ecc_write_pq(
        const uint8_t *address,
        const uint8_t *data,
        size_t majorCount,
        size_t minorCount,
        size_t majorMult,
        size_t minorInc,
        uint8_t *ecc)
    {
        size_t size = majorCount * minorCount;
        size_t major;
        for (major = 0; major < majorCount; major++)
        {
            size_t index = (major >> 1) * majorMult + (major & 1);
            uint8_t ecc_a = 0;
            uint8_t ecc_b = 0;
            size_t minor;
            for (minor = 0; minor < minorCount; minor++)
            {
                uint8_t temp;
                if (index < 4)
                {
                    temp = address[index];
                }
                else
                {
                    temp = data[index - 4];
                }
                index += minorInc;
                if (index >= size)
                {
                    index -= size;
                }
                ecc_a ^= temp;
                ecc_b ^= temp;
                ecc_a = ecc_f_lut[ecc_a];
            }
            ecc_a = ecc_b_lut[ecc_f_lut[ecc_a] ^ ecc_b];
            ecc[major] = (ecc_a);
            ecc[major + majorCount] = (ecc_a ^ ecc_b);
        }
    }

    int8_t processor::ecc_check_sector(
        const uint8_t *address,
        const uint8_t *data,
        const uint8_t *ecc)
    {
        return ecc_checkpq(address, data, 86, 24, 2, 86, ecc) &&       // P
               ecc_checkpq(address, data, 52, 43, 86, 88, ecc + 0xAC); // Q
    }

    void processor::ecc_write_sector(
        const uint8_t *address,
        const uint8_t *data,
        uint8_t *ecc)
    {
        ecc_write_pq(address, data, 86, 24, 2, 86, ecc);         // P
        ecc_write_pq(address, data, 52, 43, 86, 88, ecc + 0xAC); // Q
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    status_code processor::encode_sector_cdda(
        data_buffer<char> &input,
        data_buffer<char> &output,
        sector_type type,
        optimizations options)
    {
        /* CDDA are raw, so no optimizations can be applied */
        if (type == ST_CDDA || !(options & OO_REMOVE_GAP))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 2352);
            output.current_position += 2352;
        }
        input.current_position += 2352;
        input.update_start_position();
        output.update_start_position();

        return STATUS_OK;
    }

    // Mode 1
    status_code processor::encode_sector_mode_1(
        data_buffer<char> &input,
        data_buffer<char> &output,
        sector_type type,
        optimizations options)
    {
        // SYNC bytes
        if (!(options & OO_REMOVE_SYNC))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x0C);
            output.current_position += 0x0C;
        }
        input.current_position += 0x0C;
        // Address bytes
        if (!(options & OO_REMOVE_MSF))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x03);
            output.current_position += 0x03;
        }
        input.current_position += 0x03;
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x01);
            output.current_position += 0x01;
        }
        input.current_position++;
        // Data bytes
        if (type == ST_MODE1 || type == ST_MODE1_RAW || !(options & OO_REMOVE_GAP))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x800);
            output.current_position += 0x800;
        }
        input.current_position += 0x800;
        // EDC bytes
        if (!(options & OO_REMOVE_EDC) || type == ST_MODE1_RAW)
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x04);
            output.current_position += 0x04;
        }
        input.current_position += 0x04;
        // Zeroed bytes
        if (!(options & OO_REMOVE_BLANKS))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x08);
            output.current_position += 0x08;
        }
        input.current_position += 0x08;
        // ECC bytes
        if (!(options & OO_REMOVE_ECC) || type == ST_MODE1_RAW)
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x114);
            output.current_position += 0x114;
        }
        input.current_position += 0x114;
        input.update_start_position();
        output.update_start_position();

        return STATUS_OK;
    }

    // Mode 2
    status_code processor::encode_sector_mode_2(
        data_buffer<char> &input,
        data_buffer<char> &output,
        sector_type type,
        optimizations options)
    {
        // SYNC bytes
        if (!(options & OO_REMOVE_SYNC))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x0C);
            output.current_position += 0x0C;
        }
        input.current_position += 0x0C;
        // Address bytes
        if (!(options & OO_REMOVE_MSF))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x03);
            output.current_position += 0x03;
        }
        input.current_position += 0x03;
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x01);
            output.current_position++;
        }
        input.current_position++;
        // Data bytes
        if (type == ST_MODE2 || !(options & OO_REMOVE_GAP))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x920);
            output.current_position += 0x920;
        }
        input.current_position += 0x920;
        input.update_start_position();
        output.update_start_position();

        return STATUS_OK;
    }

    // Mode 2 XA GAP
    status_code processor::encode_sector_mode_2_xa_gap(
        data_buffer<char> &input,
        data_buffer<char> &output,
        sector_type type,
        optimizations options)
    {
        // SYNC bytes
        if (!(options & OO_REMOVE_SYNC))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x0C);
            output.current_position += 0x0C;
        }
        input.current_position += 0x0C;
        // Address bytes
        if (!(options & OO_REMOVE_MSF))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x03);
            output.current_position += 0x03;
        }
        input.current_position += 0x03;
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x01);
            output.current_position++;
        }
        input.current_position++;
        // Flags bytes
        if (!(options & OO_REMOVE_REDUNDANT_FLAG))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x08);
            output.current_position += 0x08;
        }
        else
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x04);
            output.current_position += 0x04;
        }
        input.current_position += 0x08;
        // GAP bytes
        if (!(options & OO_REMOVE_GAP))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x918);
            output.current_position += 0x918;
        }
        input.current_position += 0x918;
        input.update_start_position();
        output.update_start_position();

        return STATUS_OK;
    }

    // Mode 2 XA 1
    status_code processor::encode_sector_mode_2_xa_1(
        data_buffer<char> &input,
        data_buffer<char> &output,
        sector_type type,
        optimizations options)
    {
        // SYNC bytes
        if (!(options & OO_REMOVE_SYNC))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x0C);
            output.current_position += 0x0C;
        }
        input.current_position += 0x0C;
        // Address bytes
        if (!(options & OO_REMOVE_MSF))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x03);
            output.current_position += 0x03;
        }
        input.current_position += 0x03;
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x01);
            output.current_position++;
        }
        input.current_position++;
        // Flags bytes
        if (!(options & OO_REMOVE_REDUNDANT_FLAG))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x08);
            output.current_position += 0x08;
        }
        else
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x04);
            output.current_position += 0x04;
        }
        input.current_position += 0x08;
        // Data bytes
        if (type == ST_MODE2_XA1 || !(options & OO_REMOVE_GAP))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x800);
            output.current_position += 0x800;
        }
        input.current_position += 0x800;
        // EDC bytes
        if (!(options & OO_REMOVE_EDC))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x04);
            output.current_position += 0x04;
        }
        input.current_position += 0x04;
        // ECC bytes
        if (!(options & OO_REMOVE_ECC))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x114);
            output.current_position += 0x114;
        }
        input.current_position += 0x114;
        input.update_start_position();
        output.update_start_position();

        return STATUS_OK;
    }

    // Mode 2 XA 1
    status_code processor::encode_sector_mode_2_xa_2(
        data_buffer<char> &input,
        data_buffer<char> &output,
        sector_type type,
        optimizations options)
    {
        // SYNC bytes
        if (!(options & OO_REMOVE_SYNC))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x0C);
            output.current_position += 0x0C;
        }
        input.current_position += 0x0C;
        // Address bytes
        if (!(options & OO_REMOVE_MSF))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x03);
            output.current_position += 0x03;
        }
        input.current_position += 0x03;
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x01);
            output.current_position++;
        }
        input.current_position++;
        // Flags bytes
        if (!(options & OO_REMOVE_REDUNDANT_FLAG))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x08);
            output.current_position += 0x08;
        }
        else
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x04);
            output.current_position += 0x04;
        }
        input.current_position += 0x08;
        // Data bytes
        if (type == ST_MODE2_XA2 || !(options & OO_REMOVE_GAP))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x914);
            output.current_position += 0x914;
        }
        input.current_position += 0x914;
        // EDC bytes
        if (!(options & OO_REMOVE_EDC))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x04);
            output.current_position += 0x04;
        }
        input.current_position += 0x04;
        input.update_start_position();
        output.update_start_position();

        return STATUS_OK;
    }

    // Unknown data mode
    status_code processor::encode_sector_mode_X(
        data_buffer<char> &input,
        data_buffer<char> &output,
        sector_type type,
        optimizations options)
    {
        // SYNC bytes
        if (!(options & OO_REMOVE_SYNC))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x0C);
            output.current_position += 0x0C;
        }
        input.current_position += 0x0C;
        // Address bytes
        if (!(options & OO_REMOVE_MSF))
        {
            memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x03);
            output.current_position += 0x03;
        }
        input.current_position += 0x03;
        // Rest of bytes
        memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x921);
        output.current_position += 0x921;
        input.current_position += 0x921;
        input.update_start_position();
        output.update_start_position();

        return STATUS_OK;
    }

    status_code processor::decode_sector_cdda(
        data_buffer<char> &input,
        data_buffer<char> &output,
        sector_type type,
        optimizations options)
    {
        // CDDA are directly copied
        if (type == ST_CDDA || !(options & OO_REMOVE_GAP))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 2352);
            input.current_position += 2352;
        }
        else
        {
            std::memset(output.get_current_data_position(), 0x00, 2352);
        }
        output.current_position += 2352;
        output.update_start_position();

        return STATUS_OK;
    }

    // Mode 1
    status_code processor::decode_sector_mode_1(
        data_buffer<char> &input,
        data_buffer<char> &output,
        sector_type type,
        optimizations options)
    {
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x01);
            input.current_position += 0x01;
        }
        else
        {
            *output.get_current_data_position() = 0x01;
        }
        output.current_position += 0x01;
        // Data bytes
        if (type == ST_MODE1 || type == ST_MODE1_RAW || !(options & OO_REMOVE_GAP))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x800);
            input.current_position += 0x800;
        }
        else
        {
            std::memset(output.get_current_data_position(), 0x00, 0x800);
        }
        output.current_position += 0x800;
        // EDC bytes
        if (!(options & OO_REMOVE_EDC) || type == ST_MODE1_RAW)
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x04);
            input.current_position += 0x04;
        }
        else
        {
            put32lsb(output, edc_compute(output.get_start_data_position(), 0x810));
        }
        output.current_position += 0x04;
        // Zeroed bytes
        if (!(options & OO_REMOVE_BLANKS))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x08);
            input.current_position += 0x08;
        }
        else
        {
            std::memset(output.get_current_data_position(), 0x00, 0x08);
        }
        output.current_position += 0x08;
        // ECC bytes
        if (!(options & OO_REMOVE_ECC) || type == ST_MODE1_RAW)
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x114);
            input.current_position += 0x114;
        }
        else
        {
            ecc_write_sector(reinterpret_cast<uint8_t *>(output.get_start_data_position() + 0xC), reinterpret_cast<uint8_t *>(output.get_start_data_position() + 0x10), reinterpret_cast<uint8_t *>(output.get_current_data_position()));
        }
        output.current_position += 0x114;
        output.update_start_position();

        return STATUS_OK;
    }

    // Mode 2
    status_code processor::decode_sector_mode_2(
        data_buffer<char> &input,
        data_buffer<char> &output,
        sector_type type,
        optimizations options)
    {
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x01);
            input.current_position += 0x01;
        }
        else
        {
            *output.get_current_data_position() = 0x02;
        }
        output.current_position += 0x01;
        // Data bytes
        if (type == ST_MODE2 || !(options & OO_REMOVE_GAP))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x920);
            input.current_position += 0x920;
        }
        else
        {
            memset(output.get_current_data_position(), 0x00, 0x920);
        }
        output.current_position += 0x920;
        output.update_start_position();

        return STATUS_OK;
    }

    // Mode 2 XA GAP
    status_code processor::decode_sector_mode_2_xa_gap(
        data_buffer<char> &input,
        data_buffer<char> &output,
        sector_type type,
        optimizations options)
    {
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x01);
            input.current_position += 0x01;
        }
        else
        {
            *output.get_current_data_position() = 0x02;
        }
        output.current_position += 0x01;
        // Flags bytes
        if (!(options & OO_REMOVE_REDUNDANT_FLAG))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x08);
            input.current_position += 0x08;
        }
        else
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x04);
            std::memcpy(output + 0x04, input.get_current_data_position(), 0x04);
            input.current_position += 0x04;
        }
        output.current_position += 0x08;
        // GAP bytes
        if (!(options & OO_REMOVE_GAP))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x918);
            input.current_position += 0x918;
        }
        else
        {
            std::memset(output.get_current_data_position(), 0x00, 0x918);
        }
        output.current_position += 0x918;
        output.update_start_position();

        return STATUS_OK;
    }

    // Mode 2 XA 1
    status_code processor::decode_sector_mode_2_xa_1(
        data_buffer<char> &input,
        data_buffer<char> &output,
        sector_type type,
        optimizations options)
    {
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x01);
            input.current_position += 0x01;
        }
        else
        {
            *output.get_current_data_position() = 0x02;
        }
        output.current_position += 0x01;
        // Flags bytes
        if (!(options & OO_REMOVE_REDUNDANT_FLAG))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x08);
            input.current_position += 0x08;
        }
        else
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x04);
            std::memcpy(output + 0x04, input.get_current_data_position(), 0x04);
            input.current_position += 0x04;
        }
        output.current_position += 0x08;
        // Data bytes
        if (type == ST_MODE2_XA1 || !(options & OO_REMOVE_GAP))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x800);
            input.current_position += 0x800;
        }
        else
        {
            std::memset(output.get_current_data_position(), 0x00, 0x800);
        }
        output.current_position += 0x800;
        // EDC bytes
        if (!(options & OO_REMOVE_EDC))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x04);
            input.current_position += 0x04;
        }
        else
        {
            put32lsb(output, edc_compute(output.get_start_data_position() + 0x10, 0x808));
        }
        output.current_position += 0x04;
        // ECC bytes
        if (!(options & OO_REMOVE_ECC))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x114);
            input.current_position += 0x114;
        }
        else
        {
            ecc_write_sector(zeroaddress, reinterpret_cast<uint8_t *>(output.get_start_data_position() + 0x10), reinterpret_cast<uint8_t *>(output.get_current_data_position()));
        }
        output.current_position += 0x114;
        output.update_start_position();

        return STATUS_OK;
    }

    // Mode 2 XA 2
    status_code processor::decode_sector_mode_2_xa_2(
        data_buffer<char> &input,
        data_buffer<char> &output,
        sector_type type,
        optimizations options)
    {
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x01);
            input.current_position += 0x01;
        }
        else
        {
            *output.get_current_data_position() = 0x02;
        }
        output.current_position += 0x01;
        // Flags bytes
        if (!(options & OO_REMOVE_REDUNDANT_FLAG))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x08);
            input.current_position += 0x08;
        }
        else
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x04);
            input.current_position += 0x04;
            output.get_current_data_position()[4] = output.get_current_data_position()[0];
            output.get_current_data_position()[5] = output.get_current_data_position()[1];
            output.get_current_data_position()[6] = output.get_current_data_position()[2];
            output.get_current_data_position()[7] = output.get_current_data_position()[3];
        }
        output.current_position += 0x08;
        // Data bytes
        if (type == ST_MODE2_XA2 || !(options & OO_REMOVE_GAP))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x914);
            input.current_position += 0x914;
        }
        else
        {
            std::memset(output.get_current_data_position(), 0x00, 0x914);
        }
        output.current_position += 0x914;
        // EDC bytes
        if (!(options & OO_REMOVE_EDC))
        {
            std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x04);
            input.current_position += 0x04;
        }
        else
        {
            put32lsb(output, edc_compute(output.get_start_data_position() + 0x10, 0x91C));
        }
        output.current_position += 0x04;
        output.update_start_position();

        return STATUS_OK;
    }

    // Data sector unknown mode
    status_code processor::decode_sector_mode_X(
        data_buffer<char> &input,
        data_buffer<char> &output,
        sector_type type,
        optimizations options)
    {
        // Rest of bytes
        std::memcpy(output.get_current_data_position(), input.get_current_data_position(), 0x921);
        output.current_position += 0x921;
        input.current_position += 0x921;
        output.update_start_position();

        return STATUS_OK;
    }

    size_t processor::get_encoded_sector_size(
        sector_type type,
        optimizations options)
    {
        size_t output_size = 0;
        switch (type)
        {
        case ST_CDDA:
        case ST_CDDA_GAP:
            // CDDA are directly copied
            if (type == ST_CDDA || !(options & OO_REMOVE_GAP))
            {
                output_size = 2352;
            }
            break;

        case ST_MODE1:
        case ST_MODE1_GAP:
        case ST_MODE1_RAW:
            // SYNC bytes
            if (!(options & OO_REMOVE_SYNC))
            {
                output_size += 0x0C;
            }
            // Address bytes
            if (!(options & OO_REMOVE_MSF))
            {
                output_size += 0x03;
            }
            // Mode bytes
            if (!(options & OO_REMOVE_MODE))
            {
                output_size += 0x01;
            }
            // Data bytes
            if (type == ST_MODE1 || type == ST_MODE1_RAW || !(options & OO_REMOVE_GAP))
            {
                output_size += 0x800;
            }
            // EDC bytes
            if (!(options & OO_REMOVE_EDC) || type == ST_MODE1_RAW)
            {
                output_size += 0x04;
            }
            // Zeroed bytes
            if (!(options & OO_REMOVE_BLANKS))
            {
                output_size += 0x08;
            }
            // ECC bytes
            if (!(options & OO_REMOVE_ECC) || type == ST_MODE1_RAW)
            {
                output_size += 0x114;
            }
            break;

        case ST_MODE2:
        case ST_MODE2_GAP:
            // SYNC bytes
            if (!(options & OO_REMOVE_SYNC))
            {
                output_size += 0x0C;
            }
            // Address bytes
            if (!(options & OO_REMOVE_MSF))
            {
                output_size += 0x03;
            }
            // Mode bytes
            if (!(options & OO_REMOVE_MODE))
            {
                output_size += 0x01;
            }
            // Data bytes
            if (type == ST_MODE2 || !(options & OO_REMOVE_GAP))
            {
                output_size += 0x920;
            }
            break;

        case ST_MODE2_XA_GAP:
            // SYNC bytes
            if (!(options & OO_REMOVE_SYNC))
            {
                output_size += 0x0C;
            }
            // Address bytes
            if (!(options & OO_REMOVE_MSF))
            {
                output_size += 0x03;
            }
            // Mode bytes
            if (!(options & OO_REMOVE_MODE))
            {
                output_size += 0x01;
            }
            // Flags bytes
            if (!(options & OO_REMOVE_REDUNDANT_FLAG))
            {
                output_size += 0x08;
            }
            else
            {
                output_size += 0x04;
            }
            // Full sector gap
            if (!(options & OO_REMOVE_GAP))
            {
                output_size += 0x918;
            }

        case ST_MODE2_XA1:
        case ST_MODE2_XA1_GAP:
            // SYNC bytes
            if (!(options & OO_REMOVE_SYNC))
            {
                output_size += 0x0C;
            }
            // Address bytes
            if (!(options & OO_REMOVE_MSF))
            {
                output_size += 0x03;
            }
            // Mode bytes
            if (!(options & OO_REMOVE_MODE))
            {
                output_size += 0x01;
            }
            // Flags bytes
            if (!(options & OO_REMOVE_REDUNDANT_FLAG))
            {
                output_size += 0x08;
            }
            else
            {
                output_size += 0x04;
            }
            // Data bytes
            if (type == ST_MODE2_XA1 || !(options & OO_REMOVE_GAP))
            {
                output_size += 0x800;
            }
            // EDC bytes
            if (!(options & OO_REMOVE_EDC))
            {
                output_size += 0x04;
            }
            // ECC bytes
            if (!(options & OO_REMOVE_ECC))
            {
                output_size += 0x114;
            }
            break;

        case ST_MODE2_XA2:
        case ST_MODE2_XA2_GAP:
            // SYNC bytes
            if (!(options & OO_REMOVE_SYNC))
            {
                output_size += 0x0C;
            }
            // Address bytes
            if (!(options & OO_REMOVE_MSF))
            {
                output_size += 0x03;
            }
            // Mode bytes
            if (!(options & OO_REMOVE_MODE))
            {
                output_size += 0x01;
            }
            // Flags bytes
            if (!(options & OO_REMOVE_REDUNDANT_FLAG))
            {
                output_size += 0x08;
            }
            else
            {
                output_size += 0x04;
            }
            // Data bytes
            if (type == ST_MODE2_XA2 || !(options & OO_REMOVE_GAP))
            {
                output_size += 0x914;
            }
            // EDC bytes
            if (!(options & OO_REMOVE_EDC))
            {
                output_size += 0x04;
            }
            break;

        case ST_MODEX:
            // SYNC bytes
            if (!(options & OO_REMOVE_SYNC))
            {
                output_size += 0x0C;
            }
            // Address bytes
            if (!(options & OO_REMOVE_MSF))
            {
                output_size += 0x03;
            }
            // Rest of bytes
            output_size += 0x921;

            break;
        }

        return output_size;
    }

    std::vector<char> inline processor::sector_to_time(
        uint32_t sectorNumber)
    {
        std::vector<char> timeData(3);
        uint8_t sectors = sectorNumber % 75;
        uint8_t seconds = (sectorNumber / 75) % 60;
        uint8_t minutes = (sectorNumber / 75) / 60;

        // Converting decimal to hex base 10
        // 15 -> 0x15 instead 0x0F
        timeData[0] = (minutes / 10 * 16) + (minutes % 10);
        timeData[1] = (seconds / 10 * 16) + (seconds % 10);
        timeData[2] = (sectors / 10 * 16) + (sectors % 10);
        return timeData;
    }

    /**
     * @brief Get the three bytes with the sector MSF and converts it to sector number
     *
     * @param in The buffer with the three MSF bytes
     * @return uint32_t The value of the MSF in sector number
     */
    uint32_t processor::time_to_sector(data_buffer<char> &input)
    {
        uint16_t minutes = (input.get_current_data_position()[0] / 16 * 10) + (input.get_current_data_position()[0] % 16);
        uint16_t seconds = (input.get_current_data_position()[1] / 16 * 10) + (input.get_current_data_position()[1] % 16);
        uint16_t sectors = (input.get_current_data_position()[2] / 16 * 10) + (input.get_current_data_position()[2] % 16);

        uint32_t time = (minutes * 60 * 75) + (seconds * 75) + sectors;
        return time;
    }
}