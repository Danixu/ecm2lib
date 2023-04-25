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

static struct option long_options[] = {
    {"input", required_argument, NULL, 'i'},
    {"output", required_argument, NULL, 'o'},
    {"decoding", no_argument, NULL, 'd'},
    {NULL, 0, NULL, 0}};

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

    options.inputFile = "test.bin";
    options.outputFile = "test.ecm2";
    options.decode = false;

    std::ifstream inputFile;
    std::ofstream outputFile;

    /* Open the input and output file */
    inputFile.open(options.inputFile, std::ios::binary);
    if (!inputFile.is_open())
    {
        printf("ERROR: There was an error opening the input file.\n");
        return 1;
    }

    outputFile.open(options.outputFile, std::ios::binary);
    if (!outputFile.is_open())
    {
        printf("ERROR: There was an error opening the output file.\n");
        inputFile.close();
        return 1;
    }

    /* If mode is encoding */
    if (!options.decode)
    {
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
           This can be done at block level if the optimizations are saved in
           any way, but in this case only the full file optimizations will be saved. */

        for (uint32_t i = 0; i < sectors; i += BUFFERSECTORS)
        {
            uint32_t readedData = 0;
            if (BUFFERSECTORS < (sectors - i))
            {
                readedData = BUFFERSECTORS * 2352; // must equals to buffersize
            }
            else
            {
                readedData = (sectors - i) * 2352;
            }
            /* Fill the buffer */
            inputFile.read((char *)inputBuffer, readedData);
            /* Optimize the data and get the optimal optimizations */
            uint64_t writtenData = BUFFERSIZE;

            uint64_t currentPos = inputFile.tellg();

            ecmEncoder.cleanStream(outputBuffer, writtenData, inputBuffer, readedData, 150 + i, optimizations, index + i, sectors - i);
        }

        /* At this point the optimizations will have changed if required, so the next step is to write the output file */
        printf("Optimizations now: %d\n", optimizations);

        /* Reserve the config and index data header. */
        outputFile.write((char *)&optimizations, sizeof(optimizations));
        outputFile.write((char *)&sectors, sizeof(sectors));
        outputFile.write((char *)index, sectors);

        /* Well, the header was written and now is time to write the optimized sectors */

        /* Free the reserved resources */
        delete[] inputBuffer;
        delete[] outputBuffer;
        delete[] index;
    }
    /* Else if the mode is decoding */
    else
    {
    }

    return 0;
}