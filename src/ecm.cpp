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
     * @param outSize (uint64_t) Size of the output buffer. This variable will be updated with the space left in the buffer after the operation.
     * @param in (uint8_t*) Input buffer with the sectors to optimize.
     * @param inSize (uint64_t) Size of the input buffer.
     * @param startSectorNumber (uint32_t) Sector number of the first sector in the stream, required to regenerate an optimization.
     * @param options (optimizations) Optimizations to use on sectors optimizations
     * @param sectorsIndex
     * @param sectorsIndexSize
     * @param useTheBestOptimizations (bool) Check if the data integrity of the sectors can be maintained with the desired optimizations. If not, the "options" argument will be updated with the best optimizations for the stream.
     * @return int8_t int8_t Returns 0 if everything was OK. Otherwise a negative number will be returned.
     */
    status_code processor::cleanStream(
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

        /* Create a buffer and try to encode the sectors one by one. If any of them cannot be recovered in a lossless way
           detect the optimization which has caused the error and deactivate it */
        for (uint32_t i = 0; i < inputSectorsNumber; i++)
        {
            uint32_t currentPos = 2352 * i;

            /* Try to detect the sector type */
            sectorsIndex.get_current_data_position()[i] = detect((uint8_t *)&input.buffer[currentPos]);

            if (useTheBestOptimizations)
            {
                /* Call the function which will determine if those optimizations are the best for that sector */
                options = checkOptimizations((uint8_t *)&input.buffer[currentPos], startSectorNumber + i, options, sectorsIndex.get_current_data_position()[i]);
            }
        }

        /* Do a fast calculation to see if the stream fits the output buffer. Otherwise, return an error */
        uint64_t outputCalculatedSize = 0;
        uint64_t blockCalculatedSize = 0;
        for (uint32_t i = 0; i < inputSectorsNumber; i++)
        {
            getEncodedSectorSize(sectorsIndex.get_current_data_position()[i], blockCalculatedSize, options);
            outputCalculatedSize += blockCalculatedSize;
        }

        if (outputCalculatedSize > (output.buffer.size() - output.current_position))
        {
            return STATUS_ERROR_NO_ENOUGH_OUTPUT_BUFFER_SPACE;
        }

        /* Optimize the stream into the output buffer */
        for (uint32_t i = 0; i < inputSectorsNumber; i++)
        {
            cleanSector(input, output, *sectorsIndex.get_current_data_position(), options);
            sectorsIndex.current_position++;
        }

        return STATUS_OK;
    }

    status_code processor::regenerateStream(
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
        uint64_t inputCalculatedSize = 0;
        uint64_t blockCalculatedSize = 0;
        for (uint32_t i = 0; i < inputSectorsNumber; i++)
        {
            getEncodedSectorSize(sectorsIndex.get_current_data_position()[i], blockCalculatedSize, options);
            inputCalculatedSize += blockCalculatedSize;
        }

        if (inputCalculatedSize > input.get_available_items())
        {
            return STATUS_ERROR_NO_ENOUGH_INPUT_DATA;
        }

        /* Start to decode every sector and place it in the output buffer */
        for (uint32_t i = 0; i < inputSectorsNumber; i++)
        {
            uint16_t readedBytes = 0;
            regenerateSector((uint8_t *)output.get_current_data_position(), (uint8_t *)input.get_current_data_position(), sectorsIndex.get_current_data_position()[i], startSectorNumber + i, readedBytes, options);
            /* Add the readed bytes to the current input position */
            input.current_position += readedBytes;
            output.current_position += 2352;
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
    status_code processor::cleanSector(
        data_buffer<char> &input,
        data_buffer<char> &output,
        sector_type type,
        optimizations options)
    {
        uint16_t outputSize = 0;
        status_code return_code = STATUS_OK;
        switch (type)
        {
        case ST_CDDA:
        case ST_CDDA_GAP:
            return_code = cleanSectorCDDA((uint8_t *)output.get_current_data_position(), (uint8_t *)input.get_current_data_position(), type, outputSize, options);
            input.current_position += 2352;
            output.current_position += outputSize;
            break;

        case ST_MODE1:
        case ST_MODE1_GAP:
        case ST_MODE1_RAW:
            return_code = cleanSectorMode1((uint8_t *)output.get_current_data_position(), (uint8_t *)input.get_current_data_position(), type, outputSize, options);
            input.current_position += 2352;
            output.current_position += outputSize;
            break;

        case ST_MODE2:
        case ST_MODE2_GAP:
            return_code = cleanSectorMode2((uint8_t *)output.get_current_data_position(), (uint8_t *)input.get_current_data_position(), type, outputSize, options);
            input.current_position += 2352;
            output.current_position += outputSize;
            break;

        case ST_MODE2_1:
        case ST_MODE2_1_GAP:
            return_code = cleanSectorMode2XA1((uint8_t *)output.get_current_data_position(), (uint8_t *)input.get_current_data_position(), type, outputSize, options);
            input.current_position += 2352;
            output.current_position += outputSize;
            break;

        case ST_MODE2_2:
        case ST_MODE2_2_GAP:
            return_code = cleanSectorMode2XA2((uint8_t *)output.get_current_data_position(), (uint8_t *)input.get_current_data_position(), type, outputSize, options);
            input.current_position += 2352;
            output.current_position += outputSize;
            break;

        case ST_MODEX:
            return_code = cleanSectorModeX((uint8_t *)output.get_current_data_position(), (uint8_t *)input.get_current_data_position(), type, outputSize, options);
            input.current_position += 2352;
            output.current_position += outputSize;
            break;
        }

        return return_code;
    }

    /**
     * @brief Switcher of the sector regeneration functions. It will execute the function depending of the desired type
     *
     * @param out
     * @param sector
     * @param type
     * @param sectorNumber
     * @param bytesReaded
     * @param options
     * @return int8_t
     */
    status_code processor::regenerateSector(
        uint8_t *out,
        uint8_t *sector,
        sector_type type,
        uint32_t sectorNumber,
        uint16_t &bytesReaded,
        optimizations options)
    {
        bytesReaded = 0;
        uint16_t currentPos = 0;
        // sync and address bytes in data sectors, common to almost all types
        if (type >= ST_MODE1)
        {
            // SYNC bytes
            if (!(options & OO_REMOVE_SYNC))
            {
                memcpy(out, sector, 12);
                bytesReaded += 0x0C;
            }
            else
            {
                out[0] = 0x00;
                memset(out + 1, 0xFF, 10);
                out[11] = 0x00;
            }
            currentPos += 0x0C;
            // Address bytes
            if (!(options & OO_REMOVE_MSF))
            {
                memcpy(out + currentPos, sector + bytesReaded, 0x03);
                bytesReaded += 0x03;
            }
            else
            {
                sectorToTime(out + currentPos, sectorNumber);
            }
            currentPos += 0x03;
        }

        // The rest of the sector
        switch (type)
        {
        case ST_CDDA:
        case ST_CDDA_GAP:
            return regenerateSectorCDDA(out, sector, type, currentPos, bytesReaded, options);

        case ST_MODE1:
        case ST_MODE1_GAP:
        case ST_MODE1_RAW:
            return regenerateSectorMode1(out, sector, type, currentPos, bytesReaded, options);

        case ST_MODE2:
        case ST_MODE2_GAP:
            return regenerateSectorMode2(out, sector, type, currentPos, bytesReaded, options);

        case ST_MODE2_1:
        case ST_MODE2_1_GAP:
            return regenerateSectorMode2XA1(out, sector, type, currentPos, bytesReaded, options);

        case ST_MODE2_2:
        case ST_MODE2_2_GAP:
            return regenerateSectorMode2XA2(out, sector, type, currentPos, bytesReaded, options);

        case ST_MODEX:
            return regenerateSectorModeX(out, sector, type, currentPos, bytesReaded, options);
        }

        return STATUS_OK;
    }

    /**
     * @brief Detects if the sector is a gap or contains data
     *
     * @param sector (uint8_t*) Stream containing all the bytes which will be compared
     * @param length (uint16_t) Length of the stream to check
     * @return true The sectors are a gap
     * @return false Any or all sectors are not zeroed, so is not a gap.
     */
    bool inline processor::is_gap(uint8_t *sector, uint16_t length)
    {
        for (uint16_t i = 0; i < length; i++)
        {
            if ((sector[i]) != 0x00)
            {
                return false; // Sector contains data, so is not a GAP
            }
        }

        return true;
    }

    sector_type processor::detect(uint8_t *sector)
    {
        if (
            sector[0x000] == 0x00 && // sync (12 bytes)
            sector[0x001] == 0xFF &&
            sector[0x002] == 0xFF &&
            sector[0x003] == 0xFF &&
            sector[0x004] == 0xFF &&
            sector[0x005] == 0xFF &&
            sector[0x006] == 0xFF &&
            sector[0x007] == 0xFF &&
            sector[0x008] == 0xFF &&
            sector[0x009] == 0xFF &&
            sector[0x00A] == 0xFF &&
            sector[0x00B] == 0x00)
        {
            // Sector is a MODE1/MODE2 sector
            if (
                sector[0x00F] == 0x01 && // mode (1 byte)
                sector[0x814] == 0x00 && // reserved (8 bytes)
                sector[0x815] == 0x00 &&
                sector[0x816] == 0x00 &&
                sector[0x817] == 0x00 &&
                sector[0x818] == 0x00 &&
                sector[0x819] == 0x00 &&
                sector[0x81A] == 0x00 &&
                sector[0x81B] == 0x00)
            {
                //  The sector is surelly MODE1 but we will check the EDC
                if (
                    eccChecksector(
                        sector + 0xC,
                        sector + 0x10,
                        sector + 0x81C) &&
                    edcCompute(0, sector, 0x810) == get32lsb(sector + 0x810))
                {
                    if (is_gap(sector + 0x010, 0x800))
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
                sector[0x00F] == 0x02 // mode (1 byte)
            )
            {
                //  The sector is MODE2, and now we will detect what kind
                //
                // Might be Mode 2, Form 1
                //
                if (
                    eccChecksector(zeroaddress, sector + 0x010, sector + 0x81C) &&
                    edcCompute(0, sector + 0x010, 0x808) == get32lsb(sector + 0x818))
                {
                    if (is_gap(sector + 0x018, 0x800))
                    {
                        return ST_MODE2_1_GAP;
                    }
                    else
                    {
                        return ST_MODE2_1; //  Mode 2, Form 1
                    }
                }
                //
                // Might be Mode 2, Form 2
                //
                if (
                    edcCompute(0, sector + 0x010, 0x91C) == get32lsb(sector + 0x92C))
                {
                    if (is_gap(sector + 0x018, 0x914))
                    {
                        return ST_MODE2_2_GAP;
                    }
                    else
                    {
                        return ST_MODE2_2; // Mode 2, Form 2
                    }
                }

                // Checking if sector is MODE 2 without XA
                if (is_gap(sector + 0x010, 0x920))
                {
                    return ST_MODE2_GAP;
                }
                else
                {
                    return ST_MODE2;
                }
            }

            // Data sector detected but was not possible to determine the mode. Maybe is a copy protection sector.
            return ST_MODEX;
        }
        else
        {
            // Sector is not recognized, so might be a CDDA sector
            if (is_gap(sector, 0x930))
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

    optimizations processor::checkOptimizations(uint8_t *sector, uint32_t sectorNumber, optimizations options, sector_type sectorType)
    {
        if (sectorType & ST_CDDA_GAP || sectorType & ST_CDDA)
        {
            // Audio optimizations are always right
            return options;
        }

        if (sectorType & ST_UNKNOWN)
        {
            // Unknown sector will not be optimized, so the options will not be affected
            return options;
        }

        optimizations newOptions = options;

        /* Common data optimizations check */
        if (newOptions & OO_REMOVE_MSF && (sectorType && ST_MODEX ||
                                           sectorType && ST_MODE1 ||
                                           sectorType && ST_MODE1_GAP ||
                                           sectorType && ST_MODE2 ||
                                           sectorType && ST_MODE2_1 ||
                                           sectorType && ST_MODE2_1_GAP ||
                                           sectorType && ST_MODE2_2 ||
                                           sectorType && ST_MODE2_2_GAP))
        {
            /* The sync part is always used to detect the sector, so always will be OK */
            /* We will chech the MSF part */
            uint8_t generatedMSF[3] = {0};
            sectorToTime(generatedMSF, sectorNumber);

            if (sector[0x0C] != generatedMSF[0] ||
                sector[0x0D] != generatedMSF[1] ||
                sector[0x0E] != generatedMSF[2])
            {
                newOptions = (optimizations)(newOptions & (~OO_REMOVE_MSF));
            }
        }

        /* Mode 2 optimizations check */
        if (sectorType == ST_MODE2_1 ||
            sectorType == ST_MODE2_1_GAP ||
            sectorType == ST_MODE2_2 ||
            sectorType == ST_MODE2_2_GAP)
        {
            /* This mode is detected using the EDC and ECC, so will not be checked */
            if (newOptions & OO_REMOVE_REDUNDANT_FLAG &&
                (sector[0x10] != sector[0x14] ||
                 sector[0x11] != sector[0x15] ||
                 sector[0x12] != sector[0x16] ||
                 sector[0x13] != sector[0x17]))
            {
                newOptions = (optimizations)(newOptions & (~OO_REMOVE_REDUNDANT_FLAG));
            }
        }

        /* Return the optimizations before all checks */
        return newOptions;
    }

    inline uint32_t processor::get32lsb(const uint8_t *src)
    {
        return (((uint32_t)(src[0])) << 0) |
               (((uint32_t)(src[1])) << 8) |
               (((uint32_t)(src[2])) << 16) |
               (((uint32_t)(src[3])) << 24);
    }

    inline void processor::put32lsb(uint8_t *dest, uint32_t value)
    {
        dest[0] = (uint8_t)(value);
        dest[1] = (uint8_t)(value >> 8);
        dest[2] = (uint8_t)(value >> 16);
        dest[3] = (uint8_t)(value >> 24);
    }

    inline uint32_t processor::edcCompute(
        uint32_t edc,
        const uint8_t *src,
        size_t size)
    {
        for (; size; size--)
        {
            edc = (edc >> 8) ^ edc_lut[(edc ^ (*src++)) & 0xFF];
        }
        return edc;
    }

    int8_t processor::eccCheckpq(
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

    void processor::eccWritepq(
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

    int8_t processor::eccChecksector(
        const uint8_t *address,
        const uint8_t *data,
        const uint8_t *ecc)
    {
        return eccCheckpq(address, data, 86, 24, 2, 86, ecc) &&       // P
               eccCheckpq(address, data, 52, 43, 86, 88, ecc + 0xAC); // Q
    }

    void processor::eccWritesector(
        const uint8_t *address,
        const uint8_t *data,
        uint8_t *ecc)
    {
        eccWritepq(address, data, 86, 24, 2, 86, ecc);         // P
        eccWritepq(address, data, 52, 43, 86, 88, ecc + 0xAC); // Q
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    status_code processor::cleanSectorCDDA(
        uint8_t *out,
        uint8_t *sector,
        sector_type type,
        uint16_t &outputSize,
        optimizations options)
    {
        /* CDDA are raw, so no optimizations can be applied */
        if (type == ST_CDDA || !(options & OO_REMOVE_GAP))
        {
            memcpy(out, sector, 2352);
            outputSize = 2352;
        }

        return STATUS_OK;
    }

    // Mode 1
    status_code processor::cleanSectorMode1(
        uint8_t *out,
        uint8_t *sector,
        sector_type type,
        uint16_t &outputSize,
        optimizations options)
    {
        // SYNC bytes
        if (!(options & OO_REMOVE_SYNC))
        {
            memcpy(out, sector, 0x0C);
            outputSize += 0x0C;
        }
        // Address bytes
        if (!(options & OO_REMOVE_MSF))
        {
            memcpy(out + outputSize, sector + 0x0C, 0x03);
            outputSize += 0x03;
        }
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            memcpy(out + outputSize, sector + 0x0F, 0x01);
            outputSize += 0x01;
        }
        // Data bytes
        if (type == ST_MODE1 || type == ST_MODE1_RAW || !(options & OO_REMOVE_GAP))
        {
            memcpy(out + outputSize, sector + 0x10, 0x800);
            outputSize += 0x800;
        }
        // EDC bytes
        if (!(options & OO_REMOVE_EDC) || type == ST_MODE1_RAW)
        {
            memcpy(out + outputSize, sector + 0x810, 0x04);
            outputSize += 0x04;
        }
        // Zeroed bytes
        if (!(options & OO_REMOVE_BLANKS))
        {
            memcpy(out + outputSize, sector + 0x814, 0x08);
            outputSize += 0x08;
        }
        // ECC bytes
        if (!(options & OO_REMOVE_ECC) || type == ST_MODE1_RAW)
        {
            memcpy(out + outputSize, sector + 0x81C, 0x114);
            outputSize += 0x114;
        }

        return STATUS_OK;
    }

    // Mode 2
    status_code processor::cleanSectorMode2(
        uint8_t *out,
        uint8_t *sector,
        sector_type type,
        uint16_t &outputSize,
        optimizations options)
    {
        // SYNC bytes
        if (!(options & OO_REMOVE_SYNC))
        {
            memcpy(out, sector, 0x0C);
            outputSize += 0x0C;
        }
        // Address bytes
        if (!(options & OO_REMOVE_MSF))
        {
            memcpy(out + outputSize, sector + 0x0C, 0x03);
            outputSize += 0x03;
        }
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            memcpy(out + outputSize, sector + 0x0F, 0x01);
            outputSize += 0x01;
        }
        // Data bytes
        if (type == ST_MODE2 || !(options & OO_REMOVE_GAP))
        {
            memcpy(out + outputSize, sector + 0x10, 0x920);
            outputSize += 0x920;
        }

        return STATUS_OK;
    }

    // Mode 2 XA 1
    status_code processor::cleanSectorMode2XA1(
        uint8_t *out,
        uint8_t *sector,
        sector_type type,
        uint16_t &outputSize,
        optimizations options)
    {
        // SYNC bytes
        if (!(options & OO_REMOVE_SYNC))
        {
            memcpy(out, sector, 0x0C);
            outputSize += 0x0C;
        }
        // Address bytes
        if (!(options & OO_REMOVE_MSF))
        {
            memcpy(out + outputSize, sector + 0x0C, 0x03);
            outputSize += 0x03;
        }
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            memcpy(out + outputSize, sector + 0x0F, 0x01);
            outputSize += 0x01;
        }
        // Flags bytes
        if (!(options & OO_REMOVE_REDUNDANT_FLAG))
        {
            memcpy(out + outputSize, sector + 0x10, 0x08);
            outputSize += 0x08;
        }
        else
        {
            memcpy(out + outputSize, sector + 0x10, 0x04);
            outputSize += 0x04;
        }
        // Data bytes
        if (type == ST_MODE2_1 || !(options & OO_REMOVE_GAP))
        {
            memcpy(out + outputSize, sector + 0x18, 0x800);
            outputSize += 0x800;
        }
        // EDC bytes
        if (!(options & OO_REMOVE_EDC))
        {
            memcpy(out + outputSize, sector + 0x818, 0x04);
            outputSize += 0x04;
        }
        // ECC bytes
        if (!(options & OO_REMOVE_ECC))
        {
            memcpy(out + outputSize, sector + 0x81C, 0x114);
            outputSize += 0x114;
        }

        return STATUS_OK;
    }

    // Mode 2 XA 1
    status_code processor::cleanSectorMode2XA2(
        uint8_t *out,
        uint8_t *sector,
        sector_type type,
        uint16_t &outputSize,
        optimizations options)
    {
        // SYNC bytes
        if (!(options & OO_REMOVE_SYNC))
        {
            memcpy(out, sector, 0x0C);
            outputSize += 0x0C;
        }
        // Address bytes
        if (!(options & OO_REMOVE_MSF))
        {
            memcpy(out + outputSize, sector + 0x0C, 0x03);
            outputSize += 0x03;
        }
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            memcpy(out + outputSize, sector + 0x0F, 0x01);
            outputSize += 0x01;
        }
        // Flags bytes
        if (!(options & OO_REMOVE_REDUNDANT_FLAG))
        {
            memcpy(out + outputSize, sector + 0x10, 0x08);
            outputSize += 0x08;
        }
        else
        {
            memcpy(out + outputSize, sector + 0x10, 0x04);
            outputSize += 0x04;
        }
        // Data bytes
        if (type == ST_MODE2_2 || !(options & OO_REMOVE_GAP))
        {
            memcpy(out + outputSize, sector + 0x18, 0x914);
            outputSize += 0x914;
        }
        // EDC bytes
        if (!(options & OO_REMOVE_EDC))
        {
            memcpy(out + outputSize, sector + 0x92C, 0x04);
            outputSize += 0x04;
        }

        return STATUS_OK;
    }

    // Unknown data mode
    status_code processor::cleanSectorModeX(
        uint8_t *out,
        uint8_t *sector,
        sector_type type,
        uint16_t &outputSize,
        optimizations options)
    {
        // SYNC bytes
        if (!(options & OO_REMOVE_SYNC))
        {
            memcpy(out, sector, 0x0C);
            outputSize += 0x0C;
        }
        // Address bytes
        if (!(options & OO_REMOVE_MSF))
        {
            memcpy(out + outputSize, sector + 0x0C, 0x03);
            outputSize += 0x03;
        }
        // Rest of bytes
        memcpy(out + outputSize, sector + 0x0F, 0x921);
        outputSize += 0x921;

        return STATUS_OK;
    }

    status_code processor::regenerateSectorCDDA(
        uint8_t *out,
        uint8_t *sector,
        sector_type type,
        uint16_t currentPos,
        uint16_t &bytesReaded,
        optimizations options)
    {
        // CDDA are directly copied
        if (type == ST_CDDA || !(options & OO_REMOVE_GAP))
        {
            memcpy(out, sector, 2352);
            bytesReaded = 2352;
        }
        else
        {
            memset(out, 0x00, 2352);
        }

        return STATUS_OK;
    }

    // Mode 1
    status_code processor::regenerateSectorMode1(
        uint8_t *out,
        uint8_t *sector,
        sector_type type,
        uint16_t currentPos,
        uint16_t &bytesReaded,
        optimizations options)
    {
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x01);
            bytesReaded += 0x01;
        }
        else
        {
            out[currentPos] = 0x01;
        }
        currentPos += 0x01;
        // Data bytes
        if (type == ST_MODE1 || type == ST_MODE1_RAW || !(options & OO_REMOVE_GAP))
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x800);
            bytesReaded += 0x800;
        }
        else
        {
            memset(out + currentPos, 0x00, 0x800);
        }
        currentPos += 0x800;
        // EDC bytes
        if (!(options & OO_REMOVE_EDC) || type == ST_MODE1_RAW)
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x04);
            bytesReaded += 0x04;
        }
        else
        {
            put32lsb(out + currentPos, edcCompute(0, out, 0x810));
        }
        currentPos += 0x04;
        // Zeroed bytes
        if (!(options & OO_REMOVE_BLANKS))
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x08);
            bytesReaded += 0x08;
        }
        else
        {
            memset(out + currentPos, 0x00, 0x08);
        }
        currentPos += 0x08;
        // ECC bytes
        if (!(options & OO_REMOVE_ECC) || type == ST_MODE1_RAW)
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x114);
            bytesReaded += 0x114;
        }
        else
        {
            eccWritesector(out + 0xC, out + 0x10, out + currentPos);
        }
        currentPos += 0x114;

        return STATUS_OK;
    }

    // Mode 2
    status_code processor::regenerateSectorMode2(
        uint8_t *out,
        uint8_t *sector,
        sector_type type,
        uint16_t currentPos,
        uint16_t &bytesReaded,
        optimizations options)
    {
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x01);
            bytesReaded += 0x01;
        }
        else
        {
            out[currentPos] = 0x02;
        }
        currentPos += 0x01;
        // Data bytes
        if (type == ST_MODE2 || !(options & OO_REMOVE_GAP))
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x920);
            bytesReaded += 0x920;
        }
        else
        {
            memset(out + currentPos, 0x00, 0x920);
        }
        currentPos += 0x920;

        return STATUS_OK;
    }

    // Mode 2 XA 1
    status_code processor::regenerateSectorMode2XA1(
        uint8_t *out,
        uint8_t *sector,
        sector_type type,
        uint16_t currentPos,
        uint16_t &bytesReaded,
        optimizations options)
    {
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x01);
            bytesReaded += 0x01;
        }
        else
        {
            out[currentPos] = 0x02;
        }
        currentPos += 0x01;
        // Flags bytes
        if (!(options & OO_REMOVE_REDUNDANT_FLAG))
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x08);
            bytesReaded += 0x08;
        }
        else
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x04);
            bytesReaded += 0x04;
            out[currentPos + 4] = out[currentPos];
            out[currentPos + 5] = out[currentPos + 1];
            out[currentPos + 6] = out[currentPos + 2];
            out[currentPos + 7] = out[currentPos + 3];
        }
        currentPos += 0x08;
        // Data bytes
        if (type == ST_MODE2_1 || !(options & OO_REMOVE_GAP))
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x800);
            bytesReaded += 0x800;
        }
        else
        {
            memset(out + currentPos, 0x00, 0x800);
        }
        currentPos += 0x800;
        // EDC bytes
        if (!(options & OO_REMOVE_EDC))
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x04);
            bytesReaded += 0x04;
        }
        else
        {
            put32lsb(out + currentPos, edcCompute(0, out + 0x10, 0x808));
        }
        currentPos += 0x04;
        // ECC bytes
        if (!(options & OO_REMOVE_ECC))
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x114);
            bytesReaded += 0x114;
        }
        else
        {
            eccWritesector(zeroaddress, out + 0x10, out + currentPos);
        }
        currentPos += 0x114;

        return STATUS_OK;
    }

    // Mode 2 XA 2
    status_code processor::regenerateSectorMode2XA2(
        uint8_t *out,
        uint8_t *sector,
        sector_type type,
        uint16_t currentPos,
        uint16_t &bytesReaded,
        optimizations options)
    {
        // Mode bytes
        if (!(options & OO_REMOVE_MODE))
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x01);
            bytesReaded += 0x01;
        }
        else
        {
            out[currentPos] = 0x02;
        }
        currentPos += 0x01;
        // Flags bytes
        if (!(options & OO_REMOVE_REDUNDANT_FLAG))
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x08);
            bytesReaded += 0x08;
        }
        else
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x04);
            bytesReaded += 0x04;
            out[currentPos + 4] = out[currentPos];
            out[currentPos + 5] = out[currentPos + 1];
            out[currentPos + 6] = out[currentPos + 2];
            out[currentPos + 7] = out[currentPos + 3];
        }
        currentPos += 0x08;
        // Data bytes
        if (type == ST_MODE2_2 || !(options & OO_REMOVE_GAP))
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x914);
            bytesReaded += 0x914;
        }
        else
        {
            memset(out + currentPos, 0x00, 0x914);
        }
        currentPos += 0x914;
        // EDC bytes
        if (!(options & OO_REMOVE_EDC))
        {
            memcpy(out + currentPos, sector + bytesReaded, 0x04);
            bytesReaded += 0x04;
        }
        else
        {
            put32lsb(out + currentPos, edcCompute(0, out + 0x10, 0x91C));
        }
        currentPos += 0x04;

        return STATUS_OK;
    }

    // Data sector unknown mode
    status_code processor::regenerateSectorModeX(
        uint8_t *out,
        uint8_t *sector,
        sector_type type,
        uint16_t currentPos,
        uint16_t &bytesReaded,
        optimizations options)
    {
        // Rest of bytes
        memcpy(out + currentPos, sector + bytesReaded, 0x921);
        currentPos += 0x921;
        bytesReaded = 0x921;

        return STATUS_OK;
    }

    status_code processor::getEncodedSectorSize(
        sector_type type,
        size_t &output_size,
        optimizations options)
    {
        output_size = 0;
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

        case ST_MODE2_1:
        case ST_MODE2_1_GAP:
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
            if (type == ST_MODE2_1 || !(options & OO_REMOVE_GAP))
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

        case ST_MODE2_2:
        case ST_MODE2_2_GAP:
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
            if (type == ST_MODE2_2 || !(options & OO_REMOVE_GAP))
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

        return STATUS_OK;
    }

    void inline processor::sectorToTime(
        uint8_t *out,
        uint32_t sectorNumber)
    {
        uint8_t sectors = sectorNumber % 75;
        uint8_t seconds = (sectorNumber / 75) % 60;
        uint8_t minutes = (sectorNumber / 75) / 60;

        // Converting decimal to hex base 10
        // 15 -> 0x15 instead 0x0F
        out[0] = (minutes / 10 * 16) + (minutes % 10);
        out[1] = (seconds / 10 * 16) + (seconds % 10);
        out[2] = (sectors / 10 * 16) + (sectors % 10);
    }

    /**
     * @brief Get the three bytes with the sector MSF and converts it to sector number
     *
     * @param in The buffer with the three MSF bytes
     * @return uint32_t The value of the MSF in sector number
     */
    uint32_t processor::timeToSector(uint8_t *in)
    {
        uint16_t minutes = (in[0] / 16 * 10) + (in[0] % 16);
        uint16_t seconds = (in[1] / 16 * 10) + (in[1] % 16);
        uint16_t sectors = (in[2] / 16 * 10) + (in[2] % 16);

        uint32_t time = (minutes * 60 * 75) + (seconds * 75) + sectors;
        return time;
    }
}