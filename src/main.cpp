#include <iostream>
#include <fstream>
#include <filesystem>


namespace fs = std::filesystem;

/*
    Firmware structure:
    Device identifier (0-terminated string of 8 characters)
    Total size of all sections (uint32_t)
    Repeat:
        Section Identifier (uint32_t)
        Section CRC (uint32_t)
        Section data size (uint32_t)
        Section data (uint32_t)
*/

uint32_t crcTable[256];

/// @brief initialize the CRC table for CRC-32
/// https://crccalc.com/?crc=123456789&method=CRC-32/JAMCRC&datatype=ascii&outtype=hex
/// https://stackoverflow.com/questions/26049150/calculate-a-32-bit-crc-lookup-table-in-c-c
void initCrcTable() {
    uint32_t POLYNOMIAL = 0x04C11DB7;
    uint32_t remainder;
    uint8_t b = 0;
    do {
        // Start with the data byte
        remainder = b << 24;
        for (uint32_t bit = 8; bit > 0; --bit) {
            if (remainder & 0x80000000)
                remainder = (remainder << 1) ^ POLYNOMIAL;
            else
                remainder = (remainder << 1);
        }
        crcTable[b] = remainder;
        ++b;
    } while (b != 0);
}

void initCrcTableRev() {
    uint32_t POLYNOMIAL = 0xEDB88320; // reverse of 0x04C11DB7
    uint32_t remainder;
    uint8_t b = 0;
    do {
        // Start with the data byte
        remainder = b;
        for (uint32_t bit = 8; bit > 0; --bit) {
            if (remainder & 1)
                remainder = (remainder >> 1) ^ POLYNOMIAL;
            else
                remainder = (remainder >> 1);
        }
        crcTable[b] = remainder;
        ++b;
    } while (b != 0);
}

/*
uint32_t calcCrc(uint32_t crc, const uint8_t *buf, size_t size) {
    while (size > 0) {
        crc = crcTable[(crc >> 24) ^ *buf] ^ (crc << 8);
        buf++;
        --size;
    }

    return crc;
}
*/


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

/*
uint32_t calcCrcRev(uint32_t crc, const uint8_t *buf, size_t size) {
    while (size > 0) {
        crc = crcTable[(crc ^ *buf) & 0xFF] ^ (crc >> 8);
        buf++;
        --size;
    }

    return crc;
}*/

union Buffer {
    char ch[8192];
    uint32_t u32[2048];
};

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

void printFileInfo(const fs::path &path, int sectionSize) {
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

    std::cout << " file " << path
        << " size " << sectionSize / 1024 << 'k' //<< " (" << std::hex << std::setw(8) << std::setfill('0') << sectionSize << ')'
        //<< " crc " << sectionCrc << std::dec
        << " time " << formattedTime << std::endl;
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
    if (argc <= 4)
        return 1;

    // output file name
    fs::path outPath = argv[1];

    auto ext = outPath.extension().string();
    if (ext == ".bin") {
        // output binary firmware image
        std::cout << "Binary firmware image" << std::endl;

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
            printFileInfo(path, sectionSize);
        }
    } else if (ext == ".hex") {
        // output hex firmware image
        std::cout << "Hex firmware image" << std::endl;

    } else {
        // device identifier
        char deviceIdentifier[16 + 1] = {};
        strncpy(deviceIdentifier, argv[2], 16);

        std::cout << "Device " << deviceIdentifier << std::endl;
        std::cout << "Output " << outPath.string() << std::endl;

        // flags (future extensions)
        uint32_t flags = 0;

        // calculate total size (not including file header)
        uint32_t totalSize = 0;
        for (int i = 3; i < argc; i += 2) {
            uint32_t sectionIdentifier = std::stoi(argv[i + 0]);
            fs::path path = argv[i + 1];
            std::error_code ec;
            uint32_t sectionSize = fs::file_size(path, ec);
            if (ec) {
                std::cerr << "Error: Cannot access file " << path << ": " << ec.message() << std::endl;
                return 1;
            }

            //std::cout << "section " << sectionIdentifier << " file " << path << " size " << sectionSize << std::endl;
            totalSize += 12 + sectionSize;
            //++sectionCount;
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

            // get file path and size
            fs::path path = argv[i + 1];
            uint32_t sectionSize = fs::file_size(path);

            // determine section CRC
            std::ifstream f(path, std::ios::binary);
            uint32_t sectionCrc = calcCrc(f, buffer);
            //uint32_t sectionSize2 = f.tellg();

            // write section header
            fw.write((char *)&sectionIdentifier, 4);
            fw.write((char *)&sectionCrc, 4);
            fw.write((char *)&sectionSize, 4);

            // copy file
            //f.clear();
            //f.seekg(0);
            while (true) {
                f.read(buffer.ch, 8192);
                int count = f.gcount();
                if (count == 0)
                    break;
                fw.write(buffer.ch, count);
            }

            std::cout << "section " << sectionIdentifier;
            printFileInfo(path, sectionSize);
/*
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


            " file " << path
                << " size " << sectionSize / 1024 << 'k' //<< " (" << std::hex << std::setw(8) << std::setfill('0') << sectionSize << ')'
                //<< " crc " << sectionCrc << std::dec
                << " time " << formattedTime << std::endl;
*/
        }
    }
    return 0;
}
