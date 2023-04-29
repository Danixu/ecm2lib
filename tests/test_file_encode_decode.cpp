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
#include <chrono>

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
    auto start = std::chrono::high_resolution_clock::now();
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
    ecm::data_buffer<char> inputBuffer(BUFFERSIZE);
    ecm::data_buffer<char> outputBuffer(BUFFERSIZE);
    ecm::data_buffer<ecm::sector_type> index(sectors);

    /* Initialize the optimizer */
    ecm::processor ecmEncoder = ecm::processor();

    /* We will do a full image processing to get the correct optimizations.
       Some protection methods will be broken if the file is overoptimized.
       This can be done at block level if the output file allows it, but this one is just
       a test and is not fully optimized. */

    printf("Analizing the data to determine the best optimizations and generate the index.\n");
    for (uint32_t i = 0; i < sectors; i += BUFFERSECTORS)
    {
        uint32_t sectorsToRead = 0;
        size_t dataToRead = 0;
        if (BUFFERSECTORS < (sectors - i))
        {
            sectorsToRead = BUFFERSECTORS; // must equals to buffersize
        }
        else
        {
            sectorsToRead = (sectors - i); // If there are no enough sectors to fill the buffer, just read what you have
        }
        /* If the buffer size doesn't matches the readed data, then will be recreated */
        /* We first destroy the vector and then we recreate it to avoid the overhead of to copy the current data to another memory location */
        dataToRead = sectorsToRead * 2352;

        /* Reset the buffer position in every iterarion */
        inputBuffer.current_position = 0;
        outputBuffer.current_position = 0;

        /* Read the data */
        inputFile.read(inputBuffer.buffer.data(), dataToRead);
        /* Optimize the data and get the optimal optimizations */
        ecmEncoder.clean_stream(inputBuffer, outputBuffer, index, sectorsToRead, 150 + i, optimizations);
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
    _oif.write((char *)index.buffer.data(), sizeof(ecm::sector_type) * sectors);
    _oif.close();

    printf("Writting the header and index data.\n");
    /* Reserve the config and index data header. */
    outputFile.write((char *)&optimizations, sizeof(optimizations));
    outputFile.write((char *)&sectors, sizeof(sectors));
    outputFile.write((char *)index.buffer.data(), sizeof(ecm::sector_type) * sectors);

    /* Reset the positions */
    inputFile.seekg(0, std::ios::beg);
    index.current_position = 0;

    printf("Processing the input file and storing the data in the output file.\n");
    /* Well, the header was written and now is time to write the optimized sectors */
    for (uint32_t i = 0; i < sectors; i += BUFFERSECTORS)
    {
        uint32_t sectorsToRead = 0;
        size_t dataToRead = 0;
        if (BUFFERSECTORS < (sectors - i))
        {
            sectorsToRead = BUFFERSECTORS; // must equals to buffersize
        }
        else
        {
            sectorsToRead = (sectors - i); // If there are no enough sectors to fill the buffer, just read what you have
        }
        /* If the buffer size doesn't matches the readed data, then will be recreated */
        /* We first destroy the vector and then we recreate it to avoid the overhead of to copy the current data to another memory location */
        dataToRead = sectorsToRead * 2352;

        /* Reset the buffer position in every iterarion */
        inputBuffer.current_position = 0;
        outputBuffer.current_position = 0;

        /* Read the data */
        inputFile.read(inputBuffer.buffer.data(), dataToRead);

        ecmEncoder.clean_stream(inputBuffer, outputBuffer, index, sectorsToRead, 150 + i, optimizations);
        outputFile.write(outputBuffer.buffer.data(), outputBuffer.current_position);
    }

    printf("The data was sucesfully encoded. Closing the files.\n");
    inputFile.close();
    outputFile.close();

    printf("Freeing the encoding resources to start again in a clean mode\n");

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

    index.current_position = 0;

    /* Reserve the index data and read it */
    inputFile.read((char *)index.buffer.data(), sizeof(ecm::sector_type) * sectors);

    /* Reset the buffers */
    inputBuffer.buffer.resize(BUFFERSIZE);
    outputBuffer.buffer.resize(BUFFERSIZE);

    uint32_t sectorsToRead = BUFFERSECTORS;
    /* Now that we have the data, it is time to decode the stream */
    for (uint32_t i = 0; i < sectors; i += sectorsToRead)
    {
        if ((sectors - i) < BUFFERSECTORS)
        {
            sectorsToRead = sectors - i;
        }
        /* First calculate how much data we need to read from the encoded file */
        uint64_t toRead = 0;
        for (uint32_t j = 0; j < sectorsToRead; j++)
        {
            uint64_t calculatedSectorSize = 0;
            ecmEncoder.get_encoded_sector_size(index.buffer[i + j], calculatedSectorSize, optimizations);
            toRead += calculatedSectorSize;
        }

        /* Reset the buffer position in every iterarion */
        inputBuffer.current_position = 0;
        outputBuffer.current_position = 0;

        /* Read the calculated size */
        inputFile.read((char *)inputBuffer.buffer.data(), toRead);

        /* Decode the readed data */
        ecmEncoder.regenerate_stream(inputBuffer, outputBuffer, index, sectorsToRead, 150 + i, optimizations);

        /* Write the decoded data to the output file */
        outputFile.write(outputBuffer.buffer.data(), outputBuffer.current_position);
        // printf("Output data to write Decoding: %d\n", outputBuffer.current_position);
    }

    printf("The data was sucesfully decoded. Closing the files.\n");
    inputFile.close();
    outputFile.close();
    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
    printf("\n\nThe file was processed without any problem\n");
    printf("Total execution time: %0.3fs\n\n", duration.count() / 1000.0F);

    return 0;
}