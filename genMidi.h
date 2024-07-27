#pragma once
#include<cstdint>
#include<string>
#include<iostream>
#include<fstream>
#include<filesystem>
namespace fs = std::filesystem;

struct genMidi_t {
    genMidi_t(std::string& filename) {
        size_t fileSize = fs::file_size(filename);
        if(fileSize != sizeof(genMidi_t)) throw std::string("Inappropriate filesize");
        std::ifstream in(filename);
        if(in) {
            in.read(reinterpret_cast<char *>(this), sizeof(genMidi_t));
        }
    }
    char header[8];
    struct genmidiInstrument_t {
        uint16_t flags;
        enum flagValues {
            fixedPitch = 1,
            unknown = 2,
            doubleVoice = 4
        };
        uint8_t fineTuning;
        uint8_t fixedPitchValue;
        struct oplVoice_t {
            uint8_t modAmFmSusKsrMulti;
            uint8_t modArDr;
            uint8_t modSrRr;
            uint8_t modWaveform;
            uint8_t modKsl;
            uint8_t modOutputLevel;
            uint8_t modFeedback;

            uint8_t carAmFmSusKsrMulti;
            uint8_t carArDr;
            uint8_t carSrRr;
            uint8_t carWaveform;
            uint8_t carKsl;
            uint8_t carOutputLevel;
            uint8_t carUnused;

            int16_t baseNoteOffset;
        } voice[2];
    } instrument[175];
    char names[175][32];
};
