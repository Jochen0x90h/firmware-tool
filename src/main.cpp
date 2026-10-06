#include <iostream>
#include <fstream>
#include <filesystem>


namespace fs = std::filesystem;

/*
    Firmware file structure:
    Device identifier (0-padded string of 16 characters)
    Flags for future extensions (uint32_t)
    Total size of all sections (uint32_t)
    Per section:
        Section Identifier (uint32_t)
        Section CRC (uint32_t)
        Section data size (uint32_t)
        Section data (uint32_t)
*/

constexpr uint32_t CRC_POLYNOMIAL = 0x04C11DB7;
uint32_t crcTable[256];

/// @brief initialize the CRC table for CRC-32
/// https://crccalc.com/?crc=123456789&method=CRC-32/JAMCRC&datatype=ascii&outtype=hex
/// https://stackoverflow.com/questions/26049150/calculate-a-32-bit-crc-lookup-table-in-c-c
void initCrcTable() {
    uint32_t remainder;
    for (int i = 0; i < 256; ++i) {
        // Start with the data byte
        remainder = i << 24;
        for (uint32_t bit = 8; bit > 0; --bit) {
            if (remainder & 0x80000000)
                remainder = (remainder << 1) ^ CRC_POLYNOMIAL;
            else
                remainder = (remainder << 1);
        }
        crcTable[i] = remainder;
    }
}

/// @brief Calculate CRC-32 for a buffer of 32-bit values
/// @param crc Starting CRC value (usually 0xffffffff)
/// @param buf Buffer containing 32-bit values
/// @param size Size of the buffer in 32-bit words
/// @return Resulting CRC value
uint32_t calcCrc(uint32_t crc, const uint32_t *buf, size_t size) {
    while (size > 0) {
        auto value = *buf;
        crc = crcTable[(crc >> 24) ^ (value >> 24)] ^ (crc << 8);
        crc = crcTable[(crc >> 24) ^ ((value >> 16) & 0xff)] ^ (crc << 8);
        crc = crcTable[(crc >> 24) ^ ((value >> 8) & 0xff)] ^ (crc << 8);
        crc = crcTable[(crc >> 24) ^ (value & 0xff)] ^ (crc << 8);
        buf++;
        --size;
    }

    return crc;
}

union Buffer {
    char ch[8192];
    uint32_t u32[2048];
};

/// @brief Calculate the CRC of a file.
/// @param f File
/// @param buffer Buffer for loading data and calculating the CRC
/// @return CRC of whole file
uint32_t calcCrc(std::ifstream &f, Buffer &buffer) {
    uint32_t crc = 0xffffffff;

    while (true) {
        f.read(buffer.ch, 8192);
        int count = f.gcount();
        if (count == 0)
            break;
        int size = (count + 3) >> 2;
        std::fill(buffer.ch + count, buffer.ch + size * 4, 0);
        crc = calcCrc(crc, buffer.u32, size);
    }
    f.clear();
    f.seekg(0);

    return crc;
}

void printFileInfo(const fs::path &path, int sectionSize, uint32_t sectionCrc) {
    // get file modification date
    // https://omegaup.com/docs/cpp/en/cpp/chrono/format.html
    // %F = %Y-%m-%d
    // %R = %H:%M
    // %T = %H:%M:%S
    auto fileTime = fs::last_write_time(path);
    auto systemTime = std::chrono::clock_cast<std::chrono::system_clock>(fileTime);
    auto zonedTime = std::chrono::zoned_time(std::chrono::current_zone(), systemTime);
    //auto formattedTime = std::format("{0:%F} {0:%R}", zonedTime);
    auto formattedTime = std::format("{0:%R}", zonedTime);

    std::cout << " file " << path;
    std::cout << " size ";
    if (sectionSize < 1000)
        std::cout << sectionSize << 'B';
    else
        std::cout << sectionSize / 1000 << "kB"; //<< " (" << std::hex << std::setw(8) << std::setfill('0') << sectionSize << ')'
    std::cout << " crc " << std::hex << sectionCrc << std::dec;
    std::cout << " time " << formattedTime << std::endl;
}

