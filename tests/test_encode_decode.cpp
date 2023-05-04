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

#include "test.h"

#define OPTIMIZATIONS ecm::OO_REMOVE_SYNC |               \
                          ecm::OO_REMOVE_MSF |            \
                          ecm::OO_REMOVE_MODE |           \
                          ecm::OO_REMOVE_BLANKS |         \
                          ecm::OO_REMOVE_REDUNDANT_FLAG | \
                          ecm::OO_REMOVE_ECC |            \
                          ecm::OO_REMOVE_EDC |            \
                          ecm::OO_REMOVE_GAP

// function to return the current exe path
std::string get_program_path()
{
    std::filesystem::path output_path;

#ifdef _WIN32
    // Initialize the required vars
    LPSTR out_wstr = new char[MAX_PATH]();

    // get the current path
    GetModuleFileNameA(NULL, out_wstr, MAX_PATH);

    // Convert it to char and then to string
    // wcstombs_s(&i, out_char, MAX_PATH, out_wstr, MAX_PATH);
    // output_path = out_char;
    output_path = out_wstr;

    // Cleanup
    delete[] out_wstr;

#else
    output_path = std::filesystem::canonical("/proc/self/exe");
#endif

    return output_path.parent_path().string();
}

struct file_info
{
    std::string filename = "";
    ecm::sector_type sectorType = ecm::ST_UNKNOWN;
    bool msf = false;
};

/**
 * @brief Tests to check the library optimizations. All the tests will be done in memory, so at least 2352 + (2352 * sectorsNumber) of RAM will be required.
 *
 * @return int
 */
