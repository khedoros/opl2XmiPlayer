#include<cstdint>
#include<array>
#include<iostream>

#define NATIVE_OPL_SAMPLE_RATE          49716.0
const int NATIVE_SAMPLE_RATE = 49716;
const int64_t NATIVE_SAMPLE_RATE_64 = 49716;
//#define OPL_SAMPLE_RATE             48000.0
const int64_t OPL_SAMPLE_RATE_64 = 48000;
const int OPL_SAMPLE_RATE = 48000;

const int MASK = 0x3ff<<10;
//APU::YM3812 melody chan 0 attack key-off->on
//Block: 5 FNum: 385
//Modulator:
//0x20: AM:false VIB:false sustain:false KSR:false mult:1
//0x40: KSL:0 TL:63
//0x60: AR:7 DR:3
//0x80: SL:11 RR:5
//0xC0: Feedback:0 AM Connection:fm
//0xE0: Waveform:0
//Carrier:
//0x20: AM:false VIB:false sustain:false KSR:false mult:16
//0x40: KSL:0 TL:3
//0x60: AR:15 DR:4
//0x80: SL:3 RR:4
//0xC0: Feedback:0 AM Connection:fm
//0xE0: Waveform:0
struct note_t {
    unsigned freqMult:4;   //frequency multiplier, 1 of 3 elements that define the frequency
    unsigned fNum: 10; // 2nd of 3 elements that define the frequency
    unsigned int octave: 3; //3rd element that defines the frequency
    unsigned phaseCnt:20;
};


struct note64_t {
    uint64_t freqMult;
    uint64_t fNum;
    uint64_t octave;
    unsigned phaseCnt:20;
};

const std::array<uint8_t,16> multVal {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};
const std::array<uint64_t,16> multVal64 {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};

int convertWavelength(int wavelength) {
    return (static_cast<int64_t>(wavelength) * static_cast<int64_t>(NATIVE_SAMPLE_RATE)) / static_cast<int64_t>(OPL_SAMPLE_RATE);
}

int convertWavelength64(int64_t wavelength) {
    return (wavelength * NATIVE_SAMPLE_RATE_64) / OPL_SAMPLE_RATE_64;
}

int main() {
    note_t n{8,385,5};
    note64_t n64{8,385,5};
    int phaseInc = convertWavelength(((n.fNum << n.octave) * multVal[n.freqMult]) >> 1);
    int64_t phaseInc64 = convertWavelength64(((n64.fNum << n64.octave) * multVal64[n64.freqMult]) >> 1);
    for(int i=0;i<100;i++) {
    std::cout<<phaseInc<<" "<<phaseInc64<<'\n';
}
