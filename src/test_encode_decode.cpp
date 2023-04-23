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
    uint8_t *sector = nullptr;
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
    testFiles.push_back({"..\\tests\\bins\\SECTOR_MODE2_XA1.bin", ecm::ST_MODE2_1, true});
    testFiles.push_back({"..\\tests\\bins\\SECTOR_MODE2_XA1_GAP.bin", ecm::ST_MODE2_1_GAP, true});
    testFiles.push_back({"..\\tests\\bins\\SECTOR_MODE2_XA2.bin", ecm::ST_MODE2_2, true});
    testFiles.push_back({"..\\tests\\bins\\SECTOR_MODE2_XA2_GAP.bin", ecm::ST_MODE2_2_GAP, true});

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
        testFiles[i].sector = new uint8_t[2352]();
        _fp.read((char *)testFiles[i].sector, 2352);
        _fp.close();
    }

    /* It is time to do the tests */
    uint8_t *processBuffer = new uint8_t[2352]();
    uint8_t *outputBuffer = new uint8_t[2352]();
    ecm::processor sectorsProcessor;
    for (uint8_t i = 0; i < testFiles.size(); i++)
    {
        printf("----------------------------------------------------------------------\n");
        if (testFiles[i].sector == nullptr)
        {
            /* There was any kind of error opening the file */
            printf("The file %s cannot be opened so no test were done on it.\n", testFiles[i].filename.c_str());
        }
        else
        {
            printf("Testing the optimizations for the sector %s of type %d\n", testFiles[i].filename.c_str(), testFiles[i].sectorType);

            /* Clean the buffers */
            memset(processBuffer, 0, 2352);
            memset(outputBuffer, 0, 2352);

            /* Encode the sector in the output buffer */
            uint16_t outputProcessedSize = 0;
            sectorsProcessor.cleanSector(processBuffer, testFiles[i].sector, testFiles[i].sectorType, outputProcessedSize, OPTIMIZATIONS);
            printf("The encoded size is %d.\n", outputProcessedSize);

            /* Get the original sector number */
            uint32_t sectorNumber = sectorsProcessor.timeToSector(testFiles[i].sector + 0x0C);

            /* Try to regenerate the sector again */
            uint16_t outputReadedSize = 0;
            sectorsProcessor.regenerateSector(outputBuffer, processBuffer, testFiles[i].sectorType, sectorNumber, outputReadedSize, OPTIMIZATIONS);
            printf("Readed bytes from encoded stream (must match the encoded size): %d.\n", outputReadedSize);

            /* Verify if the sector matches (it is perfectly encoded) */
            int checkSector = memcmp(testFiles[i].sector, outputBuffer, 2352);
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
                errorFile.write((char *)outputBuffer, 2352);
                errorFile.close();
            }
            printf("----------------------------------------------------------------------\n");
        }
    }
    /* Delete the temporal buffers */
    delete[] processBuffer;
    delete[] outputBuffer;

    printf("----------------------------------------------------------------------\n");
    printf("Creating a stream buffer of %d bytes.\n", 2352 * testFiles.size());
    uint8_t *inputBuffer = new uint8_t[2352 * testFiles.size()]();

    /* Copy all the sectors to the test buffer */
    uint64_t currentPos = 0;
    for (file_info x : testFiles)
    {
        memccpy(inputBuffer + currentPos, x.sector, 2352, 2352);
        currentPos += 2352;
    }

    /* Test the multi sector encoder & decoder */
    outputBuffer = new uint8_t[2352 * testFiles.size()]();
    uint8_t *encodedBuffer = new uint8_t[2352 * testFiles.size()]();
    ecm::sector_type *sectorIndex = new ecm::sector_type[testFiles.size()]();
    uint64_t encodedSize = 2352 * testFiles.size();
    // ecm::optimizations usedOptimizations = (ecm::optimizations)((OPTIMIZATIONS) & ~ecm::OO_REMOVE_MSF);
    ecm::optimizations usedOptimizations = (ecm::optimizations)(OPTIMIZATIONS);
    ecm::optimizations resultedOptimizations = usedOptimizations;

    /* Encode the data using the stream encoder */
    sectorsProcessor.cleanStream(
        encodedBuffer,
        encodedSize,
        inputBuffer,
        2352 * testFiles.size(),
        1,
        resultedOptimizations,
        sectorIndex,
        testFiles.size());

    if (usedOptimizations != resultedOptimizations)
    {
        printf("WARNING: The optimizations has changed...\n\tOld: %d\n\tNew: %d\n", usedOptimizations, resultedOptimizations);
    }

    /* The sectors were optimized correctly, so now it's time to decode and test */
    uint64_t inputSize = encodedSize;
    sectorsProcessor.regenerateStream(
        outputBuffer,
        2352 * testFiles.size(),
        encodedBuffer,
        inputSize,
        1,
        resultedOptimizations,
        sectorIndex,
        testFiles.size());

    if (inputSize != encodedSize)
    {
        printf("WARNING: The encoded stream size and the readed bytes size doesn't match.\n");
    }

    int status = memcmp(inputBuffer, outputBuffer, 2352 * testFiles.size());
    if (status != 0)
    {
        printf("ERROR: The original and the decoded streams doesn't match. Check the code...\n");

        /* We will save both buffers to verify */
        std::ofstream output;
        output.open("original_stream.bin", std::ios::binary | std::ios::trunc);
        output.write((char *)inputBuffer, 2352 * testFiles.size());
        output.close();
        output.open("decoded_stream.bin", std::ios::binary | std::ios::trunc);
        output.write((char *)outputBuffer, 2352 * testFiles.size());
        output.close();
    }
    else
    {
        printf("The stream encoding and decoding was done without any problem.\n");
    }
    printf("----------------------------------------------------------------------\n");

    printf("Deleting the stream buffers.\n");
    delete[] inputBuffer;
    delete[] outputBuffer;
    delete[] encodedBuffer;

    /* It's time to free the reserved buffers to avoid memory leaks */
    for (file_info x : testFiles)
    {
        if (x.sector != nullptr)
        {
            printf("Deleting the file %s sector buffer.\n", x.filename.c_str());
            delete[] x.sector;
        }
    }

    return 0;
}