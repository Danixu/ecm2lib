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

#define EXTRACT true

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

int main()
{
    // Working dir will be always the exe folder
    std::filesystem::current_path(get_program_path());

    std::fstream _fp;
    _fp.open("test.bin", std::ios::in | std::ios::binary);

    if (!_fp.is_open())
    {
        printf("There was an error trying to open the test.bin file.\n");
        return 1;
    }
    else
    {
        printf("The file test.bin was opened correctly.\n");
    }

    /* Get the file size */
    _fp.seekg(0, std::ios::end);
    uint64_t fileSize = _fp.tellg();

    /* If the file is not a CDROM image, exit */
    if (fileSize % 2352)
    {
        printf("The file size doesn't fit a CD-ROM image");
        _fp.close();
        return 1;
    }

    /* Seek to the start point */
    uint64_t currentPos = 0;
    _fp.seekg(0);
    /* Clear all the errors */
    _fp.clear();

    /* Initialize the ECM library */
    ecm::processor ecmProcessor;

    bool cdda = false;
    bool cddaGap = false;
    bool mode1 = false;
    bool mode1Gap = false;
    bool mode1Raw = false;
    bool mode2 = false;
    bool mode2Gap = false;
    bool mode2_1 = false;
    bool mode2_1_Gap = false;
    bool mode2_2 = false;
    bool mode2_2_Gap = false;
    bool modeX = false;

    std::ofstream _oif("test.idx", std::ios::trunc | std::ios::binary);

    /* Read the file sector by sector and try the library */
    ecm::data_buffer<char> buffer(2352);
    while (currentPos < fileSize)
    {
        if (_fp.eof())
        {
            printf("There file is already on EOF and the while still working. Something weird happens, so check the code.\n");
            break;
        }
        /* Read a sector */
        _fp.read(buffer.buffer.data(), 2352);

        /* Check the sector type */
        ecm::sector_type detectedType = ecmProcessor.detect(buffer);
        _oif.write((char *)&detectedType, sizeof(detectedType));

        if (detectedType == ecm::ST_CDDA)
        {
            if (cdda == false)
            {
                uint64_t detectedPos = (uint64_t)_fp.tellg() - 2352;
                printf("Detected the first CDDA type sector in the position %d\n", detectedPos);
                if (EXTRACT)
                {
                    std::fstream _of;
                    _of.open("SECTOR_CDDA.bin", std::ios::out | std::ios::trunc | std::ios::binary);
                    _of.write(buffer.buffer.data(), 2352);
                    _of.close();
                }
                cdda = true;
            }
            currentPos = _fp.tellg();
            continue;
        }
        else if (detectedType == ecm::ST_CDDA_GAP)
        {
            if (cddaGap == false)
            {
                uint64_t detectedPos = (uint64_t)_fp.tellg() - 2352;
                printf("Detected the first CDDA GAP type sector in the position %d\n", detectedPos);
                if (EXTRACT)
                {
                    std::fstream _of;
                    _of.open("SECTOR_CDDA_GAP.bin", std::ios::out | std::ios::trunc | std::ios::binary);
                    _of.write(buffer.buffer.data(), 2352);
                    _of.close();
                }
                cddaGap = true;
            }
            currentPos = _fp.tellg();
            continue;
        }
        else if (detectedType == ecm::ST_MODE1)
        {
            if (mode1 == false)
            {
                uint64_t detectedPos = (uint64_t)_fp.tellg() - 2352;
                printf("Detected the first Mode1 type sector in the position %d\n", detectedPos);
                if (EXTRACT)
                {
                    std::fstream _of;
                    _of.open("SECTOR_MODE1.bin", std::ios::out | std::ios::trunc | std::ios::binary);
                    _of.write(buffer.buffer.data(), 2352);
                    _of.close();
                }
                mode1 = true;
            }
            currentPos = _fp.tellg();
            continue;
        }
        else if (detectedType == ecm::ST_MODE1_GAP)
        {
            if (mode1Gap == false)
            {
                uint64_t detectedPos = (uint64_t)_fp.tellg() - 2352;
                printf("Detected the first Mode1 GAP type sector in the position %d\n", detectedPos);
                if (EXTRACT)
                {
                    std::fstream _of;
                    _of.open("SECTOR_MODE1_GAP.bin", std::ios::out | std::ios::trunc | std::ios::binary);
                    _of.write(buffer.buffer.data(), 2352);
                    _of.close();
                }
                mode1Gap = true;
            }
            currentPos = _fp.tellg();
            continue;
        }
        else if (detectedType == ecm::ST_MODE1_RAW)
        {
            if (mode1Raw == false)
            {
                uint64_t detectedPos = (uint64_t)_fp.tellg() - 2352;
                printf("Detected the first Mode1 RAW type sector in the position %d\n", detectedPos);
                if (EXTRACT)
                {
                    std::fstream _of;
                    _of.open("SECTOR_MODE1_RAW.bin", std::ios::out | std::ios::trunc | std::ios::binary);
                    _of.write(buffer.buffer.data(), 2352);
                    _of.close();
                }
                mode1Raw = true;
            }
            currentPos = _fp.tellg();
            continue;
        }
        else if (detectedType == ecm::ST_MODE2)
        {
            if (mode2 == false)
            {
                uint64_t detectedPos = (uint64_t)_fp.tellg() - 2352;
                printf("Detected the first Mode2 type sector in the position %d\n", detectedPos);
                if (EXTRACT)
                {
                    std::fstream _of;
                    _of.open("SECTOR_MODE2.bin", std::ios::out | std::ios::trunc | std::ios::binary);
                    _of.write(buffer.buffer.data(), 2352);
                    _of.close();
                }
                mode2 = true;
            }
            currentPos = _fp.tellg();
            continue;
        }
        else if (detectedType == ecm::ST_MODE2_GAP)
        {
            if (mode2Gap == false)
            {
                uint64_t detectedPos = (uint64_t)_fp.tellg() - 2352;
                printf("Detected the first Mode2 GAP type sector in the position %d\n", detectedPos);
                if (EXTRACT)
                {
                    std::fstream _of;
                    _of.open("SECTOR_MODE2_GAP.bin", std::ios::out | std::ios::trunc | std::ios::binary);
                    _of.write(buffer.buffer.data(), 2352);
                    _of.close();
                }
                mode2Gap = true;
            }
            currentPos = _fp.tellg();
            continue;
        }
        else if (detectedType == ecm::ST_MODE2_XA_GAP)
        {
            if (mode2_1 == false)
            {
                uint64_t detectedPos = (uint64_t)_fp.tellg() - 2352;
                printf("Detected the first Mode2 XA GAP type sector in the position %d\n", detectedPos);
                if (EXTRACT)
                {
                    std::fstream _of;
                    _of.open("SECTOR_MODE2_XA_GAP.bin", std::ios::out | std::ios::trunc | std::ios::binary);
                    _of.write(buffer.buffer.data(), 2352);
                    _of.close();
                }
                mode2_1 = true;
            }
            currentPos = _fp.tellg();
            continue;
        }
        else if (detectedType == ecm::ST_MODE2_XA1)
        {
            if (mode2_1 == false)
            {
                uint64_t detectedPos = (uint64_t)_fp.tellg() - 2352;
                printf("Detected the first Mode2 XA1 type sector in the position %d\n", detectedPos);
                if (EXTRACT)
                {
                    std::fstream _of;
                    _of.open("SECTOR_MODE2_XA1.bin", std::ios::out | std::ios::trunc | std::ios::binary);
                    _of.write(buffer.buffer.data(), 2352);
                    _of.close();
                }
                mode2_1 = true;
            }
            currentPos = _fp.tellg();
            continue;
        }
        else if (detectedType == ecm::ST_MODE2_XA1_GAP)
        {
            if (mode2_1_Gap == false)
            {
                uint64_t detectedPos = (uint64_t)_fp.tellg() - 2352;
                printf("Detected the first Mode2 XA1 GAP type sector in the position %d\n", detectedPos);
                if (EXTRACT)
                {
                    std::fstream _of;
                    _of.open("SECTOR_MODE2_XA1_GAP.bin", std::ios::out | std::ios::trunc | std::ios::binary);
                    _of.write(buffer.buffer.data(), 2352);
                    _of.close();
                }
                mode2_1_Gap = true;
            }
            currentPos = _fp.tellg();
            continue;
        }
        else if (detectedType == ecm::ST_MODE2_XA2)
        {
            if (mode2_2 == false)
            {
                uint64_t detectedPos = (uint64_t)_fp.tellg() - 2352;
                printf("Detected the first Mode2 XA2 type sector in the position %d\n", detectedPos);
                if (EXTRACT)
                {
                    std::fstream _of;
                    _of.open("SECTOR_MODE2_XA2.bin", std::ios::out | std::ios::trunc | std::ios::binary);
                    _of.write(buffer.buffer.data(), 2352);
                    _of.close();
                }
                mode2_2 = true;
            }
            currentPos = _fp.tellg();
            continue;
        }
        else if (detectedType == ecm::ST_MODE2_XA2_GAP)
        {
            if (mode2_2_Gap == false)
            {
                uint64_t detectedPos = (uint64_t)_fp.tellg() - 2352;
                printf("Detected the first Mode2 XA2 GAP type sector in the position %d\n", detectedPos);
                if (EXTRACT)
                {
                    std::fstream _of;
                    _of.open("SECTOR_MODE2_XA2_GAP.bin", std::ios::out | std::ios::trunc | std::ios::binary);
                    _of.write(buffer.buffer.data(), 2352);
                    _of.close();
                }
                mode2_2_Gap = true;
            }
            currentPos = _fp.tellg();
            continue;
        }
        else if (detectedType == ecm::ST_MODEX)
        {
            if (modeX == false)
            {
                uint64_t detectedPos = (uint64_t)_fp.tellg() - 2352;
                printf("Detected the first ModeX type sector in the position %d\n", detectedPos);
                if (EXTRACT)
                {
                    std::fstream _of;
                    _of.open("SECTOR_MODEX.bin", std::ios::out | std::ios::trunc | std::ios::binary);
                    _of.write(buffer.buffer.data(), 2352);
                    _of.close();
                }
                modeX = true;
            }
            currentPos = _fp.tellg();
            continue;
        }

        /* If we are here, then maybe the sector was not detected (something impossible because if all modes are covered) */
        uint64_t detectedPos = (uint64_t)_fp.tellg() - 2352;
        printf("WARNING: The sector in the position %d is an unkown sector type\n. Check the library!.\n", detectedPos);
    }

    _fp.close();
    _oif.close();

    return 0;
}