int main()
{
    /* Working dir will be always the exe folder */
    std::filesystem::current_path(get_program_path());

    /* Sector files to test */
    std::vector<file_info> testFiles;
    testFiles.push_back({"..\\tests\\bins\\SECTOR_CDDA.bin", ecm::ST_CDDA, false});
    testFiles.push_back({"..\\tests\\bins\\SECTOR_CDDA_GAP.bin", ecm::ST_CDDA_GAP, false});
    testFiles.push_back({"..\\tests\\bins\\SECTOR_MODE1.bin", ecm::ST_MODE1, true});
    testFiles.push_back({"..\\tests\\bins\\SECTOR_MODE1_GAP.bin", ecm::ST_MODE1_GAP, true});
    testFiles.push_back({"..\\tests\\bins\\SECTOR_MODE1_RAW.bin", ecm::ST_MODE1_RAW, true});
    testFiles.push_back({"..\\tests\\bins\\SECTOR_MODE2.bin", ecm::ST_MODE2, true});
    testFiles.push_back({"..\\tests\\bins\\SECTOR_MODE2_GAP.bin", ecm::ST_MODE2_GAP, true});
    testFiles.push_back({"..\\tests\\bins\\SECTOR_MODE2_XA_GAP.bin", ecm::ST_MODE2_XA_GAP, true});
    testFiles.push_back({"..\\tests\\bins\\SECTOR_MODE2_XA1.bin", ecm::ST_MODE2_XA1, true});
    testFiles.push_back({"..\\tests\\bins\\SECTOR_MODE2_XA1_GAP.bin", ecm::ST_MODE2_XA1_GAP, true});
    testFiles.push_back({"..\\tests\\bins\\SECTOR_MODE2_XA2.bin", ecm::ST_MODE2_XA2, true});
    testFiles.push_back({"..\\tests\\bins\\SECTOR_MODE2_XA2_GAP.bin", ecm::ST_MODE2_XA2_GAP, true});
    testFiles.push_back({"..\\tests\\bins\\SECTOR_MODEX.bin", ecm::ST_MODEX, true});

    /* Preparing the buffers and the processor */
    ecm::data_buffer<char> inputBuffer(2352);
    ecm::data_buffer<char> processBuffer(2352);
    ecm::data_buffer<char> outputBuffer(2352);
    ecm::processor sectorsProcessor;

    for (uint8_t i = 0; i < testFiles.size(); i++)
    {
        printf("Testing the file: %s.\n", testFiles[i].filename.c_str());

        std::ifstream _fp(testFiles[i].filename, std::ios::binary);

        if (!_fp.is_open())
        {
            printf("There was an error opening the %s file.\n", testFiles[i].filename.c_str());
            continue;
        }

        /* Read the file into the buffer */
        _fp.read(inputBuffer.get_current_data_position(), 2352);
        _fp.close();

        /* It is time to do the tests */

        printf("----------------------------------------------------------------------\n");
        if (inputBuffer.buffer.size() == 0)
        {
            /* There was any kind of error opening the file */
            printf("The file %s cannot be opened so no test will be done on it.\n", testFiles[i].filename.c_str());
        }
        else
        {
            printf("Testing the optimizations for the sector %s of type %d\n", testFiles[i].filename.c_str(), testFiles[i].sectorType);

            /* Detect the sector type and confirm that matches the provided */
            ecm::sector_type detected_sector_type = sectorsProcessor.detect(inputBuffer);

            if (detected_sector_type == testFiles[i].sectorType)
            {
                printf("The detected sector matches the provided sector type.\n");
            }
            else
            {
                printf("The detected sector doesn't matches thee provided sector type: %d\n", detected_sector_type);
            }

            /* Encode the sector in the output buffer */
            sectorsProcessor.encode_sector(inputBuffer, processBuffer, testFiles[i].sectorType, OPTIMIZATIONS);
            printf("Readed from source %d and the encoded size is %d.\n", inputBuffer.current_position, processBuffer.current_position);

            /* Get the original sector number */
            inputBuffer.current_position = 0x0C;
            uint32_t sectorNumber = sectorsProcessor.time_to_sector(inputBuffer);

            printf("The sector number is %d\n", sectorNumber);

            inputBuffer.reset_positions();
            processBuffer.reset_positions();

            /* Try to regenerate the sector again */
            sectorsProcessor.decode_sector(processBuffer, outputBuffer, testFiles[i].sectorType, sectorNumber, OPTIMIZATIONS);
            printf("Readed bytes from encoded stream (must match the encoded size): %d and written: %d.\n", processBuffer.current_position, outputBuffer.current_position);

            outputBuffer.reset_positions();
            processBuffer.reset_positions();

            /* Verify if the sector matches (it is perfectly encoded) */
            int checkSector = memcmp(inputBuffer.buffer.data(), outputBuffer.buffer.data(), 2352);
            if (checkSector == 0)
            {
                printf("The sector was encoded and decoded without any problem.\n");
            }
            else
            {
                printf("There was any kind of error encoding or decoding the original sector. Maybe is time to check the code...\n");

                /* Save the sector output */
                char fname[17] = {0};
                sprintf(fname, "output_%d_%d.bin", i, testFiles[i].sectorType);
                std::ofstream errorFile = std::ofstream(fname, std::ios::binary);
                errorFile.write(outputBuffer.buffer.data(), 2352);
                errorFile.close();
            }
            printf("----------------------------------------------------------------------\n");
        }
    }

    printf("----------------------------------------------------------------------\n");
    printf("Resizing the buffers to %d bytes.\n", 2352 * testFiles.size());
    inputBuffer.buffer.resize(2352 * testFiles.size());
    inputBuffer.reset_positions();
    processBuffer.buffer.resize(2352 * testFiles.size());
    processBuffer.reset_positions();
    outputBuffer.buffer.resize(2352 * testFiles.size());
    outputBuffer.reset_positions();

    /* Copy all the sectors to the test buffer */
    for (file_info x : testFiles)
    {
        std::ifstream _fp(x.filename, std::ios::binary);

        if (!_fp.is_open())
        {
            printf("There was an error opening the %s file.\n", x.filename.c_str());
            continue;
        }

        /* Read the file into the buffer */
        _fp.read(inputBuffer.get_current_data_position(), 2352);
        _fp.close();

        inputBuffer.current_position += 2352;
    }

    /* Reset the input buffer position again */
    inputBuffer.reset_positions();

    ///////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////

    /* Test the multi sector encoder & decoder */
    ecm::data_buffer<ecm::sector_type> sectorIndex(testFiles.size());
    // ecm::optimizations usedOptimizations = (ecm::optimizations)((OPTIMIZATIONS) & ~ecm::OO_REMOVE_MSF);
    ecm::optimizations usedOptimizations = (ecm::optimizations)(OPTIMIZATIONS);
    ecm::optimizations resultedOptimizations = usedOptimizations;

    /* Encode the data using the stream encoder */
    sectorsProcessor.encode_stream(
        inputBuffer,
        processBuffer,
        sectorIndex,
        testFiles.size(),
        1,
        resultedOptimizations);

    if (usedOptimizations != resultedOptimizations)
    {
        printf("WARNING: The optimizations has changed...\n\tOld: %d\n\tNew: %d\n", usedOptimizations, resultedOptimizations);
    }

    size_t encodedSize = processBuffer.current_position;
    processBuffer.reset_positions();
    sectorIndex.reset_positions();

    for (size_t i = 0; i < sectorIndex.buffer.size(); i++)
    {
        printf("Header entry %d is a sector of type %d\n", i, sectorIndex[i]);
    }

    /* The sectors were optimized correctly, so now it's time to decode and test */
    sectorsProcessor.decode_stream(
        processBuffer,
        outputBuffer,
        sectorIndex,
        testFiles.size(),
        1,
        resultedOptimizations);

    if (processBuffer.current_position != encodedSize)
    {
        printf("WARNING: The encoded stream size and the readed bytes size doesn't match.\n");
    }

    int status = memcmp(inputBuffer.buffer.data(), outputBuffer.buffer.data(), 2352 * testFiles.size());
    if (status != 0)
    {
        printf("ERROR: The original and the decoded streams doesn't match. Check the code...\n");

        /* We will save both buffers to verify */
        std::ofstream output;
        output.open("original_stream.bin", std::ios::binary | std::ios::trunc);
        output.write(inputBuffer.buffer.data(), 2352 * testFiles.size());
        output.close();
        output.open("decoded_stream.bin", std::ios::binary | std::ios::trunc);
        output.write(outputBuffer.buffer.data(), 2352 * testFiles.size());
        output.close();
    }
    else
    {
        printf("The stream encoding and decoding was done without any problem.\n");
    }
    printf("----------------------------------------------------------------------\n");

    return 0;
}