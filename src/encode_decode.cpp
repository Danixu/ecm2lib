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

#include "encode_decode.h"
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

static struct option long_options[] = {
    {"input", required_argument, NULL, 'i'},
    {"output", required_argument, NULL, 'o'},
    {"decode", no_argument, NULL, 'd'},
    {NULL, 0, NULL, 0}};

struct program_options
{
    std::string input_filename = "";
    std::string output_filename = "";
    bool decode = false;
};

#pragma pack(push, 1)
struct ecm_file_configuration
{
    ecm::optimizations optimizations = ecm::OO_NONE;
    uint8_t index_pack_mode = 0;
    uint32_t index_entries = 0;
    uint32_t sectors = 0;
};
#pragma pack(pop)

void print_help();
int get_options(
    int argc,
    char **argv,
    program_options &options);

int main(int argc, char **argv)
{
    auto start = std::chrono::high_resolution_clock::now();
    // Input/Output files
    std::ifstream input_file;
    std::ofstream output_file;

    program_options options;
    int return_code = 0;
    size_t input_size = 0;

    /* The encoder and decoder uses the same class */
    ecm::processor ecm_processor;

    return_code = get_options(argc, argv, options);
    if (return_code)
    {
        goto exit;
    }

    /* Open the input file */
    input_file.open(options.input_filename.c_str(), std::ios::binary);
    /* Tricky way to check if was oppened correctly.
       The "is_open" method was failing on cross compiled EXE */
    {
        char dummy;
        if (!input_file.read(&dummy, 0))
        {
            fprintf(stderr, "ERROR: The input file cannot be opened.\n");
            return 1;
        }
    }

    input_file.seekg(0, std::ios::end);
    input_size = input_file.tellg();
    input_file.seekg(0, std::ios::beg);

    if (!options.decode && input_size % 2352)
    {
        /* If the input file is supposed to be encoded, then a CD-ROM image is expected */
        fprintf(stderr, "ERROR: The input file is not a disk image file or is damaged.\n");
        return_code = 1;
        goto exit;
    }

    /* Open the output file */
    output_file.open(options.output_filename.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
    // Check if file was oppened correctly.
    if (!output_file.good())
    {
        fprintf(stderr, "ERROR: output file cannot be opened.\n");
        return_code = 1;
        goto exit;
    }

    if (options.decode)
    {
        printf("Checking that the input file is a ECM2 file.\n");
        char header[5];
        if (!input_file.read(header, 5))
        {
            fprintf(stderr, "ERROR: There was an error reading the input file header.\n");
            return_code = 1;
            goto exit;
        }

        if (
            header[0] != 'E' ||
            header[1] != 'C' ||
            header[2] != 'M' ||
            header[3] != '2' ||
            header[4] != 0x01)
        {
            fprintf(stderr, "ERROR: There input file header doesn't mathes the ECM2 header. Is not an ECM2 file or is damaged.\n");
            return_code = 1;
            goto exit;
        }

        printf("Reading the input file used configurations.\n");
        ecm_file_configuration input_config;
        if (!input_file.read((char *)&input_config, sizeof(input_config)))
        {
            fprintf(stderr, "ERROR: There was an error reading the input file index.\n");
            return_code = 1;
            goto exit;
        }

        /* The max sectors estimated in a CD-ROM of 800MB are 356659 so we will fail above 400k */
        if (input_config.index_entries > 400000)
        {
            fprintf(stderr, "ERROR: There was an error reading the input file index. The number of entries exceeded the max allowed, so maybe is damaged.\n");
            return_code = 1;
            goto exit;
        }

        printf("Optimizations: %d, Sectors: %d, Index Pack Mode: %d.\n", input_config.optimizations, input_config.index_entries, input_config.index_pack_mode);

        size_t current_position = input_file.tellg();
        printf("Current position: %d\n", current_position);

        /* The index information is packed into blocks composed by type + count
           Type is always ecm::sector_type (uint8_t), but count size depends of the
           index pack mode used.
           NOTE: Index pack mode means "bytes used for the count", so a type 1 index will use 1 byte to store the count

           We will create a vector with the required space and we will read the index from the input file
        */
        printf("Reading and Unpacking the index (%d bytes).\n", (input_config.index_pack_mode + 1) * input_config.index_entries);
        std::vector<char> index_packed((input_config.index_pack_mode + 1) * input_config.index_entries);
        if (!input_file.read(index_packed.data(), index_packed.size()))
        {
            fprintf(stderr, "ERROR: There was an error reading the input file index data.\n");
            return_code = 1;
            goto exit;
        }

        /* Unpack the index into a simple index type */
        ecm::data_buffer<ecm::sector_type> unpacked_header = ecm_processor.unpack_header(index_packed, input_config.index_pack_mode);
        printf("Unpacked count (Must match the index entries): %d\n", unpacked_header.buffer.size());

        if ((unpacked_header.buffer.size() / sizeof(ecm::sector_type)) != input_config.sectors)
        {
            /* The header is damaged... */
            fprintf(stderr, "The input index data is damaged.");
            return_code = 1;
            goto exit;
        }

        /* Create a buffer to keep the data to process */
        ecm::data_buffer<char> input_buffer(BUFFERSIZE);
        ecm::data_buffer<char> output_buffer(BUFFERSIZE);

        /* Now is time to decode the file */
        for (size_t i = 0; i < unpacked_header.buffer.size(); i += BUFFERSECTORS)
        {
            /* First calculate how much data we need to read from the encoded file */
            uint32_t sectors_to_read = BUFFERSECTORS;
            size_t data_to_read = 0;
            if ((unpacked_header.buffer.size() - i) < BUFFERSECTORS)
            {
                sectors_to_read = unpacked_header.buffer.size() - i;
            }
            for (uint32_t j = 0; j < sectors_to_read; j++)
            {
                uint64_t calculatedSectorSize = ecm_processor.get_encoded_sector_size(unpacked_header.buffer[i + j], input_config.optimizations);
                data_to_read += calculatedSectorSize;
            }

            /* Reset the buffer position in every iterarion */
            input_buffer.reset_positions();
            output_buffer.reset_positions();

            /* Read the calculated size */
            input_file.read((char *)input_buffer.buffer.data(), data_to_read);

            /* Decode the readed data */
            ecm_processor.decode_stream(input_buffer, output_buffer, unpacked_header, sectors_to_read, 150 + i, input_config.optimizations);

            /* Write the decoded data to the output file */
            output_file.write(output_buffer.buffer.data(), output_buffer.current_position);
        }
    }
    else
    {
        /* Write the header into the output file */
        char ecm_header[] = {'E', 'C', 'M', '2', 0x01};
        output_file.write(ecm_header, sizeof(ecm_header));

        /* Initialize the options */
        ecm_file_configuration output_config;
        output_config.optimizations = OPTIMIZATIONS;

        /* Get the number of sectors in the input file */
        uint32_t sectors = input_size / 2352;

        /* Create a buffer to keep the data to process */
        ecm::data_buffer<char> input_buffer(BUFFERSIZE);
        ecm::data_buffer<char> output_buffer(BUFFERSIZE);
        ecm::data_buffer<ecm::sector_type> index(sectors);

        /* Analize the input file to detect the optimal optimizations and get an index */
        fprintf(stdout, "Analizing the data to determine the best optimizations and generate the index.\n");
        for (uint32_t i = 0; i < sectors; i += BUFFERSECTORS)
        {
            uint32_t sectors_to_read = 0;
            size_t data_to_read = 0;
            if (BUFFERSECTORS < (sectors - i))
            {
                sectors_to_read = BUFFERSECTORS; // must equals to buffersize
            }
            else
            {
                sectors_to_read = (sectors - i); // If there are no enough sectors to fill the buffer, just read what you have
            }

            /* Data to read in bytes */
            data_to_read = sectors_to_read * 2352;

            /* Reset the buffer position in every iterarion */
            input_buffer.reset_positions();
            output_buffer.reset_positions();

            /* Read the data */
            input_file.read(input_buffer.buffer.data(), data_to_read);
            /* Optimize the data and get the optimal optimizations */
            ecm_processor.encode_stream(input_buffer, output_buffer, index, sectors_to_read, CDROM_IMAGE_START_SECTOR + i, output_config.optimizations);
        }

        index.reset_positions();

        fprintf(stdout, "The best optimizations detected are %d.\n", output_config.optimizations);
        fprintf(stdout, "Packing the index with size %d using the best options.\n", index.buffer.size());

        size_t last_size = 0;
        uint8_t best_pack_size = 0;
        for (uint8_t j = 1; j <= 3; j++)
        {
            std::vector<char> size_tester = ecm_processor.pack_header(index, j);

            if (last_size == 0 || size_tester.size() < last_size)
            {
                best_pack_size = j;
                last_size = size_tester.size();
            }
        }

        fprintf(stdout, "The best count size for this disk is %d. Packing the definitive header and storing the configuration.\n", best_pack_size);
        std::vector<char> packed_index = ecm_processor.pack_header(index, best_pack_size);
        output_config.index_entries = packed_index.size() / (best_pack_size + 1);
        output_config.index_pack_mode = best_pack_size;
        output_config.sectors = sectors;

        output_file.write((char *)&output_config, sizeof(output_config));
        output_file.write(packed_index.data(), packed_index.size());

        /* Fine, the configuration and the index was written into the output file, so it's time to process the input file */
        /* Reset the positions */
        input_file.seekg(0, std::ios::beg);

        printf("Processing the input file and storing the data in the output file.\n");
        /* Well, the header was written and now is time to write the optimized sectors */
        for (uint32_t i = 0; i < sectors; i += BUFFERSECTORS)
        {
            uint32_t sectors_to_read = 0;
            size_t data_to_read = 0;
            if (BUFFERSECTORS < (sectors - i))
            {
                sectors_to_read = BUFFERSECTORS; // must equals to buffersize
            }
            else
            {
                sectors_to_read = (sectors - i); // If there are no enough sectors to fill the buffer, just read what you have
            }
            /* If the buffer size doesn't matches the readed data, then will be recreated */
            /* We first destroy the vector and then we recreate it to avoid the overhead of to copy the current data to another memory location */
            data_to_read = sectors_to_read * 2352;

            /* Reset the buffer position in every iterarion */
            input_buffer.reset_positions();
            output_buffer.reset_positions();

            /* Read the data */
            input_file.read(input_buffer.buffer.data(), data_to_read);

            ecm_processor.encode_stream(input_buffer, output_buffer, index, sectors_to_read, 150 + i, output_config.optimizations);
            output_file.write(output_buffer.buffer.data(), output_buffer.current_position);
        }

        printf("The data was sucesfully encoded.\n");
    }

exit:
    if (input_file.is_open())
    {
        input_file.close();
    }
    if (output_file.is_open())
    {
        output_file.close();
    }

    if (return_code == 0)
    {
        auto stop = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
        fprintf(stdout, "\n\nThe file was processed without any problem\n");
        fprintf(stdout, "Total execution time: %0.3fs\n\n", duration.count() / 1000.0F);
    }
    else
    {
        // Something went wrong, so output file must be deleted if keep == false
        // We will remove the file if something went wrong
        fprintf(stderr, "\n\nERROR: there was an error processing the input file.\n\n");
        std::ifstream out_remove_tmp(options.output_filename.c_str(), std::ios::binary);
        char dummy;
        if (out_remove_tmp.read(&dummy, 0))
        {
            out_remove_tmp.close();
            if (remove(options.output_filename.c_str()))
            {
                fprintf(stderr, "There was an error removing the output file... Please remove it manually.\n");
            }
        }
    }
    return return_code;
}

/**
 * @brief Arguments parser for the program. It stores the options in the options struct
 *
 * @param argc: Number of arguments passed to the program
 * @param argv: Array with the passed arguments
 * @param options: The output struct to place the parsed options
 * @return int: non zero on error
 */
int get_options(
    int argc,
    char **argv,
    program_options &options)
{
    char ch;
    // temporal variables for options parsing
    uint64_t temp_argument = 0;

    while ((ch = getopt_long(argc, argv, "i:o:d", long_options, NULL)) != -1)
    {
        // check to see if a single character or long option came through
        switch (ch)
        {
        // short option '-i', long option '--input'
        case 'i':
            options.input_filename = optarg;
            break;

        // short option '-o', long option "--output"
        case 'o':
            options.output_filename = optarg;
            break;

        // short option '-e', long option "--extreme-compresison" (only LZMA)
        case 'd':
            options.decode = true;
            break;

        case '?':
            print_help();
            return 0;
            break;
        }
    }

    // Test encode
    // options.input_filename = "test.bin";
    // options.output_filename = "test.ecm2";
    // options.decode = false;

    // Test decode
    // options.input_filename = "test.ecm2";
    // options.output_filename = "test.dec";
    // options.decode = true;

    if (options.input_filename.empty() || options.output_filename.empty())
    {
        print_help();
        return 1;
    }

    return 0;
}

void print_help()
{
    fprintf(stderr,
            "Usage:\n"
            "\n"
            "To encode:\n"
            "    ecmtool -i/--input cdimagefile -o/--output ecmfile\n"
            "\n"
            "To decode:\n"
            "    ecmtool -d/--decode -i/--input ecmfile -o/--output cdimagefile\n"
            "\n");
}