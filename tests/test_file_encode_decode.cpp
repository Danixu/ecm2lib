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

/*******************************************************************************
 * This program is just a test program wich can be hardly improved.
 * As a test, doesn't worth to do it, and the resulting file can be bigger compared
 * with an improved version. For example, the index can be condensed and then avoid
 * to use a lot of bytes to store it. Also some compression can be added and even
 * any kind of CRC check.
 ********************************************************************************/

#include "test.h"
#include <getopt.h>

#define OPTIMIZATIONS ecm::OO_REMOVE_SYNC |               \
                          ecm::OO_REMOVE_MSF |            \
                          ecm::OO_REMOVE_MODE |           \
                          ecm::OO_REMOVE_BLANKS |         \
                          ecm::OO_REMOVE_REDUNDANT_FLAG | \
                          ecm::OO_REMOVE_ECC |            \
                          ecm::OO_REMOVE_EDC |            \
                          ecm::OO_REMOVE_GAP

#define BUFFERSECTORS 100
#define BUFFERSIZE (2352 * BUFFERSECTORS)

struct program_options
{
    std::string inputFile = "";
    std::string outputFile = "";
    bool decode = false;
};

/**
 * @brief Tests to check the library optimizations. This test will convert a file into
 *
 * @return int
 */
int main(int argc, char **argv)
{
    // Input/Output files
    std::ifstream in_file;
    std::fstream out_file;

    program_options options;
    int return_code = 0;

    std::ifstream inputFile;
    std::ofstream outputFile;

    /* Open the input and output file */
    inputFile.open("test.bin", std::ios::binary);
    if (!inputFile.is_open())
    {
        printf("ERROR: There was an error opening the input file.\n");
        return 1;
    }

    outputFile.open("test.ecm2", std::ios::binary);
    if (!outputFile.is_open())
    {
        printf("ERROR: There was an error opening the output file.\n");
        inputFile.close();
        return 1;
    }

    /* First encode the test file */
    printf("Encoding the input file to ECM2...\n");
    ecm::optimizations optimizations = OPTIMIZATIONS;
    /* Check if the input file is a ISO image */
    inputFile.seekg(0, std::ios::end);
    uint64_t inputSize = inputFile.tellg();
    inputFile.seekg(0, std::ios::beg);
    if (inputSize % 2352)
    {
        printf("ERROR: The input file is not an ISO image or is corrupted.\n");
        inputFile.close();
        outputFile.close();
        return 1;
    }
    uint32_t sectors = inputSize / 2352;

    /* Create a buffer to keep the data to process */
    uint8_t *inputBuffer = new uint8_t[BUFFERSIZE]();
    uint8_t *outputBuffer = new uint8_t[BUFFERSIZE]();
    ecm::sector_type *index = new ecm::sector_type[sectors]();
    uint8_t indexPos = sizeof(optimizations) + sizeof(sectors);

    /* Initialize the optimizer */
    ecm::processor ecmEncoder = ecm::processor();

    /* We will do a full image processing to get the correct optimizations.
       Some protection methods will be broken if the file is overoptimized.
       This can be done at block level if the output file allows it, but this one is just
       a test and is not fully optimized. */

    printf("Analizing the data to determine the best optimizations and generate the index.\n");
    for (uint32_t i = 0; i < sectors; i += BUFFERSECTORS)
    {
        uint32_t readedData = 0;
        if (BUFFERSECTORS < (sectors - i))
        {
            readedData = BUFFERSECTORS * 2352; // must equals to buffersize
        }
        else
        {
            readedData = (sectors - i) * 2352; // If there are no enough sectors to fill the buffer, just read what you have
        }
        /* Read the data */
        inputFile.read((char *)inputBuffer, readedData);
        /* Optimize the data and get the optimal optimizations */
        uint64_t writtenData = BUFFERSIZE;

        ecmEncoder.cleanStream(outputBuffer, writtenData, inputBuffer, readedData, 150 + i, optimizations, index + i, sectors - i);
    }

    /* At this point the optimizations will have changed if required, so the next step is to write the output file */
    printf("Best optimizations detected: %d\n", optimizations);
    if (!(optimizations & ecm::OO_REMOVE_SYNC))
    {
        printf("The 'Remove sync' optimization is disabled.\n");
    }
    if (!(optimizations & ecm::OO_REMOVE_MSF))
    {
        printf("The 'Remove msf' optimization is disabled.\n");
    }
    if (!(optimizations & ecm::OO_REMOVE_MODE))
    {
        printf("The 'Remove mode' optimization is disabled.\n");
    }
    if (!(optimizations & ecm::OO_REMOVE_BLANKS))
    {
        printf("The 'Remove blanks' optimization is disabled.\n");
    }
    if (!(optimizations & ecm::OO_REMOVE_REDUNDANT_FLAG))
    {
        printf("The 'Remove redundant flag' optimization is disabled.\n");
    }
    if (!(optimizations & ecm::OO_REMOVE_ECC))
    {
        printf("The 'Remove ecc' optimization is disabled.\n");
    }
    if (!(optimizations & ecm::OO_REMOVE_EDC))
    {
        printf("The 'Remove edc' optimization is disabled.\n");
    }
    if (!(optimizations & ecm::OO_REMOVE_GAP))
    {
        printf("The 'Remove gap' optimization is disabled.\n");
    }

    std::ofstream _oif("test_encode.idx", std::ios::trunc | std::ios::binary);
    _oif.write((char *)index, sizeof(ecm::sector_type) * sectors);
    _oif.close();

    printf("Writting the header and index data.\n");
    /* Reserve the config and index data header. */
    outputFile.write((char *)&optimizations, sizeof(optimizations));
    outputFile.write((char *)&sectors, sizeof(sectors));
    outputFile.write((char *)index, sizeof(ecm::sector_type) * sectors);

    inputFile.seekg(0, std::ios::beg);

    printf("Processing the input file and storing the data in the output file.\n");
    /* Well, the header was written and now is time to write the optimized sectors */
    for (uint32_t i = 0; i < sectors; i += BUFFERSECTORS)
    {
        uint32_t readedData = 0;
        if (BUFFERSECTORS < (sectors - i))
        {
            readedData = BUFFERSECTORS * 2352; // must equals to buffersize
        }
        else
        {
            readedData = (sectors - i) * 2352; // If there are no enough sectors to fill the buffer, just read what you have
        }
        /* Read the data */
        inputFile.read((char *)inputBuffer, readedData);
        /* Optimize the data and get the optimal optimizations */
        uint64_t writtenData = BUFFERSIZE;

        ecmEncoder.cleanStream(outputBuffer, writtenData, inputBuffer, readedData, 150 + i, optimizations, index + i, sectors - i);
        outputFile.write((char *)outputBuffer, writtenData);
    }

    printf("The data was sucesfully encoded. Closing the files.\n");
    inputFile.close();
    outputFile.close();

    printf("Freeing the encoding resources to start again in a clean mode\n");
    /* Free the reserved resources */
    delete[] inputBuffer;
    delete[] outputBuffer;
    delete[] index;

    /* Once is encoded, try to decode it again */
    printf("Now is time to decode the encoded data to be sure that is working fine.\n");
    /* Clear the buffers to be sure that the data is new */
    inputFile.open("test.ecm2", std::ios::binary);
    if (!inputFile.is_open())
    {
        printf("ERROR: There was an error opening the input file.\n");
        return 1;
    }

    outputFile.open("test.dec", std::ios::binary);
    if (!outputFile.is_open())
    {
        printf("ERROR: There was an error opening the output file.\n");
        inputFile.close();
        return 1;
    }

    /* Read the sectors number and the optimizations */
    inputFile.read((char *)&optimizations, sizeof(optimizations));
    inputFile.read((char *)&sectors, sizeof(sectors));

    /* Create a buffer to keep the data to process */
    inputBuffer = new uint8_t[BUFFERSIZE]();
    outputBuffer = new uint8_t[BUFFERSIZE]();

    /* Reserve the index data and read it */
    index = new ecm::sector_type[sectors]();
    inputFile.read((char *)index, sizeof(ecm::sector_type) * sectors);

    uint32_t sectorsToRead = BUFFERSECTORS;
    /* Now that we have the data, it is time to decode the stream */
    for (uint32_t i = 0; i < sectors; i += sectorsToRead)
    {
        if ((sectors - i) < BUFFERSECTORS)
        {
            sectorsToRead = sectors - i;
            printf("Current sector: %d - Total sectors: %d - ToRead = %d\n", i, sectors, sectorsToRead);
        }
        /* First calculate how much data we need to read from the encoded file */
        uint64_t toRead = 0;
        for (uint32_t j = 0; j < sectorsToRead; j++)
        {
            uint64_t calculatedSectorSize = 0;
            ecmEncoder.getEncodedSectorSize(index[i + j], calculatedSectorSize, optimizations);
            toRead += calculatedSectorSize;
        }

        /* Read the calculated size */
        inputFile.read((char *)inputBuffer, toRead);

        /* Decode the readed data */
        uint64_t toWrite = BUFFERSIZE;
        ecmEncoder.regenerateStream(outputBuffer, toWrite, inputBuffer, toRead, 150 + i, optimizations, index + i, sectorsToRead);

        /* Write the decoded data to the output file */
        outputFile.write((char *)outputBuffer, toWrite);
    }

    /* Free the reserved resources */
    delete[] inputBuffer;
    delete[] outputBuffer;
    delete[] index;

    printf("The data was sucesfully decoded. Closing the files.\n");
    inputFile.close();
    outputFile.close();
    printf("All done!, exiting...\n");

    return 0;
}