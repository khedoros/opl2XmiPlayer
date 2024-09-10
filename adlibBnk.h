// Basic loader for the Doom GENMIDI lump, containing the game's instrument definitions

#pragma once
#include<cstdint>
#include<string>
#include<iostream>
#include<fstream>
#include<filesystem>
#include<vector>
namespace fs = std::filesystem;

struct adlibBnk_t {

    struct header_t {
        uint16_t version;
        char signature[6];
        uint16_t numUsed;
        uint16_t numInstruments;
        uint32_t offsetInstNames;
        uint32_t offsetInstData;
        char pad[8];
    } header;

    struct oplRegs_t {
        uint8_t ksl;   // 0x40 bits 6-7
        uint8_t mult;  // 0x20 bits 0-3
        uint8_t fb;    // 0xc0 bits 1-3, op 0 only
        uint8_t ar;    // 0x60 bits 4-7
        uint8_t sl;    // 0x80 bits 4-7
        uint8_t eg;    // 0x20 bit   5 envelope gain? non-zero value is "on"
        uint8_t dr;    // 0x60 bits 0-3
        uint8_t rr;    // 0x80 bits 0-3
        uint8_t tl;    // 0x40 bits 0-5
        uint8_t am;    // 0x20 bit   7
        uint8_t vib;   // 0x20 bit   6
        uint8_t ksr;   // 0x20 bit   4
        uint8_t con;   // 0xc0 bit   0, op 0 only, apparently inverted from the register value
    };

    struct inst_t {
        uint8_t isPercussive; // 0 = melodic, 1 = percussive
        uint8_t voiceNum;     // Voice number (percussion only)
        oplRegs_t mod;    
        oplRegs_t car;
        uint8_t modWave;      // 0xe0 bits 0-1
        uint8_t carWave;      // 0xe0
    };

    struct instName_t {
        uint16_t dataIndex;
        uint8_t flags;
        char name[9];
    };

    std::vector<instName_t> index;
    std::vector<inst_t> instruments;

    adlibBnk_t() {}
    adlibBnk_t(std::string& filename) {
        size_t fileSize = fs::file_size(filename);
        std::ifstream in(filename);
        if(in) {
            in.read(reinterpret_cast<char *>(&header), sizeof(header_t));
        }
        else {
            throw std::string("Couldn't open file "+filename);
        }
        const int expectedFilesize = sizeof(header) + (sizeof(instName_t) + sizeof(inst_t)) * header.numInstruments;
        if(fileSize != expectedFilesize) {
            throw std::string("Expected "+std::to_string(expectedFilesize)+" bytes, but file had "+std::to_string(fileSize));
        }
        index.resize(header.numInstruments);
        instruments.resize(header.numInstruments);
        in.seekg(header.offsetInstNames);
        in.read(reinterpret_cast<char *>(index.data()), header.numInstruments * sizeof(instName_t));
        in.seekg(header.offsetInstData);
        in.read(reinterpret_cast<char *>(instruments.data()), header.numInstruments * sizeof(inst_t));
    }
};