int main(int argc, char **argv) {
    initCrcTable();

/*
    // print CRC table
    for (int i = 0; i < 256; ++i) {
        std::cout << std::setw(8) << std::setfill('0') << std::hex << crcTable[i] << ", ";
        if (i % 8 == 7)
            std::cout << std::endl;
    }

    // test CRC calculation
    uint32_t testData[] = {0x12345678, 0x9abcdef0};
    std::cout << std::hex << calcCrc(0xffffffff, testData, 2) << std::dec << std::endl;
*/


    std::cout << "firmware-tool" << std::endl;
    if (argc <= 4) {
        std::cout << "Usage: firmware-tool <output file> <type> <offset/id> <file> <offset/id> <file> ..." << std::endl;
        return 1;
    }

    // get output file path
    fs::path outPath = argv[1];

    auto ext = outPath.extension().string();
    if (ext == ".bin") {
        // output binary firmware image
        std::cout << "Create binary file " << outPath << std::endl;

        // create output file
        std::ofstream fw(outPath, std::ios::trunc | std::ios::binary);


        // write sections
        int offset = 0;
        for (int i = 3; i < argc; i += 2) {
            Buffer buffer;

            // get section offset
            int sectionOffset = std::stoi(argv[i + 0]);

            // get file path and size
            fs::path path = argv[i + 1];
            int sectionSize = fs::file_size(path);

            // determine section CRC
            std::ifstream f(path, std::ios::binary);
            uint32_t sectionCrc = calcCrc(f, buffer);

            // fill gap with 0xff
            while (offset < sectionOffset) {
                uint32_t fillSize = std::min(8192, sectionOffset - offset);
                std::fill(buffer.ch, buffer.ch + fillSize, 0xff);
                fw.write(buffer.ch, fillSize);
                offset += fillSize;
            }

            // copy file
            for (int i = 0; true; ++i) {
                f.read(buffer.ch, 8192);
                int count = f.gcount();
                if (count == 0)
                    break;

                if (i == 0) {
                    buffer.u32[7] = sectionSize;
                    buffer.u32[8] = sectionCrc;
                    buffer.u32[9] = ~sectionSize;
                    buffer.u32[10] = ~sectionCrc;
                }

                fw.write(buffer.ch, count);

                offset += count;
            }

            std::cout << "offset " << sectionOffset;
            printFileInfo(path, sectionSize, 0);
        }
    } else if (ext == ".hex") {
        // output hex firmware image
        std::cout << "Create hex file " << outPath << std::endl;

        // todo
    } else {
        // output path of firmware file
        std::cout << "Create firmware file " << outPath << std::endl;

        // device identifier
        if (strlen(argv[2]) > 16)
            std::cout << "Warning: Device identifier too long" << std::endl;
        char deviceIdentifier[16 + 1] = {};
        strncpy(deviceIdentifier, argv[2], 16);

        std::cout << "Device " << deviceIdentifier << std::endl;

        // flags (future extensions)
        uint32_t flags = 0;

        // calculate total size (not including file header)
        uint32_t totalSize = 0;
        for (int i = 3; i < argc; i += 2) {
            uint32_t sectionIdentifier = std::stoi(argv[i + 0]);
            fs::path path = argv[i + 1];
            uint32_t sectionSize = 0;
            if (path != "-") {
                // get section size
                std::error_code ec;
                sectionSize = fs::file_size(path, ec);
                if (ec) {
                    std::cerr << "Error: Cannot access file " << path << ": " << ec.message() << std::endl;
                    return 1;
                }
            }

            //std::cout << "Section " << sectionIdentifier << " file " << path << " size " << sectionSize << std::endl;
            totalSize += 12 + sectionSize;
        }

        // create output file
        std::ofstream fw(outPath, std::ios::trunc | std::ios::binary);

        // write file header
        fw.write(deviceIdentifier, 16);
        fw.write((char *)&flags, 4);
        fw.write((char *)&totalSize, 4);

        // write sections
        for (int i = 3; i < argc; i += 2) {
            Buffer buffer;

            // get section identifier
            uint32_t sectionIdentifier = std::stoi(argv[i + 0]);

            // get file path
            fs::path path = argv[i + 1];

            uint32_t sectionSize = 0;
            uint32_t sectionCrc = 0xffffffff;
            std::ifstream f;
            std::cout << "Section " << sectionIdentifier;
            if (path != "-") {
                // get section size
                sectionSize = fs::file_size(path);

                // open file
                f.open(path, std::ios::binary);

                // determine section CRC
                sectionCrc = calcCrc(f, buffer);

                printFileInfo(path, sectionSize, sectionCrc);
            } else {
                std::cout << " empty" << std::endl;
            }

            // write section header
            fw.write((char *)&sectionIdentifier, 4);
            fw.write((char *)&sectionCrc, 4);
            fw.write((char *)&sectionSize, 4);

            // copy file
            /*while (true) {
                f.read(buffer.ch, 8192);
                int count = f.gcount();
                if (count == 0)
                    break;
                fw.write(buffer.ch, count);
            }*/
            while (sectionSize > 0) {
                int toCopy = std::min(sectionSize, 8192u);
                f.read(buffer.ch, toCopy);
                fw.write(buffer.ch, toCopy);
                sectionSize -= toCopy;
            }

        }
    }
    return 0;
}
